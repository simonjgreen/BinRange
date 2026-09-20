"""First-install safeguards; no device or network is used by these tests."""
import importlib.util
import json
import pathlib
import socket
import tempfile
import unittest
from unittest.mock import patch


class TargetFixture:
    """Command recorder for stop-before-write assertions, not a hardware simulator."""
    def __init__(self, readable=0, installed=False, mismatch=None):
        self.readable, self.installed, self.mismatch = readable, installed, mismatch
        self.commands, self.events, self.request = [], [], []
        self.health_polls = 0

    def command(self, command):
        self.commands.append(command)
        if command == 'nrf52.dap apreg 1 0xfc':
            return '0x02880000'
        if command == 'nrf52.dap apreg 1 0xc':
            return str(self.readable)
        if command == 'nrf52_recover':
            self.events.append('recover')
            self.readable = 1
        if command == 'nrf52.cpu curstate':
            return 'halted'
        if command.startswith('write_memory'):
            self.request = [int(v,0) for v in command.split('{')[1].rstrip('}').split()]
        return ''

    def read(self, address, count=1, width=32):
        if address == 0x200006d4:
            self.health_polls += 1
        values = {0x10000060:[1,2], 0x10000100:[0x52833], 0x10000010:[4096,128],
                  0x10000:[0x96f3b83d if self.installed else 0],
                  0x100000a0:[1,0x12345678,0xabcd], 0x10001000:[0xffffffff]*1024,
                  0x4001e400:[1], 0x20004000:self.request,
                  0x200006d4:[0]*9, 0x200006fc:[0]*15, 0x200006c0:[0]*5}
        if address not in values:
            raise AssertionError(f'Unexpected fixture read {address:#x}')
        return values[address]

    def dump(self, path, address, size):
        if self.mismatch == address:
            return b'wrong'
        if address == 0x10001000:
            data = bytearray(b'\xff'*4096)
            data[0x20c:0x210] = b'\xfe\xff\xff\xff'
            return bytes(data)
        return {0:b'boot',0x10000:b'app'}[address]

    def update(self, **values):
        self.data.update(values)
        if 'program_state' in values:
            self.events.append(values['program_state'])


class ProgrammerSafety(unittest.TestCase):
    def setUp(self):
        path = pathlib.Path(__file__).with_name('k4w_program.py')
        self.assertTrue(path.exists(), 'single-command programmer not implemented')
        spec = importlib.util.spec_from_file_location('programmer', path)
        self.p = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.p)
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.target = self.p.identity('b103', 'Test Bin 1', '02:00:00:00:00:03', '100003')

    def test_label_validation_and_normalization(self):
        self.assertEqual(self.target['label_mac'], '02:00:00:00:00:03')
        self.assertEqual(self.target['assigned_uwb_id'], 'b103')
        for values in [('b001', 'Test Bin A', '020000000003', '100003'),
                       ('b103', '', '020000000003', '100003'),
                       ('b103', 'Test Bin A', '../bad', '100003')]:
            with self.subTest(values=values), self.assertRaises(ValueError):
                self.p.identity(*values)

    def test_duplicate_id_name_or_casing_refused(self):
        folder = self.root / '020000000002'
        folder.mkdir()
        entry = self.p.identity('b102', 'Test Bin A', '020000000002', '100002')
        entry['hardware_id'] = '1111111122222222'
        (folder / 'identity.json').write_text(json.dumps(entry))
        self.p.check_registry(self.root, self.target, ['aaaaaaaa55555555'])
        for key, value in [('assigned_uwb_id', 'b102'), ('bin_name', 'Test Bin A'),
                           ('label_mac', '02:00:00:00:00:02')]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.p.check_registry(self.root, dict(self.target, **{key: value}), ['aaaaaaaa55555555'])
        self.assertEqual(self.p.check_registry(self.root, self.target, ['aaaaaaaa55555555']),
                         {'1111111122222222', 'aaaaaaaa55555555'})

    def test_missing_malformed_or_empty_protected_hardware_ids_stop_before_preflight(self):
        photo = self.root / 'photo.jpg'
        photo.write_bytes(b'photo fixture')
        for value in (None, [], ['not-a-chip-id']):
            with self.subTest(value=value), patch.object(self.p, 'artifacts') as artifacts, \
                    patch.object(self.p, 'Probe') as probe:
                profile = self.root / f'profile-{str(value)}.json'
                payload = {} if value is None else {'protected_hardware_ids': value}
                profile.write_text(json.dumps(payload))
                with self.assertRaises(ValueError):
                    self.p.main(['--tag', 'b103', '--name', 'Test Bin 1', '--mac', '020000000003',
                                 '--serial', '100003', '--photo', str(photo), '--profile', str(profile),
                                 '--check'])
                artifacts.assert_not_called()
                probe.assert_not_called()

    def test_interrupted_record_blocks_repeat_and_is_private(self):
        photo = self.root / 'photo.jpg'
        photo.write_bytes(b'photo fixture')
        records = self.root / 'tags'
        records.mkdir()
        record = self.p.Record(records, dict(self.target,
                              operator_confirmed_casing='020000000003/100003'), photo)
        record.update(program_state='erase_started')
        self.assertEqual(json.loads(record.path.read_text())['program_state'], 'erase_started')
        self.assertEqual(record.path.stat().st_mode & 0o777, 0o600)
        self.assertEqual(json.loads(record.path.read_text())['operator_confirmed_casing'],
                         '020000000003/100003')
        self.assertEqual((record.directory / 'casing-label.jpg').read_bytes(), b'photo fixture')
        with self.assertRaises(ValueError):
            self.p.check_registry(records, self.target, ['aaaaaaaa55555555'])
        with self.assertRaises(FileExistsError):
            self.p.Record(records, self.target, photo)

    def test_corrupt_registry_fails_closed(self):
        folder = self.root / '020000000002'
        folder.mkdir()
        (folder / 'identity.json').write_text('{corrupt')
        with self.assertRaises(ValueError):
            self.p.check_registry(self.root, self.target, ['aaaaaaaa55555555'])

    def test_wrong_or_known_chip_rejected(self):
        self.assertEqual(self.p.check_chip([0x12345678, 0xabcdef12], 0x52833, [4096,128], set()),
                         '12345678abcdef12')
        for args in [([1,2],0x52840,[4096,128],set()),
                     ([1,2],0x52833,[4096,64],set()),
                     ([0x11111111,0x22222222],0x52833,[4096,128],{'1111111122222222'})]:
            with self.subTest(args=args), self.assertRaises(ValueError):
                self.p.check_chip(*args)
        with self.assertRaises(ValueError):
            self.p.check_chip([1,2],0x52833,[4096,128],set(), expected='0000000300000004')

    def test_changed_artifact_rejected(self):
        artifact = self.root / 'app.bin'
        artifact.write_bytes(b'abc')
        self.p.check_hash(artifact, 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
        artifact.write_bytes(b'bad')
        with self.assertRaises(ValueError):
            self.p.check_hash(artifact, 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')

    def test_tcl_error_and_timeout_are_not_replayed(self):
        # Real client protocol, with only the physical socket replaced.
        client, server = socket.socketpair()
        self.addCleanup(client.close)
        self.addCleanup(server.close)
        debugger = self.p.Tcl(client)
        server.sendall(b'1:failed\x1a')
        with self.assertRaises(RuntimeError):
            debugger.command('nrf52_recover')
        sent = server.recv(4096)
        self.assertEqual(sent.count(b'nrf52_recover'), 1)
        client.settimeout(0.01)
        with self.assertRaises(TimeoutError):
            debugger.command('flash write_image {app.hex}')
        sent = server.recv(4096)
        self.assertEqual(sent.count(b'flash write_image'), 1)
        server.settimeout(0.01)
        with self.assertRaises(TimeoutError):
            server.recv(4096)

    def test_missing_consent_or_duplicate_stops_before_probe(self):
        # CLI ordering: no subprocess/device creation if consent is absent.
        with patch.object(self.p, 'Probe') as probe:
            with self.assertRaises(ValueError):
                self.p.main(['--tag','b103','--name','Test Bin 1','--mac','020000000003',
                             '--serial','100003','--photo',str(self.root/'photo.jpg')])
            probe.assert_not_called()

    def test_missing_physical_confirmation_stops_before_probe(self):
        with patch.object(self.p, 'Probe') as probe:
            with self.assertRaisesRegex(ValueError, 'confirm-casing'):
                self.p.main(['--tag','b103','--name','Test Bin 1','--mac','020000000003',
                             '--serial','100003','--photo',str(self.root/'photo.jpg'),
                             '--erase-factory','--battery-disconnected'])
            probe.assert_not_called()

    def install_fixture(self, target, known=None):
        target.data = dict(self.target)
        target.directory = self.root / str(len(list(self.root.iterdir())))
        target.directory.mkdir()
        bundle = {'symbols':{'ota_provision_request':0x20004000, 'ota_diag':0x200006d4,
                             'tag_diag':0x200006fc, 'status':0x200006c0},
                  'images':[('boot_hex',0,b'boot'),('app_hex',0x10000,b'app')],
                  'blobs':{'boot_hex':b'hex boot','app_hex':b'hex app','boot_bin':b'boot'},
                  'manifest':{'version':'fixture'}}
        # Exercise bounded unhealthy polling without sleeping or inventing a boot.
        clock = iter(range(10000))
        with patch.object(self.p.time, 'monotonic', side_effect=lambda:next(clock)), \
             patch.object(self.p.time, 'sleep'), \
             patch.object(self.p, 'progress'):
            self.p.install(target,target,bundle,known or set())

    def test_both_protection_states_record_before_single_erase_and_health_stops(self):
        for readable in (0,1):
            with self.subTest(readable=readable):
                target = TargetFixture(readable=readable)
                with self.assertRaisesRegex(RuntimeError, 'health'):
                    self.install_fixture(target)
                self.assertEqual(target.events[:2], ['erase_started','recover'])
                self.assertEqual(target.commands.count('nrf52_recover'), 1)
                self.assertEqual(target.commands.count('resume'), 1)
                self.assertEqual(target.commands.count('reset halt'), 2)
                self.assertGreater(target.health_polls, 1)
                self.assertNotIn('verified',target.events)

    def test_known_or_installed_readable_chip_never_erased(self):
        for installed,known in [(True,set()),(False,{'0000000100000002'})]:
            with self.subTest(installed=installed):
                target = TargetFixture(readable=1,installed=installed)
                with self.assertRaises(ValueError):
                    self.install_fixture(target,known)
                self.assertNotIn('nrf52_recover',target.commands)
                self.assertNotIn('erase_started',target.events)

    def test_readback_failure_stops_later_writes(self):
        for address,flashes in [(0x10001000,0),(0,1),(0x10000,2)]:
            with self.subTest(address=address):
                target = TargetFixture(mismatch=address)
                with self.assertRaises(ValueError):
                    self.install_fixture(target)
                self.assertEqual(sum(c.startswith('flash write_image') for c in target.commands),flashes)
                self.assertFalse(any(c.startswith('write_memory') for c in target.commands))
                self.assertNotIn('resume',target.commands)


if __name__ == '__main__':
    unittest.main()
