# Roadmap and acceptance

The architecture is settled: motion-triggered battery tags, one powered UWB
anchor, MQTT, per-tag Home Assistant devices, signed BLE maintenance and SWD
rescue. The prototype has reached a six-tag field installation.

This is the general project roadmap. Household-specific priorities, identifiers,
addresses, reminder choices and dated observations are deliberately kept outside
Git. For an existing local installation, consult its ignored deployment notes.

## Established

- Real tag-initiated ranging and per-tag MQTT/HA attribution.
- Motion/wake reporting, settling, UWB sleep and ten-minute stationary check-ins.
- Signed BLE application updates, local confirmation and watchdog rollback.
- Persistent anchor identity and bond-preserving recovery.
- Multiple real tags commissioned; per-peer Bluetooth store-capacity defect fixed.
- Separate daily/diagnostic dashboards; freshness distinct from long absence.

Evidence and qualifications are in [findings](findings.md), not an exhaustive
chronological build log.

## Next acceptance work

| Area | Completion criterion |
| --- | --- |
| Physical installation | Stable anchor power, secure weather-appropriate mounting, known orientation and accessible batteries |
| Coverage / location | Each tag checked at storage and collection, while moving and after settling; distances independently measured; threshold has useful margin |
| Battery / motion | Meter-checked voltage, representative whole-tag idle/active current, persisted tuning and no excessive nuisance wakes |
| Reliability | Real fleet activity, missed reports, sensor faults and anchor/broker outages have truthful status and bounded recovery |
| Maintenance | Repeatable bonded slow-advertising OTA without jig assistance or operator retries, exact active hash/local confirmation, representative post-trial interruptions and safe uncertain outcomes |
| HA reminders | Real provider calendar/category mapping, stale-safe put-out/return policy, deduplication and explicitly selected notification target |
| Handover | Reproducible signed release, recovery/key backup, current instructions and sanitized evidence |

## Proposed four-week field trial

Observe normal use across several collection cycles without changing firmware
or reporting cadence unnecessarily. It is a reliability/drain screen, not a
capacity test or a battery-lifetime claim.

Record raw voltage trends, real reception gaps, motion/wake-count changes,
local missed-exchange changes, sensor faults, anchor availability/restarts and
broker health. Mark battery swaps, handling, placement and firmware/settings
changes. Counter resets must not be mistaken for negative activity.

HA voltage measurement sensors support hourly long-term statistics. Wake counts,
last-seen and binary health history need explicit retention/export planning.
Check the installation's Recorder retention/exclusions before starting; the
[default detailed-history retention](https://www.home-assistant.io/integrations/recorder/)
is ten days. Do not increase whole-house retention blindly.

Suggested cadence: daily health/data capture, weekly summary and a final
four-week review. Missing observer/anchor access is monitoring unavailable, not
a dead tag. A quiet voltage trace alone cannot establish months of battery life.
The trial is a design recommendation, not an automation installed by this repo.

## Keep the work bounded

Reuse valid physical evidence. Do not repeatedly commission deployed tags or
run bulk OTA transfers merely to poll their batteries. Keep loss/recovery
experiments on a selected development unit; strong checks belong around identity,
authentication, unsafe writes, invalid images, interruptions and false success.

Do not introduce an ESPHome migration, cloud dependency, extra service or
multi-anchor redesign as part of this acceptance phase.
