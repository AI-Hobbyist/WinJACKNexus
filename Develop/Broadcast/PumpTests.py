"""Real worker/UDP/HTTP regression with 256-frame input and delayed timer wakes."""

import http.client
import json
import subprocess
import unittest

import RelayTests as relay


class PumpTests(unittest.TestCase):
    setUp = relay.RelayTests.setUp
    start_server = relay.RelayTests.start_server
    stop_server = relay.RelayTests.stop_server

    def test_delayed_wakes_drain_audio_without_losing_the_live_stream(self):
        with subprocess.Popen(
            [
                str(relay.ROOT / "build/release/bin/wjn_bc_pump_probe.exe"),
                f"http://{self.address}/bc",
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        ) as probe:
            probe.stdin.write("test-publish-secret\n")
            probe.stdin.close()
            probe.stdin = None
            ready = json.loads(probe.stdout.readline())
            connection = http.client.HTTPConnection(self.address, timeout=4)
            try:
                connection.request(
                    "GET",
                    f"/bc/listen?publisher=station&stream={ready['stream']}&format=wav",
                )
                response = connection.getresponse()
                status = response.status
                audio = response.read(44 + 4800 * 4)
            finally:
                connection.close()
            stdout, stderr = probe.communicate(timeout=6)
            self.assertEqual(probe.returncode, 0, stderr + stdout)
            self.assertEqual(status, 200)
            self.assertEqual(audio[:4], b"RIFF")
            self.assertEqual(len(audio), 44 + 4800 * 4)


if __name__ == "__main__":
    unittest.main()
