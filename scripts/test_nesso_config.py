"""Check the probe's hardware contract against defaults or a generated sdkconfig."""
import os
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(os.environ.get("NESSO_SDKCONFIG", ROOT / "firmware/nesso-probe/sdkconfig.defaults"))


class NessoConfiguration(unittest.TestCase):
    def assert_disabled(self, settings, key, generated=False):
        # Kconfig omits these when JTAG is off / the SoC has only two UARTs.
        conditional = {"ESP_DAP_1_JTAG_NTRST_SUPPORTED", "ESP_UART_BRIDGE_3_ENABLED"}
        default = "n" if generated and key in conditional else None
        self.assertEqual(settings.get("CONFIG_" + key, default), "n", key)

    def test_omitted_safety_setting_is_rejected(self):
        for key in ("ESP_DAP_1_NRESET_SUPPORTED", "ESP_DAP_1_JTAG_SUPPORTED",
                    "ESP_DAP_SOCKET_CONSOLE_ENABLED"):
            with self.subTest(key=key):
                with self.assertRaises(AssertionError):
                    self.assert_disabled({}, key)

    def test_safe_probe_configuration(self):
        self.assertTrue(CONFIG.is_file(), f"Missing Nesso configuration: {CONFIG}")
        settings = {}
        for line in CONFIG.read_text().splitlines():
            if line.startswith("CONFIG_") and "=" in line:
                key, value = line.split("=", 1)
                settings[key] = value
            elif line.startswith("# CONFIG_") and line.endswith(" is not set"):
                settings[line[2:-11]] = "n"
        expected = {
            "ESP_DAP_1_GPIO_SWCLK_TCK": "6",
            "ESP_DAP_1_GPIO_SWDIO_TMS": "7",
            "ESP_DAP_1_SWD_SUPPORTED": "y",
            "ESP_DAP_1_LED_NONE": "y",
            "ESP_CONSOLE_USB_SERIAL_JTAG": "y",
            "ESP_DAP_SERIAL_CONSOLE_ENABLED": "y",
            "ESP_WIFI_SSID": '\"\"',
            "ESP_WIFI_PASSWORD": '\"\"',
            "ESPTOOLPY_FLASHSIZE": '\"16MB\"',
        }
        for key, value in expected.items():
            with self.subTest(key=key):
                self.assertEqual(settings.get("CONFIG_" + key), value)
        disabled = [
            "ESP_DAP_1_JTAG_SUPPORTED", "ESP_DAP_1_JTAG_NTRST_SUPPORTED",
            "ESP_DAP_1_NRESET_SUPPORTED", "ESP_DAP_1_LED_STANDARD",
            "ESP_DAP_1_LED_RGB", "ESP_DAP_2_ENABLED", "ESP_DAP_3_ENABLED",
            "ESP_UART_BRIDGE_1_ENABLED", "ESP_UART_BRIDGE_2_ENABLED",
            "ESP_UART_BRIDGE_3_ENABLED", "ESP_ADC_STREAM_ENABLED",
            "ESP_DAP_SOCKET_CONSOLE_ENABLED",
        ]
        for key in disabled:
            with self.subTest(key=key):
                self.assert_disabled(settings, key, CONFIG.name != "sdkconfig.defaults")


if __name__ == "__main__":
    unittest.main()
