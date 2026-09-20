"""Scoped SMP v1 recovery client (Python 3.11+, runtime bleak and cbor2).

Use an OS-authenticated Secure Connections bond provisioned by the controller.
This program never supplies/logs a PIN or unpairs/re-pairs a device. Select an
explicit BLE address (device UUID on macOS); names are not identity evidence.

  python3 scripts/k4w_ble.py --address ADDRESS status
  python3 scripts/k4w_ble.py --address ADDRESS --expected-id f00dbaad12345678 \
upload build/zephyr.signed.bin --stop-after 4096
  python3 scripts/k4w_ble.py --address ADDRESS --expected-id f00dbaad12345678 trial
  python3 scripts/k4w_ble.py --address ADDRESS reset

Upload does not arm a trial or reset. Trial only arms a TEST upgrade; firmware
must confirm locally after health checks. No standard image-state writes exist.
--stop-after is an absolute acknowledged image offset, not bytes this session.
Images and --stop-after must be at least 32 bytes so the first upload contains
the MCUboot header. Requested chunks are 32..384 bytes; later final chunks may
be smaller. Every encoded request, including SMP/CBOR overhead, is capped at
the tag's 512-byte reassembly capacity before any of its fragments are sent.
Status includes each image's 32-byte hash as lowercase hex for comparing builds
with the same version; malformed hashes and unknown diagnostic fields are omitted.
To resume, explicitly run upload again with the same signed image: the first
off=0 request supplies len/sha and the receiver reports its retained offset.
A lost response (including reset disconnect) is uncertain: abort, inspect
status in a new invocation, and decide explicitly; no automatic command replay.
"""
import argparse
import struct
import re
import hashlib
import asyncio
import io
import json
from pathlib import Path
import sys

# Zephyr include/zephyr/mgmt/mcumgr/transport/smp_bt.h (pinned NCS checkout).
SMP_SERVICE_UUID = '8d53dc1d-1db7-4cd3-868b-8a527460aa84'
SMP_CHARACTERISTIC_UUID = 'da2e7828-fbce-4e01-ae9e-261174997c48'
HEADER = struct.Struct('>BBHHBB')
MAX_FRAME = 4096
MAX_REQUEST_FRAME = 512
IMAGE_HEADER_SIZE = 32
MAX_UPLOAD_CHUNK = 384
ALLOWED_COMMANDS = {(0, 1, 0), (0, 64, 0), (2, 1, 1), (2, 64, 1), (2, 0, 5),
                    (0, 64, 2), (2, 64, 2)}
MOTION_BOUNDS = {'moving_ms': (1000, 60000), 'idle_ms': (60000, 3600000),
                 'quiet_ms': (5000, 300000), 'threshold_mg': (32, 1000),
                 'duration_samples': (1, 127)}


class RecoveryError(Exception):
    """A failure requiring an explicit new operator action."""


def validate_upload(image, chunk_size, stop_after):
    if len(image) < IMAGE_HEADER_SIZE:
        raise RecoveryError('Image must contain at least the 32-byte MCUboot header')
    if not IMAGE_HEADER_SIZE <= chunk_size <= MAX_UPLOAD_CHUNK:
        raise RecoveryError('Requested chunk size must be 32..384 bytes')
    if stop_after is not None and not IMAGE_HEADER_SIZE <= stop_after <= len(image):
        raise RecoveryError('--stop-after must be at least 32 bytes and within the image')


def encode_frame(op, group, sequence, command, payload):
    return HEADER.pack(op, 0, len(payload), group, sequence, command) + payload


class ResponseAssembler:
    def __init__(self, op, group, sequence, command, max_frame=MAX_FRAME):
        self.expected = (op + 1, 0, group, sequence, command)
        self.max_frame = max_frame
        self.buffer = bytearray()

    def feed(self, fragment):
        if len(self.buffer) + len(fragment) > self.max_frame:
            raise RecoveryError('Oversized SMP response')
        self.buffer.extend(fragment)
        if len(self.buffer) < HEADER.size:
            return None
        op, flags, length, group, sequence, command = HEADER.unpack_from(self.buffer)
        if (op, flags, group, sequence, command) != self.expected:
            raise RecoveryError('SMP response header mismatch')
        if HEADER.size + length > self.max_frame:
            raise RecoveryError('Oversized SMP response')
        if len(self.buffer) > HEADER.size + length:
            raise RecoveryError('Trailing SMP response data')
        if len(self.buffer) < HEADER.size + length:
            return None
        return bytes(self.buffer[HEADER.size:])


class RecoveryClient:
    def __init__(self, session):
        self.session = session

    async def status(self):
        return {'image': await self.session.request(0, 1, 0, {}),
                'tag': await self.session.request(0, 64, 0, {})}

    async def reset(self):
        return await self.session.request(2, 0, 5, {})

    async def require_identity(self, expected_id):
        if not isinstance(expected_id, str) or not re.fullmatch('[0-9a-f]{16}', expected_id):
            raise RecoveryError('--expected-id must be the lowercase 16-hex hardware ID')
        status = await self.session.request(0, 64, 0, {})
        if status.get('id') != expected_id:
            raise RecoveryError('Hardware identity mismatch; no write sent')
        return status

    async def trial(self, expected_id):
        await self.require_identity(expected_id)
        return await self.session.request(2, 64, 1, {})

    async def configuration(self):
        reply = await self.session.request(0, 64, 2, {})
        return {key: reply[key] for key in MOTION_BOUNDS
                if type(reply.get(key)) is int}

    async def configure(self, expected_id, tuning):
        if (set(tuning) != set(MOTION_BOUNDS) or
                any(type(tuning[k]) is not int or not lo <= tuning[k] <= hi
                    for k, (lo, hi) in MOTION_BOUNDS.items()) or
                tuning['idle_ms'] < tuning['moving_ms']):
            raise RecoveryError('Invalid complete motion configuration')
        await self.require_identity(expected_id)
        await self.session.request(2, 64, 2, tuning)
        return await self.configuration()

    async def upload(self, image, expected_id, *, chunk_size=256, stop_after=None,
                     progress=lambda off, total: None):
        validate_upload(image, chunk_size, stop_after)
        await self.require_identity(expected_id)
        limit = len(image) if stop_after is None else stop_after
        offset, stalled = 0, 0
        first = True
        while offset < limit:
            end = min(offset + chunk_size, limit)
            body = {'off': offset, 'data': image[offset:end]}
            if offset == 0:
                body.update(len=len(image), sha=hashlib.sha256(image).digest())
            reply = await self.session.request(2, 1, 1, body)
            next_offset = reply.get('off')
            if (type(next_offset) is not int or not offset <= next_offset <= len(image)
                    or (not first and next_offset > end)):
                raise RecoveryError('Invalid receiver upload offset; start a new upload explicitly')
            stalled = stalled + 1 if next_offset == offset else 0
            if stalled >= 3:
                raise RecoveryError('Upload made no progress after three acknowledged requests')
            offset, first = next_offset, False
            progress(offset, len(image))
        return offset


def cbor_encode(body):
    import cbor2
    return cbor2.dumps(body)


def cbor_decode(payload):
    import cbor2
    stream = io.BytesIO(payload)
    result = cbor2.CBORDecoder(stream).decode()
    if stream.read(1):
        raise RecoveryError('Trailing CBOR response data')
    return result


class SMPSession:
    """One in-flight request. Any transport/protocol failure poisons this session."""
    def __init__(self, write, *, timeout=10, encode=cbor_encode, decode=cbor_decode):
        self.write, self.encode, self.decode = write, encode, decode
        self.timeout = timeout
        self.lock = asyncio.Lock()
        self.ready = asyncio.Event()
        self.sequence = 0
        self.rx = None
        self.payload = None
        self.error = None

    def abort(self, message='Connection lost; start a new command explicitly'):
        self.error = self.error or RecoveryError(message)
        self.rx = self.payload = None
        self.ready.set()

    def notify(self, characteristic, data):
        if self.error:
            return
        if self.rx is None:
            self.abort('Unexpected SMP notification; session aborted')
            return
        try:
            payload = self.rx.feed(data)
            if payload is not None:
                self.payload = payload
                self.ready.set()
        except RecoveryError as exc:
            self.abort(str(exc))

    async def request(self, op, group, command, body):
        if (op, group, command) not in ALLOWED_COMMANDS:
            raise RecoveryError('Command is outside recovery policy')
        async with self.lock:
            if self.error:
                raise self.error
            try:
                frame = encode_frame(op, group, self.sequence, command, self.encode(body))
                if len(frame) > MAX_REQUEST_FRAME:
                    raise RecoveryError('Encoded SMP request exceeds tag reassembly capacity of 512 bytes')
                self.rx = ResponseAssembler(op, group, self.sequence, command)
                self.sequence = (self.sequence + 1) % 256
                self.payload = None
                self.ready.clear()
                async with asyncio.timeout(self.timeout):
                    await self.write(frame)
                    await self.ready.wait()
                if self.error:
                    raise self.error
                reply = self.decode(self.payload)
                if not isinstance(reply, dict):
                    raise RecoveryError('SMP response must be a CBOR map')
                if reply.get('rc', 0) != 0 or 'err' in reply:
                    raise RecoveryError(
                        'Device denied command; check authenticated OS bond, maintenance '
                        'mode and confirmed running image. No automatic pairing or retry.')
                self.rx = self.payload = None
                return reply
            except asyncio.CancelledError:
                self.abort('Request cancelled; start a new command explicitly')
                raise
            except Exception as exc:
                message = (str(exc) if isinstance(exc, RecoveryError) else
                           'SMP request failed or timed out; check authenticated OS bond '
                           'and connection. Start a new command explicitly; none was replayed.')
                self.abort(message)
                raise self.error from None


async def write_frame(client, characteristic, frame):
    size = characteristic.max_write_without_response_size
    if size < 1:
        raise RecoveryError('Invalid BLE write capacity')
    for offset in range(0, len(frame), size):
        await client.write_gatt_char(characteristic, frame[offset:offset + size], response=False)


def diagnostics(status):
    tag_keys = {'id', 'tag', 'version', 'confirmed', 'uptime_ms', 'reset_reason',
                'maintenance', 'radio_ok', 'ble_ok', 'sensor_error', 'moving',
                'wake_count', 'irq_count', 'battery_mv', 'misses', 'uwb_sleeping',
                'config_pending'}
    image_keys = {'image', 'slot', 'version', 'confirmed', 'active', 'pending',
                  'permanent', 'bootable'}
    # Only scalar diagnostic fields and validated image hashes; never dump raw
    # remote maps or authentication material.
    def select(mapping, keys):
        return {k: v for k, v in mapping.items()
                if k in keys and type(v) in (str, int, bool)}
    image = status['image']
    result = {'image': {}, 'tag': select(status['tag'], tag_keys)}
    if isinstance(image.get('images'), list):
        images = []
        for item in image['images']:
            if not isinstance(item, dict):
                continue
            diagnostic = select(item, image_keys)
            image_hash = item.get('hash')
            if type(image_hash) is bytes and len(image_hash) == 32:
                diagnostic['hash'] = image_hash.hex()
            images.append(diagnostic)
        result['image']['images'] = images
    return result


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--address', required=True, help='Explicit bonded device address/UUID')
    parser.add_argument('--expected-id', help='Exact lowercase DEVICEID0 then DEVICEID1 hex (16 chars)')
    parser.add_argument('--timeout', type=float, default=10, help='Seconds per request/connect (0..120)')
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('status', help='Read diagnostic image and tag status')
    commands.add_parser('config', help='Read saved motion configuration')
    configure = commands.add_parser('configure', help='Save complete bounded motion tuning')
    for key, (lo, hi) in MOTION_BOUNDS.items():
        configure.add_argument('--' + key.replace('_', '-'), type=int, required=True,
                               help=f'{lo}..{hi}; all five values required')
    upload = commands.add_parser('upload', help='Upload a signed image; does not arm or reset')
    upload.add_argument('path', type=Path)
    upload.add_argument('--stop-after', type=int,
                        help='Stop at this acknowledged absolute byte offset (>=32, within image)')
    upload.add_argument('--chunk-size', type=int, default=256,
                        help='Requested image bytes per SMP request (32..384; final chunk may be smaller)')
    commands.add_parser('trial', help='Arm TEST upgrade; requires locally confirmed running image')
    commands.add_parser('reset', help='Request reset; disconnect before response is an uncertain outcome')
    args = parser.parse_args(argv)
    if not args.address.strip():
        parser.error('--address cannot be empty')
    if not 0 < args.timeout <= 120:
        parser.error('--timeout must be >0 and <=120 seconds')
    if args.command in ('upload', 'trial', 'configure') and (
            not args.expected_id or not re.fullmatch('[0-9a-f]{16}', args.expected_id)):
        parser.error('writes require --expected-id with exactly 16 lowercase hex characters')
    if args.command == 'upload':
        if not args.path.name.endswith('.signed.bin'):
            parser.error('upload requires a .signed.bin image')
        if not IMAGE_HEADER_SIZE <= args.chunk_size <= MAX_UPLOAD_CHUNK:
            parser.error('requested chunk size must be 32..384 bytes')
        if args.stop_after is not None and args.stop_after < IMAGE_HEADER_SIZE:
            parser.error('--stop-after must be at least 32 bytes')
    return args


async def run(args):
    image = None
    if args.command == 'upload':
        image = args.path.read_bytes()
        validate_upload(image, args.chunk_size, args.stop_after)
    try:
        from bleak import BleakClient
        import cbor2  # noqa: F401 -- validate runtime dependency before connecting
    except ImportError:
        raise RecoveryError('Runtime requires bleak and cbor2 in your Python environment') from None
    session = None

    def disconnected(client):
        if session is not None:
            session.abort()

    client = BleakClient(args.address, timeout=args.timeout, pair=False,
                         disconnected_callback=disconnected)
    try:
        await asyncio.wait_for(client.connect(), args.timeout)
        service = client.services.get_service(SMP_SERVICE_UUID)
        characteristic = None if service is None else service.get_characteristic(SMP_CHARACTERISTIC_UUID)
        if characteristic is None:
            raise RecoveryError('Official SMP service/characteristic unavailable on selected device')

        async def write(frame):
            await write_frame(client, characteristic, frame)

        session = SMPSession(write, timeout=args.timeout)
        await asyncio.wait_for(client.start_notify(characteristic, session.notify), args.timeout)
        recovery = RecoveryClient(session)
        if args.command == 'status':
            print(json.dumps(diagnostics(await recovery.status()), indent=2))
        elif args.command == 'config':
            print(json.dumps(await recovery.configuration(), indent=2))
        elif args.command == 'configure':
            tuning = {key: getattr(args, key) for key in MOTION_BOUNDS}
            print(json.dumps(await recovery.configure(args.expected_id, tuning), indent=2))
            print('Saved; check status for config_pending and sensor_error after application')
        elif args.command == 'upload':
            offset = await recovery.upload(
                image, args.expected_id, chunk_size=args.chunk_size, stop_after=args.stop_after,
                progress=lambda off, total: print(f'Upload acknowledged: {off}/{total} bytes', flush=True))
            print('Upload complete' if offset == len(image) else f'Intentional partial stop at {offset} bytes')
        elif args.command == 'trial':
            await recovery.trial(args.expected_id)
            print('Trial armed; reset explicitly to boot it')
        else:
            await recovery.reset()
            print('Reset acknowledged')
    finally:
        if session is not None:
            session.abort()
        await asyncio.wait_for(client.disconnect(), args.timeout)


def main(argv=None):
    args = parse_args(argv)
    try:
        asyncio.run(run(args))
        return 0
    except KeyboardInterrupt:
        print('Cancelled; inspect status before explicitly retrying', file=sys.stderr)
        return 130
    except RecoveryError as exc:
        print(f'Error: {exc}', file=sys.stderr)
    except Exception:
        # Backend exceptions may contain sensitive device/agent data. Do not echo.
        print('Error: check image path, device address, authenticated OS bond and maintenance mode. '
              'No automatic pairing or replay; inspect status in a new invocation.', file=sys.stderr)
    return 1


if __name__ == '__main__':
    sys.exit(main())
