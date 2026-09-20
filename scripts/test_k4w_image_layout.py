"""Reject unsafe programming ranges and images that cannot support offset swap."""
import importlib.util
import pathlib
import unittest


class ImageLayout(unittest.TestCase):
    def setUp(self):
        path = pathlib.Path(__file__).with_name('k4w_image_layout.py')
        self.assertTrue(path.exists(), 'image layout validator not implemented')
        spec = importlib.util.spec_from_file_location('layout', path)
        self.mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.mod)
        self.layout = {'boot': (0, 65536), 'primary': (65536, 217088),
                       'secondary': (282624, 221184), 'settings': (503808, 20480)}

    def test_valid_offset_swap_layout_and_signed_size(self):
        self.mod.validate_layout(self.layout, 524288, 4096)
        self.mod.validate_image_size(212992, self.layout['primary'][1], 4096)

    def test_rejects_overlap_overflow_and_unaligned_regions(self):
        for name, value in [('primary', (61440, 217088)),
                            ('settings', (503808, 24576)),
                            ('boot', (0, 65535))]:
            candidate = dict(self.layout, **{name: value})
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.mod.validate_layout(candidate, 524288, 4096)

    def test_rejects_missing_offset_sector(self):
        self.layout['secondary'] = (282624, 217088)
        with self.assertRaises(ValueError):
            self.mod.validate_layout(self.layout, 524288, 4096)

    def test_rejects_image_overflow_including_trailer(self):
        for size in [0, -1, 212993, 217088]:
            with self.subTest(size=size), self.assertRaises(ValueError):
                self.mod.validate_image_size(size, 217088, 4096)

    def test_missing_effective_setting_is_rejected(self):
        required = {'CONFIG_MCUMGR': 'y', 'CONFIG_BOOTLOADER_MCUBOOT': 'y'}
        self.assertTrue(hasattr(self.mod, 'validate_config'), 'effective-config check missing')
        self.mod.validate_config('CONFIG_MCUMGR=y\nCONFIG_BOOTLOADER_MCUBOOT=y\n', required)
        for text in ['# CONFIG_MCUMGR is not set\nCONFIG_BOOTLOADER_MCUBOOT=y\n',
                     'CONFIG_MCUMGR=y\n', 'CONFIG_MCUMGR=n\nCONFIG_BOOTLOADER_MCUBOOT=y\n']:
            with self.subTest(text=text), self.assertRaises(ValueError):
                self.mod.validate_config(text, required)

    def test_programming_ranges_cannot_cross_into_settings_or_uicr(self):
        self.mod.validate_segments([(0, 40000), (65536, 278000)],
                                   [(0, 65536), (65536, 282624)])
        for ranges in [[(65536, 282625)], [(0x10001000, 0x10001004)],
                       [(503808, 503812)], [(20, 20)], []]:
            with self.subTest(ranges=ranges), self.assertRaises(ValueError):
                self.mod.validate_segments(ranges, [(0, 65536), (65536, 282624)])


if __name__ == '__main__':
    unittest.main()
