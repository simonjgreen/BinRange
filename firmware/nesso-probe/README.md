# Nesso SWD probe

Optional standalone programmer/recovery jig; not a deployment dependency.

Upstream: [cmsis_dap_tcp_esp32](https://github.com/bkuschak/cmsis_dap_tcp_esp32),
pinned `cc547237006452be22fcf03e25f84cffb0de6a65` (Apache-2.0).
From this directory:

```sh
git clone https://github.com/bkuschak/cmsis_dap_tcp_esp32.git upstream
git -C upstream checkout --detach cc547237006452be22fcf03e25f84cffb0de6a65
pio run -e nesso
python ../../scripts/test_nesso_config.py
NESSO_SDKCONFIG=sdkconfig.nesso python ../../scripts/test_nesso_config.py
```

The upstream checkout is ignored; preserve its licence. Platform espressif32
6.11.0 selects ESP-IDF 5.4.1. Local compatibility code maps newer help-command
deregistration to that API; `-ftls-model=local-exec` avoids RISC-V GOT TLS sections.

No Wi-Fi credentials are compiled in. Provision using the USB console's `wifi`
command and reboot; do not log password-bearing traffic. TCP 4441 is trusted-LAN
only. A blank display is normal for this headless firmware.

## Wiring and power

Header numbers follow Arduino's diagram, not M5Stack's reversed table:

| Tag pad | Nesso header |
| --- | --- |
| GND | 1 |
| CLK | 5 / GPIO6 |
| DIO | 3 / GPIO7 |
| VDD | 7 / 3.3 V |

Remove the tag battery. Check polarity and voltage before attachment; never use
5 V or BAT pins for the tag. Power Nesso fully off before rewiring.
Nesso is battery-backed: unplugging USB alone does not cut target power.
For a coordinated power-cut test, removing all clip contacts disconnects the tag.

Back up and identify your Nesso before replacing its original firmware.
Existing local backups/serials are private, not a generic recovery image.
Tag first installation must deliberately handle read protection and identity;
never infer blank flash from an unlit LED.

## Host transport

OpenOCD needs the CMSIS-DAP TCP backend. Tested:
Espressif `v0.12.0-esp32-20260831`; older distribution builds may lack it.
`openocd-nrf52.cfg` sets 100 kHz after target defaults and binds host control
listeners to localhost. The config itself contains no erase/programming calls.

From repository root, explicitly select your probe:

```sh
python scripts/dap_relay.py --probe PROBE_IP
```

Point OpenOCD at relay `PROBE_IP=127.0.0.1`; set `cmsis-dap quirk enable`
and `cmsis-dap tcp min_timeout 1000` before init. The conservative relay allows
one request/64-byte packet and exits when the debugger disconnects.
A transport failure never authorizes replaying a flash.

## First-install command

`scripts/k4w_program.py` owns its relay/debugger and performs one guarded first
installation. It is **not an updater** for an already commissioned tag.

Use the SDK Python with intelhex, pyelftools and imgtool dependencies.
This example uses synthetic casing identifiers and performs offline checks only:

```sh
python scripts/k4w_program.py \
  --tag b110 --name 'Example bin' --mac 020000000010 --serial 100010 \
  --photo /absolute/path/to/your-casing-photo.jpg \
  --profile /absolute/path/to/private-programmer.json --check
```

Replace every identity/photo with the actual new unit. Optional `--qr` must be
independently decoded. `--check` creates no record and connects to no hardware.
Live installation additionally requires `--erase-factory`,
`--battery-disconnected` and `--confirm-casing MAC/SERIAL`. These assertions
must reflect a real physical check; they are not proof of board/casing identity.
Stop other debugger/relay sessions and leave the tag in place until closure.

Private profile fields:

- `probe_ip`, expected `probe_serial`, OpenOCD executable.
- Matching `build_dir`, `imgtool`, private signing-key path `key`.
- Required nonempty `protected_hardware_ids`: unique lowercase 16-hex chip IDs
  never eligible for first installation. Missing/malformed values fail closed.
- `files` and `sha256` mappings for exactly `boot_hex`, `boot_bin`,
  `boot_elf`, `app_hex`, `app_bin`, `app_elf`.

Use deliberately verified artifacts; the tool never selects an arbitrary latest
build. It reserves a private casing record before mutation, rejects existing
casing/UWB/name/chip identities and unresolved previous attempts. Read-protected
factory chips cannot expose DEVICEID before recovery, so physical selection
remains essential. Probe serial is checked before erase; chip guards run as soon
as readable and before later writes.

Full NFC→GPIO/boot/app readbacks, signed-image confirmation, local health and
identity persistence are checked. PIN/outputs remain private. Adoption and BLE
pairing are separate steps.

**On failure, inspect rather than rerun.** Do not delete an existing record just
to bypass the guard. Determine actual hardware state before any continuation.
See [signed recovery](../k4w-tag/BUILD.md).
