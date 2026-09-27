"""Native behavior tests for calibrated tipping detection."""
import pathlib
import subprocess
import tempfile
import unittest


class TipPolicy(unittest.TestCase):
    def test_monitor_persistence_and_reporting(self):
        root = pathlib.Path(__file__).resolve().parents[1] / 'firmware/k4w-tag'
        self.assertTrue((root / 'src/tip_monitor.h').exists(), 'tip monitor is not implemented')
        with tempfile.TemporaryDirectory(prefix='binrange-tip-monitor-') as tmp:
            exe = str(pathlib.Path(tmp) / 'test')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(root / 'src'), str(root / 'tests/tip_monitor_test.c'),
                            str(root / 'src/tip_policy.c'), str(root / 'src/tip_monitor.c'),
                            '-o', exe], check=True)
            subprocess.run([exe], check=True)

    def test_native_policy(self):
        root = pathlib.Path(__file__).resolve().parents[1] / 'firmware/k4w-tag'
        self.assertTrue((root / 'src/tip_policy.h').exists(), 'tipping detector is not implemented')
        with tempfile.TemporaryDirectory(prefix='binrange-tip-') as tmp:
            exe = str(pathlib.Path(tmp) / 'test')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(root / 'src'), str(root / 'tests/tip_policy_test.c'),
                            str(root / 'src/tip_policy.c'), '-o', exe], check=True)
            subprocess.run([exe], check=True)


if __name__ == '__main__':
    unittest.main()
