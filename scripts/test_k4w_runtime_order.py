"""Guard the runtime ordering boundary not exercised by pure policy tests."""
from pathlib import Path
import unittest


class RuntimeOrder(unittest.TestCase):
    def test_poll_samples_time_under_session_lock(self):
        source = (Path(__file__).resolve().parents[1] /
                  'firmware/k4w-tag/src/ota_runtime.c').read_text()
        poll = source.split('void br_ota_poll(bool radio_healthy) {', 1)[1]
        # Otherwise connected() may install a later session timestamp between
        # the sample and mutex acquisition, underflowing unsigned elapsed time.
        acquire = poll.index('k_mutex_lock(&lock, K_FOREVER);')
        sample = poll.index('uint64_t now = k_uptime_get();')
        self.assertLess(acquire, sample)
        self.assertLess(sample, poll.index('br_ota_expire(&access, now);'))


if __name__ == '__main__':
    unittest.main()
