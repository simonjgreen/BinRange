"""Compile and run the pure-C k4w motion policy against native behavior tests."""
import pathlib
import subprocess
import tempfile
import unittest


class MotionPolicy(unittest.TestCase):
    def test_configuration_application_acknowledges_only_the_taken_revision(self):
        root = pathlib.Path(__file__).resolve().parents[1] / "firmware/k4w-tag"
        with tempfile.TemporaryDirectory(prefix="binrange-settings-") as tmp:
            exe = str(pathlib.Path(tmp) / "test")
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I" + str(root / "src"),
                str(root / "tests/motion_settings_test.c"),
                str(root / "src/motion_settings.c"),
                str(root / "src/motion_policy.c"), "-o", exe,
            ], check=True)
            subprocess.run([exe], check=True)

    def test_native_policy(self):
        root = pathlib.Path(__file__).resolve().parents[1] / "firmware/k4w-tag"
        self.assertTrue((root / "src/motion_policy.h").exists(),
                        "motion policy header not implemented")
        with tempfile.TemporaryDirectory(prefix="binrange-motion-") as tmp:
            exe = str(pathlib.Path(tmp) / "test")
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I" + str(root / "src"),
                str(root / "tests/motion_policy_test.c"),
                str(root / "src/motion_policy.c"), "-o", exe,
            ], check=True)
            subprocess.run([exe], check=True)


if __name__ == "__main__":
    unittest.main()
