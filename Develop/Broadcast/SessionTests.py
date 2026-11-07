"""Real Qt publishing control session against the standalone Rust relay."""

import json
import subprocess
import unittest

import RelayTests as relay

PROBE = relay.ROOT / "build/release/bin/wjn_bc_session_probe.exe"


class SessionTests(unittest.TestCase):
    setUp = relay.RelayTests.setUp
    start_server = relay.RelayTests.start_server
    stop_server = relay.RelayTests.stop_server
    open_publisher = relay.RelayTests.open_publisher

    def test_cpp_udp_sender_reaches_real_http_wav_and_opus_listeners(self):
        fixtures = json.loads((relay.ROOT / "Develop/Broadcast/ListenSignals.json").read_text())
        roles = ["L", "R", "C", "LFE", "Ls", "Rs", "Lrb", "Rrb"]
        full = fixtures["full"]["7.1"]
        specification = {
            "registration": {
                "type": "register",
                "requestId": 7,
                "streamId": "news",
                "handle": 7,
                "epoch": 1,
                "displayName": "7.1 sender",
                "category": [],
                "layout": "7.1",
                "pcmRate": 48000,
                "opusHeads": {
                    "all": full["head"],
                    **{role: fixtures["mono"][role]["head"] for role in roles},
                },
            },
            "variants": {
                "0": full["packets"],
                **{str(index + 1): fixtures["mono"][role]["packets"] for index, role in enumerate(roles)},
            },
        }
        fixture_path = relay.Path(self.temporary.name) / "media.json"
        fixture_path.write_text(json.dumps(specification))
        probe = subprocess.Popen(
            [
                str(relay.ROOT / "build/release/bin/wjn_bc_sender_probe.exe"),
                f"http://{self.address}/bc",
                "station",
                str(fixture_path),
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        def cleanup():
            if probe.poll() is None:
                probe.terminate()
            probe.wait(timeout=5)
            probe.stdout.close()
            probe.stderr.close()

        self.addCleanup(cleanup)
        probe.stdin.write("test-publish-secret\n")
        probe.stdin.close()
        probe.stdin = None
        self.assertEqual(json.loads(probe.stdout.readline())["state"], "registered")
        listener = relay.ROOT / "build/bc-target/release/bc-listen.exe"
        for codec, channel, channels in [("wav", "all", 8), ("opus", "all", 8), ("opus", "Rrb", 1)]:
            with self.subTest(codec=codec, channel=channel):
                result = subprocess.run(
                    [
                        str(listener),
                        "--url",
                        f"http://{self.address}/bc/listen?publisher=station&stream=news&format={codec}&channel={channel}",
                        "--check",
                        "--frames",
                        "4800",
                    ],
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    timeout=10,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                report = json.loads(result.stdout)
                self.assertEqual((report["channels"], report["frames"]), (channels, 4800))
                if codec == "wav":
                    self.assertEqual(
                        report["peaks"], [1 / 32, 2 / 32, 3 / 32, 4 / 32, 7 / 32, 8 / 32, 5 / 32, 6 / 32]
                    )
                else:
                    self.assertTrue(all(peak > 0.02 for peak in report["peaks"]))

    def test_wss_validates_certificates_and_accepts_a_process_local_test_ca(self):
        self.stop_server()
        self.start_server(tls=True)
        arguments = [str(PROBE), f"https://{self.address}/bc", "station"]
        rejected = subprocess.run(
            arguments,
            input="test-publish-secret\n",
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
        )
        self.assertNotEqual(rejected.returncode, 0)
        self.assertEqual(json.loads(rejected.stdout)["state"], "error")
        trusted = subprocess.run(
            arguments + [str(relay.Path(self.temporary.name) / "test.pem")],
            input="test-publish-secret\n",
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
        )
        self.assertEqual(trusted.returncode, 0, trusted.stderr)
        self.assertEqual(json.loads(trusted.stdout)["state"], "online")

    def test_publish_control_negotiates_authenticated_media_session(self):
        result = subprocess.run(
            [str(PROBE), f"http://{self.address}/bc", "station"],
            input="test-publish-secret\n",
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report["state"], "online")
        self.assertEqual(report["udpPort"], int(self.udp_address.rsplit(":", 1)[1]))
        self.assertEqual(report["maxDatagram"], 1200)
        self.assertNotIn("test-publish-secret", result.stdout + result.stderr)
        self.assertNotIn("key", report)

    def test_wrong_token_is_a_terminal_authentication_error(self):
        result = subprocess.run(
            [str(PROBE), f"http://{self.address}/bc", "station"],
            input="wrong-secret\n",
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(json.loads(result.stdout)["state"], "authentication-error", result.stderr)
        self.assertNotIn("wrong-secret", result.stdout + result.stderr)

    def test_occupied_namespace_is_rejected_without_identity_takeover(self):
        publisher, _ = self.open_publisher()
        result = subprocess.run(
            [str(PROBE), f"http://{self.address}/bc", "station"],
            input="test-publish-secret\n",
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(json.loads(result.stdout)["state"], "identity-conflict")
        publisher.ping().wait(timeout=2)


if __name__ == "__main__":
    unittest.main()
