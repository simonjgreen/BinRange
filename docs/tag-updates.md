# Tag-update contract and integration constraints

Use this for updater implementation; [current work](current-work.md) alone owns
status and acceptance tasks. Existing physical checks are in [findings](findings.md).

## Ownership and success

The anchor runs ranging, registry/MQTT, local web admin and one serialized BLE
updater. Main loop owns jobs, their NVS snapshot and publication; worker owns
BLE and its identity/bond persistence; radio owns SPI on core 1. Bounded
commands/events carry nonreused session/operation IDs.
Discard stale events; keep callback contexts and staged bytes alive until actual
transport quiescence. Disconnect timeout or queue overflow fails visibly closed.

An update succeeds only when the commissioned hardware reports the expected
active image hash **and local confirmation**. Accepted bytes, bootable metadata,
version text or reset acknowledgment are insufficient. `0.2.2` versus `0.2.2+0`
is not a hash mismatch. The anchor never sends remote confirmation.

## Tag recovery and access

- MCUboot signed dual-slot offset swap retains the previous app. Only inactive
  application space is written by OTA; preserve identity/versioned settings and
  their compatibility with the previous image. Bootloader/UICR updates, mass
  erase/readout lock and rollback-resistant counters are outside this stage.
- MCUboot verifies the project signature. Keep the private key off anchor and
  Git, backed up securely; no demonstration key. Anchor metadata parsing is not
  cryptographic signature verification.
- Trial confirmation needs radio initialization, BLE management and continuing
  main-loop progress during bounded local observation. Anchor RF reception is
  not required. Early watchdog protection must recover pre-main hangs and allow
  interrupted swaps to resume; the SDK watchdog-feed macro alone starts no timer.
- Management needs an explicit bounded commissioning/maintenance window and
  authenticated authorization for every exposed command. Enable only required
  groups, not sample shell/filesystem/erase capabilities. Physical KEY wiring
  must be verified before relying on it. Keep an authorized laptop/SWD rescue path.
- Associate adopted UWB ID, selected public/static BLE address and immutable
  hardware identity after PIN-authenticated commissioning. A name/address alone
  is not identity. Lost bonds require operator action, not automatic re-pairing.
- Expose version, image hash, confirmation, reset reason and maintenance health.
  Preserve ranging outside bounded maintenance sessions.
- Queue for rendezvous rather than promising instant reachability. Current tag
  policy is 5 s moving / 10 min idle UWB reports, sleeping UWB between attempts.
  BLE remains discoverable with slow 2.0–2.1 s connectable advertising and a short
  fast-advertising local maintenance window. This default tradeoff still
  needs whole-board current measurements; no battery-life claim follows from it.
  Advertising restart waits for connection recycling; the callback only signals
  the main owner, which performs HCI work.

Exact partition layout, signing, installation preflight and recovery are owned by
the [signed build guide](../firmware/k4w-tag/BUILD.md).

## Anchor storage and state

- One immutable staged image in 212992-byte PSRAM capacity. No internal-heap
  fallback, formatting or partition change; preserve both 1280-KiB anchor OTA slots.
- Check NVS read/write/commit results. Missing and unreadable are different;
  unreadable credentials/jobs must not become defaults. Persist phase, exact
  target/release and attempt checkpoints before upload, trial and reset, not
  per packet. Persistence uncertainty disables unsafe operations.
- Keep the anchor's static-random BLE identity in checked, board-bound NVS
  (`binble/identity`), independent of jobs. Reapply it before scanning/connecting.
  Never replace an existing identity or bond on boot. Missing identity with saved
  bonds/associations requires explicit recovery; unreadable identity disables BLE.
  For legacy recovery only, build with `BINRANGE_BLE_IDENTITY_FILE` pointing to a
  private mode-0600 JSON containing `anchor_mac` (Wi-Fi MAC) and `ble_address`
  (the verified static-random address), both colon-separated display order.
  Generated bootstrap input is Git-ignored and board-specific, not a generic
  firmware default. After durable recovery, ordinary builds omit this input.
  `/api/tag-updates.local_ble_address` reports the initialized identity, or null.
- Use the explicit CRC/versioned snapshot codec, uniqueness and no-live-job-
  eviction rules. On restart preserve job IDs/targets/hashes; discard volatile
  offsets and monotonic timestamps. Lost staging means NeedsRelease, never
  silently another image/tag. Free-entry count is only a capacity estimate.
- Every session checks identity and slots. Initial upload sends off=0, length
  and whole-file SHA; receiver offsets govern continuation. Inspect a matching
  unconfirmed active image instead of overwriting it. Pending trial on an old
  image needs explicit action; uncertainty after trial/reset means inspect,
  not replay or claim cancellation. Keep maintenance state bounded and visible.
- Use long-lived snapshot scratch, not large ESP32 stack locals. Existing
  measured storage begin stack was 288 bytes; decoder 3984 bytes.

## Pinned BLE integration

Target is espressif32 6.5.0 / Arduino-ESP32 2.0.14 / NimBLE-Arduino 2.5.1.
Read the actual pinned headers rather than those of another installed SDK.
Keep ARDUINO_RUNNING_CORE=0 and CONFIG_BT_NIMBLE_PINNED_TO_CORE=0 explicit in
build flags. Measure final linked fit and live coexistence before deployment.
Use Wi-Fi `WIFI_PS_MIN_MODEM`, not the old `setSleep(false)` setting: the pinned
ESP-IDF coexistence layer aborts when BLE starts with `WIFI_PS_NONE`.
The BLE worker checks this precondition before initialization.

- Use raw asynchronous GAP/GATT with bounded callbacks and application deadlines;
  C++ discovery/read/write helpers can block. Avoid implicit retries/security.
- Preserve full-source-hash checks in scripts/anchor_ble_build.py. Both the C++
  client and native ble_sm.c key-missing (518) path can silently retry pairing;
  both must fail closed. Native security_initiate can also pair when no key is found: ordinary
  updates must use a validated existing bond, not enter commissioning implicitly.
- Ignore unsolicited peripheral Security Requests; the worker alone starts
  authorized security. Check bond restoration at startup, latch store failures,
  and reject replacement of existing key material. RAM-only keys are not proof
  of persistence; a store fault disables maintenance until restart.
- Reject store-full rather than evict bonds. Reject default numeric/passkey
  acceptance. Use the supplied PIN only for explicit commissioning; clear
  temporary PIN/key material and never log it.
- Verify encryption, MITM/authentication, bonding, 16-byte key and matching stored
  ble_store_value_sec.sc. NimBLEConnInfo alone cannot establish Secure Connections.
- Associations store addresses MSB-first; native ble_addr_t is LSB-first. Check
  exact peer identity/OTA address, connection, value handle and operation.
- Scan 15 s; connect 15 s; outer connect operation deadline 35 s (scan, connect,
  queue/callback margin); request 10 s; idle 45 s; session at most 540 s.
  At most three attempts with at least 60 s cooldown after exhaustion/expiry;
  confirmation observation at most 150 s. Fragment writes at negotiated MTU-3.
- Reuse bounded SMP/image codecs and actual-tag CBOR fixtures, including
  indefinite maps/arrays. Do not add an alternate parser or confirm command.

## Web administration and feedback

Private bootstrap password: local-backups/anchor-admin-password.txt (0600).
BINRANGE_ADMIN_FILE passes a path, never secret text; relative paths resolve
from firmware/anchor. Omitted input clears stale generated bootstrap; explicit
invalid input fails the build. Valid stored credentials take precedence.
Generated headers/provisioned binaries remain private.

Digest must match actual URI and qop=auth; mutations also require per-boot CSRF.
Protect every multipart callback/finalization, including parser error exits.
Pinned WebServer handles the request synchronously inside handleClient(); keep
cleanup around that scope and finalize once. Recheck if upgrading the SDK.
The hash-pinned project-local WebServer parser replacement bounds headers,
text fields, disconnect handling and the total multipart deadline before
callbacks run; do not modify the shared installed framework.
Persist password changes before activation; rotation requires an explicit restart.
ArduinoOTA uses the same admin secret with mDNS disabled to preserve HTTP discovery.
This is trusted-LAN HTTP, not TLS or Internet-safe access.

Bound inputs, render strings as text, clear secret fields, never serialize secrets.
Use 400 for invalid input, 409 for conflicts, 507 for capacity. No automatic POST
retries. Poll once per second with one request in flight; retain last-known values
and forms but label stale/disconnected state honestly. Page/API schema lives in
firmware/anchor/src/update_page.h, not a second document.

Publish retained per-tag phase, bytes, error, expected/observed version/hash,
timestamps and restart/maintenance state under the [MQTT contract](mqtt.md).
Updater MQTT is status-only; broker credentials cannot bypass admin authorization.
