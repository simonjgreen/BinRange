"""Offline protocol and recovery-policy tests; no optional dependencies needed."""
import unittest
import asyncio

import k4w_ble as ble

# MCUboot's 32-byte image_header, followed by a 64-byte payload. Firmware is
# responsible for signature validation; these tests exercise upload framing.
IMAGE_HEADER = bytes.fromhex(
    '3db8f39600000000200000004000000000000000010000000000000000000000')
IMAGE = IMAGE_HEADER + bytes(range(64))


class Receiver:
    """Scripted remote peer at the decoded SMP boundary."""
    def __init__(self, offsets=(), identity='0011223344556677'):
        self.offsets = iter(offsets)
        self.identity = identity
        self.requests = []

    async def request(self, op, group, command, body):
        self.requests.append((op, group, command, body))
        if (op, group, command) == (0, 64, 0):
            return {'id': self.identity, 'tag': 0xb100, 'confirmed': True}
        if (op, group, command) == (2, 1, 1):
            return {'off': next(self.offsets)}
        return {}


class PolicyTests(unittest.IsolatedAsyncioTestCase):
    async def test_motion_configuration_checks_identity_and_bounds_before_write(self):
        tuning = dict(moving_ms=5000, idle_ms=600000, quiet_ms=30000,
                      threshold_mg=250, duration_samples=2)
        peer = Receiver()
        client = ble.RecoveryClient(peer)
        with self.assertRaises(ble.RecoveryError):
            await client.configure('deadbeefdeadbeef', tuning)
        self.assertFalse(any(r[0] == 2 for r in peer.requests))
        for bad in ({**tuning, 'threshold_mg': 0}, {**tuning, 'idle_ms': True},
                    {**tuning, 'unknown': 3}, {'moving_ms': 5000}):
            peer.requests.clear()
            with self.assertRaises(ble.RecoveryError):
                await client.configure(peer.identity, bad)
            self.assertEqual(peer.requests, [])
        await client.configure(peer.identity, tuning)
        self.assertEqual([r for r in peer.requests if r[0] == 2], [(2, 64, 2, tuning)])
        self.assertEqual(peer.requests[-1], (0, 64, 2, {}))

    async def test_status_and_reset_use_only_scoped_commands(self):
        peer = Receiver()
        client = ble.RecoveryClient(peer)
        self.assertEqual(await client.status(), {
            'image': {}, 'tag': {'id': peer.identity, 'tag': 0xb100, 'confirmed': True}})
        await client.reset()
        self.assertEqual([(r[0], r[1], r[2]) for r in peer.requests],
                         [(0, 1, 0), (0, 64, 0), (2, 0, 5)])

    async def test_identity_guards_upload_and_trial_before_any_write(self):
        for action in ('upload', 'trial'):
            for expected in (None, '', 'wrong', 'deadbeefdeadbeef'):
                peer = Receiver()
                client = ble.RecoveryClient(peer)
                with self.assertRaises(ble.RecoveryError):
                    if action == 'upload':
                        await client.upload(IMAGE, expected)
                    else:
                        await client.trial(expected)
                self.assertFalse(any(r[0] == 2 for r in peer.requests))
        peer = Receiver()
        await ble.RecoveryClient(peer).trial('0011223344556677')
        self.assertEqual(peer.requests[-1], (2, 64, 1, {}))

    async def test_upload_resumes_at_receiver_offset_and_stops_at_byte_limit(self):
        peer = Receiver([64, 80])
        progress = []
        result = await ble.RecoveryClient(peer).upload(
            IMAGE, peer.identity, chunk_size=32, stop_after=80,
            progress=lambda off, total: progress.append((off, total)))
        writes = [r[3] for r in peer.requests if r[0] == 2]
        self.assertEqual(writes[0], {
            'off': 0, 'len': 96, 'data': IMAGE_HEADER,
            'sha': bytes.fromhex('e25b1f7f3e82855825f754f0720cba8f370dea3ef76dc11ea529b8d21d524646')})
        self.assertEqual(writes[1], {'off': 64, 'data': bytes(range(32, 48))})
        self.assertEqual(progress, [(64, 96), (80, 96)])
        self.assertEqual(result, 80)

    async def test_upload_rejects_invalid_offsets_and_bounds_no_progress(self):
        for offsets in ([97], [-1], [True], ['32'], [None], [32, 31],
                        [32, 65], [0, 0, 0]):
            with self.subTest(offsets=offsets):
                peer = Receiver(offsets)
                with self.assertRaises(ble.RecoveryError):
                    await ble.RecoveryClient(peer).upload(
                        IMAGE, peer.identity, chunk_size=32)
                self.assertLessEqual(len(peer.requests), 4)

    async def test_full_upload_finishes_without_arming_or_resetting(self):
        peer = Receiver([40, 80, 96])
        result = await ble.RecoveryClient(peer).upload(IMAGE, peer.identity, chunk_size=40)
        self.assertEqual(result, 96)
        self.assertEqual([r[3]['data'] for r in peer.requests if r[0] == 2],
                         [IMAGE_HEADER + bytes(range(8)), bytes(range(8, 48)), bytes(range(48, 64))])
        self.assertTrue(all(r[:3] in ((0, 64, 0), (2, 1, 1)) for r in peer.requests))

    async def test_upload_rejects_unsafe_sizes_before_any_request(self):
        cases = [(IMAGE[:size], 32, None) for size in (0, 1, 31)]
        cases += [(IMAGE, size, None) for size in (1, 31, 385, 1024)]
        cases += [(IMAGE, 32, stop) for stop in (1, 31, 97)]
        for image, chunk_size, stop_after in cases:
            with self.subTest(length=len(image), chunk=chunk_size, stop=stop_after):
                peer = Receiver([len(image)])
                with self.assertRaises(ble.RecoveryError):
                    await ble.RecoveryClient(peer).upload(
                        image, peer.identity, chunk_size=chunk_size, stop_after=stop_after)
                self.assertEqual(peer.requests, [])

    async def test_minimum_header_and_maximum_requested_chunk_are_accepted(self):
        for image, chunk, stop in ((IMAGE_HEADER, 32, None), (IMAGE, 384, 32)):
            peer = Receiver([32])
            self.assertEqual(await ble.RecoveryClient(peer).upload(
                image, peer.identity, chunk_size=chunk, stop_after=stop), 32)
            self.assertEqual(peer.requests[-1][3]['data'], IMAGE_HEADER)


class FramingTests(unittest.TestCase):
    def test_literal_v1_request_and_fragmented_response(self):
        self.assertEqual(ble.encode_frame(0, 64, 7, 0, b'\xa0'),
                         bytes.fromhex('0000000100400700a0'))
        rx = ble.ResponseAssembler(0, 64, 7, 0)
        self.assertIsNone(rx.feed(bytes.fromhex('010000')))
        self.assertIsNone(rx.feed(bytes.fromhex('0100400700')))
        self.assertEqual(rx.feed(b'\xa0'), b'\xa0')

    def test_rejects_mismatched_headers_oversize_and_trailing_data(self):
        for wire in ('0300000100400700a0', '0101000100400700a0',
                     '0100000100410700a0', '0100000100400800a0',
                     '0100000100400701a0', '0100100000400700',
                     '0100000100400700a000'):
            with self.subTest(wire=wire), self.assertRaises(ble.RecoveryError):
                ble.ResponseAssembler(0, 64, 7, 0).feed(bytes.fromhex(wire))
        rx = ble.ResponseAssembler(0, 64, 7, 0)
        rx.feed(bytes.fromhex('0100000100400700a0'))
        with self.assertRaises(ble.RecoveryError):
            rx.feed(b'\x00')


def encode_empty(body):
    assert body == {}
    return b'\xa0'


def decode_empty(payload):
    if payload != b'\xa0':
        raise ValueError('Expected literal CBOR empty map')
    return {}


class WirePeer:
    """In-memory BLE boundary, using literal SMP response fixtures."""
    def __init__(self, replies):
        self.replies = iter(replies)
        self.writes = []

    async def write(self, frame):
        self.writes.append(frame)
        fragments = next(self.replies)
        for fragment in fragments:
            self.session.notify(None, fragment)

    def connect(self):
        self.session = ble.SMPSession(self.write, encode=encode_empty,
                                      decode=decode_empty, timeout=0.02)
        return self.session


class SessionTests(unittest.IsolatedAsyncioTestCase):
    async def test_encoded_requests_include_header_in_512_byte_limit(self):
        for op, group, command, response in (
                (0, 1, 0, '0100000100010000a0'),
                (0, 64, 0, '0100000100400000a0'),
                (2, 1, 1, '0300000100010001a0'),
                (2, 64, 1, '0300000100400001a0'),
                (2, 0, 5, '0300000100000005a0')):
            for data_size, prefix in ((495, 'a164646174615901ef'),
                                      (496, 'a164646174615901f0')):
                with self.subTest(command=(op, group, command), size=data_size):
                    # Literal CBOR map {"data": b'x' * data_size}; encoded payload
                    # is 504/505 bytes, giving a full SMP request of 512/513.
                    encoded = bytes.fromhex(prefix) + b'x' * data_size
                    peer = WirePeer([[bytes.fromhex(response)]])
                    session = peer.connect()
                    session.encode = lambda body: encoded
                    if data_size == 495:
                        self.assertEqual(await session.request(op, group, command, {}), {})
                        self.assertEqual(len(peer.writes[0]), 512)
                    else:
                        with self.assertRaisesRegex(ble.RecoveryError, '512'):
                            await session.request(op, group, command, {})
                        self.assertEqual(peer.writes, [])

    async def test_denials_and_malformed_maps_abort(self):
        for decoded in ({'rc': 8}, {'err': {'group': 64, 'rc': 1}}, [], None):
            with self.subTest(decoded=decoded):
                peer = WirePeer([[bytes.fromhex('0100000100400000a0')]])
                session = peer.connect()
                session.decode = lambda payload: decoded
                with self.assertRaises(ble.RecoveryError):
                    await session.request(0, 64, 0, {})
                with self.assertRaises(ble.RecoveryError):
                    await session.request(0, 64, 0, {})
                self.assertEqual(len(peer.writes), 1)

    async def test_write_exception_is_sanitized_and_never_replayed(self):
        calls = []

        async def failing_write(frame):
            calls.append(frame)
            raise RuntimeError('secret passkey material')

        session = ble.SMPSession(failing_write, encode=encode_empty, decode=decode_empty)
        for _ in range(2):
            with self.assertRaises(ble.RecoveryError) as caught:
                await session.request(0, 64, 0, {})
            self.assertNotIn('secret', str(caught.exception))
        self.assertEqual(len(calls), 1)

    async def test_cancelled_request_cannot_be_reused(self):
        entered = asyncio.Event()

        async def write(frame):
            entered.set()
            await asyncio.Event().wait()

        session = ble.SMPSession(write, encode=encode_empty, decode=decode_empty)
        task = asyncio.create_task(session.request(0, 64, 0, {}))
        await entered.wait()
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        with self.assertRaises(ble.RecoveryError):
            await session.request(0, 64, 0, {})

    async def test_ble_write_splits_by_characteristic_capacity(self):
        class Characteristic:
            max_write_without_response_size = 4

        class Gatt:
            def __init__(self):
                self.writes = []

            async def write_gatt_char(self, char, data, *, response):
                self.writes.append((data, response))

        gatt = Gatt()
        await ble.write_frame(gatt, Characteristic(), bytes.fromhex('0000000100400000a0'))
        self.assertEqual(gatt.writes, [(bytes.fromhex('00000001'), False),
                                       (bytes.fromhex('00400000'), False), (b'\xa0', False)])

    async def test_literal_wire_and_serialized_sequence(self):
        peer = WirePeer([[bytes.fromhex('010000'), bytes.fromhex('0100400000a0')],
                         [bytes.fromhex('0100000100400100a0')]])
        session = peer.connect()
        self.assertEqual(await asyncio.gather(session.request(0, 64, 0, {}),
                                              session.request(0, 64, 0, {})), [{}, {}])
        self.assertEqual(peer.writes, [bytes.fromhex('0000000100400000a0'),
                                       bytes.fromhex('0000000100400100a0')])

    async def test_failure_aborts_session_without_replay_or_stale_receive(self):
        for fragments in ([], [bytes.fromhex('0100000100400100a0')],
                          [bytes.fromhex('0100000100400000a0'), b'\x00'],
                          [bytes.fromhex('0100000200400000a000')]):
            with self.subTest(fragments=fragments):
                peer = WirePeer([fragments])
                session = peer.connect()
                with self.assertRaises(ble.RecoveryError):
                    await session.request(0, 64, 0, {})
                session.notify(None, bytes.fromhex('0100000100400000a0'))
                with self.assertRaises(ble.RecoveryError):
                    await session.request(0, 64, 0, {})
                self.assertEqual(len(peer.writes), 1)

    async def test_standard_image_state_write_is_never_transmitted(self):
        peer = WirePeer([])
        with self.assertRaises(ble.RecoveryError):
            await peer.connect().request(2, 1, 0, {})
        self.assertEqual(peer.writes, [])


class CLITests(unittest.TestCase):
    def test_cli_rejects_chunks_and_stops_below_header_or_above_chunk_limit(self):
        import contextlib
        import io
        base = ['--address', 'AA', '--expected-id', '0011223344556677',
                'upload', 'test.signed.bin']
        for flag, values in (('--chunk-size', (1, 31, 385, 1024)),
                             ('--stop-after', (1, 31))):
            for value in values:
                with self.subTest(flag=flag, value=value), contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit):
                        ble.parse_args(base + [flag, str(value)])

    def test_explicit_address_identity_and_signed_path_are_required(self):
        import contextlib
        import io
        for argv in (['status'], ['--address', 'AA', 'upload', 'test.signed.bin'],
                     ['--address', 'AA', 'trial'],
                     ['--address', 'AA', '--expected-id', '0011223344556677',
                      'upload', 'test.bin']):
            with self.subTest(argv=argv), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    ble.parse_args(argv)
        args = ble.parse_args(['--address', 'AA', '--expected-id', '0011223344556677',
                               'upload', 'test.signed.bin', '--stop-after', '512'])
        self.assertEqual((args.address, args.stop_after), ('AA', 512))

    def test_diagnostics_do_not_echo_unknown_or_authentication_fields(self):
        status = {'image': {'images': [{'slot': 0, 'version': '1.0', 'pin': 'secret'}],
                            'secret': 'secret'},
                  'tag': {'id': '0011223344556677', 'tag': 0xb100, 'pin': 'secret'}}
        self.assertEqual(ble.diagnostics(status), {
            'image': {'images': [{'slot': 0, 'version': '1.0'}]},
            'tag': {'id': '0011223344556677', 'tag': 0xb100}})

    def test_same_version_images_are_distinguished_by_32_byte_hash(self):
        status = {'image': {'images': [
            {'slot': 0, 'version': '1.0', 'hash': b'\x12' * 32, 'pin': 'secret'},
            {'slot': 1, 'version': '1.0', 'hash': b'\xab' * 32, 'key': b'secret'}]},
            'tag': {'id': '0011223344556677', 'pin': 'secret'}}
        self.assertEqual(ble.diagnostics(status), {
            'image': {'images': [
                {'slot': 0, 'version': '1.0',
                 'hash': '1212121212121212121212121212121212121212121212121212121212121212'},
                {'slot': 1, 'version': '1.0',
                 'hash': 'abababababababababababababababababababababababababababababababab'}]},
            'tag': {'id': '0011223344556677'}})

    def test_invalid_hash_values_and_unknown_bytes_are_filtered(self):
        for value in (b'', b'x' * 31, b'x' * 33, '12' * 32, 32, None,
                      bytearray(b'x' * 32), {'pin': 'secret'}, [0] * 32):
            with self.subTest(value=value):
                status = {'image': {'images': [
                    {'slot': 0, 'hash': value, 'auth': b'x' * 32}]},
                    'tag': {'hash': b'x' * 32, 'secret': b'x' * 32}}
                self.assertEqual(ble.diagnostics(status),
                                 {'image': {'images': [{'slot': 0}]}, 'tag': {}})


class RuntimeTests(unittest.IsolatedAsyncioTestCase):
    async def test_invalid_upload_sizes_fail_before_connect(self):
        import types
        from unittest.mock import patch

        connected = []

        def unexpected_client(*args, **kwargs):
            connected.append(args)
            raise AssertionError('Invalid upload reached BLE client')

        modules = {'bleak': types.SimpleNamespace(BleakClient=unexpected_client),
                   'cbor2': types.SimpleNamespace()}
        for size, chunk, stop in ((0, 32, None), (31, 32, None), (96, 31, None),
                                 (96, 385, None), (96, 32, 31), (96, 32, 97)):
            args = types.SimpleNamespace(command='upload', path=ble.Path('test.signed.bin'),
                                         chunk_size=chunk, stop_after=stop,
                                         address='AA', timeout=10)
            with self.subTest(size=size, chunk=chunk, stop=stop), \
                    patch.dict('sys.modules', modules), \
                    patch.object(ble.Path, 'read_bytes', return_value=IMAGE[:size]):
                with self.assertRaises(ble.RecoveryError):
                    await ble.run(args)
            self.assertEqual(connected, [])

    async def test_explicit_bonded_connection_notifications_and_cleanup(self):
        import contextlib
        import io
        import types
        from unittest.mock import patch

        for denied in (False, True):
            events = []

            class Characteristic:
                max_write_without_response_size = 20

            class Service:
                def get_characteristic(self, uuid):
                    self_test.assertEqual(uuid, 'da2e7828-fbce-4e01-ae9e-261174997c48')
                    return Characteristic()

            class Services:
                def get_service(self, uuid):
                    self_test.assertEqual(uuid, '8d53dc1d-1db7-4cd3-868b-8a527460aa84')
                    return Service()

            class Client:
                services = Services()

                def __init__(self, address, **options):
                    events.append(('connect-options', address, options['pair']))

                async def connect(self):
                    events.append('connected')

                async def disconnect(self):
                    events.append('disconnected')

                async def start_notify(self, characteristic, callback):
                    if denied:
                        raise RuntimeError('Authentication denied: secret')
                    self.callback = callback

                async def write_gatt_char(self, characteristic, data, *, response):
                    self_test.assertFalse(response)
                    events.append(data)
                    if data == bytes.fromhex('0000000100010000a0'):
                        self.callback(characteristic, bytes.fromhex('0100000100010000a0'))
                    elif data == bytes.fromhex('0000000100400100a0'):
                        self.callback(characteristic, bytes.fromhex('0100000100400100a0'))
                    else:
                        raise AssertionError('Unexpected command')

            class Decoder:
                def __init__(self, stream):
                    self.stream = stream

                def decode(self):
                    return decode_empty(self.stream.read(1))

            self_test = self
            modules = {'bleak': types.SimpleNamespace(BleakClient=Client),
                       'cbor2': types.SimpleNamespace(dumps=encode_empty, CBORDecoder=Decoder)}
            with patch.dict('sys.modules', modules), contextlib.redirect_stdout(io.StringIO()):
                args = ble.parse_args(['--address', 'explicit-address', 'status'])
                if denied:
                    with self.assertRaises(RuntimeError):
                        await ble.run(args)
                else:
                    await ble.run(args)
            self.assertEqual(events[0], ('connect-options', 'explicit-address', False))
            self.assertEqual(events[-1], 'disconnected')


if __name__ == '__main__':
    unittest.main()
