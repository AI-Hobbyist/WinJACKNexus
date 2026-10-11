"""Package this editable theme as a standard WinJACKNexus .wjnskin file."""

from __future__ import annotations

import json
import zipfile
from pathlib import Path

TEMPLATE = Path(__file__).resolve().parent
ALLOWED_ASSETS = {".png", ".jpg", ".jpeg", ".webp", ".ttf", ".otf"}


def entry(name: str, *, directory: bool = False) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.flag_bits = 0x800
    info.external_attr = (0o40755 if directory else 0o100644) << 16
    return info


def main() -> None:
    metadata_bytes = (TEMPLATE / "meta.json").read_bytes()
    metadata = json.loads(metadata_bytes.decode("utf-8"))
    theme_id = metadata["id"]
    if (
        not isinstance(theme_id, str)
        or not theme_id
        or any(
            character not in "abcdefghijklmnopqrstuvwxyz0123456789.-"
            for character in theme_id
        )
    ):
        raise ValueError("meta.json id must be a lowercase DNS-style name")
    style = (TEMPLATE / "style.css").read_bytes()
    modules = sorted(
        path
        for folder in ("host", "node", "native_fx")
        for path in (TEMPLATE / folder).glob("*.css")
    )
    assets = sorted(
        path
        for path in (TEMPLATE / "assets").rglob("*")
        if path.is_file() and path.name != ".gitkeep"
    )
    for asset in assets:
        if asset.suffix.lower() not in ALLOWED_ASSETS:
            raise ValueError(f"unsupported asset: {asset}")
        if asset.stat().st_size > 16 * 1024 * 1024:
            raise ValueError(f"asset exceeds 16 MiB: {asset}")
    destination = TEMPLATE.parent / f"{theme_id}.wjnskin"
    with zipfile.ZipFile(destination, "w") as archive:
        archive.writestr(entry("assets/", directory=True), b"")
        archive.writestr(entry("meta.json"), metadata_bytes)
        archive.writestr(entry("style.css"), style)
        for folder in ("host", "node", "native_fx"):
            archive.writestr(entry(folder + "/", directory=True), b"")
        for path in modules:
            archive.writestr(
                entry(path.relative_to(TEMPLATE).as_posix()), path.read_bytes()
            )
        for path in assets:
            name = "assets/" + path.relative_to(TEMPLATE / "assets").as_posix()
            archive.writestr(entry(name), path.read_bytes())
    print(destination)


if __name__ == "__main__":
    main()
