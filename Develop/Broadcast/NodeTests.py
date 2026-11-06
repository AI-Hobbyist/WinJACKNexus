"""Actual node DLL callbacks with offline JACK buffers and real UDP/HTTP."""

import ctypes
import json
import struct
import subprocess
import unittest
import uuid

import ListenTests as listen
import RelayTests as relay


class NodeTests(unittest.TestCase):
    setUp = relay.RelayTests.setUp
    start_server = relay.RelayTests.start_server
    stop_server = relay.RelayTests.stop_server

    def test_actual_node_dll_four_layouts_and_all_channels_reach_http(self):
        target = "WinJACKNexus/Broadcast/NodeTest-" + str(uuid.uuid4())
        probe = subprocess.Popen(
            [
                str(relay.ROOT / "build/release/bin/wjn_bc_node_probe.exe"),
                f"http://{self.address}/bc",
                "station",
                target,
            ],
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
        ready = json.loads(probe.stdout.readline())
        self.assertEqual(ready["state"], "publishing")
        self.assertEqual(ready["ports"], 34)
        roles = ["L", "R", "C", "LFE", "Ls", "Rs", "Lrb", "Rrb"]
        frequencies = [330, 440, 550, 60, 770, 880, 990, 1100]
        for stream, channels in zip(ready["streams"], [1, 2, 6, 8]):
            names = ["M"] if channels == 1 else roles[:channels]
            tones = [220] if channels == 1 else frequencies[:channels]
            for encoding in ["wav", "opus"]:
                for selection in ["all"] + names:
                    with self.subTest(
                        channels=channels, encoding=encoding, channel=selection
                    ):
                        expected = (
                            tones
                            if selection == "all"
                            else [tones[names.index(selection)]]
                        )
                        if channels == 8 and selection == "all":
                            expected = [
                                tones[index] for index in [0, 1, 2, 3, 6, 7, 4, 5]
                            ]
                        output = relay.Path(self.temporary.name) / "node-output.f32"
                        result = subprocess.run(
                            [
                                str(
                                    relay.ROOT / "build/bc-target/release/bc-listen.exe"
                                ),
                                "--url",
                                f"http://{self.address}/bc/listen?publisher=station&stream={stream}&format={encoding}&channel={selection}",
                                "--check",
                                "--frames",
                                "12000" if encoding == "opus" else "2400",
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
                            self.assertEqual(max(powers, key=powers.get), frequency)
                            peak = max(abs(value) for value in signal[-4800:])
                            # Source amplitude .2 and one -6.0206dB gain stage.
                            self.assertAlmostEqual(
                                peak,
                                0.1,
                                delta=0.008 if encoding == "opus" else 0.00004,
                            )
        stdout, stderr = probe.communicate(timeout=12)
        self.assertEqual(probe.returncode, 0, stderr)
        final = json.loads(stdout)
        self.assertTrue(final["monitorSilent"])
        self.assertGreater(final["callbacks"], 100)
        self.assertEqual(final["remainingPorts"], 0)
        self.assertEqual(final["remainingClients"], 0)
        self.assertNotIn("test-publish-secret", stdout + stderr)


if __name__ == "__main__":
    unittest.main()
