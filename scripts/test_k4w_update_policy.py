"""Compile and exercise the pure tag update policy on the host."""
import pathlib
import subprocess
import tempfile
import unittest


class UpdatePolicy(unittest.TestCase):
    def test_policy_boundaries(self):
        root = pathlib.Path(__file__).resolve().parents[1] / 'firmware/k4w-tag'
        with tempfile.TemporaryDirectory(prefix='binrange-update-policy-') as tmp:
            exe = str(pathlib.Path(tmp) / 'policy-test')
            subprocess.run([
                'cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                '-I' + str(root / 'src'), str(root / 'tests/update_policy_test.c'),
                str(root / 'src/update_policy.c'), '-o', exe,
            ], check=True)
            for scenario in ('confirmation', 'maintenance', 'wide-clock',
                             'delayed-startup', 'health-recovery', 'progress-recovery'):
                with self.subTest(scenario=scenario):
                    subprocess.run([exe, scenario], check=True)


if __name__ == '__main__':
    unittest.main()
