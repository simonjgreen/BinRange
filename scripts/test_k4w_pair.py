import importlib.util
import pathlib
import unittest
import asyncio
import contextlib
import io
import types
from unittest.mock import patch, mock_open

import k4w_pair as pairing


class FakeDBus:
    """Only the external D-Bus boundary is faked; no real bus is opened."""
    def __init__(self, *, hangs=(), failures=()):
        self.hangs = set(hangs)
        self.failures = set(failures)
        self.events = []
        self.entered = {name: asyncio.Event() for name in (
            'connect', 'manager-info', 'register', 'device-info', 'pair',
            'cancel', 'unregister')}

    async def step(self, name):
        self.events.append(name)
        self.entered[name].set()
        if name in self.failures:
            raise RuntimeError('private commissioning material 123456')
        if name in self.hangs:
            await asyncio.Event().wait()

    async def connect(self):
        await self.step('connect')
        return self

    def export(self, path, agent):
        self.agent = agent

    async def introspect(self, destination, path):
        await self.step('manager-info' if path == '/org/bluez' else 'device-info')
        return None

    def get_proxy_object(self, *args):
        return self

    def get_interface(self, name):
        return self

    async def call_register_agent(self, path, capability):
        assert capability == 'KeyboardOnly'
        await self.step('register')

    async def call_pair(self):
        await self.step('pair')

    async def call_cancel_pairing(self):
        await self.step('cancel')

    async def call_unregister_agent(self, path):
        await self.step('unregister')

    def disconnect(self):
        self.events.append('disconnect')

    def modules(self):
        return {
            'dbus_fast': types.SimpleNamespace(BusType=types.SimpleNamespace(SYSTEM=1),
                                              DBusError=RuntimeError),
            'dbus_fast.aio': types.SimpleNamespace(MessageBus=lambda **kwargs: self),
            'dbus_fast.annotations': types.SimpleNamespace(DBusObjectPath=str, DBusUInt32=int),
            'dbus_fast.service': types.SimpleNamespace(
                ServiceInterface=type('Interface', (), {'__init__': lambda self, name: None}),
                dbus_method=lambda: lambda function: function),
        }


class Lifecycle(unittest.IsolatedAsyncioTestCase):
    def harness(self, bus):
        stack = contextlib.ExitStack()
        stack.enter_context(patch.dict('sys.modules', bus.modules()))
        for name in ('SETUP_TIMEOUT', 'PAIR_TIMEOUT', 'CLEANUP_TIMEOUT'):
            stack.enter_context(patch.object(pairing, name, 0.02, create=True))
        stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
        return stack

    async def test_setup_deadline_disconnects_even_before_connect_completes(self):
        for phase in ('connect', 'manager-info', 'register', 'device-info'):
            with self.subTest(phase=phase):
                bus = FakeDBus(hangs=(phase,))
                with self.harness(bus):
                    task = asyncio.create_task(pairing.pair(
                        pairing.PasskeyGate('AA:BB:CC:DD:EE:FF', 123456)))
                    done, _ = await asyncio.wait({task}, timeout=0.2)
                    if not done:
                        task.cancel()
                    with contextlib.suppress(Exception, asyncio.CancelledError):
                        await task
                self.assertTrue(done, 'setup exceeded its deadline')
                self.assertEqual(bus.events[-1], 'disconnect')
                self.assertNotIn('pair', bus.events)
                if phase in ('register', 'device-info'):
                    self.assertIn('unregister', bus.events)

    async def test_uncertain_pair_is_cancelled_and_cleanup_always_disconnects(self):
        for hangs, failures in ((('pair',), ()),
                                (('pair', 'cancel', 'unregister'), ()),
                                (('pair',), ('cancel', 'unregister'))):
            with self.subTest(hangs=hangs, failures=failures):
                bus = FakeDBus(hangs=hangs, failures=failures)
                with self.harness(bus):
                    task = asyncio.create_task(pairing.pair(
                        pairing.PasskeyGate('AA:BB:CC:DD:EE:FF', 123456)))
                    done, _ = await asyncio.wait({task}, timeout=0.2)
                    if not done:
                        task.cancel()
                    with self.assertRaises(BaseException):
                        await task
                self.assertTrue(done, 'cleanup exceeded its deadline')
                self.assertEqual(bus.events[-3:], ['cancel', 'unregister', 'disconnect'])
                self.assertEqual(bus.events.count('pair'), 1)

    async def test_cancellation_in_pairing_still_cleans_up(self):
        bus = FakeDBus(hangs=('pair',))
        with self.harness(bus):
            task = asyncio.create_task(pairing.pair(
                pairing.PasskeyGate('AA:BB:CC:DD:EE:FF', 123456)))
            await asyncio.wait_for(bus.entered['pair'].wait(), 0.2)
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await asyncio.wait_for(task, 0.2)
        self.assertEqual(bus.events[-3:], ['cancel', 'unregister', 'disconnect'])

    async def test_cleanup_error_does_not_replace_cancellation(self):
        bus = FakeDBus(hangs=('pair',), failures=('unregister',))
        with self.harness(bus):
            task = asyncio.create_task(pairing.pair(
                pairing.PasskeyGate('AA:BB:CC:DD:EE:FF', 123456)))
            await asyncio.wait_for(bus.entered['pair'].wait(), 0.2)
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await asyncio.wait_for(task, 0.2)
        self.assertEqual(bus.events[-1], 'disconnect')

    async def test_cancellation_during_cleanup_is_preserved_after_disconnect(self):
        bus = FakeDBus(hangs=('unregister',))
        with self.harness(bus), contextlib.redirect_stdout(io.StringIO()) as output:
            task = asyncio.create_task(pairing.pair(
                pairing.PasskeyGate('AA:BB:CC:DD:EE:FF', 123456)))
            await asyncio.wait_for(bus.entered['unregister'].wait(), 0.2)
            task.cancel()
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await asyncio.wait_for(task, 0.2)
        self.assertEqual(bus.events[-1], 'disconnect')
        self.assertNotIn('Selected device paired', output.getvalue())
        self.assertNotIn('cancel', bus.events)  # Pair already acknowledged success.

    async def test_success_is_reported_only_after_successful_cleanup(self):
        for failures in ((), ('unregister',)):
            with self.subTest(failures=failures):
                bus = FakeDBus(failures=failures)
                with self.harness(bus), contextlib.redirect_stdout(io.StringIO()) as output:
                    try:
                        await pairing.pair(pairing.PasskeyGate('AA:BB:CC:DD:EE:FF', 123456))
                    except Exception as exc:
                        self.assertTrue(failures)
                        self.assertNotIn('123456', str(exc))
                    else:
                        self.assertFalse(failures)
                self.assertEqual(bus.events[-1], 'disconnect')
                self.assertNotIn('cancel', bus.events)
                self.assertEqual('Selected device paired' in output.getvalue(), not failures)

class Pairing(unittest.TestCase):
    def test_passkey_is_released_only_to_selected_device(self):
        path = pathlib.Path(__file__).with_name('k4w_pair.py')
        self.assertTrue(path.exists(), 'scoped pairing helper missing')
        spec = importlib.util.spec_from_file_location('pair', path)
        mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
        gate = mod.PasskeyGate('AA:BB:CC:DD:EE:FF', 123456)
        self.assertEqual(gate.request('/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF'),123456)
        for p in ['/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FE',
                  '/org/bluez/hci1/dev_AA_BB_CC_DD_EE_FF','']:
            with self.assertRaises(ValueError): gate.request(p)
        for pin in [-1,1000000,True,'123456']:
            with self.assertRaises(ValueError): mod.PasskeyGate('AA:BB:CC:DD:EE:FF',pin)
        with self.assertRaises(ValueError): mod.PasskeyGate('nearby-tag',123456)


class CLI(unittest.TestCase):
    def inputs(self):
        stack = contextlib.ExitStack()
        stack.enter_context(patch('sys.argv', ['k4w_pair.py', '--address',
            'AA:BB:CC:DD:EE:FF', '--pin-file', '/not-a-real-pin-file']))
        stack.enter_context(patch.object(pairing.Path, 'open', mock_open(read_data='123456\n')))
        stack.enter_context(patch.object(pairing.os, 'fstat', return_value=
            types.SimpleNamespace(st_mode=pairing.stat.S_IFREG | 0o600)))
        return stack

    def test_backend_errors_are_sanitized_and_disconnect(self):
        bus = FakeDBus(failures=('pair', 'cancel', 'unregister'))
        with self.inputs(), patch.dict('sys.modules', bus.modules()), \
                contextlib.redirect_stdout(io.StringIO()) as output, \
                contextlib.redirect_stderr(io.StringIO()) as errors:
            self.assertEqual(pairing.main(), 1)
        self.assertEqual(bus.events[-3:], ['cancel', 'unregister', 'disconnect'])
        self.assertNotIn('123456', output.getvalue() + errors.getvalue())
        self.assertNotIn('private commissioning material', output.getvalue() + errors.getvalue())
        self.assertNotIn('Selected device paired', output.getvalue())
        self.assertIn('inspect', output.getvalue().lower())

    def test_interrupt_has_sanitized_output_and_exit_130(self):
        def interrupted(coroutine):
            coroutine.close()
            raise KeyboardInterrupt('private commissioning material 123456')

        with self.inputs(), patch.object(pairing.asyncio, 'run', side_effect=interrupted), \
                contextlib.redirect_stdout(io.StringIO()) as output, \
                contextlib.redirect_stderr(io.StringIO()) as errors:
            self.assertEqual(pairing.main(), 130)
        self.assertNotIn('123456', output.getvalue() + errors.getvalue())
        self.assertIn('inspect', output.getvalue().lower())


if __name__ == '__main__':
    unittest.main()
