"""Generate a private C++ bootstrap credential header for an anchor build.

The caller must place --output under its ignored build directory and stop the
build on any error. No --secret means an explicitly disabled bootstrap, even
if a previous provisioned build left a header behind. An explicit invalid or
missing secret is an error, not permission to reuse the previous header.
Never pass a password itself on the command line or in compiler definitions.
"""

import argparse
import os
from pathlib import Path
import stat
import sys
import tempfile


def _read_secret(path: Path) -> bytes:
    # O_NONBLOCK avoids hanging if a mistaken path names a FIFO/device; fstat
    # rejects it before any read. No following a symlink to a different secret.
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(descriptor, "rb") as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or stat.S_IMODE(info.st_mode) & 0o077:
            raise ValueError("Credential must be a private regular file")
        value = stream.read(67)
    if value.endswith(b"\r\n"):
        value = value[:-2]
    elif value.endswith(b"\n"):
        value = value[:-1]
    if not 12 <= len(value) <= 64 or any(c < 32 or c > 126 for c in value):
        raise ValueError("Credential must contain 12..64 printable ASCII characters")
    return value


def generate_header(secret: Path | None, output: Path) -> None:
    """Atomically write a mode-0600 header without emitting credential material."""
    temporary = None
    try:
        output = Path(output)
        if output.is_symlink():
            raise ValueError("Refusing a symlink output")
        if secret is not None and output.exists() and os.path.samefile(secret, output):
            raise ValueError("Input and output must be different files")
        value = _read_secret(Path(secret)) if secret is not None else b""
        # A byte initializer avoids C/C++ escaping, trigraphs and quote injection.
        initializer = ",".join(str(byte) for byte in value + b"\0")
        header = ("// Private generated build input; do not publish.\n#pragma once\n"
                  "namespace binrange {\n"
                  "static constexpr char admin_bootstrap[] = {" + initializer + "};\n}\n")
        output.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        descriptor, temporary = tempfile.mkstemp(prefix=".admin-", dir=output.parent)
        with os.fdopen(descriptor, "w", encoding="ascii") as stream:
            stream.write(header)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, output)
        temporary = None
    except OSError:
        raise ValueError("Cannot read private credential or write build header") from None
    finally:
        if temporary is not None:
            os.unlink(temporary)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--secret", type=Path, help="Private credential file, never a password")
    parser.add_argument("--output", type=Path, required=True, help="Header in an ignored build directory")
    args = parser.parse_args()
    try:
        generate_header(args.secret, args.output)
    except ValueError as error:
        print("Admin bootstrap: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
