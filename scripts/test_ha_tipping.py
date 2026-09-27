"""Notification-free execution of the shipped YAML's Jinja/action control flow.

No HA connection, production helper writes, or notify calls. This deliberately
loads the actual package rather than a second implementation of its policy.
The small interpreter supports exactly the actions used by this package;
unknown actions fail. Native HA schema/execution validation remains a rollout check.
"""
import ast
from copy import deepcopy
from datetime import datetime, timedelta
import json
from pathlib import Path
import unittest
from zoneinfo import ZoneInfo

from jinja2 import Environment, StrictUndefined
import yaml

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = yaml.safe_load((ROOT / 'examples/home-assistant/tipping-notifications.yaml').read_text())
CONFIG = PACKAGE['automation'][0]
LONDON = ZoneInfo('Europe/London')


def as_datetime(value):
    if isinstance(value, datetime):
        return value
    if isinstance(value, (int, float)):
        return datetime.fromtimestamp(value, LONDON)
    return datetime.fromisoformat(value)


def as_timestamp(value, default=None):
    try:
        return as_datetime(value).timestamp()
    except (TypeError, ValueError):
        return default


def from_json(value, default=None):
    try:
        return json.loads(value)
    except (TypeError, ValueError):
        return default


class StopRun(Exception):
    pass


class Harness:
    def __init__(self, states=None):
        self.time = datetime(2026, 9, 22, 10, 0, tzinfo=LONDON)
        self.states = deepcopy(states) if states is not None else {
            'input_text.binrange_tip_ledger': '{}', 'input_text.binrange_tip_batch': '{}'}
        for i in range(1, 7):
            slug = f'replace_me_bin_{i}'
            self.states.setdefault(f'sensor.{slug}_location', 'Out')
            self.states.setdefault(f'binary_sensor.{slug}_range_freshness_a', 'off')
            self.states.setdefault(f'binary_sensor.{slug}_motion_sensor_fault_a', 'off')
        self.events = [{'start': '2026-09-22', 'summary': 'Waste, Recycling, Food, Garden, Glass'}]
        self.messages = []
        self.calls = []
        self.fail_notify = False
        self.env = Environment(undefined=StrictUndefined)
        self.env.filters.update(to_json=json.dumps, from_json=from_json,
                                as_local=lambda v: v.astimezone(LONDON))
        self.env.globals.update(now=lambda: self.time, as_datetime=as_datetime,
                                as_timestamp=as_timestamp, strptime=datetime.strptime,
                                timedelta=timedelta,
                                states=lambda k: self.states.get(k, 'unknown'),
                                is_state=lambda k, v: self.states.get(k) == v)

    def render(self, node, context):
        if isinstance(node, str) and ('{{' in node or '{%' in node):
            text = self.env.from_string(node).render(**context).strip()
            try:
                return ast.literal_eval(text)
            except (ValueError, SyntaxError):
                return text
        if isinstance(node, list):
            return [self.render(v, context) for v in node]
        if isinstance(node, dict):
            return {k: self.render(v, context) for k, v in node.items()}
        return node

    def conditions(self, entries, context):
        return all(self.render(entry['value_template'], context) is True for entry in entries)

    def sequence(self, actions, context):
        for action in actions:
            if 'variables' in action:
                for k, v in action['variables'].items():
                    context[k] = self.render(v, context)
            elif 'condition' in action:
                if not self.conditions([action], context):
                    raise StopRun
            elif 'if' in action:
                branch = 'then' if self.conditions(action['if'], context) else 'else'
                self.sequence(action.get(branch, []), context)
            elif 'repeat' in action:
                for item in self.render(action['repeat']['for_each'], context):
                    context['repeat'] = {'item': item}
                    self.sequence(action['repeat']['sequence'], context)
            elif action.get('action') == 'input_text.set_value':
                data = self.render(action['data'], context)
                value = data['value']
                if not isinstance(value, str):
                    value = json.dumps(value, separators=(',', ':'))
                assert len(value) <= 255, value
                entity = action['target']['entity_id']
                self.states[entity] = value
                self.calls.append(('write', entity, value))
            elif action.get('action') == 'calendar.get_events':
                context[action['response_variable']] = {
                    context['calendar_entity']: {'events': self.events}}
            elif action.get('action') == 'notify.send_message':
                self.calls.append(('notify',))
                if self.fail_notify:
                    raise RuntimeError('Simulated notify failure')
                self.messages.append(self.render(action['data'], context))
            else:
                raise AssertionError(f'Unsupported action {action}')

    def run(self, trigger=None):
        try:
            self.sequence(CONFIG['actions'], {} if trigger is None else {'trigger': trigger})
        except StopRun:
            pass

    def report(self, bin_id=1, **overrides):
        payload = dict(tip_count=1, tip_age_s=0, tip_ready=True, stale=False,
                       sensor_fault=False, moving=True, ts=self.time.isoformat())
        payload.update(overrides)
        self.run({'id': 'report', 'platform': 'mqtt',
                  'topic': f'binrange/tag/replace_me_tag_{bin_id}/anchor/replace_me_anchor/state',
                  'payload': json.dumps(payload)})

    def tick(self, seconds=0, startup=False):
        self.time += timedelta(seconds=seconds)
        self.run({'id': 'tick', 'platform': 'homeassistant' if startup else 'time_pattern'})

    def helper(self, name):
        return json.loads(self.states[f'input_text.binrange_tip_{name}'])


class TippingTest(unittest.TestCase):
    def test_first_connection_recent_event_and_singular_wording(self):
        h = Harness()
        h.report(tip_count=58, tip_age_s=500)
        h.tick(59)
        self.assertEqual(h.messages, [])
        h.tick(1)
        self.assertEqual(h.messages, [{'title': 'The bin has just been emptied',
            'message': 'Bin 1 has just been emptied. You can now bring it in.'}])
        self.assertEqual(h.calls[-3][1], 'input_text.binrange_tip_ledger')
        self.assertEqual(h.calls[-2][1], 'input_text.binrange_tip_batch')
        self.assertEqual(h.calls[-1], ('notify',))

    def test_fixed_window_three_bins_and_boundary_new_batch(self):
        h = Harness()
        h.report(1)
        deadline = h.helper('batch')['1']
        h.tick(30)
        h.report(2)
        h.tick(29)
        h.report(6)
        self.assertEqual(h.helper('batch')['1'], deadline)
        h.tick(1)
        h.report(4)
        self.assertEqual(h.messages[0]['message'],
                         'Bin 1, Bin 2 and Bin 6 have just been emptied. You can now bring them in.')
        h.tick(60)
        self.assertEqual(len(h.messages), 2)

    def test_duplicate_reports_multiple_counts_restart_and_reconnect(self):
        h = Harness()
        h.report()
        h.report()
        restarted = Harness(h.states)
        restarted.time = h.time + timedelta(seconds=40)
        restarted.tick(startup=True)
        restarted.report()
        restarted.tick(20)
        restarted.report(tip_count=2)
        restarted.tick(60)
        self.assertEqual(len(restarted.messages), 1)
        restored = Harness(restarted.states)
        restored.time = restarted.time
        restored.report(tip_count=2)
        restored.tick(60)
        self.assertEqual(restored.messages, [])

    def test_out_recovery_after_first_report_location_and_freshness_lag(self):
        h = Harness()
        h.states['sensor.replace_me_bin_1_location'] = 'Home'
        h.report()
        self.assertEqual(h.helper('batch'), {})
        h.states['sensor.replace_me_bin_1_location'] = 'Out'
        h.states['binary_sensor.replace_me_bin_1_range_freshness_a'] = 'on'
        h.tick(10)
        self.assertEqual(h.helper('batch'), {})
        h.states['binary_sensor.replace_me_bin_1_range_freshness_a'] = 'off'
        h.tick(10)
        h.tick(60)
        self.assertEqual(len(h.messages), 1)

    def test_restart_after_deadline_defers_unresolved_bin_until_ready(self):
        h = Harness()
        h.report()
        h.states['sensor.replace_me_bin_1_location'] = 'unknown'
        h.tick(70, startup=True)
        self.assertEqual(h.messages, [])
        self.assertEqual(h.helper('ledger')['1'][2], 0)
        h.states['sensor.replace_me_bin_1_location'] = 'Out'
        h.tick(5)
        self.assertEqual(len(h.messages), 1)
        h.tick(60)
        self.assertEqual(len(h.messages), 1)

    def test_mixed_batch_retains_unresolved_and_new_bin_gets_own_window(self):
        h = Harness()
        h.report(1)
        h.report(2)
        h.states['binary_sensor.replace_me_bin_2_range_freshness_a'] = 'unavailable'
        h.tick(70, startup=True)
        self.assertEqual(len(h.messages), 1)
        self.assertEqual(h.messages[0]['message'], 'Bin 1 has just been emptied. You can now bring it in.')
        self.assertEqual(h.helper('ledger')['2'][2], 0)
        h.report(3)
        h.tick(5)
        self.assertEqual(len(h.messages), 1)
        h.states['binary_sensor.replace_me_bin_2_range_freshness_a'] = 'off'
        h.tick(5)
        self.assertEqual(len(h.messages), 2)
        h.tick(49)
        self.assertEqual(len(h.messages), 2)
        h.tick(1)
        self.assertEqual(len(h.messages), 3)
        h.report(1, tip_count=2)
        h.tick(60)
        self.assertEqual(len(h.messages), 3)

    def test_bad_reports_do_not_create_candidates(self):
        bad = [dict(tip_count=0), dict(tip_count=-1), dict(tip_count=True),
               dict(tip_count=4294967296), dict(tip_age_s=None), dict(tip_age_s=1801),
               dict(tip_age_s=-1), dict(tip_ready=False), dict(tip_ready=None),
               dict(stale=True), dict(sensor_fault=True), dict(sensor_fault=None),
               dict(moving=None), dict(ts='bad'), dict(ts='2026-09-22T10:00:01+01:00'),
               dict(ts='2026-09-22T09:00:00+01:00')]
        for payload in bad:
            with self.subTest(payload=payload):
                h = Harness()
                h.report(**payload)
                h.tick(60)
                self.assertEqual(h.helper('ledger'), {})
                self.assertEqual(h.messages, [])

    def test_fault_unknown_out_and_expired_pending_suppress(self):
        for key, value in [('sensor.replace_me_bin_1_location', 'Home'),
                           ('sensor.replace_me_bin_1_location', 'unknown'),
                           ('binary_sensor.replace_me_bin_1_range_freshness_a', 'on'),
                           ('binary_sensor.replace_me_bin_1_motion_sensor_fault_a', 'on')]:
            with self.subTest(key=key, value=value):
                h = Harness()
                h.report()
                h.states[key] = value
                h.tick(60)
                self.assertEqual(h.messages, [])
        h = Harness()
        h.report()
        h.tick(1801, startup=True)
        self.assertEqual(h.messages, [])

    def test_due_category_and_event_local_date_not_arrival_date(self):
        h = Harness()
        h.events = [{'start': '2026-09-23', 'summary': 'Waste'},
                    {'start': '2026-09-22', 'summary': ' Recycling, Food '}]
        h.report(1)
        h.report(2)
        h.report(3)
        h.tick(60)
        self.assertEqual(h.messages[0]['message'],
                         'Bin 2 and Bin 3 have just been emptied. You can now bring them in.')
        h = Harness()
        h.time = datetime(2026, 9, 23, 0, 1, tzinfo=LONDON)
        h.report(tip_age_s=120)
        h.tick(60)
        self.assertEqual(len(h.messages), 1)
        self.assertEqual(h.helper('ledger')['1'][2], 20260922)

    def test_saved_reminder_history_recovers_previous_day_after_midnight(self):
        h = Harness()
        h.time = datetime(2026, 9, 23, 0, 1, tzinfo=LONDON)
        h.events = []
        h.states['input_text.replace_me_collection_dates'] = '{"2026-09-22":["waste"]}'
        h.report(tip_age_s=120)
        h.tick(60)
        self.assertEqual(len(h.messages), 1)
        # Today's changed schedule takes precedence over an old snapshot.
        h = Harness()
        h.events = []
        h.states['input_text.replace_me_collection_dates'] = '{"2026-09-22":["waste"]}'
        h.report()
        h.tick(60)
        self.assertEqual(h.messages, [])

    def test_next_collection_cycle_can_notify_again(self):
        h = Harness()
        h.report()
        h.tick(60)
        h.time += timedelta(days=7)
        h.events = [{'start': '2026-09-29', 'summary': 'Waste'}]
        h.report(tip_count=2)
        h.tick(60)
        self.assertEqual(len(h.messages), 2)

    def test_manual_run_invalid_helpers_and_payload_are_inert(self):
        h = Harness()
        h.run()
        h.run({'id': 'report', 'platform': 'user'})
        self.assertEqual(h.calls, [])
        for bad in ['unknown', 'unavailable', 'bad', '[]', '{"1":[]}', '{"1":[1,"bad",0]}']:
            h = Harness()
            h.states['input_text.binrange_tip_ledger'] = bad
            h.report()
            self.assertEqual(h.calls, [])
        for bad in ['{"missing":1}', '{"1":null}', '[]']:
            h = Harness()
            h.states['input_text.binrange_tip_batch'] = bad
            h.report()
            self.assertEqual(h.calls, [])
        h = Harness()
        h.run({'id':'report','platform':'mqtt','topic':'binrange/tag/unmapped/anchor/a/state','payload':'bad'})
        self.assertEqual(h.calls, [])

    def test_notify_failure_records_attempt_and_does_not_retry(self):
        h = Harness()
        h.report()
        h.fail_notify = True
        with self.assertRaises(RuntimeError):
            h.tick(60)
        h.fail_notify = False
        h.report()
        h.tick(60)
        self.assertEqual(h.messages, [])
        self.assertEqual(h.helper('batch'), {})
        self.assertEqual(h.helper('ledger')['1'][2], 20260922)

    def test_six_bins_maximum_counter_fit_helpers(self):
        h = Harness()
        for i in range(1, 7):
            h.report(i, tip_count=4294967295)
        h.tick(60)
        ledger = h.states['input_text.binrange_tip_ledger']
        self.assertLessEqual(len(ledger), 255)
        self.assertEqual(len(h.helper('ledger')), 6)
        self.assertEqual(len(h.messages), 1)

    def test_fractional_reception_and_timestamp_age_combination(self):
        h = Harness()
        h.time = h.time.replace(microsecond=500000)
        h.report()
        h.tick(61)
        self.assertEqual(len(h.messages), 1)
        h = Harness()
        h.report(ts=(h.time - timedelta(seconds=900)).isoformat(), tip_age_s=901)
        self.assertEqual(h.helper('ledger'), {})


if __name__ == '__main__':
    unittest.main()
