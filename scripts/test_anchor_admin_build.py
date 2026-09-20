"""Credential build boundary tests; all passwords here are synthetic fixtures."""
import contextlib
import io
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest

import anchor_admin_build


class AdminBuildTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.secret = self.root / "private-password"
        self.header = self.root / "generated" / "anchor_admin_bootstrap.h"

    def secret_file(self, value):
        self.secret.write_bytes(value)
        self.secret.chmod(0o600)

    def compiled_value(self):
        # Exercise the generated C++ artifact, not a string matching its source.
        source = self.root / "read.cpp"
        source.write_text('#include "anchor_admin_bootstrap.h"\n'
                          '#include <cstdio>\n'
                          'int main() { std::fputs(binrange::admin_bootstrap, stdout); }\n')
        binary = self.root / "read"
        result = subprocess.run(["g++", "-std=c++11", "-I", str(self.header.parent),
                                 str(source), "-o", str(binary)], capture_output=True)
        self.assertEqual(result.returncode, 0, "Generated header must compile")
        return subprocess.check_output([str(binary)])

    def test_no_credential_build_disables_bootstrap_and_replaces_stale_secret(self):
        self.secret_file(b"synthetic-fixture-password")
        anchor_admin_build.generate_header(self.secret, self.header)
        anchor_admin_build.generate_header(None, self.header)
        self.assertEqual(self.compiled_value(), b"")

    def test_printable_password_survives_cpp_quoting_and_spaces(self):
        value = b' space " \\ ?!&^()[] '
        self.secret_file(value + b"\n")
        with contextlib.redirect_stdout(io.StringIO()) as stdout:
            with contextlib.redirect_stderr(io.StringIO()) as stderr:
                anchor_admin_build.generate_header(self.secret, self.header)
        self.assertEqual(stdout.getvalue(), "")
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(self.compiled_value(), value)
        self.assertEqual(stat.S_IMODE(self.header.stat().st_mode), 0o600)

    def test_exact_length_boundaries_and_crlf(self):
        for length in (12, 64):
            with self.subTest(length=length):
                self.secret_file(b"Z" * length + b"\r\n")
                anchor_admin_build.generate_header(self.secret, self.header)
                self.assertEqual(self.compiled_value(), b"Z" * length)

    def test_invalid_bytes_or_lengths_never_create_header(self):
        for value in (b"", b"x" * 11, b"x" * 65, b"x" * 12 + b"\n\n",
                      b"x" * 12 + b"\x00", b"x" * 12 + b"\t",
                      b"x" * 12 + b"\x7f", b"x" * 12 + b"\xff",
                      b"x" * 12 + b"\r", b"x" * 10000):
            with self.subTest(length=len(value)):
                self.secret_file(value)
                with self.assertRaises(ValueError) as raised:
                    anchor_admin_build.generate_header(self.secret, self.header)
                self.assertNotIn(repr(value), str(raised.exception))
                self.assertFalse(self.header.exists())

    def test_missing_or_public_or_symlink_secret_fails_closed(self):
        with self.assertRaises(ValueError):
            anchor_admin_build.generate_header(self.secret, self.header)
        self.secret_file(b"synthetic-fixture-password")
        self.secret.chmod(0o644)
        with self.assertRaises(ValueError):
            anchor_admin_build.generate_header(self.secret, self.header)
        self.secret.chmod(0o600)
        link = self.root / "linked-secret"
        link.symlink_to(self.secret)
        with self.assertRaises(ValueError):
            anchor_admin_build.generate_header(link, self.header)
        self.assertFalse(self.header.exists())

    def test_output_symlink_cannot_overwrite_another_file(self):
        other = self.root / "unrelated"
        other.write_text("preserve me")
        self.header.parent.mkdir()
        self.header.symlink_to(other)
        with self.assertRaises(ValueError):
            anchor_admin_build.generate_header(None, self.header)
        self.assertEqual(other.read_text(), "preserve me")

    def test_cli_is_quiet_and_does_not_echo_invalid_secret(self):
        fixture = b"synthetic-invalid-secret\x00"
        self.secret_file(fixture)
        script = Path(anchor_admin_build.__file__)
        args = [sys.executable, str(script), "--output", str(self.header)]
        success = subprocess.run(args, capture_output=True)
        self.assertEqual(success.returncode, 0)
        self.assertEqual(success.stdout + success.stderr, b"")
        failure = subprocess.run(args + ["--secret", str(self.secret)], capture_output=True)
        self.assertNotEqual(failure.returncode, 0)
        self.assertEqual(failure.stdout, b"")
        self.assertNotIn(b"synthetic-invalid-secret", failure.stderr)
        self.assertNotIn(b"Traceback", failure.stderr)

    def test_nonregular_input_fails_without_blocking(self):
        fifo = self.root / "credential-fifo"
        os.mkfifo(fifo, 0o600)
        result = subprocess.run(
            [sys.executable, anchor_admin_build.__file__, "--secret", str(fifo),
             "--output", str(self.header)], capture_output=True, timeout=3)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.header.exists())

    def test_same_path_and_hard_link_cannot_replace_credential(self):
        value = b"synthetic-fixture-password"
        self.secret_file(value)
        alias = self.root / "credential-alias"
        os.link(self.secret, alias)
        for output in (self.secret, alias):
            with self.subTest(output=output.name):
                with self.assertRaises(ValueError):
                    anchor_admin_build.generate_header(self.secret, output)
                self.assertEqual(self.secret.read_bytes(), value)


if __name__ == "__main__":
    unittest.main()
