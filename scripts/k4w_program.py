#!/usr/bin/env python3
"""Guarded K4W first installation. No MQTT, pairing, OTA or automatic recovery retry."""
import argparse
import datetime
import fcntl
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / 'local-backups/packaged-tags'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def progress(message):
    print(message, flush=True)


def identity(tag, name, mac, serial):
    tag, name = tag.lower(), name.strip()
    compact = mac.replace(':', '').upper()
    require(re.fullmatch(r'b1[0-9a-f]{2}', tag) and tag != 'b100', 'Use a deployment tag ID b101–b1ff')
    require(0 < len(name) <= 32 and all(c.isprintable() for c in name), 'Invalid bin name')
    require(re.fullmatch(r'[0-9A-F]{12}', compact), 'Invalid casing MAC')
    require(re.fullmatch(r'[0-9]{1,20}', serial), 'Invalid casing serial')
    return {'schema': 1, 'model_on_casing': 'K4W', 'assigned_uwb_id': tag,
            'bin_name': name, 'label_mac': ':'.join(compact[i:i+2] for i in range(0,12,2)),
            'label_serial': serial, 'hardware_id': None, 'anchor_paired': False}


def protected_hardware_ids(config):
    require(isinstance(config, dict), 'Profile must be an object')
    ids = config.get('protected_hardware_ids')
    require(isinstance(ids, list) and ids, 'Profile needs a non-empty protected_hardware_ids list')
    require(all(isinstance(chip, str) and re.fullmatch(r'[0-9a-f]{16}', chip) for chip in ids),
            'Invalid protected_hardware_ids entry')
    require(len(ids) == len(set(ids)), 'Duplicate protected_hardware_ids entry')
    return set(ids)


def check_registry(root, target, protected_ids):
    compact = target['label_mac'].replace(':', '')
    require(not (root / compact).exists(), 'Casing already recorded: inspect it; never replay installation')
    chips = set(protected_ids)
    # Fail closed on incomplete/corrupt records, not just successful installs.
    for directory in root.iterdir() if root.exists() else []:
        if not directory.is_dir():
            continue
        path = directory / 'identity.json'
        require(path.is_file(), f'Unresolved tag directory: {directory.name}')
        entry = json.loads(path.read_text())
        for key in ('label_mac', 'assigned_uwb_id', 'bin_name'):
            require(isinstance(entry.get(key), str), f'Invalid identity record: {directory.name}')
            require(entry[key].casefold() != target[key].casefold(), f'Already registered {key}: {entry[key]}')
        if entry.get('hardware_id'):
            chips.add(entry['hardware_id'])
    return chips


def check_chip(words, part, geometry, known, expected=None):
    require(len(words) == 2 and words not in ([0,0], [0xffffffff,0xffffffff]), 'Invalid DEVICEID')
    require(part == 0x52833 and geometry == [4096,128], 'Not the verified nRF52833 / 512-KiB target')
    chip = ''.join(f'{word:08x}' for word in words)
    require(chip not in known, 'Already commissioned hardware: refusing first installation')
    require(expected is None or chip == expected, 'Connected chip changed: stopped before write')
    return chip


def check_hash(path, expected):
    require(re.fullmatch(r'[0-9a-f]{64}', expected), 'Invalid artifact SHA256')
    data = path.read_bytes()
    require(hashlib.sha256(data).hexdigest() == expected, f'Artifact hash mismatch: {path.name}')
    return data


def private_write(path, data):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


class Record:
    def __init__(self, root, target, photo):
        self.directory = root / target['label_mac'].replace(':', '')
        self.directory.mkdir(mode=0o700)
        self.path = self.directory / 'identity.json'
        self.data = dict(target, photo='casing-label.jpg', program_state='started',
                         factory_backup_complete=False,
                         factory_contents_disposition='Owner waived factory backups and authorized first-install erase/unlock')
        self.update()
        private_write(self.directory / 'casing-label.jpg', photo.read_bytes())

    def update(self, **values):
        self.data.update(values)
        self.data['updated_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        fd, temporary = tempfile.mkstemp(prefix='.identity-', dir=self.directory)
        try:
            with os.fdopen(fd, 'w') as stream:
                json.dump(self.data, stream, indent=2)
                stream.write('\n')
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, self.path)
            for directory in (self.directory, self.directory.parent):
                directory_fd = os.open(directory, os.O_RDONLY)
                try:
                    os.fsync(directory_fd)
                finally:
                    os.close(directory_fd)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)


def tcl_path(path):
    value = str(path)
    require(not any(c in value for c in '{}\\\r\n'), 'Path contains unsupported Tcl characters')
    return '{' + value + '}'


class Tcl:
    def __init__(self, stream):
        self.stream = stream

    def command(self, command):
        wrapped = 'set rc [catch {' + command + '} msg]; format "%d:%s" $rc $msg'
        self.stream.sendall(wrapped.encode() + b'\x1a')
        response = bytearray()
        while not response.endswith(b'\x1a'):
            part = self.stream.recv(4096)
            if not part:
                raise RuntimeError('Debugger disconnected; no command replay')
            response.extend(part)
            require(len(response) < 1024 * 1024, 'Unexpected debugger response size')
        result = response[:-1].decode().strip()
        if not result.startswith('0:'):
            raise RuntimeError('Debugger command failed; inspect private OpenOCD log')
        return result[2:]

    def read(self, address, count=1, width=32):
        result = [int(v,0) for v in self.command(f'read_memory {address:#x} {width} {count}').split()]
        require(len(result) == count, 'Incomplete memory read')
        return result

    def dump(self, path, address, size):
        private_write(path, b'')  # Reserve only this output; never overwrite old evidence.
        self.command(f'dump_image {tcl_path(path)} {address:#x} {size:#x}')
        data = path.read_bytes()
        require(len(data) == size, 'Incomplete readback')
        return data


class Probe:
    """Own only the relay/debugger children started by this invocation."""
    def __init__(self, config, directory):
        self.config, self.directory = config, directory
        self.children, self.logs, self.threads = [], [], []
        self.tcl = None

    def start(self, name, args, ready, extra=None):
        log = open(self.directory / (name + '.log'), 'x', buffering=1)
        os.chmod(log.name, 0o600)
        self.logs.append(log)
        child = subprocess.Popen(args, cwd=ROOT, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True)
        self.children.append(child)
        started, extra_seen = threading.Event(), threading.Event()
        def consume():
            for line in child.stdout:
                log.write(line)
                if extra and extra in line:
                    extra_seen.set()
                if ready in line:
                    started.set()
        thread = threading.Thread(target=consume, daemon=True)
        thread.start()
        self.threads.append(thread)
        deadline = time.monotonic() + 35
        while not started.wait(0.1):
            require(child.poll() is None, f'{name} exited; inspect its private log')
            require(time.monotonic() < deadline, f'{name} startup timed out')
        require(extra is None or extra_seen.is_set(), 'Wrong probe serial; no target mutation')

    def __enter__(self):
        try:
            # Refuse someone else's debugger; never attach to it or kill it.
            for port in (4441,6666,3333,4444):
                with socket.socket() as check:
                    check.bind(('127.0.0.1',port))
            ipaddress.ip_address(self.config['probe_ip'])
            self.start('relay', [sys.executable,str(ROOT/'scripts/dap_relay.py'),
                                '--probe',self.config['probe_ip']], 'Relay ready')
            self.start('openocd', [self.config['openocd'],'-c','set PROBE_IP 127.0.0.1',
                                  '-f',str(ROOT/'firmware/nesso-probe/openocd-nrf52.cfg'),
                                  '-c','cmsis-dap quirk enable','-c','cmsis-dap tcp min_timeout 1000',
                                  '-c','init'], 'Listening on port 6666',
                       'Serial# = ' + self.config['probe_serial'])
            stream = socket.create_connection(('127.0.0.1',6666),timeout=5)
            stream.settimeout(180)
            self.tcl = Tcl(stream)
            return self.tcl
        except BaseException:
            self.__exit__(None,None,None)
            raise

    def __exit__(self, *unused):
        if self.tcl:
            try:
                self.tcl.stream.settimeout(2)
                self.tcl.command('shutdown')
            except (OSError,RuntimeError,ValueError):
                pass
            self.tcl.stream.close()
        for child in reversed(self.children):
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=2)
        for thread in self.threads:
            thread.join(timeout=2)
        for log in self.logs:
            log.close()


def artifacts(config):
    from elftools.elf.elffile import ELFFile
    from intelhex import IntelHex
    from k4w_ota_preflight import check
    from k4w_release_manifest import parse_image, release_manifest
    require(__debug__, 'Do not run with python -O: preflight assertions are required')
    files = {key: Path(value).expanduser().resolve() for key,value in config['files'].items()}
    expected = config['sha256']
    require(set(files) == set(expected) == {'boot_hex','boot_bin','boot_elf','app_hex','app_bin','app_elf'},
            'Profile must pin all six artifacts')
    blobs = {key:check_hash(path,expected[key]) for key,path in files.items()}
    build = Path(config['build_dir'])
    check(build)
    for key, relative in {'app_hex':'k4w-tag/zephyr/zephyr.signed.hex',
                          'app_bin':'k4w-tag/zephyr/zephyr.signed.bin',
                          'app_elf':'k4w-tag/zephyr/zephyr.elf',
                          'boot_hex':'mcuboot/zephyr/zephyr.hex',
                          'boot_bin':'mcuboot/zephyr/zephyr.bin',
                          'boot_elf':'mcuboot/zephyr/zephyr.elf'}.items():
        require((build/relative).read_bytes() == blobs[key], 'Preflight build does not match selected artifacts')
    for kind in ('app_bin','app_hex'):
        subprocess.run([sys.executable,config['imgtool'],'verify','-k',config['key'],str(files[kind])],
                       check=True,timeout=30,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    symbols = {}
    with files['app_elf'].open('rb') as stream:
        elf = ELFFile(stream)
        for symbol in elf.get_section_by_name('.symtab').iter_symbols():
            name = symbol.name.split('.lto_priv.')[0]
            if name in {'ota_provision_request','ota_diag','tag_diag','status','ota_pairing_pin',
                        'ble_ok','bt_dev'}:
                require(name not in symbols, f'Ambiguous ELF symbol: {name}')
                symbols[name] = (symbol['st_value'],symbol['st_size'])
    for name,size in {'ota_provision_request':20,'ota_diag':36,'tag_diag':60,'status':20,
                      'ota_pairing_pin':4,'ble_ok':1}.items():
        require(name in symbols and symbols[name][1] == size, f'Unsupported diagnostic ABI: {name}')
    require('bt_dev' in symbols, 'Missing Bluetooth identity symbol')
    images = []
    for key,base in (('boot_hex',0),('app_hex',0x10000)):
        image = IntelHex(str(files[key]))
        require(len(image.segments()) == 1 and image.minaddr() == base, 'Expected one contiguous image segment')
        images.append((key,base,bytes(image.tobinarray(start=base,end=image.maxaddr()))))
    require(images[0][2] == blobs['boot_bin'], 'Boot HEX and BIN differ')
    # BIN/HEX ECDSA encodings may differ, but their signed payload/hash must agree.
    parsed, hex_parsed = parse_image(blobs['app_bin']), parse_image(images[1][2])
    require(parsed == hex_parsed, 'Signed BIN/HEX release mismatch')
    require(blobs['app_bin'][:parsed['signed_region_size']] == images[1][2][:parsed['signed_region_size']],
            'Signed BIN/HEX payload differs')
    offset = parsed['signed_region_size'] + 4
    while int.from_bytes(blobs['app_bin'][offset:offset+2],'little') != 0x10:
        offset += 4 + int.from_bytes(blobs['app_bin'][offset+2:offset+4],'little')
    return {'files':files,'blobs':blobs,'symbols':{k:v[0] for k,v in symbols.items()},
            'images':images,'manifest':release_manifest(files['app_bin']), 'hash_offset':offset+4}


def install(tcl, record, bundle, known):
    symbols = bundle['symbols']
    chip = None
    def guard():
        return check_chip(tcl.read(0x10000060,2),tcl.read(0x10000100)[0],
                          tcl.read(0x10000010,2),known,chip)
    require(int(tcl.command('nrf52.dap apreg 1 0xfc'),0) == 0x02880000, 'Unexpected CTRL-AP')
    readable = int(tcl.command('nrf52.dap apreg 1 0xc'),0)
    require(readable in (0,1), 'Unknown protection state')
    # APPROTECTSTATUS=1 permits reads. Protected factory chips require the
    # operator's physical casing confirmation; FICR is unavailable until erase.
    if readable == 1:
        chip = guard()
        require(tcl.read(0x10000)[0] != 0x96f3b83d, 'Existing signed installation; first-install refused')
    record.update(program_state='erase_started', hardware_id=chip)
    progress('Erasing factory contents once (backup waived); never automatically replayed.')
    tcl.command('nrf52_recover')
    require(int(tcl.command('nrf52.dap apreg 1 0xc'),0) == 1, 'Recovery not verified')
    chip = guard()
    address = tcl.read(0x100000a0,3)
    raw_address = ':'.join(f'{v:02X}' for v in (address[1].to_bytes(4,'little')+
                           address[2].to_bytes(4,'little')[:2])[::-1])
    record.update(program_state='recovered',hardware_id=chip,chip='nRF52833',
                  ficr_deviceaddr_raw=raw_address,ficr_deviceaddrtype_raw=f'{address[0]:#x}')
    progress(f'Chip {chip}; provisioning {record.data["assigned_uwb_id"]} / {record.data["bin_name"]}.')
    tcl.command('reset halt')
    require(tcl.command('nrf52.cpu curstate') == 'halted', 'Target did not halt')
    guard()
    require(tcl.read(0x10001000,1024) == [0xffffffff]*1024, 'Unexpected UICR after factory erase')
    def ready():
        deadline = time.monotonic()+5
        while tcl.read(0x4001e400) != [1]:
            require(time.monotonic()<deadline, 'NVMC busy; stopped')
            time.sleep(0.02)
    record.update(program_state='writing')
    ready()
    tcl.command('mww 0x4001e504 1'); ready()
    tcl.command('mww 0x1000120c 0xfffffffe'); ready()
    tcl.command('mww 0x4001e504 0'); ready()
    uicr = bytearray(b'\xff'*4096)
    uicr[0x20c:0x210] = b'\xfe\xff\xff\xff'
    require(tcl.dump(record.directory/'uicr-nfc-gpio.bin',0x10001000,4096) == uicr,
            'UICR verification failed; no more writes')
    for key,base,data in bundle['images']:
        guard()
        # Program a private immutable snapshot of the preflighted bytes, not a live build path.
        image_path = record.directory / ('program-' + key + '.hex')
        private_write(image_path,bundle['blobs'][key])
        progress(f'Programming {key}; {len(data)} bytes, then full readback.')
        tcl.command('flash write_image ' + tcl_path(image_path))
        output = record.directory / ('installed-boot-readback.bin' if base==0 else 'installed-app-readback.bin')
        require(tcl.dump(output,base,len(data)) == data, 'Firmware readback mismatch; no resume')
    guard()
    tcl.command('reset halt')
    require(tcl.command('nrf52.cpu curstate') == 'halted', 'Target did not halt')
    guard()
    request = [0x42525056,0x53445731,int(record.data['assigned_uwb_id'],16),
               int(chip[:8],16),int(chip[8:],16)]
    tcl.command(f'write_memory {symbols["ota_provision_request"]:#x} 32 {{' +
                ' '.join(hex(v) for v in request) + '}')
    require(tcl.read(symbols['ota_provision_request'],5) == request, 'Provisioning request readback failed')
    record.update(program_state='booting',nfcpins='0xfffffffe',release=bundle['manifest'],
                  mcuboot_bin_sha256=hashlib.sha256(bundle['blobs']['boot_bin']).hexdigest())
    tcl.command('resume')
    def healthy():
        deadline = time.monotonic()+70
        while time.monotonic()<deadline:
            guard()
            ota,radio,power = tcl.read(symbols['ota_diag'],9),tcl.read(symbols['tag_diag'],15),tcl.read(symbols['status'],5)
            if ota[:5] == [0x42524f54,1,0,1,1] and radio[2]==0 and radio[3]==0xdeca0302:
                require(radio[11]==int(record.data['assigned_uwb_id'],16), 'Wrong runtime UWB identity')
                require(power[3]==0, 'Motion sensor fault')
                require(tcl.read(symbols['ble_ok'],width=8)==[1], 'BLE unhealthy')
                require(tcl.read(0x1000120c)==[0xfffffffe], 'NFC/GPIO setting changed')
                require(bytes(tcl.read(0x10000,32,8))==bundle['blobs']['app_bin'][:32], 'Running header mismatch')
                require(bytes(tcl.read(0x10000+bundle['hash_offset'],32,8)).hex()==bundle['manifest']['image_hash'],
                        'Running image digest mismatch')
                return {'wake_count':power[0],'battery_mv':power[2]&0xffff,'misses':power[2]>>16,
                        'sensor_error':power[3],'moving':bool(power[4]&255),
                        'uwb_sleeping':bool((power[4]>>8)&255),'radio_attempts':radio[4]}
            time.sleep(1)
        raise RuntimeError('Local boot health/confirmation not established; do not reflash')
    progress('Checking local health and signed-image confirmation.')
    healthy()
    progress('Restarting tag once to verify identity survives without a provisioning cookie.')
    guard()
    require(tcl.read(symbols['ota_provision_request'],5) == [0]*5, 'Provisioning cookie was not consumed')
    tcl.command('reset halt')
    require(tcl.command('nrf52.cpu curstate') == 'halted', 'Target did not halt')
    guard()
    # Invalidate the old ready flag while halted: only this new boot may pass health.
    tcl.command(f'mww {symbols["ota_diag"]+4:#x} 0')
    require(tcl.read(symbols['ota_diag']+4) == [0], 'Old diagnostic ready flag remains set')
    tcl.command('resume')
    health = healthy()
    # This diagnostic layout is pinned to the profile ELF (bt_dev starts with id_addr[0], id_count).
    ble = tcl.read(symbols['bt_dev'],8,8)
    require(ble[0]==1 and ble[7]==1 and ble[6]&0xc0==0xc0, 'Unexpected Bluetooth identity layout')
    ble_address = ':'.join(f'{value:02X}' for value in ble[1:7][::-1])
    raw = tcl.dump(record.directory/'commissioning-pin.raw',symbols['ota_pairing_pin'],4)
    pin = int.from_bytes(raw,'little')
    require(pin<=999999, 'Invalid commissioning PIN; never print it')
    private_write(record.directory/'commissioning-pin.txt',f'{pin:06d}\n'.encode())
    record.update(program_state='verified',installed_firmware=bundle['manifest']['version'],
                  image_hash=bundle['manifest']['image_hash'],locally_confirmed=True,
                  identity_survived_restart=True,binrange_ble_address=ble_address,
                  binrange_ble_address_type=1,binrange_ble_address_verified_locally=True,
                  verification=health)
    progress(f'VERIFIED {record.data["assigned_uwb_id"]} / {record.data["bin_name"]}; chip {chip}; BLE {ble_address}.')
    progress('PIN saved privately. MQTT adoption/Bluetooth pairing are separate and have NOT been performed.')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ('tag','name','mac','serial'):
        parser.add_argument('--'+field,required=True)
    parser.add_argument('--photo',type=Path,required=True)
    parser.add_argument('--profile',type=Path,default=ROOT/'local-backups/programmer.json')
    parser.add_argument('--check',action='store_true',help='Validate inputs/artifacts only; no probe or tag record')
    parser.add_argument('--erase-factory',action='store_true',help='Authorize one first-install erase without factory backup')
    parser.add_argument('--battery-disconnected',action='store_true',help='Confirm only jig VDD powers the tag')
    parser.add_argument('--confirm-casing',help='Live operator check: exact MAC/SERIAL of the new unit physically on the jig')
    parser.add_argument('--qr',help='Exact independently decoded QR payload, if available')
    args = parser.parse_args(argv)
    require(args.check or (args.erase_factory and args.battery_disconnected),
            'Live programming needs --erase-factory and --battery-disconnected')
    target = identity(args.tag,args.name,args.mac,args.serial)
    casing = target['label_mac'].replace(':','') + '/' + target['label_serial']
    require(args.check or args.confirm_casing == casing,
            'Live programming requires --confirm-casing ' + casing + ' after checking the attached new unit')
    if not args.check:
        target['operator_confirmed_casing'] = args.confirm_casing
    require(args.photo.is_file() and args.photo.stat().st_size>0, 'Missing casing photograph')
    if args.qr:
        require(args.qr==f'MAC:{target["label_mac"].replace(":","")},SERIAL:{args.serial};',
                'Decoded QR disagrees with casing inputs')
        target['qr_payload']=args.qr
    config = json.loads(args.profile.read_text())
    protected_ids = protected_hardware_ids(config)
    check_registry(REGISTRY,target,protected_ids)
    progress('Preflight: release hashes, signatures, effective config, layout and RAM handoff.')
    bundle = artifacts(config)
    if args.check:
        progress('CHECK PASSED; no probe connection, erase, flash or identity record created.')
        return 0
    os.umask(0o077)
    REGISTRY.mkdir(parents=True,exist_ok=True,mode=0o700)
    with (REGISTRY/'.programmer.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        known = check_registry(REGISTRY,target,protected_ids)
        record = Record(REGISTRY,target,args.photo)
        try:
            with Probe(config,record.directory) as tcl:
                install(tcl,record,bundle,known)
        except BaseException as error:
            record.update(program_state='needs_inspection',failure_type=type(error).__name__)
            raise
    progress('Debugger closed. Preserve this casing/board match; battery-only reporting remains to check.')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError,AssertionError) as error:
        print(f'STOPPED ({type(error).__name__}): {error}. No automatic retry; inspect before further writes.',file=sys.stderr)
        raise SystemExit(1)
