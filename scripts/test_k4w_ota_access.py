#!/usr/bin/env python3
"""Host-only tests of the pure decisions used by ota_runtime.c."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="ota-access-") as tmp:
    binary = pathlib.Path(tmp) / "test"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "firmware/k4w-tag/src"),
        str(ROOT / "firmware/k4w-tag/tests/ota_access_test.c"),
        str(ROOT / "firmware/k4w-tag/src/update_policy.c"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
