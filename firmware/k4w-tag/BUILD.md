# Signed tag firmware: build and recovery

Experimental integration candidate. Signed BLE updates, local confirmation,
watchdog rollback and motion/sleep behavior have been exercised; complete
field/battery/unattended-OTA acceptance is still open. See
[findings](../../docs/findings.md) and [roadmap](../../docs/current-work.md).

## Toolchain and key

`west.yml` pins Nordic nRF Connect SDK v3.3.0 and matching Zephyr. The tested
toolchain was Zephyr SDK 0.17.4 / ARM GCC 12.2.0, west 1.5.0, CMake 4.4.3 and
Ninja 1.13.2. Obtain the SDK separately and verify effective generated configs.

Use your own private ECDSA-P256 signing key, stored outside Git and securely
backed up. Losing it prevents signing future images accepted by that bootloader.
Never replace an installed key silently. The anchor never receives the private
key; the bootloader contains the public key.

Set paths for your checkout, SDK workspace, SDK tools, fresh build directory and
private key. The following is a **build**, not an upload:

```sh
# Replace these paths with your own; do not reuse someone else's signed image.
BINRANGE_ROOT=/absolute/path/to/BinRange
NCS_ROOT=/absolute/path/to/ncs
BUILD_DIR=/absolute/path/to/new-build
SIGNING_KEY=/absolute/path/to/private-signing-key.pem
ZEPHYR_SDK_INSTALL_DIR=/absolute/path/to/zephyr-sdk-0.17.4
export ZEPHYR_SDK_INSTALL_DIR
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
cd "$NCS_ROOT"
west build --sysbuild -b k4w/nrf52833 -d "$BUILD_DIR" \
  "$BINRANGE_ROOT/firmware/k4w-tag" -- \
  -DBOARD_ROOT="$BINRANGE_ROOT/firmware/k4w-tag" \
  -DBINRANGE_RADIO_DIAGNOSTIC=OFF \
  '-Dk4w-tag_CONF_FILE=prj.conf;ota.conf' \
  -DDTC_OVERLAY_FILE="$BINRANGE_ROOT/firmware/k4w-tag/ota.overlay" \
  -Dmcuboot_DTC_OVERLAY_FILE="$BINRANGE_ROOT/firmware/k4w-tag/sysbuild/mcuboot.overlay" \
  "-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"$SIGNING_KEY\"" \
  -DEXTRA_ZEPHYR_MODULES="$BINRANGE_ROOT/firmware/k4w-tag/modules/recovery"
```

Ensure the SDK environment provides the pinned tools before running west.
The bootloader overlay selects its partition at zero; the application selects
the primary slot. Do not pass the application's chosen partition to MCUboot.
The diagnostic CMake option uses `zephyr_get` for sysbuild. MCUmgr requires
NET_BUF/ZCBOR explicitly; inspect generated configuration, not only input files.

## Installed partition contract

| Region | Start | Size |
| --- | --- | --- |
| Bootloader | 0x00000 | 0x10000 |
| Primary | 0x10000 | 0x35000 |
| Secondary | 0x45000 | 0x36000 |
| Settings | 0x7b000 | 0x05000 |

Offset swap reserves the secondary's first 4 KiB. Image allowance after the
primary trailer is **212992 bytes**, including header/TLVs. BIN and HEX are
separately signed and signature lengths may differ. Inspect both exact artifacts.
Do not hand-write an image to the secondary base; use the SDK image manager.

LTO requires `CONFIG_ISR_TABLES_LOCAL_DECLARATION=y`; both must be effective.
The integrated candidate otherwise exceeds the signed slot. Never silently
repartition an installed tag or install a newly generated bootloader merely
because sysbuild produced one.

## Preflight and first installation

Run `scripts/k4w_ota_preflight.py BUILD_DIR` with the SDK Python, without `-O`.
It checks configs, exact HEX ranges/pages, signed size and RAM handoff against
both ELFs. Also verify both signed BIN and HEX using SDK imgtool and your key.
Layout tests are not signature verification.

Use the guarded [programmer](../nesso-probe/README.md#first-install-command) for
a deliberately selected new unit. Record casing/immutable chip identity privately;
decide whether to preserve original firmware before any erase/unlock.
A prior first-install permission never authorizes erasing an established tag.

Provisioning requires an identity-guarded reset-halt and one-shot RAM request:
magic `0x42525056`, `0x53445731`, selected UWB ID, DEVICEID0, DEVICEID1.
Use the cookie address reported by preflight, **not a copied address from another
build**. It must be beyond bootloader RAM. The app consumes/clears it before
saving identity and never replaces a valid identity with a cookie.

Capture the commissioning PIN privately through physical SWD; never Git/logs.
`k4w_pair.py` pairs only the explicit selected address using a private PIN file.
Then verify the immutable chip ID with `k4w_ble.py status`. A BLE name is not
identity. Local windows expire; ordinary updates use existing bonds.

## Runtime and tuning

Normal firmware includes accelerometer delta interrupts, internal-VDD sampling,
5 s moving / 10 min idle reporting, 30 s settling and at most two five-second
retries after a locally failed idle/settled exchange. UWB sleeps between attempts;
bounded System ON wakes preserve the 20 s watchdog. The local trial stays awake
for radio/sensor health checks. A sent FINAL alone does not prove reception.

BLE advertises at 2.0–2.1 s outside the local window. Advertising restart waits
for connection recycling; callbacks wake the main owner rather than performing
HCI work. Do not weaken authentication to work around timing.
See [hardware](../../docs/hardware.md) for pin/filter/ADC constraints.

Authenticated management group 64 command 2 reads/writes tuning. Writes require
a confirmed image, secured bond, maintenance window and all five bounded integer
fields. Settings persist before main-loop application; inspect
`config_pending`/`sensor_error`. Example placeholders:

```sh
python scripts/k4w_ble.py --address TAG_BLE_ADDRESS config
python scripts/k4w_ble.py --address TAG_BLE_ADDRESS \
  --expected-id TAG_IMMUTABLE_CHIP_ID configure \
  --moving-ms 5000 --idle-ms 600000 --quiet-ms 30000 \
  --threshold-mg 250 --duration-samples 2
```

The helper uses the existing OS bond, never implicit re-pairing.
Status includes millivolts, motion, wake/IRQ counts, sensor error, local misses and
UWB sleep state. A regulated jig reading is not battery state of charge.

## Rescue boundaries

Match DEVICEID before writes. Use reset-halt, not a plain halt before the RAM
flash algorithm. Locate diagnostics from the exact ELF; addresses vary.
Never automatically replay a failed flash. Normal OTA writes inactive application
space only; preserve bootloader, UICR, identity and bonds.

Validated radio profile: channel 5, 1024-symbol preamble, 850 kb/s,
16-symbol nonstandard SFD, STS off; FINAL delay 4500 UUS in an 8000-UUS window.
Antenna delay is not universal calibration. Retain OTP trim; reset DW3110 low
then release to input rather than driving it high.

Keep known-good signed images/manifests/hashes privately. Direct-boot diagnostic
binaries are not OTA payloads or shortcuts for MCUboot-equipped tags.
Preserve vendor source notices; see [third-party notes](../../docs/third-party.md).
