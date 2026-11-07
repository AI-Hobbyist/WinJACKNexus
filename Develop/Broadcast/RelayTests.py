"""Public-process tests: real relay, UDP sender and anonymous HTTP listeners."""

import http.client
import base64
import hashlib
import hmac
import json
from pathlib import Path
import queue
import subprocess
import tempfile
import threading
import socket
import struct
import time
import ssl
import datetime
import ipaddress
import unittest
from websockets.sync.client import connect
from websockets.exceptions import InvalidStatus

ROOT = Path(__file__).resolve().parents[2]
SERVER = ROOT / "build/bc-target/release/bc-server.exe"


class RelayTests(unittest.TestCase):
    def setUp(self):
        self.start_server()

    def start_server(self, tls=False):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        token = Path(self.temporary.name) / "publish.token"
        token.write_text("test-publish-secret", encoding="utf-8")
        arguments = [
            str(SERVER),
            "--bind",
            "127.0.0.1:0",
            "--udp-bind",
            "127.0.0.1:0",
            "--publish-token-file",
            str(token),
        ]
        if tls:
            from cryptography import x509
            from cryptography.hazmat.primitives import hashes, serialization
            from cryptography.hazmat.primitives.asymmetric import rsa

            key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
            name = x509.Name(
                [x509.NameAttribute(x509.NameOID.COMMON_NAME, "localhost")]
            )
            now = datetime.datetime.now(datetime.timezone.utc)
            issuer = name
            signing_key = key
            ca_path = None
            if tls == "ca-signed":
                signing_key = rsa.generate_private_key(
                    public_exponent=65537, key_size=2048
                )
                issuer = x509.Name(
                    [
                        x509.NameAttribute(
                            x509.NameOID.COMMON_NAME, "Broadcast test root"
                        )
                    ]
                )
                ca = (
                    x509.CertificateBuilder()
                    .subject_name(issuer)
                    .issuer_name(issuer)
                    .public_key(signing_key.public_key())
                    .serial_number(x509.random_serial_number())
                    .not_valid_before(now - datetime.timedelta(minutes=1))
                    .not_valid_after(now + datetime.timedelta(days=1))
                    .add_extension(
                        x509.BasicConstraints(ca=True, path_length=0), critical=True
                    )
                    .add_extension(
                        x509.KeyUsage(
                            digital_signature=True,
                            content_commitment=False,
                            key_encipherment=False,
                            data_encipherment=False,
                            key_agreement=False,
                            key_cert_sign=True,
                            crl_sign=True,
                            encipher_only=False,
                            decipher_only=False,
                        ),
                        critical=True,
                    )
                    .sign(signing_key, hashes.SHA256())
                )
                ca_path = Path(self.temporary.name) / "ca.pem"
                ca_path.write_bytes(ca.public_bytes(serialization.Encoding.PEM))
            certificate = (
                x509.CertificateBuilder()
                .subject_name(name)
                .issuer_name(issuer)
                .public_key(key.public_key())
                .serial_number(x509.random_serial_number())
                .not_valid_before(now - datetime.timedelta(minutes=1))
                .not_valid_after(now + datetime.timedelta(days=1))
                .add_extension(
                    x509.SubjectAlternativeName(
                        [x509.IPAddress(ipaddress.ip_address("127.0.0.1"))]
                    ),
                    critical=False,
                )
                .add_extension(
                    x509.BasicConstraints(ca=False, path_length=None), critical=True
                )
                .add_extension(
                    x509.ExtendedKeyUsage([x509.ExtendedKeyUsageOID.SERVER_AUTH]),
                    critical=False,
                )
                .sign(signing_key, hashes.SHA256())
            )
            cert_path = Path(self.temporary.name) / "test.pem"
            key_path = Path(self.temporary.name) / "test-key.pem"
            cert_path.write_bytes(certificate.public_bytes(serialization.Encoding.PEM))
            key_path.write_bytes(
                key.private_bytes(
                    serialization.Encoding.PEM,
                    serialization.PrivateFormat.PKCS8,
                    serialization.NoEncryption(),
                )
            )
            self.tls_context = ssl.create_default_context(
                cafile=str(ca_path or cert_path)
            )
            arguments.extend(["--tls-cert", str(cert_path), "--tls-key", str(key_path)])
        else:
            arguments.append("--insecure-loopback")
        self.process = subprocess.Popen(
            arguments,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        self.addCleanup(self.stop_server)
        output = queue.Queue()

        def capture():
            for line in self.process.stdout:
                print("relay:", line.rstrip(), flush=True)
                output.put(line)

        self.reader = threading.Thread(target=capture, daemon=True)
        self.reader.start()
        ready = json.loads(output.get(timeout=15))
        self.address = ready["http"]
        self.udp_address = ready["udp"]

    def stop_server(self):
        if self.process.poll() is None:
            self.process.terminate()
        self.process.wait(timeout=5)
        self.reader.join(timeout=5)
        self.process.stdout.close()

    def request(self, path, headers=None):
        connection = http.client.HTTPConnection(self.address, timeout=3)
        self.addCleanup(connection.close)
        connection.request("GET", path, headers=headers or {})
        return connection.getresponse()

    def open_publisher(self, name="station"):
        publisher = self.enterContext(
            connect(
                f"ws://{self.address}/bc/publish?publisher={name}",
                additional_headers={"Authorization": "Bearer test-publish-secret"},
                proxy=None,
            )
        )
        return publisher, json.loads(publisher.recv(timeout=3))

    def register_stream(self, publisher, stream="news", handle=7, layout="stereo", epoch=1, opus_heads=None):
        roles = {
            "mono": ["M"],
            "stereo": ["L", "R"],
            "5.1": ["L", "R", "C", "LFE", "Ls", "Rs"],
            "7.1": ["L", "R", "C", "LFE", "Ls", "Rs", "Lrb", "Rrb"],
        }[layout]
        heads = {}
        for role, count in [("all", len(roles)), *[(role, 1) for role in roles]]:
            head = b"OpusHead" + struct.pack("<BBHIhB", 1, count, 0, 48000, 0, 1 if count > 2 else 0)
            if count == 6:
                head += bytes([4, 2, 0, 4, 1, 2, 3, 5])
            elif count == 8:
                head += bytes([5, 3, 0, 6, 1, 2, 3, 4, 5, 7])
            heads[role] = base64.b64encode(head).decode()
        message = {
            "type": "register",
            "requestId": handle,
            "streamId": stream,
            "handle": handle,
            "epoch": epoch,
            "displayName": stream,
            "category": [],
            "layout": layout,
            "pcmRate": 48000,
            "opusHeads": opus_heads if opus_heads is not None else heads,
        }
        publisher.send(json.dumps(message))
        self.assertEqual(json.loads(publisher.recv(timeout=3)), {"type": "ack", "requestId": handle})
        return message

    def datagrams(
        self, hello, payload, *, sequence, position, samples, handle=7, codec=1, variant=0, epoch=1
    ):
        fragments = [payload[offset : offset + 1104] for offset in range(0, len(payload), 1104)]
        result = []
        for index, fragment in enumerate(fragments):
            packet = (
                struct.pack(
                    "<4sBBBB16sIIQQHHHHII",
                    b"WJBC",
                    1,
                    codec,
                    variant,
                    0,
                    bytes.fromhex(hello["session"]),
                    handle,
                    epoch,
                    sequence + index,
                    position,
                    samples,
                    index,
                    len(fragments),
                    0,
                    len(fragment),
                    0,
                )
                + fragment
            )
            packet += hmac.digest(bytes.fromhex(hello["key"]), packet, hashlib.sha256)
            self.assertLessEqual(len(packet), 1200)
            result.append(packet)
        return result

    def send_datagrams(self, packets):
        host, port = self.udp_address.rsplit(":", 1)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            for packet in packets:
                udp.sendto(packet, (host, int(port)))

    def test_missing_or_wrong_publish_token_is_rejected_before_upgrade(self):
        for token in (None, "Bearer wrong-secret"):
            headers = {"Authorization": token} if token else {}
            response = self.request("/bc/publish?publisher=station", headers)
            self.assertEqual(response.status, 401)
            self.assertEqual(response.getheader("WWW-Authenticate"), "Bearer")
            self.assertEqual(json.loads(response.read())["code"], "unauthorized")

    def test_authorized_publisher_gets_udp_session_and_cannot_be_stolen(self):
        url = f"ws://{self.address}/bc/publish?publisher=station"
        headers = {"Authorization": "Bearer test-publish-secret"}
        with connect(url, additional_headers=headers, proxy=None) as publisher:
            hello = json.loads(publisher.recv(timeout=3))
            self.assertEqual(hello["type"], "hello")
            self.assertEqual(len(bytes.fromhex(hello["session"])), 16)
            self.assertEqual(len(bytes.fromhex(hello["key"])), 32)
            self.assertEqual(hello["udpPort"], int(self.udp_address.rsplit(":", 1)[1]))
            self.assertEqual(hello["maxDatagram"], 1200)
            with self.assertRaises(InvalidStatus) as rejection:
                with connect(url, additional_headers=headers, proxy=None):
                    self.fail("duplicate publisher took over the namespace")
            self.assertEqual(rejection.exception.response.status_code, 409)

    def test_udp_pcm_is_served_as_live_standard_wav(self):
        url = f"ws://{self.address}/bc/publish?publisher=station"
        headers = {"Authorization": "Bearer test-publish-secret"}
        with connect(url, additional_headers=headers, proxy=None) as publisher:
            hello = json.loads(publisher.recv(timeout=3))

            def opus_head(channels):
                data = b"OpusHead" + struct.pack("<BBHIhB", 1, channels, 0, 48000, 0, 0)
                return base64.b64encode(data).decode()

            publisher.send(
                json.dumps(
                    {
                        "type": "register",
                        "requestId": 1,
                        "streamId": "news",
                        "handle": 7,
                        "epoch": 1,
                        "displayName": "News",
                        "category": ["Program", "News"],
                        "layout": "stereo",
                        "pcmRate": 48000,
                        "opusHeads": {"all": opus_head(2), "L": opus_head(1), "R": opus_head(1)},
                    }
                )
            )
            self.assertEqual(json.loads(publisher.recv(timeout=3)), {"type": "ack", "requestId": 1})
            full = self.request("/bc/listen?publisher=station&stream=news&format=wav")
            right = self.request("/bc/listen?publisher=station&stream=news&channel=R&format=wav")
            self.assertEqual(full.status, 200)
            self.assertEqual(full.getheader("Content-Type"), "audio/wav")
            self.assertNotIn("attachment", full.getheader("Content-Disposition", ""))
            header = full.read(44)
            self.assertEqual(header[:4], b"RIFF")
            self.assertEqual(header[8:16], b"WAVEfmt ")
            self.assertEqual(struct.unpack("<H", header[22:24])[0], 2)
            self.assertEqual(struct.unpack("<H", right.read(44)[22:24])[0], 1)

            payload = struct.pack("<4h", 8192, 24576, -16384, -32768)
            packet = (
                struct.pack(
                    "<4sBBBB16sIIQQHHHHII",
                    b"WJBC",
                    1,
                    1,
                    0,
                    0,
                    bytes.fromhex(hello["session"]),
                    7,
                    1,
                    1,
                    0,
                    2,
                    0,
                    1,
                    0,
                    8,
                    0,
                )
                + payload
            )
            packet += hmac.digest(bytes.fromhex(hello["key"]), packet, hashlib.sha256)
            host, port = self.udp_address.rsplit(":", 1)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
                udp.sendto(packet, (host, int(port)))
            self.assertEqual(full.read(8), payload)
            self.assertEqual(right.read(4), struct.pack("<2h", 24576, -32768))

    def test_udp_opus_is_served_in_standard_ogg_with_audio_headers(self):
        url = f"ws://{self.address}/bc/publish?publisher=station"
        with connect(
            url, additional_headers={"Authorization": "Bearer test-publish-secret"}, proxy=None
        ) as publisher:
            hello = json.loads(publisher.recv(timeout=3))
            head = b"OpusHead" + struct.pack("<BBHIhB", 1, 1, 0, 48000, 0, 0)
            publisher.send(
                json.dumps(
                    {
                        "type": "register",
                        "requestId": 1,
                        "streamId": "mono",
                        "handle": 1,
                        "epoch": 1,
                        "displayName": "Mono",
                        "category": [],
                        "layout": "mono",
                        "pcmRate": 48000,
                        "opusHeads": {name: base64.b64encode(head).decode() for name in ("all", "M")},
                    }
                )
            )
            self.assertEqual(json.loads(publisher.recv(timeout=3))["type"], "ack")
            response = self.request("/bc/listen?publisher=station&stream=mono&format=opus")
            self.assertEqual(response.status, 200)
            self.assertEqual(response.getheader("Content-Type"), "audio/ogg; codecs=opus")
            self.assertIsNone(response.getheader("Content-Length"))

            def read_page():
                page_head = response.read(27)
                self.assertEqual(page_head[:4], b"OggS")
                lacing = response.read(page_head[26])
                data = response.read(sum(lacing))
                page = bytearray(page_head + lacing + data)
                expected_crc = int.from_bytes(page[22:26], "little")
                page[22:26] = b"\0" * 4
                crc = 0
                for byte in page:
                    crc ^= byte << 24
                    for _ in range(8):
                        crc = ((crc << 1) ^ (0x04C11DB7 if crc & 0x80000000 else 0)) & 0xFFFFFFFF
                self.assertEqual(crc, expected_crc)
                return page_head, data

            page_head, data = read_page()
            self.assertEqual(page_head[5], 2)
            self.assertEqual(data, head)
            self.assertTrue(read_page()[1].startswith(b"OpusTags"))
            payload = bytes.fromhex("f8fffe")  # RFC-compatible 20 ms Opus silence packet.
            packet = (
                struct.pack(
                    "<4sBBBB16sIIQQHHHHII",
                    b"WJBC",
                    1,
                    2,
                    0,
                    0,
                    bytes.fromhex(hello["session"]),
                    1,
                    1,
                    1,
                    960,
                    960,
                    0,
                    1,
                    0,
                    3,
                    0,
                )
                + payload
            )
            packet += hmac.digest(bytes.fromhex(hello["key"]), packet, hashlib.sha256)
            host, port = self.udp_address.rsplit(":", 1)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
                udp.sendto(packet, (host, int(port)))
            page_head, data = read_page()
            self.assertEqual(data, payload)
            self.assertEqual(int.from_bytes(page_head[6:14], "little"), 960)

    def test_out_of_order_blocks_inside_reorder_window_keep_sample_order(self):
        publisher, hello = self.open_publisher()
        self.register_stream(publisher)
        response = self.request("/bc/listen?publisher=station&stream=news&format=wav")
        self.assertEqual(response.status, 200)
        response.read(44)
        first = struct.pack("<4h", 100, 200, 300, 400)
        second = struct.pack("<4h", 500, 600, 700, 800)
        self.send_datagrams(self.datagrams(hello, second, sequence=2, position=2, samples=2))
        time.sleep(0.015)
        self.send_datagrams(self.datagrams(hello, first, sequence=1, position=0, samples=2))
        self.assertEqual(response.read(16), first + second)

    def test_invalid_control_request_does_not_remove_a_live_stream(self):
        publisher, _ = self.open_publisher()
        self.register_stream(publisher)
        publisher.send(json.dumps({"type": "unregister", "handle": 7}))
        self.assertEqual(json.loads(publisher.recv(timeout=3))["code"], "invalid_request_id")
        response = self.request("/bc/listen?publisher=station&stream=news&format=wav")
        self.assertEqual(response.status, 200)

    def test_all_layouts_fragmented_full_and_single_channel_fanout(self):
        publisher, hello = self.open_publisher()
        streams = []
        for handle, (layout, count, last) in enumerate(
            [("mono", 1, "M"), ("stereo", 2, "R"), ("5.1", 6, "Rs"), ("7.1", 8, "Rrb")], 1
        ):
            name = f"layout-{handle}"
            self.register_stream(publisher, name, handle, layout)
            full = self.request(f"/bc/listen?publisher=station&stream={name}&format=wav")
            channel = self.request(f"/bc/listen?publisher=station&stream={name}&format=wav&channel={last}")
            self.assertEqual(full.status, 200)
            header = full.read(68 if count > 2 else 44)
            self.assertEqual(int.from_bytes(header[22:24], "little"), count)
            if count > 2:
                self.assertEqual(int.from_bytes(header[40:44], "little"), 0x60F if count == 6 else 0x63F)
            self.assertEqual(int.from_bytes(channel.read(44)[22:24], "little"), 1)
            values = [handle * 1000 + index * 100 + frame for frame in range(160) for index in range(count)]
            payload = struct.pack(f"<{len(values)}h", *values)
            packets = self.datagrams(
                hello, payload, handle=handle, sequence=handle * 100, position=0, samples=160
            )
            self.send_datagrams(list(reversed(packets)) + [packets[0]])
            order = [0, 1, 2, 3, 6, 7, 4, 5] if count == 8 else list(range(count))
            expected = [values[frame * count + index] for frame in range(160) for index in order]
            streams.append(
                (
                    full,
                    channel,
                    struct.pack(f"<{len(expected)}h", *expected),
                    struct.pack("<160h", *values[count - 1 :: count]),
                )
            )
        for full, channel, expected, expected_channel in streams:
            self.assertEqual(full.read(len(expected)), expected)
            self.assertEqual(channel.read(len(expected_channel)), expected_channel)

    def test_forged_stale_replayed_and_late_udp_cannot_replace_live_audio(self):
        publisher, hello = self.open_publisher()
        self.register_stream(publisher)
        response = self.request("/bc/listen?publisher=station&stream=news&format=wav")
        response.read(44)
        first = struct.pack("<4h", 100, 200, 300, 400)
        valid = self.datagrams(hello, first, sequence=1, position=0, samples=2)
        self.send_datagrams(valid)
        self.assertEqual(response.read(8), first)
        fake = bytearray(self.datagrams(hello, first, sequence=2, position=2, samples=2)[0])
        fake[-1] ^= 1
        self.send_datagrams([bytes(fake), *valid])
        self.send_datagrams(self.datagrams(hello, first, sequence=3, position=2, samples=2, epoch=2))
        third = struct.pack("<4h", 500, 600, 700, 800)
        self.send_datagrams(self.datagrams(hello, third, sequence=50, position=4, samples=2))
        self.assertEqual(response.read(16), bytes(8) + third)
        self.send_datagrams(self.datagrams(hello, first, sequence=51, position=2, samples=2))
        self.send_datagrams(self.datagrams(hello, first, sequence=50, position=6, samples=2))
        self.send_datagrams(self.datagrams(hello, third, sequence=52, position=6, samples=2))
        self.assertEqual(response.read(8), third)

    def test_http_parameter_errors_and_offline_are_explicit(self):
        publisher, _ = self.open_publisher()
        self.register_stream(publisher)
        base = "/bc/listen?publisher=station&stream=news"
        for suffix in ("&channel=Lrb", "&format=mp3", "&format=wav&format=opus", "&unknown=1"):
            self.assertEqual(self.request(base + suffix).status, 400)
        self.assertEqual(self.request("/bc/listen?publisher=station&stream=missing").status, 404)
        self.assertEqual(self.request("/bc/listen?stream=news").status, 400)
        publisher.close()
        self.assertEqual(self.request(base).status, 503)

    def test_deleting_republished_stream_does_not_leave_an_old_offline_record(self):
        publisher, _ = self.open_publisher()
        self.register_stream(publisher)
        publisher.close()
        self.assertEqual(self.request("/bc/listen?publisher=station&stream=news").status, 503)
        replacement, _ = self.open_publisher()
        self.register_stream(replacement)
        replacement.send(json.dumps({"type": "unregister", "requestId": 20, "handle": 7}))
        self.assertEqual(json.loads(replacement.recv(timeout=3))["type"], "ack")
        self.assertEqual(self.request("/bc/listen?publisher=station&stream=news").status, 404)

    def test_slow_http_subscriber_does_not_delay_other_stream(self):
        publisher, hello = self.open_publisher()
        self.register_stream(publisher, "slow", 1, "7.1")
        self.register_stream(publisher, "fast", 2, "mono")
        host, port = self.address.rsplit(":", 1)
        slow_socket = socket.socket()
        self.addCleanup(slow_socket.close)
        slow_socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
        slow_socket.settimeout(3)
        slow_socket.connect((host, int(port)))
        slow = http.client.HTTPConnection(self.address, timeout=3)
        self.addCleanup(slow.close)
        slow.sock = slow_socket
        slow.request("GET", "/bc/listen?publisher=station&stream=slow&format=wav")
        slow_response = slow.getresponse()
        self.assertEqual(slow_response.status, 200)
        fast = self.request("/bc/listen?publisher=station&stream=fast&format=wav")
        fast.read(44)
        sequence = 1
        large = struct.pack("<7680h", *([100] * 7680))
        for block in range(80):
            packets = self.datagrams(
                hello, large, handle=1, sequence=sequence, position=block * 960, samples=960
            )
            sequence += len(packets)
            self.send_datagrams(packets)
            expected = struct.pack("<960h", *([block + 1] * 960))
            packets = self.datagrams(
                hello, expected, handle=2, sequence=sequence, position=block * 960, samples=960
            )
            sequence += len(packets)
            started = time.monotonic()
            self.send_datagrams(packets)
            self.assertEqual(fast.read(len(expected)), expected)
            self.assertLess(time.monotonic() - started, 1.0)
        # Recovering the deliberately stalled connection must reach EOF rather
        # than following a historical backlog forever. Other stream stayed live.
        try:
            slow_response.read(4 * 1024 * 1024)
        except http.client.IncompleteRead:
            pass  # Socket timeout may end a chunked response without its terminator.

    def test_all_opus_layouts_and_speaker_variants_preserve_encoded_packets(self):
        fixtures = json.loads((ROOT / "Develop/Broadcast/OpusFixtures.json").read_text())
        publisher, hello = self.open_publisher()
        sequence = 1
        mono = fixtures["streams"]["1"]
        for handle, (layout, roles) in enumerate(
            [
                ("mono", ["M"]),
                ("stereo", ["L", "R"]),
                ("5.1", ["L", "R", "C", "LFE", "Ls", "Rs"]),
                ("7.1", ["L", "R", "C", "LFE", "Ls", "Rs", "Lrb", "Rrb"]),
            ],
            1,
        ):
            fixture = fixtures["streams"][str(len(roles))]
            heads = {"all": fixture["head"], **{role: mono["head"] for role in roles}}
            self.register_stream(publisher, str(handle), handle, layout, opus_heads=heads)
            for index, channel in enumerate(["all", *roles]):
                response = self.request(
                    f"/bc/listen?publisher=station&stream={handle}&channel={channel}&format=opus"
                )
                self.assertEqual(response.status, 200)

                def read_page():
                    header = response.read(27)
                    self.assertEqual(header[:4], b"OggS")
                    lacing = response.read(header[26])
                    return response.read(sum(lacing))

                self.assertEqual(read_page(), base64.b64decode(heads[channel]))
                self.assertTrue(read_page().startswith(b"OpusTags"))
                payload = base64.b64decode((fixture if channel == "all" else mono)["packets"][index])
                variant = 0 if channel == "all" or len(roles) == 1 else index
                packets = self.datagrams(
                    hello,
                    payload,
                    handle=handle,
                    codec=2,
                    variant=variant,
                    sequence=sequence,
                    position=index * 960,
                    samples=960,
                )
                sequence += len(packets)
                self.send_datagrams(list(reversed(packets)))
                self.assertEqual(read_page(), payload)


class TlsRelayTests(unittest.TestCase):
    start_server = RelayTests.start_server
    stop_server = RelayTests.stop_server

    def setUp(self):
        self.start_server(tls=True)

    def test_https_and_wss_use_a_verified_certificate(self):
        connection = http.client.HTTPSConnection(self.address, timeout=3, context=self.tls_context)
        self.addCleanup(connection.close)
        connection.request("GET", "/bc/listen?publisher=station&stream=missing&format=wav")
        self.assertEqual(connection.getresponse().status, 404)
        with connect(
            f"wss://{self.address}/bc/publish?publisher=station",
            ssl=self.tls_context,
            additional_headers={"Authorization": "Bearer test-publish-secret"},
            proxy=None,
        ) as publisher:
            self.assertEqual(json.loads(publisher.recv(timeout=3))["type"], "hello")


if __name__ == "__main__":
    unittest.main()
