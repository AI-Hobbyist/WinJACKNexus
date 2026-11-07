"""Native encoder output through real UDP/HTTP and an independent decoder."""

import base64
import json
import struct
import subprocess
import threading
import time
import unittest

import ListenTests as listen
import RelayTests as relay


class EncoderTests(unittest.TestCase):
    setUp = relay.RelayTests.setUp
    start_server = relay.RelayTests.start_server
    stop_server = relay.RelayTests.stop_server
    open_publisher = relay.RelayTests.open_publisher
    datagrams = relay.RelayTests.datagrams
    send_datagrams = relay.RelayTests.send_datagrams

    def test_native_stereo_encoder_produces_full_and_independent_mono_opus(self):
        self.check_encoded_stream(
            "stereo", 48000, [("wav", "all", [330, 440]), ("opus", "all", [330, 440]), ("opus", "R", [440])]
        )

    def test_native_mono_and_surround_encoders_preserve_speaker_identity(self):
        for layout, expected, last in [
            ("mono", [220], "M"),
            ("5.1", [330, 440, 550, 60, 770, 880], "Rs"),
            ("7.1", [330, 440, 550, 60, 990, 1100, 770, 880], "Rrb"),
        ]:
            with self.subTest(layout=layout):
                last_frequency = 220 if layout == "mono" else 880 if layout == "5.1" else 1100
                self.check_encoded_stream(
                    layout,
                    48000,
                    [("wav", "all", expected), ("opus", "all", expected), ("opus", last, [last_frequency])],
                )

    def test_streaming_resampler_preserves_the_source_clock_and_opus_speakers(self):
        for layout, rate, expected in [
            ("mono", 8000, [220]),
            ("stereo", 44100, [330, 440]),
            ("7.1", 192000, [330, 440, 550, 60, 990, 1100, 770, 880]),
        ]:
            with self.subTest(layout=layout, rate=rate):
                self.check_encoded_stream(layout, rate, [("wav", "all", expected), ("opus", "all", expected)])

    def test_downsampling_removes_an_above_nyquist_tone_instead_of_aliasing_it(self):
        self.check_encoded_stream("mono", 96000, [("wav", "all", [30000]), ("opus", "all", [0])], tone=30000)

    def check_encoded_stream(self, layout, rate, cases, tone=None):
        arguments = [str(relay.ROOT / "build/release/bin/wjn_bc_encoder_probe.exe"), layout, str(rate)]
        if tone is not None:
            arguments.append(str(tone))
        encoded = subprocess.run(
            arguments, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20
        )
        self.assertEqual(encoded.returncode, 0, encoded.stderr)
        media = json.loads(encoded.stdout)
        publisher_id = f"enc_{layout.replace('.', '_')}_{rate}"
        publisher, hello = self.open_publisher(publisher_id)
        publisher.send(
            json.dumps(
                {
                    "type": "register",
                    "requestId": 7,
                    "streamId": "news",
                    "handle": 7,
                    "epoch": 1,
                    "displayName": "Native encoder",
                    "category": [],
                    "layout": layout,
                    "pcmRate": rate,
                    "opusHeads": media["opusHeads"],
                }
            )
        )
        self.assertEqual(json.loads(publisher.recv(timeout=3)), {"type": "ack", "requestId": 7})
        processes = []
        for index, (codec, channel, frequencies) in enumerate(cases):
            output = relay.Path(self.temporary.name) / f"decoded-{index}.f32"
            frames = int((rate if codec == "wav" else 48000) * 0.2)
            process = subprocess.Popen(
                [
                    str(relay.ROOT / "build/bc-target/release/bc-listen.exe"),
                    "--url",
                    f"http://{self.address}/bc/listen?publisher={publisher_id}&stream=news&format={codec}&channel={channel}",
                    "--check",
                    "--frames",
                    str(frames),
                    "--pcm-out",
                    str(output),
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            processes.append((process, output, codec, frequencies, frames))

        def cleanup():
            for process, *_ in processes:
                if process.poll() is None:
                    process.kill()
                process.wait(timeout=3)
                process.stdout.close()
                process.stderr.close()

        self.addCleanup(cleanup)
        started = time.monotonic() + 0.3

        def send():
            sequence = 1
            for block in media["blocks"]:
                time.sleep(max(0, started + block["at"] - time.monotonic()))
                packets = self.datagrams(
                    hello,
                    base64.b64decode(block["payload"]),
                    codec=block["codec"],
                    variant=block["variant"],
                    position=block["position"],
                    samples=block["samples"],
                    sequence=sequence,
                )
                self.send_datagrams(packets)
                sequence += len(packets)

        sender = threading.Thread(target=send)
        sender.start()
        self.addCleanup(lambda: sender.join(timeout=3))
        for process, output, codec, expected, frames in processes:
            stdout, stderr = process.communicate(timeout=10)
            self.assertEqual(process.returncode, 0, stderr)
            report = json.loads(stdout)
            self.assertEqual((report["channels"], report["frames"]), (len(expected), frames))
            self.assertEqual(report["sampleRate"], rate if codec == "wav" else 48000)
            raw = output.read_bytes()
            samples = struct.unpack("<" + "f" * (len(raw) // 4), raw)
            sample_rate = rate if codec == "wav" else 48000
            for channel, frequency in enumerate(expected):
                signal = samples[channel :: len(expected)]
                if frequency == 0:
                    peak = max(abs(sample) for sample in signal[960:])
                    print(f"Above-Nyquist steady output peak: {peak:.8f}", flush=True)
                    self.assertLess(peak, 0.002)
                    continue
                # Use the original source clock for WAV correlation.
                candidates = set([60, 220, 330, 440, 550, 770, 880, 990, 1100] + expected)
                powers = {
                    candidate: listen.tone_power(signal, candidate * 48000 / sample_rate)
                    for candidate in candidates
                }
                self.assertEqual(max(powers, key=powers.get), frequency)
                self.assertGreater(powers[frequency], 0.0001)


if __name__ == "__main__":
    unittest.main()
