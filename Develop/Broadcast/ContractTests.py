"""Check the published protocol fixtures before consuming them in Rust/C++."""

import json
import hashlib
import hmac
from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parents[2]


class ContractTests(unittest.TestCase):
    def test_authenticated_udp_fixture_has_standard_http_audio_variants(self):
        fixture = json.loads(
            (ROOT / "Docs/Broadcast/protocol-fixture.json").read_text(encoding="utf-8")
        )
        packet = bytes.fromhex(fixture["udpPcmStereoHex"])
        self.assertEqual(len(packet), 104)
        self.assertEqual(
            struct.unpack("<4sBBBB16sIIQQHHHHII", packet[:64]),
            (b"WJBC", 1, 1, 0, 0, bytes(range(16)), 7, 1,
             17, 960, 2, 0, 1, 0, 8, 0),
        )
        self.assertEqual(struct.unpack("<4h", packet[64:72]), (8192, 24576, -16384, -32768))
        key = bytes.fromhex(fixture["sessionKeyHex"])
        self.assertTrue(hmac.compare_digest(
            packet[-32:], hmac.digest(key, packet[:-32], hashlib.sha256)))
        self.assertEqual(fixture["formats"], {"wav": "audio/wav", "opus": "audio/ogg; codecs=opus"})
        self.assertEqual(fixture["layouts"]["7.1"],
                         ["L", "R", "C", "LFE", "Ls", "Rs", "Lrb", "Rrb"])
        self.assertEqual(fixture["layouts"]["5.1"], ["L", "R", "C", "LFE", "Ls", "Rs"])
        self.assertEqual(fixture["layouts"]["mono"], ["M"])
        self.assertEqual(fixture["layouts"]["stereo"], ["L", "R"])
        protocol = (ROOT / "Docs/Broadcast/Protocol-v1.md").read_text(encoding="utf-8")
        for requirement in ("Authorization: Bearer", "/bc/publish", "/bc/listen?",
                            "Listen", "409", "503", "64", "100 ms", "JACK-only",
                            "format=wav", "format=opus", "原生", "1200"):
            self.assertIn(requirement, protocol)


if __name__ == "__main__":
    unittest.main()
