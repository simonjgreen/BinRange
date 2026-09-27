# BinRange

The calendar says it's bin night. Have the bins actually made it to the kerb?

BinRange gives each bin a battery-powered UWB tag. A fixed anchor measures its
distance and publishes per-tag telemetry to Home Assistant over MQTT. Moving
bins report frequently; stationary bins check in slowly. Home Assistant can
combine that physical evidence with collection dates to show what needs attention.

Collection scheduling for the Home Assistant automations leverages the brilliant
[Waste Collection Schedule](https://github.com/mampfes/hacs_waste_collection_schedule)
integration, available through [HACS](https://hacs.xyz/). It supplies the collection
calendar; BinRange supplies the bin's movement, distance and health.
A [configurable reminder example](examples/home-assistant/README.md) combines the
calendar with fresh Home/Out states for put-out and bring-in notifications.

## Phone reminders

These Home Assistant notifications were sent with sample bin data for the
documentation. The [automation YAML](examples/home-assistant/collection-reminders.yaml)
uses placeholders for your calendar, notification group, helpers and bin entities;
follow the [setup instructions](examples/home-assistant/README.md) before enabling it.

**Put the bins out:** an evening reminder for due bins still at home.

<img src="docs/images/bin-notification-put-out.png" alt="Home Assistant notification: Bins out tomorrow; put General Waste, Recycling 1 and Glass out for collection" width="660">

**Still at home:** an urgent follow-up when the bins have not been put out.
The screenshot uses illustrative wording; the supplied automation titles this
reminder “URGENT: bins still need putting out”.

<img src="docs/images/bin-notification-overdue.png" alt="Mock urgent Home Assistant notification: You have still not put the bins out" width="660">

**Bring the bins in — wording mock-up:** this screenshot illustrates an
emptying-confirmed message. The current automation does **not** detect emptying;
it sends “Bring the bins in” the evening after the scheduled collection when
the bins are still out.

<img src="docs/images/bin-notification-bring-in-mockup.png" alt="Mock-up of a future emptying-confirmed notification: The bins have just been emptied; you can now bring them in" width="660">

## The hardware

The main anchor is now wall mounted in its enclosure, all six bins have tags,
and the Home Assistant setup is in use.

<img src="docs/images/wall-mounted-anchor-enclosure.png" alt="Installed BinRange anchor in its printed case, mounted alongside the power components inside a clear-lidded wall enclosure" width="600">

*The installed anchor enclosure, photographed by the project owner.*

The anchor has a custom printable case; the battery tags attach to the bins.
These renders show the accepted **v7.1.0 WROVER case**, using the actual print
meshes. Colours and surface finish are illustrative.

| Closed case | Inside the case |
| --- | --- |
| ![Studio render of the assembled WROVER case, with a petrol-blue base and a warm light lid](docs/images/wrover-case-v7.1.0-assembled.jpg) | ![Studio render of the separate base and lid showing the PCB locating cones, spring supports and ventilation slots](docs/images/wrover-case-v7.1.0-open.jpg) |

[Case design, version history and print downloads](hardware/wrover-case/README.md).
The separate v7.2.0 DIN rail variant remains experimental.

Installed KKM K4W tags, shown on three different bins:

| Under a wheelie-bin rim | On a food caddy | Under a brown-bin rim |
| --- | --- | --- |
| ![K4W tag mounted beneath the rim of a green wheelie bin](docs/images/k4w-tag-installed-green-bin.jpg) | ![K4W tag attached to a green food caddy near its carrying handle](docs/images/k4w-tag-installed-food-caddy.jpg) | ![K4W tag mounted below the rear rim of a brown wheelie bin](docs/images/k4w-tag-installed-brown-bin.jpg) |

| Anchor board | The installation |
| --- | --- |
| ![Makerfabs ESP32-WROVER anchor board with USB connected, DW3000 radio module and UWB antenna visible](docs/images/makerfabs-wrover-dw3000-board.jpg) | ![A row of household bins in the outdoor installation](docs/images/tagged-bins-installation.jpg) |

*Real installation and development-board photos. Visible device QR codes and
identifiers are obscured; published copies contain no embedded metadata. Mounting
photos document the prototype, not a weatherproofing or coverage guarantee.*

## See it working

![UWB outdoor walk: distance rises and falls, with first-path, RSSI and range-spread traces](docs/images/uwb-walk-test.png)

*Real development walk-test capture using two WROVER boards. Reliable readings
reached roughly 30 m in that setup, with intermittent readings beyond it.
This is not a guaranteed range for a packaged tag or a mounted bin.*

<details>
<summary>Test-phase web interface: live range, link diagnostics and radio tuning</summary>

<img src="docs/images/test-rig-webui.png" alt="Early test-rig web interface showing measured distance, exchange history, signal diagnostics and configurable radio settings" width="660">

*Historical SS-TWR test interface, not the current deployment UI. Its success
percentage and signal interpretation are test-rig diagnostics—not an end-to-end
delivery guarantee or a validated obstruction detector.*

</details>

The important distinction is between **a bin that is home** and **a bin whose
last known reading was home**. Stale data stays visible as stale; silence must
not be mistaken for a bin being put out or brought back.

## How it fits together

```text
Battery tags ──UWB──> Fixed anchor ──MQTT──> Home Assistant
    ▲                    │                     ▲
    └── signed BLE OTA ──┘                     │
                                 Waste Collection Schedule
```

- **Tags:** KKM K4W, nRF52833 + DW3110 + LIS3DH; custom Zephyr firmware.
- **Anchor:** Makerfabs ESP32-WROVER / DW3000; custom Arduino firmware with MQTT,
  local web administration and signed-tag-update delivery.
- **Home Assistant:** one device per tag plus anchor diagnostics; separate daily
  status and technical monitoring views.
- **Maintenance:** signed application updates, local health confirmation and
  rollback; SWD remains the first-installation/rescue path.
- **No ESPHome component or additional BinRange server.** Ranging does not depend
  on the MQTT broker or Home Assistant being available.

## Status and limitations

This is an **experimental, working field prototype**, not a turnkey product.
A six-tag installation is in use, with the anchor wall mounted, tags on all bins
and Home Assistant configured. Multi-week battery life, installed coverage and
unattended BLE maintenance still need longer-term field evidence.

Default tag behavior is immediate motion reporting, about 5 s while moving,
settling after 30 s quiet, and a 10 min stationary check-in. Motion sensitivity
is tunable; missing-check-in monitoring is separate from short-term range
freshness. The initial absence threshold is six hours. Paired tags expose
stationary and moving check-in interval controls in Home Assistant; the anchor
applies them over authenticated Bluetooth and verifies the saved values.
See [check-in interval controls](docs/mqtt.md#changing-tag-check-in-intervals).

One anchor measures **distance, not direction or coordinates**. A Home/Out
distance threshold is site-specific; it cannot distinguish equal-radius places.
Metal, bodies, orientation and mounting affect coverage. Battery voltage is
measured; any percentage derived from it is an estimate, not a fuel gauge.
Do not infer months of battery life from a short voltage trace.

## Build your own

Start with [the setup guide](docs/getting-started.md), then the relevant firmware
instructions. You will need an appropriate UWB anchor, compatible tags, an SWD
programmer, a local MQTT broker and Home Assistant. Review the exact board pinout
and use your own credentials, tag identities and signing key.

| Reference | Purpose |
| --- | --- |
| [Getting started](docs/getting-started.md) | Prerequisites, setup sequence and local configuration |
| [Hardware](docs/hardware.md) | Components, wiring, battery and motion constraints |
| [Printable anchor case](hardware/wrover-case/README.md) | Case renders, version status, source and print files |
| [MQTT / Home Assistant](docs/mqtt.md) | Topics, adoption, freshness and scheduling integration |
| [Collection reminder example](examples/home-assistant/README.md) | Placeholder YAML, phone-group setup and put-out/return reminder behavior |
| [Signed tag build and recovery](firmware/k4w-tag/BUILD.md) | SDK, partitions, signing and safe installation |
| [BLE update contract](docs/tag-updates.md) | Authentication, persistence and recovery invariants |
| [Nesso SWD probe](firmware/nesso-probe/README.md) | Optional programmer build and wiring |
| [Findings](docs/findings.md) | Reusable measured results and their limitations |
| [Roadmap](docs/current-work.md) | Remaining project acceptance work |

## Repository hygiene

The active firmware is in `firmware/anchor` and `firmware/k4w-tag`, with the
shared radio driver in `firmware/shared`. The Nesso probe and `scripts/` contain
programming, recovery and host-test tooling. `firmware/tag-emulator` is a bench
DS-TWR test peer; `firmware/test-rig` is the frozen early SS-TWR experiment.
Neither is required on the installed bins. Older enclosure versions and their
verified archive are retained as design history, with their status recorded in
the [case index](hardware/wrover-case/README.md).

This repository contains reusable source, documentation, synthetic test
identities and selected screenshots. Local addresses, household calendars,
physical tag/casing registers, credentials, signing keys, private captures and
provisioned firmware belong in ignored local storage—not Git.

On an existing working installation, consult ignored
`local-backups/deployment/docs/current-work.md` for its private operating plan
before touching devices. Public examples are not deployment instructions for
someone else's hardware. Do not blindly replay first installation on a paired tag.

Vendor notices remain in their source files. No blanket project licence has
been selected yet; public availability alone does not grant redistribution
rights. See [third-party notes](docs/third-party.md).
