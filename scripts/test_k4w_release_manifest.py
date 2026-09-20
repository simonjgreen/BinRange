import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import k4w_release_manifest as manifest


def image(header_size=32, payload_size=32, protected=b"", include_sha=True,
          include_keyhash=True, include_signature=True, version=(1, 2, 3, 4)):
    protected_tlvs = b""
    if protected:
        protected_tlvs = (b"\x08\x69" + (4 + 4 + len(protected)).to_bytes(2, "little") +
                          b"\x60\x00" + len(protected).to_bytes(2, "little") + protected)
    header = bytearray(header_size)
    header[0:4] = (0x96F3B83D).to_bytes(4, "little")
    header[8:10] = header_size.to_bytes(2, "little")
    header[10:12] = len(protected_tlvs).to_bytes(2, "little")
    header[12:16] = payload_size.to_bytes(4, "little")
    major, minor, revision, build = version
    header[20:22] = bytes((major, minor))
    header[22:24] = revision.to_bytes(2, "little")
    header[24:28] = build.to_bytes(4, "little")
    entries = []
    if include_sha:
        entries.append((0x10, bytes([0xA5]) * 32))
    if include_keyhash:
        entries.append((0x01, bytes([0xB6]) * 32))
    if include_signature:
        entries.append((0x22, b"\x30\x06\x02\x01\x01\x02\x01\x01"))
    regular = bytearray(b"\x07\x69\x00\x00")
    for kind, value in entries:
        regular += kind.to_bytes(2, "little") + len(value).to_bytes(2, "little") + value
    regular[2:4] = len(regular).to_bytes(2, "little")
    return bytes(header) + bytes(payload_size) + protected_tlvs + bytes(regular)


def image_with_matching_sha(header_size=32, payload_size=32, protected=b"",
                            version=(1, 2, 3, 4)):
    candidate = bytearray(image(header_size, payload_size, protected, version=version))
    signed_region_size = header_size + payload_size + (0 if not protected else 8 + len(protected))
    candidate[signed_region_size + 8:signed_region_size + 40] = hashlib.sha256(
        candidate[:signed_region_size]).digest()
    return bytes(candidate)


class ManifestTests(unittest.TestCase):
    def test_reads_32_and_512_headers_and_protected_tlvs(self):
        for header_size in (32, 512):
            parsed = manifest.parse_image(image(header_size, protected=b"\x12\x34"))
            self.assertEqual(header_size + 32 + 10, parsed["signed_region_size"])
            self.assertEqual("1.2.3+4", parsed["version"])
            self.assertEqual(bytes([0xA5]) * 32, parsed["image_hash"])

    def test_rejects_structural_and_tlv_failures(self):
        bad = [
            b"",
            image()[:31],
            bytes([0]) + image()[1:],
            image()[:4] + (1).to_bytes(4, "little") + image()[8:],
            image()[:16] + (1).to_bytes(4, "little") + image()[20:],
            image()[:12] + bytes(4) + image()[16:],
            image(include_sha=False), image(include_keyhash=False), image(include_signature=False),
            image() + b"x",
        ]
        malformed_der = bytearray(image())
        malformed_der[-1] = 0
        bad.append(bytes(malformed_der))
        protected_mismatch = bytearray(image(protected=b"\x12\x34"))
        protected_mismatch[66:68] = (9).to_bytes(2, "little")
        bad.append(bytes(protected_mismatch))
        for tlv_type, value in ((0x10, bytes([0xC7]) * 32),
                                (0x01, bytes([0xC7]) * 32),
                                (0x22, b"\x30\x06\x02\x01\x01\x02\x01\x01")):
            duplicate = bytearray(image())
            duplicate += tlv_type.to_bytes(2, "little") + len(value).to_bytes(2, "little") + value
            duplicate[66:68] = (len(duplicate) - 64).to_bytes(2, "little")
            bad.append(bytes(duplicate))
        for candidate in bad:
            with self.assertRaises(ValueError):
                manifest.parse_image(candidate)

    def test_enforces_total_image_capacity_including_regular_tlvs(self):
        at_capacity = image(payload_size=212872)
        self.assertEqual(212992, len(at_capacity))
        self.assertEqual(212904, manifest.parse_image(at_capacity)["signed_region_size"])
        over_capacity = image(payload_size=212873)
        self.assertEqual(212993, len(over_capacity))
        with self.assertRaises(ValueError):
            manifest.parse_image(over_capacity)

    def test_manifest_hashes_signed_region_and_cli_prints_only_json(self):
        candidate = image_with_matching_sha(512)
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "image.signed.bin"
            path.write_bytes(candidate)
            metadata = manifest.release_manifest(path)
            self.assertEqual(1, metadata["schema"])
            self.assertEqual("k4w/nrf52833/dw3110", metadata["target"])
            self.assertEqual(len(candidate), metadata["size"])
            self.assertEqual(hashlib.sha256(candidate).hexdigest(), metadata["file_sha256"])
            self.assertEqual(hashlib.sha256(candidate[:544]).hexdigest(), metadata["image_hash"])
            completed = subprocess.run([sys.executable, str(pathlib.Path(manifest.__file__)), str(path)],
                                       text=True, capture_output=True, check=True)
            self.assertEqual("", completed.stderr)
            self.assertEqual(metadata, json.loads(completed.stdout))

    def test_manifest_rejects_a_sha_tlv_that_does_not_match_signed_region(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "image.signed.bin"
            path.write_bytes(image())
            with self.assertRaisesRegex(ValueError, "signed-region SHA-256"):
                manifest.release_manifest(path)

    def test_manifest_matches_c_version_output_limit_with_recomputed_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "image.signed.bin"
            path.write_bytes(image_with_matching_sha(
                version=(255, 255, 65535, 999999999)))
            self.assertEqual("255.255.65535+999999999",
                             manifest.release_manifest(path)["version"])
            path.write_bytes(image_with_matching_sha(
                version=(255, 255, 65535, 4294967295)))
            with self.assertRaisesRegex(ValueError, "version"):
                manifest.release_manifest(path)

if __name__ == "__main__":
    unittest.main()
