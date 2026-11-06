"""Create verified incremental client and independent Rust service archives."""

from __future__ import annotations

import hashlib
import argparse
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "build/release/bin"
QT = Path("C:/Qt/6.10.3/msvc2022_64")


def digest(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def make_archive(name: str, kind: str, sources: dict[str, Path]) -> None:
    payload = {
        destination: source.read_bytes() for destination, source in sources.items()
    }
    for destination, content in payload.items():
        if destination.endswith((".dll", ".exe")):
            offset = int.from_bytes(content[60:64], "little")
            if content[:2] != b"MZ" or content[offset : offset + 6] != b"PE\0\0d\x86":
                raise ValueError(f"Expected x64 Release PE input: {destination}")
        if destination.endswith(".json"):
            json.loads(content)
    revision = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True
    ).strip()
    manifest = {
        "kind": kind,
        "configuration": "Release",
        "architecture": "x64",
        "runtimeSourceCheckpoint": revision,
        "packagingScriptSha256": digest(Path(__file__).read_bytes()),
        "files": [
            {"path": path, "bytes": len(content), "sha256": digest(content)}
            for path, content in sorted(payload.items())
        ],
        "manualAcceptance": "SKIPPED (user instruction)",
        "realJackHardware": "not validated; callback tests use an offline boundary fixture",
    }
    output = ROOT / "build" / name
    with zipfile.ZipFile(
        output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as archive:
        for path, content in sorted(payload.items()):
            archive.writestr(path, content)
        archive.writestr(
            "manifest.json", json.dumps(manifest, ensure_ascii=False, indent=2)
        )
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:
            raise ValueError("ZIP CRC verification failed")
        if set(archive.namelist()) != set(payload) | {"manifest.json"}:
            raise ValueError("Unexpected archive contents")
        for record in manifest["files"]:
            if digest(archive.read(record["path"])) != record["sha256"]:
                raise ValueError(f"Archive hash mismatch: {record['path']}")
    checksum = digest(output.read_bytes())
    output.with_suffix(".zip.sha256").write_text(
        f"{checksum}  {output.name}\n", encoding="ascii"
    )
    print(
        f"PASS {name}: {len(payload)} files; CRC, exact contents and SHA-256 verified"
    )
    print(f"{output.stat().st_size} bytes; SHA-256 {checksum}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", default="20261107")
    parser.add_argument(
        "--component", choices=["all", "client", "server", "listener"], default="all"
    )
    arguments = parser.parse_args()
    common = {
        "运行与替换说明.md": ROOT / "Docs/Broadcast/运行与替换说明.md",
        "licenses/WinJACKNexus-AGPL-3.0.txt": ROOT / "LICENSE",
    }
    codec_license = ROOT / "build/release/_deps/bc_opus-src/COPYING"
    overlay = dict(common)
    for path in ["broadcast_runtime.dll", "nodes/bc/mod_broadcast.dll"]:
        overlay[path] = RUNTIME / path
    for language in ["manifest", "en-US", "zh-CN", "ja-JP", "ko-KR"]:
        path = f"nodes/bc/locate/{language}.json"
        overlay[path] = RUNTIME / path
    overlay.update(
        {
            "Qt6Network.dll": QT / "bin/Qt6Network.dll",
            "Qt6WebSockets.dll": QT / "bin/Qt6WebSockets.dll",
            "tls/qschannelbackend.dll": QT / "plugins/tls/qschannelbackend.dll",
            "licenses/broadcast/libopus-COPYING.txt": codec_license,
            "licenses/broadcast/libsamplerate-COPYING.txt": ROOT
            / "build/release/_deps/bc_samplerate-src/COPYING",
            "licenses/broadcast/Qt-LGPL-3.0.txt": ROOT
            / "Docs/Broadcast/licenses/LGPL-3.0-only.txt",
            "licenses/broadcast/Qt-GPL-3.0.txt": ROOT
            / "Docs/Broadcast/licenses/GPL-3.0-only.txt",
            "licenses/broadcast/Qt-distribution-notice.txt": ROOT
            / "Docs/Broadcast/Third-party-notices.txt",
        }
    )
    if arguments.component in {"all", "client"}:
        make_archive(
            f"Broadcast-Client-Overlay-{arguments.label}.zip",
            "incremental-client-overlay",
            overlay,
        )
    service_common = dict(common)
    service_common["Cargo.lock"] = ROOT / "bc/Cargo.lock"
    if arguments.component in {"all", "server"}:
        make_archive(
            f"Broadcast-Server-{arguments.label}.zip",
            "independent-relay",
            {
                **service_common,
                "bc-server.exe": ROOT / "build/bc-target/release/bc-server.exe",
                "README.md": ROOT / "bc/server/README.md",
                "server.example.json": ROOT / "bc/server/server.example.json",
                "nginx.example.conf": ROOT / "bc/server/nginx.example.conf",
            },
        )
    if arguments.component in {"all", "listener"}:
        make_archive(
            f"Broadcast-Listener-{arguments.label}.zip",
            "independent-listener",
            {
                **service_common,
                "bc-listen.exe": ROOT / "build/bc-target/release/bc-listen.exe",
                "README.md": ROOT / "bc/listen/README.md",
                "licenses/libopus-COPYING.txt": codec_license,
            },
        )


if __name__ == "__main__":
    main()
