import random
import re
import unittest

import holloman3
from holloman3.normalize import normalize_headers, split_message

CLUSTER_RE = re.compile(r"^[ cdefghijkmnopqrstuvwxyz]\.[0-9a-f]{32}$")


class TestHolloman3(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        holloman3.init(13)

    @classmethod
    def tearDownClass(cls):
        holloman3.cleanup()

    def test_avx2_available(self):
        self.assertTrue(holloman3.is_avx2_available())

    def test_format(self):
        fp = holloman3.fingerprint(b"A" * 4096)
        self.assertRegex(fp, CLUSTER_RE)

    def test_determinism(self):
        r = random.Random(7)
        data = bytes(r.randrange(256) for _ in range(8192))
        self.assertEqual(holloman3.fingerprint(data), holloman3.fingerprint(data))

    def test_known_value(self):
        # 8192 deterministic pseudo-random bytes; cross-checked against the
        # pure-Go omega-scan reference (fingerprint.BuildClusterID).
        r = random.Random(12345)
        data = bytes(r.randrange(256) for _ in range(8192))
        self.assertEqual(holloman3.fingerprint(data), "h.8181858176797a780707070800000000")

    def test_order_char_by_size(self):
        # 16 KiB -> order 7 -> 'h'; 1 MiB -> order 10 -> 'k'
        self.assertTrue(holloman3.fingerprint(b"B" * 16384).startswith("h."))
        self.assertTrue(holloman3.fingerprint(b"B" * (1 << 20)).startswith("k."))

    def test_split_and_normalize(self):
        msg = (
            b"From: Alice <alice@example.com>\r\n"
            b"Subject: hi\r\n"
            b"Message-ID: <x@y>\r\n"
            b"Content-Type: multipart/mixed; boundary=\"abc\"\r\n"
            b"\r\n"
            b"body text"
        )
        headers, body = split_message(msg)
        self.assertEqual(body, b"body text")

        norm = normalize_headers(headers)
        self.assertNotIn(b"subject", norm)
        self.assertNotIn(b"message-id", norm)
        self.assertIn(b"from: example.com", norm)
        self.assertNotIn(b"boundary", norm)
        self.assertIn(b"multipart/mixed", norm)


if __name__ == "__main__":
    unittest.main()
