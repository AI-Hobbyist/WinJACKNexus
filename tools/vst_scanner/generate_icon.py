#!/usr/bin/env python3
"""Crop the VST scanner artwork and build a multi-resolution Windows icon."""

# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


ICON_SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE = PROJECT_ROOT / "ico" / "vst_scan.png"
DEFAULT_OUTPUT = PROJECT_ROOT / "ico" / "vst_scan.ico"


def crop_transparent_edges(image: Image.Image) -> Image.Image:
    """Return the smallest rectangle that contains non-transparent pixels."""
    rgba_image = image.convert("RGBA")
    bounds = rgba_image.getchannel("A").getbbox()
    if bounds is None:
        raise ValueError("The source image has no visible pixels.")
    return rgba_image.crop(bounds)


def create_square_master(image: Image.Image, size: int) -> Image.Image:
    """Scale without distortion and center the artwork on a transparent square."""
    scale = min(size / image.width, size / image.height)
    resized_size = (
        max(1, round(image.width * scale)),
        max(1, round(image.height * scale)),
    )
    resized = image.resize(resized_size, Image.Resampling.LANCZOS)
    result = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    offset = ((size - resized.width) // 2, (size - resized.height) // 2)
    result.alpha_composite(resized, offset)
    return result


def generate_icon(source: Path, output: Path) -> None:
    if not source.is_file():
        raise FileNotFoundError(f"Source image does not exist: {source}")

    with Image.open(source) as image:
        cropped = crop_transparent_edges(image)

    cropped.save(source)
    master = create_square_master(cropped, max(ICON_SIZES))
    output.parent.mkdir(parents=True, exist_ok=True)
    master.save(output, format="ICO", sizes=[(size, size) for size in ICON_SIZES])
    print(f"Cropped {source.name} to {cropped.width}x{cropped.height}.")
    print(f"Generated {output} with sizes: {', '.join(map(str, ICON_SIZES))}.")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE, help="PNG artwork to crop.")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT, help="Generated Windows ICO file.")
    return parser.parse_args()


if __name__ == "__main__":
    arguments = parse_arguments()
    generate_icon(arguments.source, arguments.output)