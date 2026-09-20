"""Read-only checks of trusted local sysbuild output before SWD installation.

Run with the pinned SDK Python (pyelftools/intelhex installed). Cryptographic
signature verification is a separate imgtool step on both BIN and HEX.
"""
import argparse
from pathlib import Path
from elftools.elf.elffile import ELFFile
from intelhex import IntelHex
from k4w_image_layout import validate_config, validate_image_size, validate_segments


def symbol(elf, name):
    entries = elf.get_section_by_name('.symtab').get_symbol_by_name(name)
    assert entries and len(entries) == 1, f'Missing/ambiguous symbol: {name}'
    return entries[0]


def check(root):
    boot = root / 'mcuboot/zephyr'
    app = root / 'k4w-tag/zephyr'
    with (boot / 'zephyr.elf').open('rb') as f:
        elf = ELFFile(f)
        boot_end = symbol(elf, '_image_ram_end')['st_value']
        symbol(elf, 'recovery_watchdog_start')
    with (app / 'zephyr.elf').open('rb') as f:
        elf = ELFFile(f)
        cookie = symbol(elf, 'ota_provision_request')
        section = elf.get_section(cookie['st_shndx'])
        assert section.name == 'noinit' and section['sh_type'] == 'SHT_NOBITS'
        assert cookie['st_size'] == 20
        assert boot_end <= cookie['st_value'] < cookie['st_value'] + 20 <= 0x20020000, \
            'Provisioning handoff overlaps bootloader RAM or exceeds tag RAM'
        print(f'Cookie 0x{cookie["st_value"]:08x}; boot RAM end 0x{boot_end:08x}')
        for name in ('ota_diag', 'ota_pairing_pin', 'ota_local_request'):
            print(f'{name}: 0x{symbol(elf, name)["st_value"]:08x}')
    for path, allowed in ((boot / 'zephyr.hex', [(0, 0x10000)]),
                          (app / 'zephyr.signed.hex', [(0x10000, 0x44000)])):
        segments = IntelHex(str(path)).segments()
        validate_segments(segments, allowed)
        pages = sorted({p for a, b in segments for p in range(a // 4096, (b-1)//4096+1)})
        print(path.name, 'segments', [(hex(a), hex(b)) for a, b in segments])
        print('Affected 4-KiB pages:', ', '.join(f'0x{p*4096:05x}' for p in pages))
    size = (app / 'zephyr.signed.bin').stat().st_size
    validate_image_size(size, 0x35000, 4096)
    print('Signed bytes:', size)
    validate_config((app / '.config').read_text(), {
        'CONFIG_MCUMGR': 'y', 'CONFIG_MCUMGR_TRANSPORT_BT_PERM_RW_AUTHEN': 'y',
        'CONFIG_BT_SMP_SC_ONLY': 'y', 'CONFIG_BT_APP_PASSKEY': 'y',
        'CONFIG_MCUMGR_MGMT_NOTIFICATION_HOOKS': 'y',
        'CONFIG_MCUMGR_SMP_COMMAND_STATUS_HOOKS': 'y',
        'CONFIG_FLASH_LOAD_OFFSET': '0x10000',
    })
    validate_config((boot / '.config').read_text(), {
        'CONFIG_BOOT_SWAP_USING_OFFSET': 'y',
        'CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256': 'y',
        'CONFIG_BOOT_VALIDATE_SLOT0': 'y', 'CONFIG_FLASH_LOAD_OFFSET': '0x0',
        'CONFIG_BOOT_WATCHDOG_FEED_NRFX_WDT': 'y',
    })
    print('Read-only artifact checks passed; signature and hardware guards still required.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    check(parser.parse_args().build)
