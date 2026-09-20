# Findings and limitations

Reusable engineering observations from prototype testing. Site addresses,
physical device identifiers, individual bin reports and private release assets
are intentionally excluded. These results are not guarantees for another site.

## UWB link

Tests used two Makerfabs WROVER/DW3000 boards unless noted otherwise.

| Observation | Result / limit |
| --- | --- |
| Antenna orientation matters | At about 10 m, flat→upright improved observed success 37%→100%, with ~13 dB first-path improvement |
| Long PHY helped the weak link | 6.8 Mbps/128-symbol preamble: 1/346 exchanges; 850 kbps/1024: 337/337 in the same indoor setup |
| Calibration is setup-specific | Upright Long at a measured 10.1 m: mean 10.0912 m, standard deviation 0.030 m |
| Outdoor walking test | Reliable to roughly 30 m, intermittent 33–37 m; furthest recorded 37.28 m, with gaps |
| Obstructions | A car could fully block reception; body shadow weakened the signal |
| Very short distances | At roughly 10 cm, readings became multimodal; use distances around 1 m or more for calibration |

![Outdoor walk-test distance and signal traces](images/uwb-walk-test.png)

This graph uses a development/emulator identity, not a packaged tag mounted on
a bin. Absolute RSSI/first-path values lack DW3000 DGC correction and should be
treated as relative indicators. Neither their difference nor range spread is
a validated obstruction classifier. A held flat range is not proof of stillness.

The anchor cannot count POLLs it never receives and cannot derive end-to-end
success rate from received FINALs. Local tag misses are useful but different.

## Motion and power

The 0.2.5 motion baseline demonstrated:

- Actual accelerometer-triggered movement, five-second reports and wake-episode
  increment, followed by a received stationary/settled report.
- A ten-minute idle interval verified independently in MQTT and HA.
- Healthy locally confirmed firmware, watchdog progress and UWB asleep on
  non-halting inspection.
- Motion/settling on a packaged tag as well as the development unit.

This does not establish whole-board current, meter accuracy, all-site nuisance
wake behavior, deliberate-loss recovery or battery lifetime. The radio-sleep
flag is not a current measurement. VDD is sampled after UWB wake, not as a
rested voltage or captured minimum pulse sag. A generic coin-cell percentage
curve is provisional, not a calibrated fuel gauge.

Use unique MQTT observer client IDs and detect observer disconnects; a recorder
losing its own connection can otherwise be mistaken for tag silence.

An isolated 0.2.6 development-tag test changed intervals through Home Assistant,
MQTT and the bonded anchor: observed report spacing was about 60.03 s stationary
and 2.02 s moving. The saved settings survived a tag restart. Original tuning was
restored and read back; temporary HA entities and the test dashboard were removed
with existing dashboard configurations unchanged.

## Integration and recovery

MQTT checks established runtime adoption, per-tag attribution, unknown sightings
without automatic devices, retained settings, last-will availability and
reconnection. During a simulated 45 s broker outage, ranging continued and
state returned after reconnect. The household broker itself was not stopped.
An identity-switching emulator does not prove six-radio fairness.

Signed tag update/recovery checks included:

| Check | Established result |
| --- | --- |
| Upload interruption | Old app remained usable and receiver offsets governed resumption |
| Wrong-key/corrupt image | Known-good confirmed application remained available |
| Pre-main hang / failed radio initialization | Watchdog rollback without a debugger reset |
| Power loss during upload | Recovery to known-good firmware; upload restarted at the receiver's actual offset |
| Power loss during swap | Interrupted installation resumed; not a cut during a flash pulse |
| Anchor absent | Healthy tag could confirm locally without an RF response |
| Remote confirmation request | Rejected; only local health confirmation accepted |
| Anchor restart before trial | Same target/release returned NeedsRelease and resumed after matching restaging |
| Anchor-driven updates | Exact active image hash and local confirmation matched MQTT/HA without re-pairing |

An isolated WROVER/development-tag journey also exercised the anchor's legacy
and authenticated HTTP OTA routes. Its BLE identity and tag association survived
the authenticated update. After one-time test commissioning, the debug connection
was closed and the local window/cooldown expired before an anchor-driven signed
0.2.5-to-0.2.6 update. The anchor eventually verified the exact active hash and
local confirmation without further jig assistance.

That run's first upload request received no reply within the normal 10 s budget.
Bounded recovery completed the transfer on its third connection; a remote operator
retry was then needed for post-reboot verification because the connection budget
was exhausted. A separate 30 s request-budget experiment completed automatically
with two connection attempts, including verification after reboot. Its first
acknowledgement took less than 6 s, so it did not establish the
original timeout's cause or justify changing production deadlines. Test builds
used isolated radio/MQTT identifiers and a third bond slot to preserve the two
original test-tag associations. This proves the wireless upgrade path, not
repeatable unattended reliability. Retain a tested SWD/laptop recovery path.

A subsequent six-tag installed rollout upgraded the production anchor to 0.2.4
and all six tags to signed 0.2.6 entirely OTA using their existing bonds. Every
tag’s exact active image and local confirmation were verified, healthy range
reports resumed, and HA read back the existing 600 s stationary / 5 s moving
settings. Firmware jobs completed within their bounded connection budgets.
Several separate settings reads required remote retries, including one after a
longer quiet interval. The anchor identity and associations survived its single
restart; no production jig access or re-pairing was needed. This is one successful
installed rollout, not proof of unattended connection reliability.

## Resolved multi-peer pairing defect

Pinned NimBLE-Arduino 2.5.1 allocated client-supported-feature records by
simultaneous connections rather than allowed bonds. With one connection and
multiple peers, first pairing could fail after keys were already stored.

Anchor 0.2.3 sizes `MYNEWT_VAL_BLE_STORE_MAX_CSFCS` to
`CONFIG_BT_NIMBLE_MAX_BONDS`. A real-backend regression reproduced the old
capacity failure; a genuinely new physical peer paired on the corrected anchor
without a restart or bond eviction. This does not explain every connection miss.

## Remaining maintenance limits

- Slow connectable advertising can be seen but connections are not yet accepted
  as dependable. Keep bounded attempts/cooldown and existing bonds.
- Cold offset-zero requests intermittently timed out even when the tag accepted
  its first block. Pinned MCUmgr synchronously erases the incoming slot with
  progressive erase disabled. This is a latency stage, not a proven root cause;
  correlate sender/receiver timings before changing deadlines.
- Pinned WebServer holds one global Digest challenge; competing admin clients
  can invalidate each other's login. Cache the helper challenge and avoid
  competing polling. Never replay mutation POSTs after uncertain responses.
- Maintained-tag ranging pauses during bulk transfer. Background UWB/MQTT
  continued during bench tests and one sequential six-tag installed rollout;
  sustained fleet coexistence remains a field gate.

The public [roadmap](current-work.md) lists acceptance; the
[update contract](tag-updates.md) owns safety invariants.
