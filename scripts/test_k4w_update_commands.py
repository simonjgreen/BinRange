import pathlib
import subprocess
import tempfile
import unittest

class UpdateCommands(unittest.TestCase):
    def test_management_cannot_bypass_local_confirmation_or_erase_rollback(self):
        root = pathlib.Path(__file__).resolve().parents[1]/'firmware/k4w-tag'
        self.assertTrue((root/'src/update_commands.h').exists(), 'command filter missing')
        with tempfile.TemporaryDirectory(prefix='binrange-commands-') as tmp:
            exe = str(pathlib.Path(tmp)/'check')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                '-I'+str(root/'src'), str(root/'tests/update_commands_test.c'),
                '-o',exe],check=True)
            subprocess.run([exe],check=True)
