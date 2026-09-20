# Third-party components and publication notes

No blanket licence for the project's original code has been selected yet.
Do not infer redistribution permission merely from a public repository.

- **Waste Collection Schedule:** collection dates are supplied by
  [mampfes/hacs_waste_collection_schedule](https://github.com/mampfes/hacs_waste_collection_schedule),
  installed separately through HACS. BinRange does not bundle or replace it.
- **DW3000 driver:** adapted Makerfabs/Decawave code lives under
  `firmware/shared/Dw3000` and the frozen test rig. Library metadata declares
  Apache-2.0; individual source files retain Decawave copyright/rights notices.
  Preserve those notices and review upstream terms before redistribution.
- **Anchor stack:** PlatformIO/espressif32, Arduino-ESP32, PubSubClient and
  NimBLE-Arduino are separately obtained dependencies pinned/configured in
  `platformio.ini`.
- **Tag stack:** Nordic nRF Connect SDK, Zephyr, MCUboot and their dependencies
  retain their own licences. The SDK is obtained separately, not vendored here.
- **Optional Nesso probe:** uses
  [bkuschak/cmsis_dap_tcp_esp32](https://github.com/bkuschak/cmsis_dap_tcp_esp32)
  at the pinned revision in its guide; upstream is not committed.
- **Screenshots:** selected project-generated development captures are under
  `docs/images/`; they illustrate testing, not a product guarantee.

Private vendor schematics, purchase paperwork, provisioned binaries, raw local
captures, credentials and signing keys are excluded. Public test fixtures use
synthetic identities rather than real devices.
