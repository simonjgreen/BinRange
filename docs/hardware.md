# Hardware reference

BinRange currently targets a Makerfabs ESP32-WROVER/DW3000 anchor and KKM K4W
nRF52833/DW3110 tags with a LIS3DH accelerometer. Verify the actual board revision
and populated parts before adapting this pinout. Vendor-confidential schematics,
purchase records and physical tag identifiers are not distributed.

## Tag components and pins

The nRF52833 has 512 KiB flash / 128 KiB RAM. The tested DW3110 reported radio
ID DECA0302; LIS3DH responded at I2C 0x19 with WHO_AM_I 0x33.
SWD uses CLK, DIO, GND and VDD.

| Signal | nRF pin |
| --- | --- |
| DW3110 SCLK / MOSI / MISO | P0.31 / P0.30 / P0.28 |
| DW3110 CS / IRQ / reset | P0.02 / P0.03 / P0.29 |
| Accelerometer SCL / SDA | P0.09 / P0.20 |
| Accelerometer INT1 / INT2 | P1.09 / P0.11 |
| Accelerometer CS / SDO | P0.15 / P0.17 |
| KEY, active low | P0.04 |
| LED1 / LED2 | P0.05 / P0.10 |

P0.09/P0.10 are NFC-capable. Accelerometer operation on the tested revision
required explicit one-time NFCPINS configuration at 0x1000120c to 0xfffffffe.
This is a persistent hardware configuration change, not an ordinary OTA setting.
Verify the intended bit change and full UICR readback. Normal firmware must not
silently modify UICR; restoring a cleared bit needs erase/recovery.
See Nordic [pin configuration](https://docs.nordicsemi.com/r/bundle/ps_nrf52833/page/nfc.html)
and [NVMC](https://docs.nordicsemi.com/r/bundle/ps_nrf52833/page/nvmc.html).

Physical KEY behavior must be checked before relying on it for maintenance.
KEY/LED1 multiplex connector UART labels; do not assume an independent spare UART.

## Identity and safe programming

Keep a private register of casing label/QR, immutable FICR chip ID, chosen UWB ID,
firmware-selected Bluetooth address and bin name. The printed vendor MAC need
not be the on-air address; a generic BLE advertisement name is not identity.
A QR code or serial is not automatically a pairing PIN.

Verify the physical target before any write. Decide deliberately whether to back
up factory firmware before first installation; protected flash may require
destructive recovery. Never replay first-install erase on a commissioned tag.

**Never connect the battery and programmer VDD simultaneously.** Check polarity
and supply voltage before attachment. The [Nesso probe](../firmware/nesso-probe/README.md)
is battery-backed; unplugging its USB does not necessarily remove tag power.

## Battery and mounting

The tested tag accepts a removable CR2477 cell. Verify its actual battery
specification before use. BAT feeds the MCU/accelerometer supply with bulk
capacitance; UWB has 1.8 V regulation.

Internal VDD sampling needs no spare ADC pin. SAADC uses internal reference and
gain 1/6; the pinned Zephyr input is `NRF_SAADC_VDD` (128), not raw HAL
`NRF_SAADC_INPUT_VDD` (9). The app samples after waking UWB, before ranging.
It does not measure rested open-circuit voltage or minimum pulse sag.

Validate meter agreement and whole-tag idle/active current before estimating
life. A regulated jig voltage is not a battery baseline. Coin-cell percentages
derived from voltage are provisional and affected by load and temperature.

Mount securely, consider antenna orientation and metal/body shadowing, preserve
battery access and verify enclosure sealing. Shelter under a bin lip is not
an ingress-protection rating. Final anchor power should be stable and permanent.

## Accelerometer and low-power constraints

The pinned SDK supports LIS3DH via LIS2DH with dual compatible
`"st,lis3dh", "st,lis2dh"`. Only the former does not instantiate the driver.
See the pinned [RAK5010 example](https://github.com/nrfconnect/sdk-zephyr/blob/fd9204a02d52630660ce8d729945a4dd743feabf/boards/rakwireless/rak5010/rak5010_nrf52840.dts#L96).
Delta-trigger duration is 0–127 samples at the selected rate, not milliseconds;
threshold units are m/s².

Set `SENSOR_ATTR_CONFIGURATION=0x01` (CTRL2 HPIS1) and read REFERENCE to reset
the high-pass filter. The Kconfig option alone does not enable filtering;
otherwise gravity can retrigger motion. Health checks verify CTRL2.
Current defaults are 250 mg for two samples at 25 Hz. Tune using wheeling,
stationary periods, wind/lid knocks and different orientations—not a universal
"bin moved" assumption. See ST's [datasheet](https://www.st.com/resource/en/datasheet/lis3dh.pdf)
and [motion examples](https://www.st.com/resource/en/application_note/an3308-lis3dh-mems-digital-output-motion-sensor-ultralowpower-highperformance-3axis-nano-accelerometer-stmicroelectronics.pdf).

The recovery bootloader's 20 s watchdog runs during CPU sleep. Event/deadline
wakes service it at most 10 s apart; ten-minute reporting does not mean ten
minutes without CPU wakeups. UWB sleeps between attempts; BLE remains slowly
discoverable. Do not disable recovery to reduce power.

Image capacity is 212992 bytes including signing overhead. LTO/local ISR tables
are required by the integrated candidate. Verify each exact signed artifact;
never silently repartition an installed tag. See the [build guide](../firmware/k4w-tag/BUILD.md).
