# Frozen test-phase firmware

Preserved v1.0 SS-TWR source for two Makerfabs ESP32 UWB DW3000 WROVER boards.
This is not the current custom MQTT/DS-TWR deployment firmware. Do not replace
an installed anchor/tag merely to reproduce a screenshot.

Provisioned binaries and raw calibration/capture files are local-only and
excluded from Git; build with your own ignored `src/secrets.h` copied from
`src/secrets.h.example`. Never distribute a binary with compiled-in credentials.

The test firmware supports initiator/responder roles, hostname, antenna delay,
interval and PHY stored in NVS. Its web UI and `/api/stats` expose live
diagnostics; this is not the deployment anchor API contract.

![Historical test-rig interface](../../docs/images/test-rig-webui.png)

Toolchain pins are in `platformio.ini` and `VERSION`. Environment `uwb` builds
normal test firmware; `ota` supports wireless maintenance with explicitly
supplied private authentication; `nowifi` is a serial-only control build.
Uploading replaces the selected board's application.

Long (850 kbps / 1024-symbol preamble) and upright mounting worked well in the
tested pair. Antenna delay is setup-specific; neither board-pair calibration nor
range proves packaged-tag performance. See [findings](../../docs/findings.md).

Radio owns SPI on core 1; HTTP reads snapshots and OTA pauses the radio for flash
writes. The frozen protocol is unauthenticated SS-TWR, not deployment DS-TWR.
