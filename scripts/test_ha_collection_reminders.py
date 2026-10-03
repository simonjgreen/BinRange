"""Exercise the public reminder templates without a Home Assistant connection."""
import json
from pathlib import Path
import unittest

import yaml
from jinja2 import StrictUndefined
from jinja2.nativetypes import NativeEnvironment


class CollectionReminderTests(unittest.TestCase):
    def test_return_reminder_accepts_presumed_out_but_put_out_still_needs_fresh_home(self):
        # Execute the shipped reminder's selection, not a copy of its policy.
        path = Path(__file__).resolve().parents[1] / 'examples/home-assistant/collection-reminders.yaml'
        config = yaml.safe_load(path.read_text())
        steps = config['actions'][9]['repeat']['sequence']
        env = NativeEnvironment(undefined=StrictUndefined)
        env.filters['to_json'] = json.dumps
        values = {'sensor.garden_location': 'Out', 'binary_sensor.garden_range_freshness_a': 'on',
                  'sensor.glass_location': 'Home', 'binary_sensor.glass_range_freshness_a': 'on'}
        env.globals.update(states=lambda k: values.get(k, 'unknown'), is_state=lambda k,v: values.get(k)==v)
        bins = [{'slug':'garden','name':'Garden','category':'garden'}, {'slug':'glass','name':'Glass','category':'glass'}]
        locations = env.from_string(steps[2]['variables']['locations']).render(bins=bins)
        for expected_location, expected in [('Out', ['Garden']), ('Home', [])]:
            self.assertEqual(env.from_string(steps[3]['variables']['eligible']).render(
                bins=bins, categories=['garden','glass'], locations=locations, expected_location=expected_location), expected)


if __name__ == '__main__':
    unittest.main()
