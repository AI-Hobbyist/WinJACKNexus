"""Generate deterministic standard Opus fixtures with distinct speaker tones."""

import base64
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
FFMPEG = Path("D:/Program Files/ffmpeg/bin/ffmpeg.exe")
FREQUENCIES = {
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
LAYOUTS = {
    "mono": ("mono", ["M"]),
    "stereo": ("stereo", ["L", "R"]),
    # FFmpeg/libopus family 1 names its two 5.1 surrounds BL/BR (layout 5.1).
    # These occupy the project's Ls/Rs positions in the six-channel stream.
    "5.1": ("5.1", ["L", "R", "C", "LFE", "Ls", "Rs"]),
    "7.1": ("7.1", ["L", "R", "C", "LFE", "Lrb", "Rrb", "Ls", "Rs"]),
}


def packets(data):
    offset = 0
    current = bytearray()
    result = []
    while offset < len(data):
        assert data[offset : offset + 4] == b"OggS"
        count = data[offset + 26]
        lacing = data[offset + 27 : offset + 27 + count]
        offset += 27 + count
        for size in lacing:
            current.extend(data[offset : offset + size])
            offset += size
            if size < 255:
                result.append(base64.b64encode(current).decode())
                current.clear()
    assert not current
    return {"head": result[0], "packets": result[2:14]}


def encode(layout, roles, amplitude=0.1):
    expressions = "|".join(f"{amplitude}*sin(2*PI*{FREQUENCIES[role]}*t)" for role in roles)
    command = [
        str(FFMPEG),
        "-hide_banner",
        "-loglevel",
        "error",
        "-f",
        "lavfi",
        "-i",
        f"aevalsrc={expressions}:s=48000:d=0.4:c={layout}",
        "-c:a",
        "libopus",
        "-b:a",
        str(64000 * len(roles)),
        "-frame_duration",
        "20",
        "-mapping_family",
        "1" if len(roles) > 2 else "0",
        "-f",
        "ogg",
        "pipe:1",
    ]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors="replace"))
    print(f"Encoded {layout}: {roles}", flush=True)
    return packets(result.stdout)


if __name__ == "__main__":
    fixtures = {
        "source": subprocess.check_output([str(FFMPEG), "-version"], text=True).splitlines()[0],
        "recipe": "aevalsrc amplitude 0.1; 48kHz; 0.4s; libopus 64kbps/channel; 20ms; first 12 packets",
        "frequencies": FREQUENCIES,
        "full": {name: encode(layout, roles) for name, (layout, roles) in LAYOUTS.items()},
        "mono": {role: encode("mono", [role]) for role in FREQUENCIES},
        "silence": encode("stereo", ["L", "R"], amplitude=0),
    }
    (ROOT / "Develop/Broadcast/ListenSignals.json").write_text(json.dumps(fixtures, indent=2) + "\n")
