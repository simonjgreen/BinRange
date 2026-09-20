"""Offline tests for the pinned NimBLE build patch integration."""

import hashlib
import configparser
import os
import shlex
import pathlib
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from unittest.mock import patch

import anchor_ble_build as ble


ROOT = pathlib.Path(__file__).resolve().parents[1]
REFERENCE_CANDIDATES = (
    *((pathlib.Path(os.environ["BINRANGE_NIMBLE_REFERENCE"]),)
      if os.environ.get("BINRANGE_NIMBLE_REFERENCE") else ()),
    ROOT / "firmware/anchor/.pio/libdeps/anchor/NimBLE-Arduino",
)
PRISTINE_SHA256 = "7e8d063cde46611230bc31bd4e4fb21c932086aa8c65c6271eb43c416d2dee41"


def verified_reference(source_name, expected_hash):
    for reference in REFERENCE_CANDIDATES:
        candidate = reference / source_name
        if candidate.is_file() and hashlib.sha256(candidate.read_bytes()).hexdigest() == expected_hash:
            return candidate
    raise RuntimeError(f"no verified fixture for {source_name} with SHA256 {expected_hash}")


def extract_secure_connection(source):
    start = source.index("bool NimBLEClient::secureConnection(bool async) const {")
    end = source.index("} // secureConnection", start) + len("} // secureConnection")
    return source[start:end]


def extract_missing_key_branch(source):
    start = source.index("if (event->enc_change.status == BLE_HS_HCI_ERR(BLE_ERR_PINKEY_MISSING)) {")
    end = source.index("\n                } else {", start)
    return source[start:end + len("\n                }")]


def extract_success_branch(source):
    start = source.index("} else {\n                    pClient->m_asyncSecureAttempt = 0;")
    end = source.index("\n                }", start) + len("\n                }")
    return source[start + 2:end]


def extract_sm_completion_region(source):
    start = source.index("        if (proc == NULL) {\n            break;\n        }\n")
    end = source.index("\n        if (!res->execute) {", start)
    return source[start:end]


class AnchorBleBuildTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tempdir.name)

    def tearDown(self):
        self.tempdir.cleanup()

    def test_first_pairing_feature_records_fit_all_six_bins_and_dev_tag(self):
        # Reproduce the live type=CSFC, stage=write, rc=ESTORE_CAP failure
        # through the pinned store implementation and actual ESP32 build flags.
        library = ROOT / "firmware/anchor/.pio/libdeps/anchor/NimBLE-Arduino"
        source = (library / "src/nimble/nimble/host/store/config/src/ble_store_config.c").read_text()
        cfg = (library / "src/nimble/esp_port/port/include/esp_nimble_cfg.h").read_text()
        def function(signature):
            start = source.index(signature)
            return source[start:source.index("\n}\n", start) + 3]
        start = cfg.index("#ifndef MYNEWT_VAL_BLE_STORE_MAX_CSFCS")
        default = cfg[start:cfg.index("#endif", start) + 6]
        project = configparser.ConfigParser()
        project.read(ROOT / "firmware/anchor/platformio.ini")
        flags = shlex.split(project['esp32']['build_flags'])
        harness = self.root / "csfc.c"
        harness.write_text(textwrap.dedent(f"""
            #include <stdint.h>
            #include <stdio.h>
            #include <string.h>
            #define MYNEWT_VAL(x) MYNEWT_VAL_ ## x
            #define BLE_HS_ESTORE_CAP 27
            #define BLE_HS_LOG(...)
            {default}
            typedef struct {{ uint8_t type, val[6]; }} ble_addr_t;
            static const ble_addr_t any={{0}};
            #define BLE_ADDR_ANY (&any)
            int ble_addr_cmp(const ble_addr_t *a,const ble_addr_t *b) {{return memcmp(a,b,sizeof(*a));}}
            struct ble_store_key_csfc {{ble_addr_t peer_addr; uint8_t idx;}};
            struct ble_store_value_csfc {{ble_addr_t peer_addr; uint8_t csfc[1];}};
            struct ble_store_value_csfc ble_store_config_csfcs[MYNEWT_VAL(BLE_STORE_MAX_CSFCS)];
            int ble_store_config_num_csfcs, persistence_rc;
            int ble_store_config_persist_csfcs(void) {{return persistence_rc;}}
            void ble_store_key_from_value_csfc(struct ble_store_key_csfc *k,const struct ble_store_value_csfc *v)
            {{k->peer_addr=v->peer_addr;k->idx=0;}}
            {function('static int\nble_store_config_find_csfc(')}
            {function('static int\nble_store_config_write_csfc(')}
            int main(void) {{
                struct ble_store_value_csfc value={{0}};
                for(int peer=1;peer<=7;++peer) {{
                    value.peer_addr.type=1;value.peer_addr.val[0]=peer;
                    int rc=ble_store_config_write_csfc(&value);
                    if(rc) {{fprintf(stderr,"first pairing peer %d: store rc %d\\n",peer,rc);return 1;}}
                }}
                if(ble_store_config_num_csfcs!=7 || ble_store_config_csfcs[0].peer_addr.val[0]!=1) return 2;
                persistence_rc=77;
                if(ble_store_config_write_csfc(&value)!=77) return 3;
                return 0;
            }}
        """))
        binary = self.root / "csfc"
        subprocess.run(["cc", "-std=c99", *flags, str(harness), "-o", str(binary)], check=True)
        run = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)

    def make_library(self, *, name="NimBLE-Arduino", version="2.5.1"):
        library = self.root / "libdeps" / "anchor" / "NimBLE-Arduino"
        (library / "src/nimble/nimble/host/src").mkdir(parents=True, exist_ok=True)
        shutil.copy2(verified_reference(ble.SOURCE_NAME, ble.PRISTINE_SHA256),
                     library / ble.SOURCE_NAME)
        shutil.copy2(verified_reference(ble.SM_SOURCE_NAME, ble.SM_PRISTINE_SHA256),
                     library / ble.SM_SOURCE_NAME)
        (library / ble.NVS_SOURCE_NAME).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(verified_reference(ble.NVS_SOURCE_NAME, ble.NVS_PRISTINE_SHA256),
                     library / ble.NVS_SOURCE_NAME)
        (library / "library.properties").write_text(
            f"name={name}\nversion={version}\n", encoding="utf-8")
        return library

    def test_pristine_source_is_patched_and_hash_is_exact(self):
        library = self.make_library()
        result = ble.validate_and_patch(library)
        patched = library / "src/NimBLEClient.cpp"
        patched_sm = library / ble.SM_SOURCE_NAME
        self.assertEqual(result, "patched")
        self.assertNotEqual(hashlib.sha256(patched.read_bytes()).hexdigest(), PRISTINE_SHA256)
        self.assertEqual(hashlib.sha256(patched.read_bytes()).hexdigest(), ble.PATCHED_SHA256)
        self.assertEqual(hashlib.sha256(patched_sm.read_bytes()).hexdigest(), ble.SM_PATCHED_SHA256)
        self.assertEqual(hashlib.sha256((library / ble.NVS_SOURCE_NAME).read_bytes()).hexdigest(),
                         ble.NVS_PATCHED_SHA256)

    def test_previous_exact_sm_patch_migrates_without_allowing_unknown_edits(self):
        library = self.make_library()
        pristine = (library / ble.SM_SOURCE_NAME).read_text()
        latest = ble._patch_sm_source(pristine)
        start = pristine.index("static void\nble_sm_sec_req_rx(")
        end = pristine.index("\n}\n", start) + 3
        new_start = latest.index("static void\nble_sm_sec_req_rx(")
        new_end = latest.index("\n}\n", new_start) + 3
        previous = latest[:new_start] + pristine[start:end] + latest[new_end:]
        self.assertEqual(hashlib.sha256(previous.encode()).hexdigest(), ble.SM_PREVIOUS_SHA256)
        (library / ble.SM_SOURCE_NAME).write_text(previous)
        ble.validate_and_patch(library)
        self.assertEqual((library / ble.SM_SOURCE_NAME).read_text(), latest)

    def test_restore_error_is_exposed_and_fresh_database_is_valid(self):
        library = self.make_library()
        ble.validate_and_patch(library)
        source = (library / ble.NVS_SOURCE_NAME).read_text()
        start = source.index("static int binrange_restore_status")
        end = source.index("\n}\n", source.index("void ble_store_config_conf_init", start)) + 3
        harness = self.root / "restore.c"
        harness.write_text(textwrap.dedent(f"""
            #include <assert.h>
            #define BLE_HS_ESTORE_FAIL 31
            #define MYNEWT_VAL(x) 1
            #define ESP_LOGE(...)
            int keys_rc, peers_rc, peer_calls;
            int ble_nvs_restore_sec_keys(void) {{ return keys_rc; }}
            int ble_nvs_restore_peer_records(void) {{ ++peer_calls; return peers_rc; }}
            {source[start:end]}
            int main(void) {{
                assert(binrange_bond_restore_status()!=0);
                keys_rc=5; ble_store_config_conf_init();
                assert(binrange_bond_restore_status()==5 && peer_calls==0);
                keys_rc=0; peers_rc=6; ble_store_config_conf_init();
                assert(binrange_bond_restore_status()==6 && peer_calls==1);
                peers_rc=0; ble_store_config_conf_init();
                assert(binrange_bond_restore_status()==0);
            }}
        """))
        binary = self.root / "restore"
        subprocess.run(["cc", "-std=c99", str(harness), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

    def test_patched_source_is_idempotent_without_rewriting(self):
        library = self.make_library()
        ble.validate_and_patch(library)
        source = library / "src/NimBLEClient.cpp"
        sm_source = library / ble.SM_SOURCE_NAME
        before = source.stat()
        sm_before = sm_source.stat()
        contents = source.read_bytes()
        sm_contents = sm_source.read_bytes()
        self.assertEqual(ble.validate_and_patch(library), "already-patched")
        self.assertEqual(source.read_bytes(), contents)
        self.assertEqual(sm_source.read_bytes(), sm_contents)
        self.assertEqual(source.stat().st_mtime_ns, before.st_mtime_ns)
        self.assertEqual(sm_source.stat().st_mtime_ns, sm_before.st_mtime_ns)

    def test_modified_source_is_rejected_without_write(self):
        library = self.make_library()
        source = library / "src/NimBLEClient.cpp"
        source.write_bytes(source.read_bytes() + b"\n// unexpected change\n")
        before = source.read_bytes()
        with self.assertRaises(ble.PatchError):
            ble.validate_and_patch(library)
        self.assertEqual(source.read_bytes(), before)

    def test_bad_generated_replacement_is_rejected_without_write(self):
        library = self.make_library()
        source = library / "src/NimBLEClient.cpp"
        before = source.read_bytes()
        with patch.object(ble, "_patch_source", return_value="bad replacement"), \
                self.assertRaises(ble.PatchError):
            ble.validate_and_patch(library)
        self.assertEqual(source.read_bytes(), before)

    def test_mutated_sm_source_is_rejected_without_writing_either_source(self):
        library = self.make_library()
        client = library / ble.SOURCE_NAME
        sm_source = library / ble.SM_SOURCE_NAME
        sm_source.write_bytes(sm_source.read_bytes() + b"\n/* unexpected change */\n")
        before_client = client.read_bytes()
        before_sm = sm_source.read_bytes()
        with self.assertRaises(ble.PatchError):
            ble.validate_and_patch(library)
        self.assertEqual(client.read_bytes(), before_client)
        self.assertEqual(sm_source.read_bytes(), before_sm)

    def test_wrong_or_duplicate_properties_are_rejected_without_write(self):
        for properties in (
            "name=NimBLE-Arduino\nversion=2.5.0\n",
            "name=NimBLE-Arduino\nname=NimBLE-Arduino\nversion=2.5.1\n",
            "name=Other\nversion=2.5.1\n",
        ):
            with self.subTest(properties=properties):
                library = self.make_library()
                (library / "library.properties").write_text(properties, encoding="utf-8")
                source = library / "src/NimBLEClient.cpp"
                before = source.read_bytes()
                with self.assertRaises(ble.PatchError):
                    ble.validate_and_patch(library)
                self.assertEqual(source.read_bytes(), before)

    def test_missing_source_and_properties_fail_without_creating_files(self):
        library = self.root / "libdeps" / "anchor" / "NimBLE-Arduino"
        library.mkdir(parents=True)
        with self.assertRaises(ble.PatchError):
            ble.validate_and_patch(library)
        self.assertFalse((library / "src/NimBLEClient.cpp").exists())

    def test_only_requested_environment_library_is_used(self):
        anchor = self.make_library()
        other = self.root / "libdeps" / "other" / "NimBLE-Arduino"
        other.mkdir(parents=True)
        self.assertEqual(ble.library_path(self.root / "libdeps", "anchor"), anchor)
        self.assertNotEqual(ble.library_path(self.root / "libdeps", "anchor"), other)

    def test_cpp_behavioral_harness_uses_patched_source(self):
        library = self.make_library()
        ble.validate_and_patch(library)
        source = (library / "src/NimBLEClient.cpp").read_text(encoding="utf-8")
        secure = extract_secure_connection(source)
        missing = extract_missing_key_branch(source)
        success = extract_success_branch(source)
        secure_body = secure[secure.index("{") + 1:secure.rindex("} // secureConnection")]
        harness = self.root / "harness.cpp"
        harness.write_text(textwrap.dedent(f"""
            #include <cassert>
            #include <string>
            #define BLE_HS_ENOTSUP 95
            #define BLE_ERR_PINKEY_MISSING 0x06
            #define BLE_HS_HCI_ERR(x) (x)
            #define NIMBLE_LOGD(...)
            #define NIMBLE_LOGE(...)
            struct Event {{ struct Enc {{ int status; }} enc_change; }};
            struct Info {{}};
            struct Callback {{ int calls = 0; void onAuthenticationComplete(Info) {{ ++calls; }} }};
            struct NimBLEDevice {{
                static int calls; static int next; static bool startSecurity(int, int* rc) {{ ++calls; *rc = next; return next == 0; }}
            }};
            int NimBLEDevice::calls = 0; int NimBLEDevice::next = 0;
            struct FakeClient {{
                mutable int m_lastErr = 0; mutable int m_asyncSecureAttempt = 0; int m_connHandle = 1;
                Callback callback; Callback* m_pClientCallbacks = &callback;
                bool secureConnection(bool async) const {{
                    {secure_body}
                }}
            }};
            int deletes = 0; int retries = 0;
            int ble_gap_conn_find(int, Info*) {{ return 0; }}
            void ble_store_util_delete_peer(void*) {{ ++deletes; }}
            void retry_security() {{ ++retries; }}
            void run_missing(FakeClient* pClient, Event* event) {{
                Info peerInfo;
                {missing}
            }}
            void run_success(FakeClient* pClient, Event* event) {{
                Info peerInfo;
                if (false) {{ }} {success}
            }}
            int main() {{
                FakeClient client;
                NimBLEDevice::calls = 0; NimBLEDevice::next = 0;
                assert(!client.secureConnection(false));
                assert(client.m_lastErr == BLE_HS_ENOTSUP && NimBLEDevice::calls == 0);
                assert(client.secureConnection(true));
                assert(NimBLEDevice::calls == 1 && client.m_asyncSecureAttempt == 1);
                NimBLEDevice::next = 17;
                assert(!client.secureConnection(true));
                assert(NimBLEDevice::calls == 2 && client.m_lastErr == 17 && client.m_asyncSecureAttempt == 0);
                Event event{{{{BLE_HS_HCI_ERR(BLE_ERR_PINKEY_MISSING)}}}};
                client.m_asyncSecureAttempt = 1;
                run_missing(&client, &event);
                assert(deletes == 0 && retries == 0 && client.m_asyncSecureAttempt == 0);
                assert(client.m_lastErr == event.enc_change.status);
                client.m_asyncSecureAttempt = 1;
                client.callback.calls = 0;
                event.enc_change.status = 0;
                run_success(&client, &event);
                assert(client.m_asyncSecureAttempt == 0 && client.callback.calls == 1);
                return 0;
            }}
        """), encoding="utf-8")
        binary = self.root / "harness"
        subprocess.run(["c++", "-std=c++17", str(harness), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

    def test_sm_key_missing_uses_normal_failure_cleanup_without_repair(self):
        library = self.make_library()
        ble.validate_and_patch(library)
        source = (library / ble.SM_SOURCE_NAME).read_text(encoding="utf-8")
        completion = extract_sm_completion_region(source)
        harness = self.root / "sm_harness.c"
        harness.write_text(textwrap.dedent(f"""
            #include <assert.h>
            #include <stddef.h>
            #define BLE_HS_ENOTCONN 7
            #define BLE_SM_PROC_F_BONDING 1
            #define BLE_SM_IOACT_NONE 0
            #define BLE_HS_DBG_ASSERT(x) assert(x)
            typedef struct {{ unsigned bhc_flags; }} ble_hs_conn;
            typedef struct {{ unsigned flags; }} ble_sm_proc;
            typedef struct {{ int action; }} passkey_params_t;
            typedef struct {{ int app_status; int sm_err; int enc_cb; int restore; int bonded; int execute; passkey_params_t passkey_params; }} ble_sm_result;
            int pairing_complete_calls, enc_event_calls, proc_free_calls, pair_calls, slave_calls;
            int pairing_complete_error, encryption_status;
            void ble_gap_pairing_complete_event(int h, int e) {{ pairing_complete_calls++; pairing_complete_error = e; }}
            void ble_gap_enc_event(int h, int s, int r, int b) {{ enc_event_calls++; encryption_status = s; }}
            void ble_sm_proc_free(ble_sm_proc *p) {{ proc_free_calls++; }}
            void ble_sm_persist_keys(ble_sm_proc *p) {{ assert(0); }}
            void ble_gap_passkey_event(int h, passkey_params_t *p) {{ assert(0); }}
            void ble_sm_pair_initiate(int h) {{ pair_calls++; }}
            void ble_sm_slave_initiate(int h) {{ slave_calls++; }}
            void run(int status, int enc_cb) {{
                int conn_handle = 1, rm = 1; ble_sm_proc proc_value = {{0}}, *proc = &proc_value;
                ble_sm_result result = {{ .app_status = status, .sm_err = 19, .enc_cb = enc_cb }};
                ble_sm_result *res = &result;
                while (1) {{
            {completion}
                    break;
                }}
            }}
            int main(void) {{
                run(518, 1);
                assert(pair_calls == 0 && slave_calls == 0);
                assert(pairing_complete_calls == 1 && enc_event_calls == 1 && proc_free_calls == 1);
                assert(pairing_complete_error == 19 && encryption_status == 518);
                pairing_complete_calls = enc_event_calls = proc_free_calls = 0;
                run(0, 1);
                assert(pairing_complete_calls == 1 && enc_event_calls == 1 && proc_free_calls == 1);
                assert(pairing_complete_error == 19 && encryption_status == 0);
                return 0;
            }}
        """), encoding="utf-8")
        binary = self.root / "sm_harness"
        subprocess.run(["cc", "-std=c99", str(harness), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

    def test_peer_security_request_never_starts_security_or_disturbs_active_procedure(self):
        library = self.make_library()
        ble.validate_and_patch(library)
        source = (library / ble.SM_SOURCE_NAME).read_text()
        start = source.index("static void\nble_sm_sec_req_rx(")
        handler = source[start:source.index("\n}\n", start) + 3]
        harness = self.root / "security_request.c"
        harness.write_text(textwrap.dedent(f"""
            #include <assert.h>
            #include <stdint.h>
            #include <string.h>
            #define BLE_HS_CONN_F_MASTER 1
            #define BLE_HS_SM_US_ERR(x) (x)
            #define BLE_SM_ERR_CMD_NOT_SUPP 7
            #define BLE_SM_PAIR_AUTHREQ_BOND 1
            #define BLE_SM_PAIR_AUTHREQ_MITM 4
            #define BLE_HS_ENOENT 5
            #define BLE_HS_EREJECT 6
            #define BLE_SM_PROC_STATE_NONE 0
            struct ble_sm_sec_req {{ uint8_t authreq; }};
            struct os_mbuf {{ void *om_data; }};
            struct ble_sm_result {{ int app_status, out_of_order, sm_err; }};
            struct ble_store_value_sec {{ int key_size, ltk, ediv, rand_num, authenticated; }};
            struct ble_store_key_sec {{ int peer_addr; }};
            struct ble_hs_conn_addrs {{ int peer_id_addr; }};
            struct ble_hs_conn {{ int bhc_flags; }} connection = {{1}};
            struct ble_sm_proc {{ int untouched; }} procedure = {{42}};
            int pullup_error, bond_present, active, starts;
            int ble_hs_mbuf_pullup_base(void *p, int n) {{ return pullup_error; }}
            void ble_hs_lock(void) {{}} void ble_hs_unlock(void) {{}}
            struct ble_hs_conn *ble_hs_conn_find_assert(int h) {{ return &connection; }}
            void ble_hs_conn_addrs(void *c, struct ble_hs_conn_addrs *a) {{ a->peer_id_addr=1; }}
            int ble_store_read_peer_sec(void *k, struct ble_store_value_sec *v) {{
                memset(v, 0, sizeof(*v)); v->authenticated=1; return bond_present ? 0 : 5;
            }}
            struct ble_sm_proc *ble_sm_proc_find(int a,int b,int c,void *d) {{ return active ? &procedure : 0; }}
            int ble_sm_enc_initiate(int a,int b,int c,int d,int e,int f) {{ ++starts; return 0; }}
            int ble_sm_pair_initiate(int h) {{ ++starts; return 0; }}
            {handler}
            int main(void) {{
                for (int flags=0; flags<8; ++flags) for(bond_present=0;bond_present<2;++bond_present)
                for(active=0;active<2;++active) {{
                    struct ble_sm_sec_req request={{flags}};
                    struct os_mbuf buffer={{&request}}, *p=&buffer;
                    struct ble_sm_result result={{0}};
                    ble_sm_sec_req_rx(1,&p,&result);
                    assert(starts==0 && result.out_of_order && result.app_status==0);
                    assert(procedure.untouched==42);
                }}
                pullup_error=9; struct ble_sm_result result={{0}};
                ble_sm_sec_req_rx(1,0,&result);
                assert(result.app_status==9 && !result.out_of_order && starts==0);
            }}
        """))
        binary = self.root / "security_request"
        subprocess.run(["cc", "-std=c99", str(harness), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

    def test_actual_bond_write_guard_latches_errors_and_rejects_key_replacement(self):
        source = (ROOT / "firmware/anchor/src/update_ble.cpp").read_text()
        start = source.index("void erase(")
        end = source.index("\nenum class Step", start)
        guard = source[start:end]
        header = (REFERENCE_CANDIDATES[0] / "src/nimble/nimble/host/include/host/ble_store.h").read_text()
        sec_start = header.index("struct ble_store_value_sec {")
        sec = header[sec_start:header.index("\n};", sec_start) + 3]
        harness = self.root / "bond_guard.cpp"
        harness.write_text(textwrap.dedent(f"""
            #include <cassert>
            #include <atomic>
            #include <cstdint>
            #include <cstring>
            #include "{ROOT / 'firmware/anchor/src/update_ble.h'}"
            #define BLE_HS_ESTORE_FAIL 31
            #define BLE_HS_ENOENT 5
            #define BLE_STORE_OBJ_TYPE_OUR_SEC 1
            #define BLE_STORE_OBJ_TYPE_PEER_SEC 2
            struct ble_addr_t {{ uint8_t type, val[6]; }};
            {sec}
            union ble_store_value {{ ble_store_value_sec sec; }};
            union ble_store_key {{ struct {{ ble_addr_t peer_addr; uint8_t idx; }} sec; }};
            typedef int ble_store_write_fn(int, const ble_store_value*);
            typedef int ble_store_read_fn(int, const ble_store_key*, ble_store_value*);
            {guard}
            int reads, writes, read_rc=BLE_HS_ENOENT, write_rc;
            ble_store_value saved{{}};
            int read_backend(int, const ble_store_key*, ble_store_value *out) {{ ++reads; *out=saved; return read_rc; }}
            int write_backend(int, const ble_store_value*) {{ ++writes; return write_rc; }}
            int main() {{
                bond_backend_read=read_backend; bond_backend_write=write_backend;
                ble_store_value value{{}}; value.sec.ltk_present=1; value.sec.ltk[0]=42;
                assert(checked_bond_write(2,&value)==BLE_HS_ESTORE_FAIL && !writes);
                bond_store_failed=false;
                assert(checked_bond_write(2,&value)==0 && writes==1 && !bond_store_failed);
                write_rc=77;
                assert(checked_bond_write(2,&value)==77 && writes==2 && bond_store_failed);
                assert(bond_failure_location==0x00020003 && bond_failure_rc==77);
                assert(first_worker_fault==UpdateBleFault::BondStore);
                record_first_fault(UpdateBleFault::DisconnectTimeout);
                assert(first_worker_fault==UpdateBleFault::BondStore);
                assert(checked_bond_write(2,&value)==BLE_HS_ESTORE_FAIL && writes==2);
                assert(bond_failure_rc==77); // later rejected calls retain the cause
                // Simulated restart with successfully restored original bond.
                bond_store_failed=false; read_rc=0; saved=value; value.sec.bond_count=99;
                assert(checked_bond_write(2,&value)==0 && writes==2 && !bond_store_failed);
                value.sec.ltk[0]=43;
                assert(checked_bond_write(2,&value)==BLE_HS_ESTORE_FAIL && writes==2 && bond_store_failed);
                assert(bond_failure_location==0x00020002 && bond_failure_rc==BLE_HS_ESTORE_FAIL);
                bond_store_failed=false; read_rc=88;
                assert(checked_bond_write(1,&value)==88 && writes==2 && bond_store_failed);
                assert(bond_failure_location==0x00010001 && bond_failure_rc==88);
                bond_store_failed=false; write_rc=79;
                assert(checked_bond_write(9,&value)==79 && writes==3 && bond_store_failed);
                assert(bond_failure_location==0x00090003 && bond_failure_rc==79);
                assert(checked_bond_write(9,&value)==BLE_HS_ESTORE_FAIL && writes==3);
            }}
        """))
        binary = self.root / "bond_guard"
        subprocess.run(["c++", "-std=c++17", "-I", str(ROOT / "firmware/anchor/src"),
                        str(harness), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
