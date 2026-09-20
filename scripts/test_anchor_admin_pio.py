"""Exercise the build hook using temporary repositories and synthetic secrets."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import anchor_admin_build
import anchor_admin_pio


class BuildEnvironment:
    def __init__(self, project):
        self.values = {"$PROJECT_DIR": str(project),
                       "$BUILD_DIR": str(project / ".pio" / "build" / "anchor")}
        self.paths = []

    def subst(self, value):
        return self.values[value]

    def Append(self, **values):
        # This is the adapter contract: flags must contain only include paths.
        if set(values) != {"CPPPATH"}:
            raise AssertionError("Unexpected compiler flags")
        self.paths.extend(values["CPPPATH"])


class AdminPioTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        repository = Path(self.tmp.name)
        self.root = repository / "firmware" / "anchor"
        self.root.mkdir(parents=True)
        (repository / "scripts").mkdir()
        shutil.copyfile(anchor_admin_build.__file__, repository / "scripts" / "anchor_admin_build.py")
        subprocess.run(["git", "init", "-q", str(repository)], check=True,
                       capture_output=True)
        (self.root / ".gitignore").write_text(".pio/\nprivate-password\n")
        self.env = BuildEnvironment(self.root)
        self.secret = self.root / "private-password"
        self.secret.write_text("synthetic-build-password\n")
        self.secret.chmod(0o600)

    def compiled_value(self):
        source = self.root / "consumer.cpp"
        source.write_text('#include "anchor_admin_bootstrap.h"\n'
                          '#include <cstdio>\n'
                          'int main() { std::fputs(binrange::admin_bootstrap, stdout); }\n')
        binary = self.root / "consumer"
        args = ["g++", "-std=c++11", str(source), "-o", str(binary)]
        for path in self.env.paths:
            args.extend(["-I", path])
        result = subprocess.run(args, capture_output=True)
        self.assertEqual(result.returncode, 0, "Hook must expose generated header")
        return subprocess.check_output([str(binary)])

    def test_default_build_is_disabled(self):
        anchor_admin_pio.configure(self.env, {})
        self.assertEqual(self.compiled_value(), b"")

    def test_scons_loader_without_file_variable_generates_a_usable_header(self):
        # Real SCons execs a script with Import/env but without __file__.
        scope = {"Import": lambda name: None, "env": self.env}
        source = Path(anchor_admin_pio.__file__).read_text()
        with patch.dict(os.environ, {}, clear=True):
            exec(compile(source, "pre-build-hook", "exec"), scope)
        self.assertEqual(self.compiled_value(), b"")

    def test_explicit_file_provisions_without_password_compiler_flags(self):
        anchor_admin_pio.configure(self.env, {"BINRANGE_ADMIN_FILE": str(self.secret)})
        self.assertEqual(self.compiled_value(), b"synthetic-build-password")
        self.assertNotIn("synthetic-build-password", repr(self.env.paths))

    def test_unprovisioned_rebuild_removes_previous_bootstrap(self):
        anchor_admin_pio.configure(self.env, {"BINRANGE_ADMIN_FILE": str(self.secret)})
        anchor_admin_pio.configure(self.env, {})
        self.assertEqual(self.compiled_value(), b"")

    def test_missing_explicit_input_aborts_without_installing_include_path(self):
        with self.assertRaises(ValueError):
            anchor_admin_pio.configure(self.env, {"BINRANGE_ADMIN_FILE": str(self.root / "missing")})
        self.assertEqual(self.env.paths, [])

    def test_nonignored_output_is_rejected(self):
        (self.root / ".gitignore").write_text("private-password\n")
        with self.assertRaises(ValueError):
            anchor_admin_pio.configure(self.env, {"BINRANGE_ADMIN_FILE": str(self.secret)})
        self.assertEqual(self.env.paths, [])
        self.assertFalse((self.root / ".pio").exists())

    def test_symlinked_build_tree_cannot_escape_ignored_project_path(self):
        external = self.root / "external"
        external.mkdir()
        (self.root / ".pio").symlink_to(external, target_is_directory=True)
        with self.assertRaises(ValueError):
            anchor_admin_pio.configure(self.env, {"BINRANGE_ADMIN_FILE": str(self.secret)})
        self.assertEqual(list(external.iterdir()), [])

    def test_tracked_generated_header_is_never_overwritten(self):
        header = self.root / ".pio" / "build" / "anchor" / "generated" / "anchor_admin_bootstrap.h"
        header.parent.mkdir(parents=True)
        header.write_text("tracked sentinel")
        subprocess.run(["git", "-C", str(self.root), "add", "-f", str(header)],
                       check=True, capture_output=True)
        with self.assertRaises(ValueError):
            anchor_admin_pio.configure(self.env, {"BINRANGE_ADMIN_FILE": str(self.secret)})
        self.assertEqual(header.read_text(), "tracked sentinel")


if __name__ == "__main__":
    unittest.main()
