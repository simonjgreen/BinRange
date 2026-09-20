"""Pair one explicitly selected K4W using its privately captured commissioning PIN.

The tag must be in locally authorized commissioning mode. This registers an
agent only for this process, never the system default, and never changes other
bonds. Follow pairing with k4w_ble.py status and hardware-identity verification.
Python 3.11+: setup has a 10-second total deadline, Pair has 45 seconds, and
CancelPairing/UnregisterAgent each have 3 seconds for best-effort cleanup.
Cancellation waits for bounded cleanup. An uncertain result is never replayed.
"""
import argparse
import asyncio
import os
import re
import stat
from pathlib import Path

SETUP_TIMEOUT = 10
PAIR_TIMEOUT = 45
CLEANUP_TIMEOUT = 3


class PairingError(Exception):
    """Sanitized failure; pairing may have completed despite a lost reply."""


class PasskeyGate:
    def __init__(self, address, pin):
        if not re.fullmatch(r'(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}', address):
            raise ValueError('Explicit Bluetooth address required')
        if type(pin) is not int or not 0 <= pin <= 999999:
            raise ValueError('Invalid commissioning PIN')
        self.path = '/org/bluez/hci0/dev_' + address.upper().replace(':','_')
        self._pin = pin

    def request(self, path):
        if path != self.path:
            raise ValueError('Pairing request is not from selected device')
        return self._pin


async def pair(gate):
    from dbus_fast import BusType, DBusError
    from dbus_fast.aio import MessageBus
    from dbus_fast.annotations import DBusObjectPath, DBusUInt32
    from dbus_fast.service import ServiceInterface, dbus_method

    class Agent(ServiceInterface):
        def __init__(self):
            super().__init__('org.bluez.Agent1')

        @dbus_method()
        def RequestPasskey(self, device: DBusObjectPath) -> DBusUInt32:
            try:
                return gate.request(device)
            except ValueError:
                raise DBusError('org.bluez.Error.Rejected','Unselected device') from None

        @dbus_method()
        def RequestAuthorization(self, device: DBusObjectPath):
            raise DBusError('org.bluez.Error.Rejected','Unauthenticated pairing forbidden')

        @dbus_method()
        def RequestConfirmation(self, device: DBusObjectPath, passkey: DBusUInt32):
            raise DBusError('org.bluez.Error.Rejected','Passkey entry required')

        @dbus_method()
        def Cancel(self):
            pass

        @dbus_method()
        def Release(self):
            pass

    bus = MessageBus(bus_type=BusType.SYSTEM)
    agent_path = '/org/binrange/CommissioningAgent'
    registration_attempted = False
    pairing_started = paired = False
    failure = None

    async def cleanup():
        failed = False
        try:
            try:
                if pairing_started and not paired:
                    try:
                        await asyncio.wait_for(device.call_cancel_pairing(), CLEANUP_TIMEOUT)
                    except Exception:
                        failed = True
            finally:
                if registration_attempted:
                    try:
                        await asyncio.wait_for(manager.call_unregister_agent(agent_path),
                                               CLEANUP_TIMEOUT)
                    except Exception:
                        failed = True
        finally:
            try:
                bus.disconnect()
            except Exception:
                failed = True
        return failed

    try:
        async with asyncio.timeout(SETUP_TIMEOUT):
            await bus.connect()
            bus.export(agent_path,Agent())
            manager_xml = await bus.introspect('org.bluez','/org/bluez')
            manager = bus.get_proxy_object('org.bluez','/org/bluez',manager_xml).get_interface('org.bluez.AgentManager1')
            # A lost registration reply does not mean BlueZ failed to register.
            registration_attempted = True
            await manager.call_register_agent(agent_path,'KeyboardOnly')
            device_xml = await bus.introspect('org.bluez',gate.path)
            device = bus.get_proxy_object('org.bluez',gate.path,device_xml).get_interface('org.bluez.Device1')
        pairing_started = True
        await asyncio.wait_for(device.call_pair(), PAIR_TIMEOUT)
        paired = True
    except (Exception, asyncio.CancelledError) as exc:
        failure = exc
    finally:
        # Keep cleanup alive even if the caller cancels again while it runs.
        cleanup_task = asyncio.create_task(cleanup())
        while True:
            try:
                cleanup_failed = await asyncio.shield(cleanup_task)
                break
            except asyncio.CancelledError as exc:
                failure = exc
                if cleanup_task.cancelled():
                    cleanup_failed = True
                    break
    if isinstance(failure, asyncio.CancelledError):
        raise failure
    if failure is not None or cleanup_failed:
        raise PairingError('Pairing or cleanup failed; inspect the selected device bond and '
                           'verify hardware identity before explicitly retrying. No replay.') from None
    print('Selected device paired. Verify hardware identity with the recovery client.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address',required=True)
    parser.add_argument('--pin-file',type=Path,required=True,
                        help='Private mode-0600 file containing only the six-digit PIN')
    args = parser.parse_args()
    try:
        with args.pin_file.open() as file:
            mode = os.fstat(file.fileno()).st_mode
            if not stat.S_ISREG(mode) or mode & 0o077:
                raise ValueError('PIN file must be private')
            value = file.read(8).strip()
        if not re.fullmatch('[0-9]{6}',value):
            raise ValueError('PIN file must contain six digits')
        asyncio.run(pair(PasskeyGate(args.address,int(value))))
        return 0
    except (KeyboardInterrupt, asyncio.CancelledError):
        print('Pairing cancelled: inspect the selected device bond and verify hardware '
              'identity before explicitly retrying. No automatic replay.')
        return 130
    except Exception:
        # DBus and input exception details can contain sensitive material.
        print('Pairing failed: check selected device, private PIN file, local commissioning '
              'window and Bluetooth state. Inspect the selected device bond and verify '
              'hardware identity before explicitly retrying. No automatic replay.')
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
