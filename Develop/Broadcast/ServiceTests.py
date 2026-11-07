"""Actual process-global worker consuming two realtime audio producers."""

import ctypes
import json
import http.client
import struct
import subprocess
import unittest
import uuid

import ListenTests as listen
import RelayTests as relay


class ServiceTests(unittest.TestCase):
    setUp = relay.RelayTests.setUp
    start_server = relay.RelayTests.start_server
    stop_server = relay.RelayTests.stop_server

    def start_probe(self, mode=None):
        target = "WinJACKNexus/Broadcast/ServiceTest-" + str(uuid.uuid4())
        arguments = [
            str(relay.ROOT / "build/release/bin/wjn_bc_service_probe.exe"),
            f"http://{self.address}/bc",
            "station",
            target,
        ]
        if mode:
            arguments.append(mode)
        probe = subprocess.Popen(
            arguments,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        def cleanup():
            if probe.poll() is None:
                probe.kill()
            probe.wait(timeout=5)
            probe.stdout.close()
            probe.stderr.close()
            delete = ctypes.WinDLL("advapi32", use_last_error=True).CredDeleteW
            delete.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32]
            delete(target, 1, 0)

        self.addCleanup(cleanup)
        probe.stdin.write("test-publish-secret\n")
        probe.stdin.close()
        probe.stdin = None
        return probe

    def test_stop_restart_and_owner_removal_keep_local_monitor_running(self):
        probe = self.start_probe("lifecycle")
        first = json.loads(probe.stdout.readline())
        stopped = json.loads(probe.stdout.readline())
        self.assertEqual(stopped, {"state": "stopped", "monitorAlive": True})
        restarted = json.loads(probe.stdout.readline())
        self.assertEqual(restarted, first)
        removed = json.loads(probe.stdout.readline())
        self.assertEqual(removed, {"state": "removed", "activeStreams": 1})
        connection = http.client.HTTPConnection(self.address, timeout=2)
        self.addCleanup(connection.close)
        connection.request(
            "GET",
            f"/bc/listen?publisher=station&stream={first['streams'][0]}&format=wav",
        )
        self.assertEqual(connection.getresponse().status, 404)
        remaining = http.client.HTTPConnection(self.address, timeout=2)
        self.addCleanup(remaining.close)
        remaining.request(
            "GET",
            f"/bc/listen?publisher=station&stream={first['streams'][1]}&format=wav",
        )
        response = remaining.getresponse()
        self.assertEqual(response.status, 200)
        self.assertEqual(response.read(4), b"RIFF")
        stdout, stderr = probe.communicate(timeout=8)
        self.assertEqual(probe.returncode, 0, stderr)
        self.assertNotIn("test-publish-secret", stdout + stderr)

    def test_connection_loss_does_not_stop_local_audio_processing(self):
        probe = self.start_probe("disconnect")
        ready = json.loads(probe.stdout.readline())
        self.assertEqual(ready["state"], "publishing")
        self.stop_server()
        stdout, stderr = probe.communicate(timeout=5)
        self.assertEqual(probe.returncode, 0, stderr)
        self.assertEqual(
            json.loads(stdout), {"state": "retrying", "monitorAlive": True}
        )

    def test_two_audio_producers_share_one_authenticated_publishing_worker(self):
        probe = self.start_probe()
        ready = json.loads(probe.stdout.readline())
        self.assertEqual(ready["state"], "publishing")
        self.assertEqual(len(ready["streams"]), 2)
        for index, (stream, expected) in enumerate(
            zip(ready["streams"], [[220], [330, 440]])
        ):
            with self.subTest(stream=index):
                output = relay.Path(self.temporary.name) / f"worker-{index}.f32"
                result = subprocess.run(
                    [
                        str(relay.ROOT / "build/bc-target/release/bc-listen.exe"),
                        "--url",
                        f"http://{self.address}/bc/listen?publisher=station&stream={stream}&format=opus",
                        "--check",
                        "--frames",
                        "4800",
                        "--pcm-out",
                        str(output),
                    ],
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    timeout=10,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                report = json.loads(result.stdout)
                self.assertEqual(report["channels"], len(expected))
                raw = output.read_bytes()
                samples = struct.unpack("<" + "f" * (len(raw) // 4), raw)
                for channel, frequency in enumerate(expected):
                    signal = samples[channel :: len(expected)]
                    powers = {
                        candidate: listen.tone_power(signal, candidate)
                        for candidate in [220, 330, 440]
                    }
                    self.assertEqual(max(powers, key=powers.get), frequency)
                    self.assertGreater(powers[frequency], 0.0001)
        stdout, stderr = probe.communicate(timeout=8)
        self.assertEqual(probe.returncode, 0, stderr)
        self.assertNotIn("test-publish-secret", stdout + stderr)

    def test_four_layouts_full_and_every_channel_in_both_formats(self):
        roles = ["L", "R", "C", "LFE", "Ls", "Rs", "Lrb", "Rrb"]
        frequencies = [330, 440, 550, 60, 770, 880, 990, 1100]
        for layout, channels in [("mono", 1), ("stereo", 2), ("5.1", 6), ("7.1", 8)]:
            with self.subTest(layout=layout):
                probe = self.start_probe(layout)
                ready = json.loads(probe.stdout.readline())
                stream = ready["streams"][0]
                names = ["M"] if channels == 1 else roles[:channels]
                tones = [220] if channels == 1 else frequencies[:channels]
                for encoding in ["wav", "opus"]:
                    for selection in ["all"] + names:
                        expected = (
                            tones
                            if selection == "all"
                            else [tones[names.index(selection)]]
                        )
                        # CLI exposes Windows speaker order for 7.1 full streams.
                        if channels == 8 and selection == "all":
                            expected = [
                                tones[index] for index in [0, 1, 2, 3, 6, 7, 4, 5]
                            ]
                        output = relay.Path(self.temporary.name) / "worker-layout.f32"
                        result = subprocess.run(
                            [
                                str(
                                    relay.ROOT / "build/bc-target/release/bc-listen.exe"
                                ),
                                "--url",
                                f"http://{self.address}/bc/listen?publisher=station&stream={stream}&format={encoding}&channel={selection}",
                                "--check",
                                "--frames",
                                "2400",
                                "--pcm-out",
                                str(output),
                            ],
                            text=True,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            timeout=5,
                        )
                        self.assertEqual(result.returncode, 0, result.stderr)
                        self.assertEqual(
                            json.loads(result.stdout)["channels"], len(expected)
                        )
                        raw = output.read_bytes()
                        samples = struct.unpack("<" + "f" * (len(raw) // 4), raw)
                        for channel, frequency in enumerate(expected):
                            signal = samples[channel :: len(expected)]
                            powers = {
                                tone: listen.tone_power(signal, tone) for tone in tones
                            }
                            self.assertEqual(
                                max(powers, key=powers.get),
                                frequency,
                                (layout, encoding, selection, channel),
                            )
                            self.assertGreater(powers[frequency], 0.0001)
                stdout, stderr = probe.communicate(timeout=8)
                self.assertEqual(probe.returncode, 0, stderr)


if __name__ == "__main__":
    unittest.main()
