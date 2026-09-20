"""Validate and apply the narrowly pinned NimBLE-Arduino security patch."""

import hashlib
import os
import pathlib
import tempfile


LIBRARY_NAME = "NimBLE-Arduino"
LIBRARY_VERSION = "2.5.1"
SOURCE_NAME = "src/NimBLEClient.cpp"
PRISTINE_SHA256 = "7e8d063cde46611230bc31bd4e4fb21c932086aa8c65c6271eb43c416d2dee41"
PATCHED_SHA256 = "760a71cfc971e2f0c98dee02b8df1a3a342e9c3b0fd4fc289bba6567dddd9c75"
SM_SOURCE_NAME = "src/nimble/nimble/host/src/ble_sm.c"
SM_PRISTINE_SHA256 = "f88810ae6fcdf1f499efcb1471b18b36a154e7cd67dd45145b70bd4822d120da"
SM_PREVIOUS_SHA256 = "e4c50f5bf1e4ec6bad1325c7494eb943ad51a7bef580bc6a35b23d3338677c11"
SM_PATCHED_SHA256 = "c7e20bc9b34bfea23f6f0013fe1cc9e6a0021e0d3fc48008436a08d7727f37b2"
NVS_SOURCE_NAME = "src/nimble/nimble/host/store/config/src/ble_store_nvs.c"
NVS_PRISTINE_SHA256 = "e4d61d3b6403e263d40f2f498c4d6ac64ee1727ccd3fd6f040ba0e1908c26144"
NVS_PATCHED_SHA256 = "7361ad0f5c21894c347e66c36363b5877e0284e2974cce66baf09b96b221f871"


class PatchError(RuntimeError):
    pass


def library_path(project_libdeps_dir, pioenv):
    return pathlib.Path(project_libdeps_dir) / pioenv / LIBRARY_NAME


def _read_properties(path):
    values = {}
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise PatchError(f"cannot read library.properties: {path}") from exc
    for line in lines:
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        key, value = key.strip(), value.strip()
        if key in values:
            raise PatchError(f"ambiguous library property: {key}")
        values[key] = value
    if values.get("name") != LIBRARY_NAME or values.get("version") != LIBRARY_VERSION:
        raise PatchError("library.properties does not identify NimBLE-Arduino 2.5.1")


def _patch_source(source):
    old_secure = '''bool NimBLEClient::secureConnection(bool async) const {
    NIMBLE_LOGD(LOG_TAG, ">> secureConnection()");

    int rc = 0;
    if (async && !NimBLEDevice::startSecurity(m_connHandle, &rc)) {
        m_lastErr            = rc;
        m_asyncSecureAttempt = 0;
        return false;
    }

    if (async) {
        m_asyncSecureAttempt++;
        return true;
    }

    NimBLETaskData taskData(const_cast<NimBLEClient*>(this), BLE_HS_ENOTCONN);
    m_pTaskData    = &taskData;
    int retryCount = 1;
    do {
        if (NimBLEDevice::startSecurity(m_connHandle)) {
            NimBLEUtils::taskWait(taskData, BLE_NPL_TIME_FOREVER);
        }
    } while (taskData.m_flags == BLE_HS_HCI_ERR(BLE_ERR_PINKEY_MISSING) && retryCount--);

    m_pTaskData = nullptr;

    if (taskData.m_flags == 0) {
        NIMBLE_LOGD(LOG_TAG, "<< secureConnection: success");
        return true;
    }

    m_lastErr = taskData.m_flags;
    NIMBLE_LOGE(LOG_TAG,
                "secureConnection: failed rc=%d %s",
                taskData.m_flags,
                NimBLEUtils::returnCodeToString(taskData.m_flags));
    return false;

} // secureConnection'''
    new_secure = '''bool NimBLEClient::secureConnection(bool async) const {
    NIMBLE_LOGD(LOG_TAG, ">> secureConnection()");

    // Anchor policy: only the explicit asynchronous security path is allowed.
    if (!async) {
        m_lastErr = BLE_HS_ENOTSUP;
        return false;
    }

    int rc = 0;
    if (!NimBLEDevice::startSecurity(m_connHandle, &rc)) {
        m_lastErr            = rc;
        m_asyncSecureAttempt = 0;
        return false;
    }

    m_asyncSecureAttempt = 1;
    return true;

} // secureConnection'''
    old_missing = '''if (event->enc_change.status == BLE_HS_HCI_ERR(BLE_ERR_PINKEY_MISSING)) {
                    // Key is missing, try deleting.
                    ble_store_util_delete_peer(&peerInfo.m_desc.peer_id_addr);
                    // Attempt a retry if async secure failed.
                    if (pClient->m_asyncSecureAttempt == 1) {
                        pClient->secureConnection(true);
                    }
                }'''
    new_missing = '''if (event->enc_change.status == BLE_HS_HCI_ERR(BLE_ERR_PINKEY_MISSING)) {
                    // Anchor policy: retain the bond and report the asynchronous failure.
                    pClient->m_asyncSecureAttempt = 0;
                    pClient->m_lastErr = event->enc_change.status;
                }'''
    if source.count(old_secure) != 1 or source.count(old_missing) != 1:
        raise PatchError("NimBLEClient.cpp patch spans are not uniquely present")
    return source.replace(old_secure, new_secure).replace(old_missing, new_missing)


def _patch_sm_source(source):
    old_retry = '''    ble_hs_conn_flags_t conn_flags;
    struct ble_hs_conn *conn;
'''
    new_retry = ''''''
    old_key_missing = '''        if (res->app_status == 518 ) {
            conn = ble_hs_conn_find(conn_handle);

            conn_flags = conn->bhc_flags;

            ble_sm_proc_free(proc);

            if (conn_flags & BLE_HS_CONN_F_MASTER) {
                ble_sm_pair_initiate(conn_handle);
            } else {
                ble_sm_slave_initiate(conn_handle);
            }
            break;
        }

'''
    if source.count(old_retry) != 1 or source.count(old_key_missing) != 1:
        raise PatchError("ble_sm.c key-missing retry spans are not uniquely present")
    return _patch_security_request(source.replace(old_retry, new_retry).replace(old_key_missing, ""))


def _patch_security_request(source):
    start = source.index("static void\nble_sm_sec_req_rx(")
    end = source.index("\n}\n", start) + 3
    return source[:start] + '''static void
ble_sm_sec_req_rx(uint16_t conn_handle, struct os_mbuf **om,
                  struct ble_sm_result *res)
{
    (void)conn_handle;
    res->app_status = ble_hs_mbuf_pullup_base(om, sizeof(struct ble_sm_sec_req));
    if (res->app_status != 0) {
        return;
    }

    /* This central's application owns initiation, including explicit pairing.
     * Ignore peer requests even during an active procedure. out_of_order makes
     * result processing leave that procedure and its callbacks untouched. */
    res->out_of_order = 1;
}
''' + source[end:]


def _validated_replacement(path, pristine_hash, patched_hash, patcher, previous=None):
    try:
        source = path.read_bytes()
    except OSError as exc:
        raise PatchError(f"cannot read pinned source: {path}") from exc
    source_hash = hashlib.sha256(source).hexdigest()
    if source_hash == patched_hash:
        return None
    if previous and source_hash == previous[0]:
        patcher = previous[1]
    elif source_hash != pristine_hash:
        raise PatchError(f"{path.name} is neither pristine nor the exact patched source")
    patched = patcher(source.decode("utf-8")).encode("utf-8")
    if hashlib.sha256(patched).hexdigest() != patched_hash:
        raise PatchError(f"generated {path.name} does not match the pinned patched hash")
    return patched


def _patch_nvs_source(source):
    start = source.index("void ble_store_config_conf_init(void)")
    end = source.index("\n}\n", start) + 3
    replacement = '''/* Anchor observes restoration failure before accepting any BLE work. */
static int binrange_restore_status = BLE_HS_ESTORE_FAIL;
int binrange_bond_restore_status(void)
{
    return __atomic_load_n(&binrange_restore_status, __ATOMIC_ACQUIRE);
}

void ble_store_config_conf_init(void)
{
    int err = ble_nvs_restore_sec_keys();
#if MYNEWT_VAL(BLE_HOST_BASED_PRIVACY)
    if (err == 0) {
        err = ble_nvs_restore_peer_records();
    }
#endif
    __atomic_store_n(&binrange_restore_status, err, __ATOMIC_RELEASE);
    if (err != 0) {
        ESP_LOGE(LOG_TAG, "NVS bond restore failed; anchor BLE disabled");
    }
}
'''
    source = source[:start] + replacement + source[end:]
    # The pristine restore reads index -1 on a fresh database. Empty is valid.
    for side in ("our", "peer"):
        old = f"ble_store_config_{side}_bond_count = ble_store_config_{side}_secs[ble_store_config_num_{side}_secs - 1].bond_count;"
        new = f"ble_store_config_{side}_bond_count = ble_store_config_num_{side}_secs ? ble_store_config_{side}_secs[ble_store_config_num_{side}_secs - 1].bond_count : 0;"
        if source.count(old) != 1:
            raise PatchError("NVS empty-store span is not uniquely present")
        source = source.replace(old, new)
    return source


def _atomic_replace(path, contents):
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as output:
        temporary = pathlib.Path(output.name)
        output.write(contents)
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, path)


def validate_and_patch(library):
    library = pathlib.Path(library)
    _read_properties(library / "library.properties")
    replacements = {
        library / SOURCE_NAME: _validated_replacement(
            library / SOURCE_NAME, PRISTINE_SHA256, PATCHED_SHA256, _patch_source),
        library / SM_SOURCE_NAME: _validated_replacement(
            library / SM_SOURCE_NAME, SM_PRISTINE_SHA256, SM_PATCHED_SHA256, _patch_sm_source,
            (SM_PREVIOUS_SHA256, _patch_security_request)),
        library / NVS_SOURCE_NAME: _validated_replacement(
            library / NVS_SOURCE_NAME, NVS_PRISTINE_SHA256, NVS_PATCHED_SHA256, _patch_nvs_source),
    }
    if all(contents is None for contents in replacements.values()):
        return "already-patched"
    for path, contents in replacements.items():
        if contents is not None:
            _atomic_replace(path, contents)
    return "patched"


def _run(env):
    libdeps = pathlib.Path(env.subst("$PROJECT_LIBDEPS_DIR"))
    validate_and_patch(library_path(libdeps, str(env["PIOENV"])))


try:
    Import("env")
except NameError:
    pass
else:
    _run(env)
