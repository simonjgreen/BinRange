"""Tests for the private anchor BLE identity bootstrap sidecar."""

import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import unittest

import anchor_identity_build
import anchor_identity_pio


class BuildEnvironment:
    def __init__(self, project):
        self.values = {"$PROJECT_DIR": str(project),
                       "$BUILD_DIR": str(project / ".pio" / "build" / "anchor")}
        self.paths = []

    def subst(self, value):
        return self.values[value]

    def Append(self, **values):
        if set(values) != {"CPPPATH"}:
            raise AssertionError("Unexpected compiler flags")
        self.paths.extend(values["CPPPATH"])


class IdentityBuildTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.identity = self.root / "identity.json"
        self.header = self.root / "generated" / "anchor_identity_bootstrap.h"

    def write_identity(self, value):
        self.identity.write_bytes(value)
        self.identity.chmod(0o600)

    def compile_values(self):
        source = self.root / "consumer.cpp"
        source.write_text(
            '#include "anchor_identity_bootstrap.h"\n'
            '#include <cstdint>\n'
            'int main() {\n'
            '  if (!binrange::ble_identity_bootstrap_present) return 1;\n'
            '  return binrange::ble_identity_bootstrap_mac[0] == 0x02 &&\n'
            '    binrange::ble_identity_bootstrap_mac[5] == 0xf0 &&\n'
            '    binrange::ble_identity_bootstrap_address[0] == 0x50 &&\n'
            '    binrange::ble_identity_bootstrap_address[5] == 0xc2 ? 0 : 2;\n'
            '}\n'
        )
        binary = self.root / "consumer"
        result = subprocess.run(["g++", "-std=c++11", "-I", str(self.header.parent),
                                 str(source), "-o", str(binary)], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        return subprocess.run([str(binary)], capture_output=True)

    def test_valid_identity_compiles_with_required_byte_order_and_private_mode(self):
        self.write_identity(b'{"anchor_mac":"02:11:22:33:44:f0",'
                            b'"ble_address":"c2:10:20:30:40:50"}')
        anchor_identity_build.generate_header(self.identity, self.header)
        result = self.compile_values()
        self.assertEqual(result.returncode, 0)
        self.assertEqual(stat.S_IMODE(self.header.stat().st_mode), 0o600)

    def test_omitted_identity_clears_stale_header_and_compiles_disabled(self):
        self.write_identity(b'{"anchor_mac":"02:11:22:33:44:f0",'
                            b'"ble_address":"c2:10:20:30:40:50"}')
        anchor_identity_build.generate_header(self.identity, self.header)
        anchor_identity_build.generate_header(None, self.header)
        source = self.root / "disabled.cpp"
        source.write_text('#include "anchor_identity_bootstrap.h"\n'
                          'int main() { return binrange::ble_identity_bootstrap_present ? 1 : 0; }\n')
        binary = self.root / "disabled"
        result = subprocess.run(["g++", "-std=c++11", "-I", str(self.header.parent),
                                 str(source), "-o", str(binary)], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(subprocess.run([str(binary)]).returncode, 0)

    def test_exact_json_keys_and_duplicate_keys_are_required(self):
        cases = [
            b'{"anchor_mac":"02:11:22:33:44:f0"}',
            b'{"anchor_mac":"02:11:22:33:44:f0","ble_address":"c2:10:20:30:40:50",'
            b'"extra":1}',
            b'{"anchor_mac":"02:11:22:33:44:f0","anchor_mac":"02:11:22:33:44:f1",'
            b'"ble_address":"c2:10:20:30:40:50"}',
        ]
        for value in cases:
            with self.subTest(value=value):
                self.write_identity(value)
                with self.assertRaises(ValueError):
                    anchor_identity_build.generate_header(self.identity, self.header)
                self.assertFalse(self.header.exists())

    def test_rejects_bad_addresses_without_echoing_content_or_path(self):
        cases = [
            ("multicast", b'{"anchor_mac":"03:11:22:33:44:55","ble_address":"c2:10:20:30:40:50"}'),
            ("zero", b'{"anchor_mac":"00:00:00:00:00:00","ble_address":"c2:10:20:30:40:50"}'),
            ("ff", b'{"anchor_mac":"ff:ff:ff:ff:ff:ff","ble_address":"c2:10:20:30:40:50"}'),
            ("not-static", b'{"anchor_mac":"02:11:22:33:44:55","ble_address":"82:00:00:00:00:01"}'),
            ("all-one-tail", b'{"anchor_mac":"02:11:22:33:44:55","ble_address":"ff:ff:ff:ff:ff:ff"}'),
            ("malformed", b'{"anchor_mac":"02:11:22:33:44:55","ble_address":"not-a-real-value"}'),
        ]
        for label, value in cases:
            with self.subTest(label=label):
                if self.header.exists():
                    self.header.unlink()
                self.write_identity(value)
                with self.assertRaises(ValueError) as raised:
                    anchor_identity_build.generate_header(self.identity, self.header)
                self.assertNotIn(label, str(raised.exception))
                self.assertNotIn(value.decode(), str(raised.exception))
                self.assertFalse(self.header.exists())

    def test_rejects_unprivate_symlink_nonregular_and_oversized_inputs(self):
        self.write_identity(b'{"anchor_mac":"02:11:22:33:44:55","ble_address":"c2:10:20:30:40:50"}')
        self.identity.chmod(0o644)
        with self.assertRaises(ValueError):
            anchor_identity_build.generate_header(self.identity, self.header)
        self.identity.chmod(0o600)
        link = self.root / "linked-identity"
        link.symlink_to(self.identity)
        with self.assertRaises(ValueError):
            anchor_identity_build.generate_header(link, self.header)
        fifo = self.root / "identity-fifo"
        os.mkfifo(fifo, 0o600)
        with self.assertRaises(ValueError):
            anchor_identity_build.generate_header(fifo, self.header)
        self.identity.unlink()
        self.write_identity(b"{" + b"x" * 512 + b"}")
        with self.assertRaises(ValueError):
            anchor_identity_build.generate_header(self.identity, self.header)

    def test_invalid_input_never_writes_or_prints(self):
        self.write_identity(b"not-json-with-private-fixture")
        with contextlib.redirect_stdout(io.StringIO()) as stdout:
            with contextlib.redirect_stderr(io.StringIO()) as stderr:
                with self.assertRaises(ValueError):
                    anchor_identity_build.generate_header(self.identity, self.header)
        self.assertEqual(stdout.getvalue(), "")
        self.assertEqual(stderr.getvalue(), "")
        self.assertFalse(self.header.exists())


class IdentityPioTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        repository = Path(self.tmp.name)
        self.root = repository / "firmware" / "anchor"
        self.root.mkdir(parents=True)
        (repository / "scripts").mkdir()
        shutil.copyfile(anchor_identity_build.__file__, repository / "scripts" / "anchor_identity_build.py")
        subprocess.run(["git", "init", "-q", str(repository)], check=True, capture_output=True)
        (self.root / ".gitignore").write_text(".pio/\nidentity.json\n")
        self.env = BuildEnvironment(self.root)
        self.identity = self.root / "identity.json"
        self.identity.write_text('{"anchor_mac":"02:11:22:33:44:55",'
                                 '"ble_address":"c2:10:20:30:40:50"}')
        self.identity.chmod(0o600)

    def test_adapter_generates_private_header_and_only_then_adds_include_path(self):
        anchor_identity_pio.configure(self.env, {"BINRANGE_BLE_IDENTITY_FILE": str(self.identity)})
        output = Path(self.env.paths[0]) / "anchor_identity_bootstrap.h"
        self.assertEqual(output.parent, self.root / ".pio" / "build" / "anchor" / "generated")
        self.assertTrue(output.exists())

    def test_omission_clears_previous_header(self):
        anchor_identity_pio.configure(self.env, {"BINRANGE_BLE_IDENTITY_FILE": str(self.identity)})
        anchor_identity_pio.configure(self.env, {})
        self.assertEqual(len(self.env.paths), 2)
        self.assertIn("ble_identity_bootstrap_present = false",
                      (Path(self.env.paths[-1]) / "anchor_identity_bootstrap.h").read_text())

    def test_empty_or_invalid_explicit_input_fails_before_stale_header_is_used(self):
        anchor_identity_pio.configure(self.env, {"BINRANGE_BLE_IDENTITY_FILE": str(self.identity)})
        self.env.paths.clear()
        with self.assertRaises(ValueError):
            anchor_identity_pio.configure(self.env, {"BINRANGE_BLE_IDENTITY_FILE": ""})
        with self.assertRaises(ValueError):
            anchor_identity_pio.configure(self.env, {"BINRANGE_BLE_IDENTITY_FILE": str(self.root / "missing")})
        self.assertEqual(self.env.paths, [])

    def test_build_output_must_be_ignored_and_inside_private_build_tree(self):
        (self.root / ".gitignore").write_text("identity.json\n")
        with self.assertRaises(ValueError):
            anchor_identity_pio.configure(self.env, {"BINRANGE_BLE_IDENTITY_FILE": str(self.identity)})
        self.assertEqual(self.env.paths, [])


if __name__ == "__main__":
    unittest.main()
