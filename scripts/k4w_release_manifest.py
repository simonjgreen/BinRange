#!/usr/bin/env python3
"""Create public metadata for a structurally valid K4W MCUboot image."""

import hashlib
import json
import sys

IMAGE_MAGIC = 0x96F3B83D
REGULAR_TLV_MAGIC = 0x6907
PROTECTED_TLV_MAGIC = 0x6908
MAX_SIGNED_SIZE = 212992


def _u16(data, offset):
    return int.from_bytes(data[offset:offset + 2], "little")


def _u32(data, offset):
    return int.from_bytes(data[offset:offset + 4], "little")


def _valid_der_signature(value):
    if not 8 <= len(value) <= 72 or value[0] != 0x30:
        return False
    if value[1] >= 0x80 or value[1] != len(value) - 2:
        return False
    offset = 2
    for _ in range(2):
        if offset >= len(value) or value[offset] != 0x02:
            return False
        offset += 1
        if offset >= len(value):
            return False
        integer_size = value[offset]
        offset += 1
        if not 1 <= integer_size <= 33 or integer_size > len(value) - offset:
            return False
        first = value[offset]
        if first & 0x80 or (first == 0 and
                           (integer_size == 1 or not value[offset + 1] & 0x80)):
            return False
        offset += integer_size
    return offset == len(value)


def _parse_tlvs(data, start, total, magic, require_release_tlvs):
    if total < 4 or start > len(data) or total > len(data) - start:
        raise ValueError("truncated TLV area")
    if _u16(data, start) != magic or _u16(data, start + 2) != total:
        raise ValueError("invalid TLV header")
    offset = start + 4
    end = start + total
    required = {}
    while offset < end:
        if end - offset < 4:
            raise ValueError("truncated TLV entry")
        tlv_type, value_size = _u16(data, offset), _u16(data, offset + 2)
        offset += 4
        if value_size > end - offset:
            raise ValueError("TLV entry outside its area")
        value = data[offset:offset + value_size]
        if require_release_tlvs and tlv_type in (0x10, 0x01, 0x22):
            if tlv_type in required:
                raise ValueError("duplicate release TLV")
            if ((tlv_type in (0x10, 0x01) and value_size != 32) or
                    (tlv_type == 0x22 and not _valid_der_signature(value))):
                raise ValueError("malformed release TLV")
            required[tlv_type] = value
        offset += value_size
    if require_release_tlvs and set(required) != {0x10, 0x01, 0x22}:
        raise ValueError("missing required release TLV")
    return required


def parse_image(data):
    """Return structural signed-region metadata; signature verification is omitted."""
    if len(data) < 32:
        raise ValueError("image is shorter than its MCUboot header")
    if _u32(data, 0) != IMAGE_MAGIC or _u32(data, 4) != 0 or _u32(data, 16) != 0:
        raise ValueError("unsupported MCUboot image header")
    header_size, protected_size, payload_size = _u16(data, 8), _u16(data, 10), _u32(data, 12)
    if header_size < 32 or header_size > len(data) or payload_size == 0:
        raise ValueError("invalid image header or empty payload")
    remaining = len(data) - header_size
    if payload_size > remaining:
        raise ValueError("payload is outside the image")
    payload_end = header_size + payload_size
    remaining -= payload_size
    if protected_size > remaining:
        raise ValueError("protected TLVs are outside the image")
    signed_region_size = payload_end + protected_size
    if len(data) > MAX_SIGNED_SIZE or signed_region_size > MAX_SIGNED_SIZE:
        raise ValueError("image exceeds K4W capacity")
    if protected_size:
        _parse_tlvs(data, payload_end, protected_size, PROTECTED_TLV_MAGIC, False)
    required = _parse_tlvs(data, signed_region_size, len(data) - signed_region_size,
                            REGULAR_TLV_MAGIC, True)
    version = f"{data[20]}.{data[21]}.{_u16(data, 22)}+{_u32(data, 24)}"
    if len(version) >= 24:
        raise ValueError("version does not fit release metadata")
    return {
        "signed_region_size": signed_region_size,
        "image_hash": required[0x10],
        "version": version,
    }


def release_manifest(path):
    """Load an image and return publishable manifest metadata after hash binding."""
    data = path.read_bytes()
    parsed = parse_image(data)
    signed_hash = hashlib.sha256(data[:parsed["signed_region_size"]]).digest()
    if signed_hash != parsed["image_hash"]:
        raise ValueError("signed-region SHA-256 does not match image TLV")
    return {
        "schema": 1,
        "target": "k4w/nrf52833/dw3110",
        "size": len(data),
        "file_sha256": hashlib.sha256(data).hexdigest(),
        "image_hash": parsed["image_hash"].hex(),
        "version": parsed["version"],
    }


def main(argv):
    if len(argv) != 2:
        raise ValueError("usage: k4w_release_manifest.py IMAGE.signed.bin")
    print(json.dumps(release_manifest(__import__("pathlib").Path(argv[1])), separators=(",", ":")))


if __name__ == "__main__":
    try:
        main(sys.argv)
    except (OSError, ValueError) as error:
        print(f"k4w_release_manifest: {error}", file=sys.stderr)
        raise SystemExit(1)
