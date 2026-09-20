"""Protect against truncated TCP frames and unsafe capability negotiation."""
import importlib.util
import pathlib
import struct
import unittest


class Fragments:
    def __init__(self, data):
        self.data = bytearray(data)
        self.sent = bytearray()

    def sendall(self, data):
        self.sent.extend(data)

    def recv(self, count):
        result = bytes(self.data[:min(count, 1)])
        del self.data[:len(result)]
        return result


class RelayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = pathlib.Path(__file__).with_name('dap_relay.py')
        cls.module_path = path
        if path.exists():
            spec = importlib.util.spec_from_file_location('dap_relay', path)
            cls.relay = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(cls.relay)

    def setUp(self):
        self.assertTrue(self.module_path.exists(), 'saved relay implementation missing')

    def test_reassembles_fragmented_response(self):
        packet = bytes.fromhex('4441500003000200000140')
        self.assertEqual(self.relay.read_frame(Fragments(packet), 2), b'\x00\x01\x40')

    def test_partial_disconnect_is_error(self):
        with self.assertRaises(EOFError):
            self.relay.read_frame(Fragments(bytes.fromhex('444150000300020000')), 2)

    def test_clean_disconnect_is_distinct(self):
        self.assertIsNone(self.relay.read_frame(Fragments(b''), 1))

    def test_rejects_bad_headers_before_payload(self):
        for signature, length, kind, reserved in [(0, 3, 2, 0),
                (0x504144, 4097, 2, 0), (0x504144, 0, 2, 0),
                (0x504144, 3, 1, 0), (0x504144, 3, 2, 1)]:
            with self.subTest(signature=signature, length=length, kind=kind, reserved=reserved):
                with self.assertRaises(ValueError):
                    self.relay.read_frame(Fragments(struct.pack('<IHBB', signature, length, kind, reserved)), 2)

    def test_caps_packet_size_and_count(self):
        self.assertEqual(self.relay.conservative_reply(b'\0\xff', b'\0\2\0\x10'), b'\0\2\x40\0')
        self.assertEqual(self.relay.conservative_reply(b'\0\xfe', b'\0\1\x08'), b'\0\1\1')

    def test_preserves_lower_limits_and_other_commands(self):
        self.assertEqual(self.relay.conservative_reply(b'\0\xff', b'\0\2\x20\0'), b'\0\2\x20\0')
        self.assertEqual(self.relay.conservative_reply(b'\0\xfe', b'\0\1\0'), b'\0\1\0')
        self.assertEqual(self.relay.conservative_reply(b'\x05\0', b'\x05\1\1\x12'), b'\x05\1\1\x12')

    def test_rejects_mismatched_command(self):
        with self.assertRaises(ValueError):
            self.relay.conservative_reply(b'\x05\0', b'\x06\0')

    def test_encodes_protocol_header(self):
        self.assertEqual(self.relay.frame(b'\0\xff', 1), bytes.fromhex('444150000200010000ff'))

    def test_relays_two_commands_without_replay(self):
        client = Fragments(bytes.fromhex('444150000200010000ff44415000020001000500'))
        probe = Fragments(bytes.fromhex('444150000400020000020010444150000400020005010112'))
        self.relay.relay(client, probe)
        self.assertEqual(bytes(probe.sent), bytes.fromhex('444150000200010000ff44415000020001000500'))
        self.assertEqual(bytes(client.sent), bytes.fromhex('444150000400020000024000444150000400020005010112'))

    def test_lost_probe_response_does_not_replay_request(self):
        client = Fragments(bytes.fromhex('44415000020001000500'))
        probe = Fragments(b'')
        with self.assertRaises(EOFError):
            self.relay.relay(client, probe)
        self.assertEqual(bytes(probe.sent), bytes.fromhex('44415000020001000500'))
        self.assertEqual(bytes(client.sent), b'')

    def test_oversized_client_packet_never_reaches_probe(self):
        client = Fragments(struct.pack('<IHBB', 0x504144, 65, 1, 0) + bytes(65))
        probe = Fragments(b'')
        with self.assertRaises(ValueError):
            self.relay.relay(client, probe)
        self.assertEqual(bytes(probe.sent), b'')

    def test_respects_negotiated_limit_smaller_than_64(self):
        query = bytes.fromhex('444150000200010000ff')
        client = Fragments(query + struct.pack('<IHBB', 0x504144, 33, 1, 0) + bytes(33))
        probe = Fragments(bytes.fromhex('444150000400020000022000'))
        with self.assertRaises(ValueError):
            self.relay.relay(client, probe)
        self.assertEqual(bytes(probe.sent), query)

    def test_oversized_response_is_not_forwarded(self):
        client = Fragments(bytes.fromhex('44415000020001000500'))
        probe = Fragments(struct.pack('<IHBB', 0x504144, 65, 2, 0) + b'\x05' + bytes(64))
        with self.assertRaises(ValueError):
            self.relay.relay(client, probe)
        self.assertEqual(bytes(client.sent), b'')


if __name__ == '__main__':
    unittest.main()
