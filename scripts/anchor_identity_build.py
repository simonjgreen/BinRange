"""Generate a private anchor BLE identity bootstrap header."""

import argparse
import json
import os
from pathlib import Path
import re
import stat
import sys
import tempfile


_ADDRESS = re.compile(r"\A[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5}\Z")
_MAX_INPUT = 512


def _read_identity(path: Path) -> tuple[bytes, bytes]:
    try:
        descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        with os.fdopen(descriptor, "rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or stat.S_IMODE(info.st_mode) & 0o077:
                raise ValueError("Identity file must be a private regular file")
            value = stream.read(_MAX_INPUT + 1)
    except ValueError:
        raise
    except OSError:
        raise ValueError("Cannot read private identity file") from None
    if len(value) > _MAX_INPUT:
        raise ValueError("Identity file is too large")
    try:
        def collect(items):
            keys = [key for key, _ in items]
            if len(keys) != len(set(keys)):
                raise ValueError("Identity JSON contains duplicate fields")
            return dict(items)

        document = json.loads(value.decode("utf-8"), object_pairs_hook=collect)
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError):
        raise ValueError("Identity file is not valid JSON") from None
    if not isinstance(document, dict) or set(document) != {"anchor_mac", "ble_address"}:
        raise ValueError("Identity JSON must contain exactly the required fields")

    addresses = []
    for field in ("anchor_mac", "ble_address"):
        item = document[field]
        if not isinstance(item, str) or not _ADDRESS.fullmatch(item):
            raise ValueError("Identity addresses must be six octets")
        addresses.append(bytes.fromhex(item.replace(":", "")))
    anchor_mac, ble_address = addresses
    if (anchor_mac[0] & 1 or
            anchor_mac == b"\0" * 6 or anchor_mac == b"\xff" * 6):
        raise ValueError("Anchor MAC is not a valid unicast address")
    ble_tail = bytes((ble_address[0] & 0x3f,)) + ble_address[1:]
    if ble_address[0] & 0xc0 != 0xc0 or ble_tail in (b"\0" * 6, bytes([0x3f]) + b"\xff" * 5):
        raise ValueError("BLE address is not a usable static random address")
    return anchor_mac, ble_address


def _array(value: bytes) -> str:
    return ", ".join(f"0x{byte:02x}" for byte in value)


def generate_header(credential: Path | None, output: Path) -> None:
    """Atomically write a mode-0600 header without emitting identity material."""
    output = Path(output)
    temporary = None
    try:
        if output.is_symlink():
            raise ValueError("Refusing a symlink output")
        if credential is not None and output.exists() and os.path.samefile(credential, output):
            raise ValueError("Input and output must be different files")
        anchor_mac, ble_address = _read_identity(Path(credential)) if credential is not None else (b"\0" * 6, b"\0" * 6)
        present = credential is not None
        # NimBLE expects the raw BLE address least-significant byte first.
        raw_address = ble_address[::-1] if present else ble_address
        header = (
            "// Private generated build input; do not publish.\n"
            "#pragma once\n"
            "#include <cstdint>\n"
            "namespace binrange {\n"
            f"constexpr bool ble_identity_bootstrap_present = {'true' if present else 'false'};\n"
            f"constexpr uint8_t ble_identity_bootstrap_mac[6] = {{{_array(anchor_mac)}}};\n"
            f"constexpr uint8_t ble_identity_bootstrap_address[6] = {{{_array(raw_address)}}};\n"
            "}\n"
        )
        output.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        descriptor, temporary = tempfile.mkstemp(prefix=".identity-", dir=output.parent)
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "w", encoding="ascii") as stream:
            stream.write(header)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, output)
        temporary = None
    except ValueError:
        raise
    except OSError:
        raise ValueError("Cannot read private identity or write build header") from None
    finally:
        if temporary is not None:
            try:
                os.unlink(temporary)
            except OSError:
                pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--identity", type=Path, help="Private JSON identity file")
    parser.add_argument("--output", type=Path, required=True, help="Header in an ignored build directory")
    args = parser.parse_args()
    try:
        generate_header(args.identity, args.output)
    except ValueError as error:
        print("Anchor identity bootstrap: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
