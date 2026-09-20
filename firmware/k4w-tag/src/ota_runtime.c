/* Tag-local confirmation and authenticated, bounded BLE management. */
#include "ota_runtime.h"
#include "update_policy.h"
#include "update_commands.h"
#include "ota_access.h"
#include "tag_power.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/settings/settings.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/mgmt/mcumgr/transport/smp_bt.h>
#include <zephyr/app_version.h>
#include <hal/nrf_wdt.h>
#include <zcbor_encode.h>
#include <zcbor_decode.h>
#include <mgmt/mcumgr/util/zcbor_bulk.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static K_MUTEX_DEFINE(lock);
static struct br_update_policy policy;
static struct br_ota_access access;
static struct bt_conn *peer;
static bool ble_ok, authorized, advertising, loaded, peer_admitted;
static atomic_t connection_available = ATOMIC_INIT(1);
static int identity_error;
static uint64_t connected_ms, next_ad_ms;
static bool key_was_down, fast_ad;
static struct gpio_callback key_callback;
static uint32_t reset_reason;
static char hardware_id[17];
static struct { uint32_t magic; uint16_t schema, tag; uint32_t passkey; } identity;
static const struct gpio_dt_spec key = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static void key_changed(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    ARG_UNUSED(dev); ARG_UNUSED(cb); ARG_UNUSED(pins);
    br_app_wake();
}

/* Private commissioning PIN is SWD-readable, never advertised or sent in status.
 * SWD already gives full physical access. Keep captured value out of public logs. */
volatile uint32_t ota_pairing_pin;
volatile uint32_t ota_local_request; /* trusted jig can write 0x4d41494e */
/* Written by the identity-guarded SWD jig after reset halt, before first run. */
/* Keep above MCUboot's current 0x3b28 RAM footprint. Artifact preflight must
 * recheck non-overlap whenever either image changes; alignment alone is not
 * a guarantee against future bootloader growth. */
__attribute__((section(".noinit"), aligned(16384), used))
volatile uint32_t ota_provision_request[5];
volatile struct {
    uint32_t magic, ready, error, confirmed, watchdog, reset_reason;
    uint32_t accepted, rejected, maintenance;
} ota_diag = { .magic = 0x42524f54 };

static int identity_load(const char *name, size_t len, settings_read_cb read_cb, void *arg) {
    if (strcmp(name, "identity")) return -ENOENT;
    if (len != sizeof(identity)) return identity_error = -EINVAL;
    int n = read_cb(arg, &identity, sizeof(identity));
    if (n != sizeof(identity) || identity.magic != 0x42524944 || identity.schema != 1 ||
        identity.tag == 0 || identity.tag == 0xffff || identity.passkey > 999999)
        return identity_error = -EINVAL;
    loaded = true;
    return 0;
}
static struct settings_handler settings = { .name = "binrange", .h_set = identity_load };

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, SMP_BT_SVC_UUID_VAL),
};
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME)-1),
};

struct bond_lookup { struct bt_conn *conn; bool known; };
static void bond_match(const struct bt_bond_info *info, void *data) {
    struct bond_lookup *lookup = data;
    if (!bt_addr_le_cmp(&info->addr, bt_conn_get_dst(lookup->conn))) lookup->known = true;
}
static void connected(struct bt_conn *conn, uint8_t err) {
    k_mutex_lock(&lock, K_FOREVER);
    uint64_t now = k_uptime_get();
    struct bond_lookup lookup = { .conn = conn };
    bt_foreach_bond(BT_ID_DEFAULT, bond_match, &lookup);
    bool allowed = br_ota_attempt(&access, now, lookup.known);
    advertising = false;
    atomic_clear(&connection_available);
    if (err) {
        if (allowed) br_ota_end_attempt(&access, now);
        k_mutex_unlock(&lock);
        br_app_wake();
        return;
    }
    peer = bt_conn_ref(conn);
    peer_admitted = allowed;
    connected_ms = now;
    authorized = false;
    advertising = false;
    k_mutex_unlock(&lock);
    br_app_wake();
    if (!allowed) bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
    else if (bt_conn_set_security(conn, BT_SECURITY_L4))
        bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
}
static void disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);
    k_mutex_lock(&lock, K_FOREVER);
    if (peer == conn) {
        bt_conn_unref(peer); peer = NULL;
        peer_admitted = false;
        br_ota_end_attempt(&access, k_uptime_get());
    }
    authorized = false;
    advertising = false;
    next_ad_ms = k_uptime_get() + 1000;
    k_mutex_unlock(&lock);
    br_app_wake();
}
static void recycled(void) {
    /* The stack still owns a reference during disconnected(). Only signal
     * here: advertising/HCI work runs in the application thread, never in
     * the stack's recycled callback (which can run on its system workqueue). */
    atomic_set(&connection_available, 1);
    br_app_wake();
}
static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err) {
    k_mutex_lock(&lock, K_FOREVER);
    bool allowed = conn == peer && br_ota_authenticated(&access, k_uptime_get(),
        peer_admitted, !err && level == BT_SECURITY_L4);
    if (conn == peer) authorized = allowed;
    k_mutex_unlock(&lock);
    br_app_wake();
    if (!allowed) bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
}
BT_CONN_CB_DEFINE(connection_callbacks) = {
    .connected = connected, .disconnected = disconnected, .security_changed = security_changed,
    .recycled = recycled,
};
static enum bt_security_err pairing_accept(struct bt_conn *conn,
                                         const struct bt_conn_pairing_feat *const feat) {
    ARG_UNUSED(conn); ARG_UNUSED(feat);
    k_mutex_lock(&lock, K_FOREVER);
    bool allowed = conn == peer && peer_admitted &&
        br_ota_pairing_allowed(&access, k_uptime_get());
    k_mutex_unlock(&lock);
    return allowed ? BT_SECURITY_ERR_SUCCESS : BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
}
static void passkey_display(struct bt_conn *conn, unsigned int passkey) {
    ARG_UNUSED(conn); ARG_UNUSED(passkey); /* PIN delivered through physical provisioning only. */
}
static void auth_cancel(struct bt_conn *conn) { bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL); }
static uint32_t app_passkey(struct bt_conn *conn) { ARG_UNUSED(conn); return identity.passkey; }
static const struct bt_conn_auth_cb auth = {
    .pairing_accept = pairing_accept, .passkey_display = passkey_display, .cancel = auth_cancel,
    .app_passkey = app_passkey,
};

static enum mgmt_cb_return command_check(uint32_t event, enum mgmt_cb_return previous,
        int32_t *rc, uint16_t *group, bool *abort_more, void *data, size_t len) {
    ARG_UNUSED(event); ARG_UNUSED(group);
    if (previous != MGMT_CB_OK) return previous;
    struct mgmt_evt_op_cmd_arg *cmd = data;
    k_mutex_lock(&lock, K_FOREVER);
    bool allowed = len == sizeof(*cmd) &&
        br_ota_command_allowed(cmd->group, cmd->id, cmd->op, boot_is_img_confirmed()) &&
        br_ota_activity(&access, k_uptime_get(), authorized);
    if (allowed) ota_diag.accepted++; else ota_diag.rejected++;
    k_mutex_unlock(&lock);
    if (allowed) return MGMT_CB_OK;
    *rc = MGMT_ERR_EACCESSDENIED;
    *abort_more = true;
    return MGMT_CB_ERROR_RC;
}
static struct mgmt_callback command_callback = {
    .callback = command_check, .event_id = MGMT_EVT_OP_CMD_RECV,
};

static int status_read(struct smp_streamer *ctxt) {
    BUILD_ASSERT(CONFIG_MCUMGR_SMP_CBOR_MAX_MAIN_MAP_ENTRIES >= 18,
                 "status includes motion/power diagnostics");
    zcbor_state_t *z = ctxt->writer->zs;
    struct br_power_status power;
    br_power_status_get(&power);
    k_mutex_lock(&lock, K_FOREVER);
    bool ok = zcbor_tstr_put_lit(z, "id") && zcbor_tstr_put_term(z, hardware_id, sizeof(hardware_id)) &&
        zcbor_tstr_put_lit(z, "tag") && zcbor_uint32_put(z, identity.tag) &&
        zcbor_tstr_put_lit(z, "version") && zcbor_tstr_put_lit(z, APP_VERSION_STRING) &&
        zcbor_tstr_put_lit(z, "confirmed") && zcbor_bool_put(z, boot_is_img_confirmed()) &&
        zcbor_tstr_put_lit(z, "uptime_ms") && zcbor_uint64_put(z, k_uptime_get()) &&
        zcbor_tstr_put_lit(z, "reset_reason") && zcbor_uint32_put(z, reset_reason) &&
        zcbor_tstr_put_lit(z, "maintenance") &&
        zcbor_bool_put(z, br_ota_access_allowed(&access, k_uptime_get(), true)) &&
        zcbor_tstr_put_lit(z, "radio_ok") && zcbor_bool_put(z, policy.radio_healthy) &&
        zcbor_tstr_put_lit(z, "ble_ok") && zcbor_bool_put(z, ble_ok) &&
        zcbor_tstr_put_lit(z, "sensor_error") && zcbor_int32_put(z, power.sensor_error) &&
        zcbor_tstr_put_lit(z, "moving") && zcbor_bool_put(z, power.moving) &&
        zcbor_tstr_put_lit(z, "wake_count") && zcbor_uint32_put(z, power.wake_count) &&
        zcbor_tstr_put_lit(z, "irq_count") && zcbor_uint32_put(z, power.irq_count) &&
        zcbor_tstr_put_lit(z, "battery_mv") && zcbor_uint32_put(z, power.battery_mv) &&
        zcbor_tstr_put_lit(z, "misses") && zcbor_uint32_put(z, power.misses) &&
        zcbor_tstr_put_lit(z, "uwb_sleeping") && zcbor_bool_put(z, power.sleeping) &&
        zcbor_tstr_put_lit(z, "config_pending") && zcbor_bool_put(z, power.config_pending) &&
        zcbor_tstr_put_lit(z, "config_schema") && zcbor_uint32_put(z, 1);
    k_mutex_unlock(&lock);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}
static int config_read(struct smp_streamer *ctxt) {
    struct br_motion_config c;
    br_power_config_get(&c);
    zcbor_state_t *z = ctxt->writer->zs;
    bool ok = zcbor_tstr_put_lit(z, "moving_ms") && zcbor_uint32_put(z, c.moving_ms) &&
        zcbor_tstr_put_lit(z, "idle_ms") && zcbor_uint32_put(z, c.idle_ms) &&
        zcbor_tstr_put_lit(z, "quiet_ms") && zcbor_uint32_put(z, c.quiet_ms) &&
        zcbor_tstr_put_lit(z, "threshold_mg") && zcbor_uint32_put(z, c.threshold_mg) &&
        zcbor_tstr_put_lit(z, "duration_samples") && zcbor_uint32_put(z, c.duration_samples);
    return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}
static int config_write(struct smp_streamer *ctxt) {
    if (!boot_is_img_confirmed()) return MGMT_ERR_EBUSY;
    struct br_motion_config c = {0};
    struct zcbor_map_decode_key_val fields[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("moving_ms", zcbor_uint32_decode, &c.moving_ms),
        ZCBOR_MAP_DECODE_KEY_DECODER("idle_ms", zcbor_uint32_decode, &c.idle_ms),
        ZCBOR_MAP_DECODE_KEY_DECODER("quiet_ms", zcbor_uint32_decode, &c.quiet_ms),
        ZCBOR_MAP_DECODE_KEY_DECODER("threshold_mg", zcbor_uint32_decode, &c.threshold_mg),
        ZCBOR_MAP_DECODE_KEY_DECODER("duration_samples", zcbor_uint32_decode, &c.duration_samples),
    };
    size_t matched = 0;
    if (zcbor_map_decode_bulk(ctxt->reader->zs, fields, ARRAY_SIZE(fields), &matched) ||
        matched != ARRAY_SIZE(fields) || !br_motion_config_valid(&c)) return MGMT_ERR_EINVAL;
    if (br_power_config_save(&c)) return MGMT_ERR_EUNKNOWN;
    /* Saved, then applied by main owner; status exposes pending/sensor_error. */
    return config_read(ctxt);
}
static int trial_write(struct smp_streamer *ctxt) {
    ARG_UNUSED(ctxt);
    if (!boot_is_img_confirmed()) return MGMT_ERR_EBUSY;
    return boot_request_upgrade(BOOT_UPGRADE_TEST) ? MGMT_ERR_EUNKNOWN : MGMT_ERR_EOK;
}
static const struct mgmt_handler handlers[] = {
    { .mh_read = status_read }, { .mh_write = trial_write },
    { .mh_read = config_read, .mh_write = config_write },
};
static struct mgmt_group group = {
    .mg_handlers = handlers, .mg_handlers_count = ARRAY_SIZE(handlers), .mg_group_id = 64,
};

int br_ota_init(void) {
    uint32_t provision[5];
    /* Consume even malformed/stale requests before any stack/settings writes. */
    br_ota_consume_cookie(ota_provision_request, provision);
    br_update_policy_init(&policy, k_uptime_get(), !boot_is_img_confirmed());
    ota_diag.watchdog = NRF_WDT0->RUNSTATUS;
    if (!ota_diag.watchdog) return -ENODEV;
    hwinfo_get_reset_cause(&reset_reason);
    ota_diag.reset_reason = reset_reason;
    hwinfo_clear_reset_cause();
    snprintf(hardware_id, sizeof(hardware_id), "%08x%08x",
             (unsigned)NRF_FICR->DEVICEID[0], (unsigned)NRF_FICR->DEVICEID[1]);
    if (!gpio_is_ready_dt(&key)) return -ENODEV;
    int rc = gpio_pin_configure_dt(&key, GPIO_INPUT);
    if (rc) return rc;
    gpio_init_callback(&key_callback, key_changed, BIT(key.pin));
    if ((rc = gpio_add_callback(key.port, &key_callback)) ||
        (rc = gpio_pin_interrupt_configure_dt(&key, GPIO_INT_EDGE_BOTH))) return rc;
    if ((rc = settings_subsys_init()) || (rc = settings_register(&settings)) ||
        (rc = br_power_settings_init()) ||
        (rc = bt_conn_auth_cb_register(&auth)) || (rc = bt_enable(NULL)) ||
        (rc = settings_load())) return rc;
    /* Some settings backends continue enumeration after a handler rejects data. */
    if (identity_error) return identity_error;
    int requested_tag = br_ota_provision(provision, loaded, identity_error != 0,
                                        NRF_FICR->DEVICEID[0], NRF_FICR->DEVICEID[1]);
    if (requested_tag < 0) return -EACCES;
    if (requested_tag > 0) {
        uint32_t random;
        if ((rc = sys_csrand_get(&random, sizeof(random)))) return rc;
        identity.magic = 0x42524944; identity.schema = 1; identity.tag = requested_tag;
        identity.passkey = random % 1000000;
        if ((rc = settings_save_one("binrange/identity", &identity, sizeof(identity)))) return rc;
        br_ota_local(&access, k_uptime_get());
    }
    ota_pairing_pin = identity.passkey;
    mgmt_callback_register(&command_callback);
    mgmt_register_group(&group);
    ble_ok = false; /* First successful advertisement establishes availability. */
    ota_diag.ready = 1;
    return 0;
}
uint16_t br_ota_tag_id(void) { return identity.tag; }
bool br_ota_trial(void) { return !boot_is_img_confirmed(); }
uint32_t br_ota_poll_interval(void) {
    k_mutex_lock(&lock, K_FOREVER);
    uint32_t interval = peer || key_was_down ? 100 :
        policy.trial || access.open || !advertising ? 1000 : 10000;
    k_mutex_unlock(&lock);
    return interval;
}
bool br_ota_radio_paused(void) {
    k_mutex_lock(&lock, K_FOREVER);
    bool paused = peer != NULL;
    k_mutex_unlock(&lock);
    return paused;
}
void br_ota_poll(bool radio_healthy) {
    static uint64_t key_down_ms;
    bool key_down = gpio_pin_get_dt(&key) > 0;
    struct bt_conn *drop = NULL;
    k_mutex_lock(&lock, K_FOREVER);
    /* Connection callbacks update timestamps under this same mutex. Sampling
     * before acquisition can predate their state and underflow elapsed time. */
    uint64_t now = k_uptime_get();
    if (key_down && !key_was_down) key_down_ms = now;
    if ((!key_down && key_was_down && now-key_down_ms >= 1000) ||
        ota_local_request == 0x4d41494e) {
        br_ota_local(&access, now);
        ota_local_request = 0;
    }
    key_was_down = key_down;
    br_ota_expire(&access, now);
    ota_diag.maintenance = br_ota_access_allowed(&access, now, true);
    if (peer && ((!authorized && now-connected_ms >= 10000) ||
        (authorized && !br_ota_access_allowed(&access, now, true))))
        drop = bt_conn_ref(peer);
    bool want_fast = br_ota_pairing_allowed(&access, now);
    if (!peer && advertising && fast_ad != want_fast) {
        int rc = bt_le_adv_stop();
        br_ota_ad_result(&ble_ok, rc);
        if (!rc) { advertising = false; next_ad_ms = now; }
        else ota_diag.error = rc;
    }
    if (!peer && !advertising && atomic_get(&connection_available) && now >= next_ad_ms) {
        const struct bt_le_adv_param *params = want_fast ? BT_LE_ADV_CONN_FAST_1 :
            BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN, 3200, 3360, NULL); /* 2.0–2.1 s */
        int rc = bt_le_adv_start(params, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
        br_ota_ad_result(&ble_ok, rc);
        if (!rc) { advertising = true; fast_ad = want_fast; }
        else ota_diag.error = rc;
        next_ad_ms = now + 5000;
    }
    /* Observe every start/stop result before deciding to confirm this image. */
    struct br_power_status power;
    br_power_status_get(&power);
    if (br_ota_health(&policy, now, radio_healthy && !power.sensor_error, ble_ok)) {
        int rc = boot_write_img_confirmed();
        ota_diag.error = rc;
        if (!rc) policy.trial = false;
    }
    ota_diag.confirmed = boot_is_img_confirmed();
    /* An unconfirmed app that cannot become locally healthy must reset/revert. */
    bool feed = !policy.trial || now-policy.boot_ms < 90000;
    k_mutex_unlock(&lock);
    if (drop) { bt_conn_disconnect(drop, BT_HCI_ERR_REMOTE_USER_TERM_CONN); bt_conn_unref(drop); }
    if (feed) NRF_WDT0->RR[0] = 0x6e524635;
}
