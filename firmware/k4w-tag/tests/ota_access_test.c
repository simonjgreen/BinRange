#include "ota_access.h"
#include <assert.h>
#include <stdio.h>

static void sessions(void) {
    struct br_ota_access a = {0};
    const uint64_t t = UINT64_C(0x100000000);
    assert(!br_ota_pairing_allowed(&a, t));
    assert(!br_ota_attempt(&a, t, false));
    assert(br_ota_attempt(&a, t, true));
    assert(!br_ota_pairing_allowed(&a, t));
    assert(!br_ota_activity(&a, t+59000, false));
    assert(!br_ota_access_allowed(&a, t+60000, true));
    br_ota_expire(&a, t+60001);
    assert(!br_ota_attempt(&a, t+119999, true));
    assert(br_ota_attempt(&a, t+120000, true));
    br_ota_end_attempt(&a, t+120001);
    assert(br_ota_attempt(&a, t+120002, true));
    br_ota_end_attempt(&a, t+120003);
    assert(br_ota_attempt(&a, t+120004, true));
    assert(!br_ota_attempt(&a, t+120005, true));
    assert(br_ota_activity(&a, t+120005, true));
    br_ota_end_attempt(&a, t+120006);
    assert(!br_ota_attempt(&a, t+180005, true));
    assert(!br_ota_local(&a, t+180005));
    assert(br_ota_attempt(&a, t+180006, true));

    a = (struct br_ota_access){0};
    assert(br_ota_local(&a, t));
    assert(br_ota_pairing_allowed(&a, t+59999));
    assert(!br_ota_pairing_allowed(&a, t+60000));
    assert(br_ota_attempt(&a, t, false));
    assert(!br_ota_authenticated(&a, t, false, true));
    assert(!br_ota_authenticated(&a, t, true, false));
    assert(br_ota_authenticated(&a, t, true, true));
    for (uint64_t n = 50000; n < 600000; n += 50000) {
        assert(br_ota_activity(&a, t+n, true));
        if (n == 100000) {
            br_ota_end_attempt(&a, t+n);
            assert(br_ota_attempt(&a, t+n, true));
        }
    }
    assert(!br_ota_access_allowed(&a, t+600000, true));
    assert(!br_ota_attempt(&a, t+659999, true));
    assert(br_ota_attempt(&a, t+660000, true));
}

static void provisioning(void) {
    uint32_t valid[5] = {0x42525056, 0x53445731, 123, 42, 43};
    volatile uint32_t cookie[5];
    uint32_t copy[5];
    for (unsigned i=0; i<5; i++) cookie[i] = valid[i];
    br_ota_consume_cookie(cookie, copy);
    for (unsigned i=0; i<5; i++) assert(cookie[i] == 0 && copy[i] == valid[i]);
    assert(br_ota_provision(copy, false, false, 42, 43) == 123);
    assert(br_ota_provision(copy, true, false, 42, 43) == 0);
    assert(br_ota_provision(copy, false, true, 42, 43) == -1);
    for (unsigned i=0; i<5; i++) {
        uint32_t saved = copy[i]; copy[i] = 0;
        assert(br_ota_provision(copy, false, false, 42, 43) == -1);
        copy[i] = saved;
    }
    copy[2] = 0xffff;
    assert(br_ota_provision(copy, false, false, 42, 43) == -1);
    copy[2] = 0x10001;
    assert(br_ota_provision(copy, false, false, 42, 43) == -1);
    assert(br_ota_provision(copy, true, false, 42, 43) == 0);
    assert(br_ota_provision(copy, true, true, 42, 43) == -1);
    for (unsigned i=0; i<5; i++) copy[i] = 0;
    assert(br_ota_provision(copy, false, false, 42, 43) == -1);
}

static void health(void) {
    struct br_update_policy p;
    bool ble = false;
    br_update_policy_init(&p, 0, true);
    assert(!br_ota_health(&p, 0, true, ble));
    br_ota_ad_result(&ble, 0);
    for (uint64_t t=0; t<30000; t+=1000)
        assert(!br_ota_health(&p, t, true, ble));
    /* A delayed start/stop fault at the confirmation boundary wins. */
    br_ota_ad_result(&ble, -5);
    assert(!br_ota_health(&p, 30000, true, ble));
    br_ota_ad_result(&ble, 0);
    for (uint64_t t=31000; t<61000; t+=1000)
        assert(!br_ota_health(&p, t, true, ble));
    /* Intended idle periods need no client or new advertising event. */
    assert(br_ota_health(&p, 61000, true, ble));
    br_ota_ad_result(&ble, -1);
    assert(!br_ota_health(&p, 62000, true, ble));
}
int main(void) {
    sessions(); provisioning(); health();
    puts("ota access: session, provisioning and health tests passed");
}
