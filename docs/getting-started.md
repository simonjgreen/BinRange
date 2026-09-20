# Getting started

BinRange currently expects embedded-development experience. It is a field
prototype, not a one-click Home Assistant integration. No deployable prebuilt
images or signing keys are distributed.

## What you need

- Compatible ESP32-WROVER/DW3000 anchor and K4W nRF52833/DW3110 tags.
- Stable permanent anchor power and Wi-Fi coverage.
- MQTT broker and Home Assistant; tags do not connect directly to Wi-Fi.
- SWD programmer for each tag's first installation and recovery.
  The [Nesso probe](../firmware/nesso-probe/README.md) is an optional implementation.
- PlatformIO for the anchor/emulator/probe, and the pinned Nordic/Zephyr toolchain
  in the [tag build guide](../firmware/k4w-tag/BUILD.md).
- Your own private signing key, admin password and tag identity register.

Review [hardware and power](hardware.md) before wiring. Never supply a tag from
its battery and programmer VDD simultaneously. Verify board identity and voltage,
not just a photograph of a similar board.

## Setup sequence

1. **Prepare the broker and anchor.** Copy the anchor's `secrets.h.example` to
   ignored `secrets.h` and set your own Wi-Fi and first-boot broker values.
   Generate private admin bootstrap input as specified in [tag updates](tag-updates.md).
   Build from the repository root with `pio run -d firmware/anchor -e anchor`.
   This builds; it does not flash. Inspect the selected physical board before
   any upload. Broker credentials can subsequently be changed in the web UI.
2. **Build and verify a signed tag image.** Follow the SDK/partition/preflight
   instructions; keep the signing key backed up outside Git and off the anchor.
   Never use someone else's provisioned binary or silently replace an installed
   bootloader/signing key.
3. **Commission one tag at a time.** Record casing label ↔ immutable chip ID ↔
   firmware BLE address ↔ UWB ID ↔ chosen name in private local storage.
   First installation can erase factory software; decide deliberately whether
   to preserve it. Provisioning is not an ordinary firmware update.
4. **Adopt and pair separately.** MQTT adoption creates the HA device;
   authenticated BLE commissioning authorizes maintenance. UWB reporting alone
   proves neither pairing nor a successful update.
5. **Check real reports in HA.** Verify distance, reception time, motion, voltage
   and health belong to the right device. Configure a daily dashboard and a
   diagnostic view using actual discovered entities, not assumed names.
6. **Configure collection schedules.** Install
   [Waste Collection Schedule via HACS](https://github.com/mampfes/hacs_waste_collection_schedule/blob/master/doc/installation.md)
   and select your own provider/calendar. Map its collection categories to your
   tags. One category may correspond to multiple bins.
7. **Validate the installed site.** Measure storage/collection distances and
   obstruction effects. Choose a useful threshold with margin; treat stale
   readings as Unknown. Only then enable position-aware reminders.

## Local/private configuration

Keep these out of Git:

| Data | Suggested local location |
| --- | --- |
| Wi-Fi / first-boot broker configuration | `firmware/<target>/src/secrets.h` |
| HA access token | `.ha-token` or a private credential store |
| Admin bootstrap / recovery secret | Ignored file supplied to the build by path |
| Signing key and provisioned images | `local-backups/`, with separate secure backup |
| Programmer profile / protected chip IDs | `local-backups/programmer.json` |
| Household dashboard helpers / entity mappings | Private deployment tooling |
| Tag/casing register, observations, photos | `local-backups/deployment/` |

Examples use synthetic identifiers and placeholder hosts. Configure tools
explicitly for your equipment. The optional programmer profile must include a
nonempty `protected_hardware_ids` list: immutable chip IDs that must never be
treated as new first-install targets. Missing protection data must fail closed.

The firmware can range without HA, but alerts obviously cannot operate during
an HA/broker outage. Keep the maintenance HTTP/MQTT endpoints on a trusted LAN;
they are not designed for direct Internet exposure.

## Before relying on it

Use [the roadmap](current-work.md) as an acceptance checklist. A few successful
bench exchanges are not a field battery-life, ingress-protection or unattended
OTA guarantee. Keep a known-good signed release and a tested recovery method.

## Host-side checks

Run `pio test -d firmware/anchor -e native` for the Arduino-free core. Python
checks live in `scripts/test_*.py`; run the `test_k4w*.py` and `test_anchor*.py`
groups with `python -m unittest discover -s scripts -p 'PATTERN'`.
These checks do not flash hardware.

The anchor integration tests compile against the installed PlatformIO framework
and pinned NimBLE library. Build the anchor first to obtain them. Set
`BINRANGE_NIMBLE_REFERENCE` to a separate, unmodified NimBLE-Arduino 2.5.1 source
directory: the tests verify upstream hashes before exercising the build patches.
`BINRANGE_PLATFORMIO_PACKAGES` can override the default
`~/.platformio/packages` location. The optional probe tests require its separately
downloaded upstream source; SDK image-preflight tools need their documented
Python dependencies.
