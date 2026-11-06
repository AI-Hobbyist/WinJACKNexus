"""Actual HTTP relay -> standard audio decoder CLI; no JACK/device required."""

import json
import struct
import subprocess
import threading
import time
import unittest

import RelayTests as relay
import base64
import math

LISTENER = relay.ROOT / "build/bc-target/release/bc-listen.exe"


class ListenTests(unittest.TestCase):
    setUp = relay.RelayTests.setUp
    start_server = relay.RelayTests.start_server
    stop_server = relay.RelayTests.stop_server
    open_publisher = relay.RelayTests.open_publisher
    register_stream = relay.RelayTests.register_stream
    datagrams = relay.RelayTests.datagrams
    send_datagrams = relay.RelayTests.send_datagrams

    def test_pcm_loss_inserts_silence_at_the_missing_sample_positions(self):
        publisher, hello = self.open_publisher()
        self.register_stream(publisher, layout="mono")
        output = relay.ROOT / "build/bc-s2-pcm-gap.f32"

        def send():
            time.sleep(0.3)
            for block in (0, 2):
                self.send_datagrams(
                    self.datagrams(
                        hello,
                        struct.pack("<480h", *([8192] * 480)),
                        sequence=block + 1,
                        position=block * 480,
                        samples=480,
                    )
                )
                time.sleep(0.01)

        sender = threading.Thread(target=send)
        sender.start()
        try:
            result = subprocess.run(
                [
                    str(LISTENER),
                    "--url",
                    f"http://{self.address}/bc/listen?publisher=station&stream=news&format=wav",
                    "--check",
                    "--frames",
                    "1440",
                    "--pcm-out",
                    str(output),
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                timeout=10,
            )
        finally:
            sender.join(timeout=3)
        self.assertEqual(result.returncode, 0, result.stdout)
        samples = struct.unpack("<1440f", output.read_bytes())
        self.assertEqual(samples, (0.25,) * 480 + (0.0,) * 480 + (0.25,) * 480)

    def test_ffmpeg_decodes_both_indefinite_live_formats(self):
        fixtures = json.loads((relay.ROOT / "Develop/Broadcast/ListenSignals.json").read_text())
        roles = ["L", "R", "C", "LFE", "Ls", "Rs", "Lrb", "Rrb"]
        full = fixtures["full"]["7.1"]
        publisher, hello = self.open_publisher()
        heads = {"all": full["head"], **{role: fixtures["mono"][role]["head"] for role in roles}}
        self.register_stream(publisher, layout="7.1", opus_heads=heads)
        stop = threading.Event()

        def send():
            pcm = struct.pack("<7680h", *([1024] * 7680))
            for block in range(1500):
                if stop.wait(0.02):
                    break
                self.send_datagrams(
                    self.datagrams(hello, pcm, sequence=block * 64 + 1, position=block * 960, samples=960)
                )
                payload = base64.b64decode(full["packets"][block % len(full["packets"])])
                self.send_datagrams(
                    self.datagrams(
                        hello, payload, codec=2, sequence=block * 64 + 32, position=block * 960, samples=960
                    )
                )

        sender = threading.Thread(target=send)
        sender.start()
        try:
            for codec in ("wav", "opus"):
                with self.subTest(codec=codec):
                    result = subprocess.run(
                        [
                            "D:/Program Files/ffmpeg/bin/ffmpeg.exe",
                            "-nostdin",
                            "-hide_banner",
                            "-loglevel",
                            "error",
                            "-probesize",
                            "32",
                            "-analyzeduration",
                            "0",
                            "-i",
                            f"http://{self.address}/bc/listen?publisher=station&stream=news&format={codec}",
                            "-t",
                            "2",
                            "-c:a",
                            "pcm_f32le",
                            "-f",
                            "f32le",
                            "pipe:1",
                        ],
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        timeout=15,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
                    self.assertEqual(len(result.stdout), 96000 * 8 * 4)
                    print(f"FFmpeg {codec}: 96000 frames, 8 channels from live HTTP", flush=True)
        finally:
            stop.set()
            sender.join(timeout=3)

    def test_all_layouts_and_subchannels_decode_distinct_signals(self):
        fixtures = json.loads((relay.ROOT / "Develop/Broadcast/ListenSignals.json").read_text())
        roles_by_layout = {
            "mono": ["M"],
            "stereo": ["L", "R"],
            "5.1": ["L", "R", "C", "LFE", "Ls", "Rs"],
            "7.1": ["L", "R", "C", "LFE", "Ls", "Rs", "Lrb", "Rrb"],
        }
        frequencies = {
            "M": 220,
            "L": 330,
            "R": 440,
            "C": 550,
            "LFE": 60,
            "Ls": 770,
            "Rs": 880,
            "Lrb": 990,
            "Rrb": 1100,
        }
        publisher, hello = self.open_publisher()
        for handle, (layout, roles) in enumerate(roles_by_layout.items(), 30):
            full = fixtures["full"][layout]
            stream = "signals_" + layout.replace(".", "_")
            heads = {"all": full["head"], **{role: fixtures["mono"][role]["head"] for role in roles}}
            self.register_stream(publisher, stream=stream, handle=handle, layout=layout, opus_heads=heads)
            stop = threading.Event()

            def send():
                values = [1024 * (index + 1) for index in range(len(roles))]
                pcm = struct.pack("<" + "h" * len(roles) * 960, *(values * 960))
                for block in range(2000):
                    if stop.wait(0.02):
                        break
                    sequence = handle * 1000000 + block * 256
                    self.send_datagrams(
                        self.datagrams(
                            hello, pcm, handle=handle, sequence=sequence, position=block * 960, samples=960
                        )
                    )
                    variants = [(0, full)] + [
                        (index + 1, fixtures["mono"][role]) for index, role in enumerate(roles)
                    ]
                    for variant, fixture in variants:
                        # Mono all/M is the same registered variant.
                        if len(roles) == 1 and variant == 1:
                            continue
                        payload = base64.b64decode(fixture["packets"][block % len(fixture["packets"])])
                        self.send_datagrams(
                            self.datagrams(
                                hello,
                                payload,
                                codec=2,
                                variant=variant,
                                handle=handle,
                                sequence=sequence + 32 + variant * 16,
                                position=block * 960,
                                samples=960,
                            )
                        )

            sender = threading.Thread(target=send)
            sender.start()
            try:
                for codec in ("wav", "opus"):
                    for channel in ["all", *roles]:
                        with self.subTest(layout=layout, codec=codec, channel=channel):
                            output = relay.ROOT / "build/bc-s2-signal.f32"
                            result = subprocess.run(
                                [
                                    str(LISTENER),
                                    "--url",
                                    f"http://{self.address}/bc/listen?publisher=station&stream={stream}&format={codec}&channel={channel}",
                                    "--check",
                                    "--frames",
                                    "4800",
                                    "--pcm-out",
                                    str(output),
                                ],
                                text=True,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT,
                                timeout=10,
                            )
                            self.assertEqual(result.returncode, 0, result.stdout)
                            report = json.loads(result.stdout)
                            order = (
                                (
                                    ["L", "R", "C", "LFE", "Lrb", "Rrb", "Ls", "Rs"]
                                    if layout == "7.1"
                                    else roles
                                )
                                if channel == "all"
                                else [channel]
                            )
                            self.assertEqual((report["channels"], report["frames"]), (len(order), 4800))
                            raw = output.read_bytes()
                            samples = struct.unpack("<" + "f" * (len(raw) // 4), raw)
                            self.assertEqual(len(samples), 4800 * len(order))
                            for index, role in enumerate(order):
                                signal = samples[index :: len(order)]
                                if codec == "wav":
                                    value = (roles.index(role) + 1) / 32
                                    self.assertTrue(all(sample == value for sample in signal))
                                else:
                                    powers = {
                                        frequency: tone_power(signal, frequency)
                                        for frequency in frequencies.values()
                                    }
                                    self.assertEqual(max(powers, key=powers.get), frequencies[role])
                                    self.assertGreater(powers[frequencies[role]], 0.0001)
            finally:
                stop.set()
                sender.join(timeout=3)

    def test_unknown_output_device_is_reported_without_fallback(self):
        publisher, _ = self.open_publisher()
        self.register_stream(publisher)
        result = subprocess.run(
            [
                str(LISTENER),
                "--url",
                f"http://{self.address}/bc/listen?publisher=station&stream=news&format=wav",
                "--device",
                "missing-device-for-bc-test",
                "--frames",
                "960",
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=10,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("output device not found", result.stdout)

    def test_unsupported_eight_channel_device_layout_is_not_downmixed(self):
        devices = json.loads(subprocess.check_output([str(LISTENER), "--list-devices"], encoding="utf-8"))
        device = next(
            (
                device
                for device in devices
                if not any(
                    config["channels"] == 8
                    and config["sampleFormat"] == "f32"
                    and config["minimumRate"] <= 48000 <= config["maximumRate"]
                    for config in device["formats"]
                )
            ),
            None,
        )
        if device is None:
            self.skipTest("No endpoint lacking 8-channel float32 support is available")
        publisher, _ = self.open_publisher()
        self.register_stream(publisher, layout="7.1")
        result = subprocess.run(
            [
                str(LISTENER),
                "--url",
                f"http://{self.address}/bc/listen?publisher=station&stream=news&format=wav",
                "--device",
                device["id"],
                "--frames",
                "960",
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=10,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not support the original layout/rate", result.stdout)

    def test_reconnect_reads_new_headers_after_format_epoch_changes(self):
        publisher, hello = self.open_publisher()
        self.register_stream(publisher)
        process = subprocess.Popen(
            [
                str(LISTENER),
                "--url",
                f"http://{self.address}/bc/listen?publisher=station&stream=news&format=wav",
                "--check",
                "--reconnect",
                "--frames",
                "960",
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.addCleanup(lambda: process.poll() is None and process.kill())
        time.sleep(0.2)
        self.send_datagrams(self.datagrams(hello, bytes(480 * 4), sequence=1, position=0, samples=480))
        time.sleep(0.1)
        self.register_stream(publisher, layout="mono", epoch=2)
        stop = threading.Event()

        def send():
            for block in range(400):
                if stop.wait(0.01):
                    break
                self.send_datagrams(
                    self.datagrams(
                        hello,
                        struct.pack("<480h", *([8192] * 480)),
                        sequence=100 + block,
                        position=block * 480,
                        samples=480,
                        epoch=2,
                    )
                )

        sender = threading.Thread(target=send)
        sender.start()
        try:
            stdout, stderr = process.communicate(timeout=10)
        finally:
            stop.set()
            sender.join(timeout=3)
        self.assertEqual(process.returncode, 0, stderr)
        report = json.loads(stdout)
        self.assertEqual((report["channels"], report["frames"], report["peaks"]), (1, 960, [0.25]))

    def test_output_devices_can_be_inspected_without_a_server_url(self):
        result = subprocess.run(
            [str(LISTENER), "--list-devices"],
            encoding="utf-8",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=15,
        )
        self.assertEqual(result.returncode, 0, result.stdout[:2000])
        devices = json.loads(result.stdout)
        print(f"Enumerated {len(devices)} output devices", flush=True)
        self.assertIsInstance(devices, list)
        for device in devices:
            self.assertIsInstance(device["id"], str)
            self.assertIsInstance(device["name"], str)
            self.assertIn("formats", device)

    def test_live_wav_reaches_a_real_shared_output_callback(self):
        publisher, hello = self.open_publisher()
        fixture = json.loads((relay.ROOT / "Develop/Broadcast/ListenSignals.json").read_text())["silence"]
        mono_head = json.loads((relay.ROOT / "Develop/Broadcast/ListenSignals.json").read_text())["mono"][
            "M"
        ]["head"]
        self.register_stream(publisher, opus_heads={"all": fixture["head"], "L": mono_head, "R": mono_head})
        stop = threading.Event()

        def send():
            for block in range(300):
                if stop.wait(0.02):
                    break
                self.send_datagrams(
                    self.datagrams(
                        hello, bytes(960 * 2 * 2), sequence=block * 64 + 1, position=block * 960, samples=960
                    )
                )
                self.send_datagrams(
                    self.datagrams(
                        hello,
                        base64.b64decode(fixture["packets"][block % len(fixture["packets"])]),
                        codec=2,
                        sequence=block * 64 + 32,
                        position=block * 960,
                        samples=960,
                    )
                )

        sender = threading.Thread(target=send)
        sender.start()
        self.addCleanup(lambda: (stop.set(), sender.join(timeout=3)))
        for codec in ("wav", "opus"):
            self.check_device_playback(codec)

    def check_device_playback(self, codec):
        result = subprocess.run(
            [
                str(LISTENER),
                "--url",
                f"http://{self.address}/bc/listen?publisher=station&stream=news&format={codec}",
                "--frames",
                "9600",
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=15,
        )
        print(result.stdout, flush=True)
        self.assertEqual(result.returncode, 0)
        report = json.loads(result.stdout)
        self.assertEqual(report["playedFrames"], 9600)
        self.assertGreater(report["outputCallbacks"], 0)
        self.assertEqual(report["channels"], 2)
        self.assertEqual(report["sampleRate"], 48000)

    def test_wav_layouts_preserve_channel_identity(self):
        publisher, hello = self.open_publisher()
        layouts = {"mono": 1, "stereo": 2, "5.1": 6, "7.1": 8}
        for handle, (layout, channels) in enumerate(layouts.items(), 10):
            with self.subTest(layout=layout):
                stream_id = layout.replace(".", "_")
                self.register_stream(publisher, stream=stream_id, handle=handle, layout=layout)
                values = [1024 * (index + 1) for index in range(channels)]
                stop = threading.Event()

                def send():
                    payload = struct.pack("<" + "h" * channels * 480, *(values * 480))
                    for block in range(200):
                        if stop.wait(0.01):
                            break
                        self.send_datagrams(
                            self.datagrams(
                                hello,
                                payload,
                                handle=handle,
                                sequence=handle * 10000 + block * 16,
                                position=block * 480,
                                samples=480,
                            )
                        )

                sender = threading.Thread(target=send)
                sender.start()
                pcm_path = relay.ROOT / "build" / f"bc-s2-{stream_id}.f32"
                try:
                    result = subprocess.run(
                        [
                            str(LISTENER),
                            "--url",
                            f"http://{self.address}/bc/listen?publisher=station&stream={stream_id}&format=wav",
                            "--check",
                            "--frames",
                            "960",
                            "--pcm-out",
                            str(pcm_path),
                        ],
                        text=True,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                        timeout=10,
                    )
                finally:
                    stop.set()
                    sender.join(timeout=3)
                self.assertEqual(result.returncode, 0, result.stdout)
                report = json.loads(result.stdout)
                expected = values
                if layout == "7.1":
                    expected = [1024, 2048, 3072, 4096, 7168, 8192, 5120, 6144]
                self.assertEqual(report["peaks"], [value / 32768 for value in expected])
                samples = struct.unpack("<" + "f" * (960 * channels), pcm_path.read_bytes())
                self.assertEqual(samples, tuple(value / 32768 for value in expected) * 960)

    def test_check_mode_decodes_actual_live_http_wav(self):
        publisher, hello = self.open_publisher()
        self.register_stream(publisher)
        stop = threading.Event()
        self.addCleanup(stop.set)

        def send():
            payload = struct.pack("<960h", *([8192, -16384] * 480))
            for block in range(100):
                if stop.wait(0.01):
                    break
                self.send_datagrams(
                    self.datagrams(hello, payload, sequence=block * 2 + 1, position=block * 480, samples=480)
                )

        sender = threading.Thread(target=send)
        self.addCleanup(lambda: sender.join(timeout=3))
        sender.start()
        result = subprocess.run(
            [
                str(LISTENER),
                "--url",
                f"http://{self.address}/bc/listen?publisher=station&stream=news&format=wav",
                "--check",
                "--frames",
                "960",
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=10,
        )
        print(result.stdout, flush=True)
        stop.set()
        self.assertEqual(result.returncode, 0)
        report = json.loads(result.stdout)
        self.assertEqual((report["channels"], report["sampleRate"], report["frames"]), (2, 48000, 960))
        self.assertEqual(report["peaks"], [0.25, 0.5])

    def test_check_mode_decodes_real_http_ogg_opus(self):
        fixtures = json.loads((relay.ROOT / "Develop/Broadcast/OpusFixtures.json").read_text())
        fixture = fixtures["streams"]["2"]
        mono_head = fixtures["streams"]["1"]["head"]
        publisher, hello = self.open_publisher()
        self.register_stream(publisher, opus_heads={"all": fixture["head"], "L": mono_head, "R": mono_head})
        stop = threading.Event()

        def send():
            for block in range(100):
                if stop.wait(0.02):
                    break
                payload = base64.b64decode(fixture["packets"][block % len(fixture["packets"])])
                packets = self.datagrams(
                    hello, payload, codec=2, sequence=block * 8 + 1, position=block * 960, samples=960
                )
                self.send_datagrams(packets)

        sender = threading.Thread(target=send)

        def finish_sender():
            stop.set()
            sender.join(timeout=3)

        self.addCleanup(finish_sender)
        sender.start()
        result = subprocess.run(
            [
                str(LISTENER),
                "--url",
                f"http://{self.address}/bc/listen?publisher=station&stream=news&format=opus",
                "--check",
                "--frames",
                "4800",
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=15,
        )
        print(result.stdout, flush=True)
        self.assertEqual(result.returncode, 0)
        report = json.loads(result.stdout)
        self.assertEqual((report["channels"], report["sampleRate"], report["frames"]), (2, 48000, 4800))
        for peak in report["peaks"]:
            self.assertGreater(peak, 0.05)
            self.assertLess(peak, 0.2)

    def test_opus_loss_keeps_the_live_sample_timeline(self):
        fixture = json.loads((relay.ROOT / "Develop/Broadcast/OpusFixtures.json").read_text())["streams"]["2"]
        mono_head = json.loads((relay.ROOT / "Develop/Broadcast/OpusFixtures.json").read_text())["streams"][
            "1"
        ]["head"]
        publisher, hello = self.open_publisher()
        self.register_stream(publisher, opus_heads={"all": fixture["head"], "L": mono_head, "R": mono_head})

        def send():
            time.sleep(0.5)
            for block in (0, 2):
                payload = base64.b64decode(fixture["packets"][block])
                self.send_datagrams(
                    self.datagrams(
                        hello, payload, codec=2, sequence=block * 8 + 1, position=block * 960, samples=960
                    )
                )
                time.sleep(0.02)

        sender = threading.Thread(target=send)
        self.addCleanup(lambda: sender.join(timeout=3))
        sender.start()
        # 3 time slots minus the actual libopus pre-skip (312). Only two encoded
        # packets arrive; a decoder ignoring granules cannot produce this count.
        result = subprocess.run(
            [
                str(LISTENER),
                "--url",
                f"http://{self.address}/bc/listen?publisher=station&stream=news&format=opus",
                "--check",
                "--frames",
                "2568",
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=10,
        )
        print(result.stdout, flush=True)
        self.assertEqual(result.returncode, 0)
        report = json.loads(result.stdout)
        self.assertEqual(report["frames"], 2568)
        self.assertEqual(report["concealedFrames"], 960)


def tone_power(samples, frequency):
    """Independent tone correlation against the literal source frequencies."""
    angle = 2 * math.pi * frequency / 48000
    real = sum(sample * math.cos(angle * index) for index, sample in enumerate(samples))
    imaginary = sum(sample * math.sin(angle * index) for index, sample in enumerate(samples))
    return (real * real + imaginary * imaginary) / len(samples) ** 2


if __name__ == "__main__":
    unittest.main()
