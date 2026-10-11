"""Validate the MIDI deployment without JACK or launching a host window."""

# SPDX-License-Identifier: AGPL-3.0-only
import hashlib
import json
from pathlib import Path
import struct
import sys


def imports(path):
    """Read standard PE import names without loading executable code."""
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe : pe + 4] != b"PE\0\0":
        raise ValueError(f"Not a PE image: {path}")
    sections, optional_size = struct.unpack_from("<H12xH", data, pe + 6)
    optional = pe + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    directories = optional + (112 if magic == 0x20B else 96)
    import_rva = struct.unpack_from("<I", data, directories + 8)[0]
    section_start = optional + optional_size

    def offset(rva):
        for index in range(sections):
            start = section_start + 40 * index
            size, address, raw_size, raw = struct.unpack_from("<IIII", data, start + 8)
            if address <= rva < address + max(size, raw_size):
                return raw + rva - address
        raise ValueError(f"Invalid RVA in {path}: {rva}")

    if not import_rva:
        return []
    cursor = offset(import_rva)
    names = []
    while any(data[cursor : cursor + 20]):
        name_rva = struct.unpack_from("<I", data, cursor + 12)[0]
        start = offset(name_rva)
        names.append(data[start : data.index(0, start)].decode("ascii").lower())
        cursor += 20
    return names


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    repository = Path(__file__).resolve().parents[2]
    runtime = repository / "build/release/bin"
    required = []
    for kind in ("sf2_loader", "midi_spilit", "midi_matrix"):
        folder = runtime / "nodes/midi_tools" / kind
        required.append(folder / f"mod_{kind}.dll")
        for language in ("en-US", "zh-CN", "ja-JP", "ko-KR", "manifest"):
            path = folder / "locate" / f"{language}.json"
            json.loads(path.read_text(encoding="utf-8"))
            required.append(path)
        for template in ("template", "template-simple-dev"):
            css = (
                repository
                / "themes"
                / template
                / "node"
                / (f"module-midi-tools-{kind.replace('_', '-')}.css")
            )
            if not css.is_file():
                raise ValueError(f"Missing node theme: {css}")
    notices = runtime / "licenses/midi-tools"
    sources = {
        "fluidsynth-2.5.7.tar.gz": "ce27840221ab00dd59bf27e85ecbba480c6c2a7c9fbec4243658f68f59c07f4a",
        "sfizz-1.2.3.tar.gz": "a9339eac7620d7f0f6b44bdfe860680fab73e66efad4b5f15b21198dd9436822",
        "gcem.zip": "28159274c54e9640354852e172d10d88eb159f4e7f2fea42edbcd20105ed3526",
    }
    for name, expected in sources.items():
        if digest(notices / name) != expected:
            raise ValueError(f"Corresponding-source checksum mismatch: {name}")
    required.extend(path for path in notices.rglob("*") if path.is_file())
    index = {path.name.lower(): path for path in runtime.rglob("*.dll")}
    checked = set()
    dependencies = {}
    queue = [path for path in required if path.suffix.lower() == ".dll"]
    while queue:
        path = queue.pop()
        if path in checked:
            continue
        checked.add(path)
        dependencies[str(path.relative_to(runtime))] = imports(path)
        for name in imports(path):
            if name in index:
                queue.append(index[name])
            elif name.startswith(("api-ms-", "ext-ms-")):
                continue
            elif not (Path("C:/Windows/System32") / name).is_file():
                raise ValueError(f"Unresolved runtime import: {path.name} -> {name}")
    required.extend(checked)
    manifest = {
        "files": {
            str(path.relative_to(runtime)).replace("\\", "/"): digest(path)
            for path in sorted(set(required))
        },
        "imports": dependencies,
        "boundary": "offline; actual JACK and manual deployment acceptance unverified/SKIPPED",
    }
    output = repository / "build/MidiTools-S6-deployment-manifest.json"
    output.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False), encoding="utf-8"
    )
    print(
        f"PASS {len(required)} deployment/license/source entries; {len(checked)} recursive DLLs"
    )
    print(output)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"FAIL {error}", file=sys.stderr)
        sys.exit(1)
