# MQTT and Home Assistant

One powered anchor runs custom firmware and publishes tag measurements to an
existing MQTT broker. Each adopted tag has its own Home Assistant device;
the anchor has a diagnostics device. No ESPHome component is required.

## Ranging and topic contract

Tags initiate DS-TWR: POLL → anchor RESPONSE → tag FINAL. The anchor calculates
distance from timestamps. Radio work owns SPI on core 1; the main loop owns
registry/MQTT on core 0 alongside the BLE worker. Hardware coexistence matters.
Ranging must not depend on Wi-Fi, broker or HA availability.

| Topic | Purpose |
| --- | --- |
| `binrange/anchor/<anchor>/status` | Retained availability / last will |
| `binrange/anchor/<anchor>/state` | Radio/network/system diagnostics |
| `binrange/anchor/<anchor>/cfg` | Current radio configuration |
| `binrange/anchor/<anchor>/cmd` | Existing non-retained normal controls |
| `binrange/tag/<tag>/config` | Retained adoption/name/settings |
| `binrange/tag/<tag>/anchor/<anchor>/state` | Distance, signal quality, telemetry |
| `binrange/tag/<tag>/status` | Tag check-in availability |
| `binrange/tag/<tag>/anchor/<anchor>/update` | Status-only tag updater diagnostics |
| `binrange/anchor/<anchor>/update` | Status-only anchor updater diagnostics |

Tag IDs are four lowercase hexadecimal digits. Preserve topic/discovery IDs and
calibration when changing labels. HA discovery prefix is `homeassistant`;
device identifiers are `binrange_tag_<tag>` and `binrange_anchor_<anchor>`.

Unknown tags generate sightings, not automatic HA devices. Retained adoption
accepts name, area, offset and stale_after; source authority is `publisher.cpp`
and `core/registry.*`. Empty adoption removes registry/discovery, not necessarily
every retained topic/history row. Cleanup must target exact IDs, not all HA data.

## Measurement and freshness

- `ts` is real reception time reconstructed from the radio event and valid wall
  clock. Stale/reconnect publication must not advance it. Without reception or
  valid clock it is null. `age` is age at publication, not a ticking value.
- After anchor restart, measurements remain unknown until fresh reception.
  Previous absence age is not reconstructed; anchor availability is separate.
- `stale` is short-term range freshness: last-known moving tags become stale
  after 20 s; otherwise the configured per-tag timeout applies.
- `missing` is the independent long absence threshold, initially six hours.
  Never-heard adoption starts unknown, then alarms at timeout. Replaying
  retained configuration does not postpone it; raised absence clears only
  after a real reception.

Extended FINAL is 33 bytes: timestamps 10–21, millivolts 22–23, flags 24
(moving bit 0 / sensor-fault bit 1), missed exchanges 25–26, wakes 27–30, then FCS.
Integers are little-endian; legacy 24/26/29-byte layouts remain accepted.
Misses/wakes saturate and reset on boot. Wakes count motion episodes, not IRQs.

Unknown telemetry is null, not zero/stationary. Zero millivolts clears an old
voltage; sensor fault makes motion unknown. `ok` is unknown because received
FINALs cannot establish an end-to-end success denominator. MQTT binary templates
use native None for unknown, not OFF.

## Home Assistant presentation

Build dashboards from the installation's actual discovered entities. A useful
split is a compact daily page (bin status, next collection, problems, battery)
and a monitoring page (range age, voltage, wakes, signal, anchor and OTA health).
Keep deployment-specific card layouts/entity mappings outside the public source.

A simple initial location rule is a chosen distance threshold:
fresh distance inside it is Home, outside is Out, otherwise Unknown.
The threshold must suit the installed storage/collection geometry; one anchor
cannot distinguish equal-radius locations. Hysteresis/settling should be explicit
if added. Row timestamps are HA updates, not necessarily physical motion times.

Firmware publishes voltage in V with measurement state class. Optional percentage
helpers should be labelled estimates, reject invalid/out-of-range values and
unavailable check-in health, and retain the measured voltage for diagnosis.
The experimental UI used generic
[Zigbee2MQTT `3V_2100`](https://github.com/Koenkk/zigbee-herdsman-converters/blob/master/src/lib/utils.ts)
interpolation; this is not a CR2477-specific capacity/runtime model.
No universal battery alarm threshold is established.

Voltage measurements can have long-term statistics; wake counts, reception
timestamps and binary health histories need separate retention planning.
Averaged missed-counter values are not an event total. Handle counter resets
and distinguish observer/network gaps from tag silence.

## Collection scheduling and automations

Collection dates come from the brilliant
[Waste Collection Schedule](https://github.com/mampfes/hacs_waste_collection_schedule)
Home Assistant integration, installed through HACS. Configure the appropriate
local provider or calendar there; BinRange does not duplicate its schedule logic.

Map provider categories to your named tags. A category may cover more than one
bin. Keep provider entity IDs, mappings, notification targets and household times
in local HA configuration.

Position-aware reminders remain under development. The intended policy combines
due collections with confident Home/Out state, groups reminders, avoids duplicate
phases, handles changed schedules and supports put-out plus return reminders.
Unknown/stale data must not accuse someone of forgetting a bin. Elapsed time
alone proves neither collection nor return. Deliberately replace overlapping
old automations rather than installing duplicates.

## Controls and security

Broker settings are runtime-configurable/persistent in the anchor UI. Never
return/log passwords. Show connection state, failures and recent publications;
a publish attempt does not prove HA received it.

Normal HA controls cover radio/diagnostics. Pairing, signed upload and tag updates
require separate admin authorization in the anchor UI. Updater MQTT entities are
status-only; broker credentials do not grant admin authority.

Use trusted-LAN HTTP/MQTT, not direct Internet exposure. Broker reconnects publish
coherent last state with its true age.
