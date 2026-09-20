"""Run the real tag wire codec on the host against independent byte fixtures."""
import pathlib
import subprocess
import tempfile
import unittest

class WireCodec(unittest.TestCase):
    def test_anchor_compatible_frames_and_strict_response_validation(self):
        root = pathlib.Path(__file__).resolve().parents[1] / 'firmware/k4w-tag'
        self.assertTrue((root/'src/protocol.c').exists(), 'tag codec not implemented')
        with tempfile.TemporaryDirectory(prefix='binrange-wire-') as tmp:
            exe = str(pathlib.Path(tmp)/'test')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                '-I'+str(root/'src'),str(root/'tests/protocol_test.c'),
                str(root/'src/protocol.c'),'-o',exe],check=True)
            subprocess.run([exe],check=True)

if __name__ == '__main__':
    unittest.main()
