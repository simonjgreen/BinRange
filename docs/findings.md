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

Current tag candidate 0.2.5 demonstrated:

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

One successful current-image update required a short trusted-jig maintenance
window before delivery. It is not proof of reliable unattended slow-advertising
OTA. Retain a tested SWD/laptop recovery path.

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
  continued in bench tests; full fleet coexistence remains a field gate.

The public [roadmap](current-work.md) lists acceptance; the
[update contract](tag-updates.md) owns safety invariants.
