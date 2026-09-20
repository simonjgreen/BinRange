#!/usr/bin/env python3
"""Conservative, single-session CMSIS-DAP TCP relay for the Nesso probe.

No command is retried. This is transport only, not a target safety policy;
the connected debugger remains responsible for any writes it requests.
"""
import argparse
import socket
import struct

HEADER = struct.Struct('<IHBB')
SIGNATURE = 0x00504144
MAX_PACKET = 4096


def read_exact(stream, length, allow_eof=False):
    data = bytearray()
    while len(data) < length:
        chunk = stream.recv(length - len(data))
        if not chunk:
            if allow_eof and not data:
                return None
            raise EOFError('connection closed in a CMSIS-DAP frame')
        data.extend(chunk)
    return bytes(data)


def read_frame(stream, expected_type):
    header = read_exact(stream, HEADER.size, allow_eof=True)
    if header is None:
        return None
    signature, length, kind, reserved = HEADER.unpack(header)
    if (signature != SIGNATURE or not 0 < length <= MAX_PACKET
            or kind != expected_type or reserved != 0):
        raise ValueError('invalid CMSIS-DAP TCP header')
    return read_exact(stream, length)


def frame(payload, kind):
    if not 0 < len(payload) <= MAX_PACKET or kind not in (1, 2):
        raise ValueError('invalid outgoing frame')
    return HEADER.pack(SIGNATURE, len(payload), kind, 0) + payload


def conservative_reply(request, response):
    if not request or not response or request[0] != response[0]:
        raise ValueError('CMSIS-DAP command mismatch')
    if request == b'\x00\xff':
        if len(response) != 4 or response[1] != 2:
            raise ValueError('invalid packet-size capability')
        size = min(64, int.from_bytes(response[2:4], 'little'))
        return b'\x00\x02' + size.to_bytes(2, 'little')
    if request == b'\x00\xfe':
        if len(response) != 3 or response[1] != 1:
            raise ValueError('invalid packet-count capability')
        return b'\x00\x01' + bytes([min(1, response[2])])
    return response


def relay(client, probe):
    packet_limit = 64
    while True:
        request = read_frame(client, 1)
        if request is None:
            return
        if len(request) > packet_limit:
            raise ValueError('client exceeded negotiated packet size')
        probe.sendall(frame(request, 1))
        response = read_frame(probe, 2)
        if response is None:
            raise EOFError('probe closed before response')
        if len(response) > packet_limit:
            raise ValueError('probe exceeded negotiated packet size')
        reply = conservative_reply(request, response)
        if request == b'\x00\xff':
            packet_limit = int.from_bytes(reply[2:4], 'little')
        client.sendall(frame(reply, 2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', required=True)
    parser.add_argument('--probe-port', type=int, default=4441)
    parser.add_argument('--listen-port', type=int, default=4441)
    args = parser.parse_args()
    with socket.socket() as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(('127.0.0.1', args.listen_port))
        server.listen(1)
        server.settimeout(120)
        print(f'Relay ready on 127.0.0.1:{args.listen_port}; one session only', flush=True)
        client, _ = server.accept()
        with client, socket.create_connection((args.probe, args.probe_port), timeout=20) as probe:
            client.settimeout(60)
            probe.settimeout(20)
            relay(client, probe)


if __name__ == '__main__':
    main()
