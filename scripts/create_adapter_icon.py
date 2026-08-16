from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "logos" / "adapter.png"
ICO_DESTINATION = ROOT / "logos" / "adapter.ico"
PNG_DESTINATION = ROOT / "logos" / "adapter_transparent.png"
ICON_SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
SOLID_BACKGROUND_TOLERANCE = 32
FEATHER_TOLERANCE = 64


def colour_distance(left: tuple[int, int, int], right: tuple[int, int, int]) -> int:
    return max(abs(component_left - component_right)
               for component_left, component_right in zip(left, right))


def remove_edge_connected_background(image: Image.Image) -> Image.Image:
    pixels = image.convert("RGBA")
    width, height = pixels.size
    data = pixels.load()
    corner_colours = {
        data[0, 0][:3],
        data[width - 1, 0][:3],
        data[0, height - 1][:3],
        data[width - 1, height - 1][:3],
    }
    queue: deque[tuple[int, int]] = deque()
    visited: set[tuple[int, int]] = set()

    for x in range(width):
        queue.extend(((x, 0), (x, height - 1)))
    for y in range(height):
        queue.extend(((0, y), (width - 1, y)))

    while queue:
        x, y = queue.popleft()
        if (x, y) in visited:
            continue

        visited.add((x, y))
        red, green, blue, alpha = data[x, y]
        distance = min(colour_distance((red, green, blue), background)
                       for background in corner_colours)
        if alpha == 0 or distance > FEATHER_TOLERANCE:
            continue

        if distance <= SOLID_BACKGROUND_TOLERANCE:
            data[x, y] = (red, green, blue, 0)
        else:
            feather_alpha = round(alpha * (distance - SOLID_BACKGROUND_TOLERANCE)
                                  / (FEATHER_TOLERANCE - SOLID_BACKGROUND_TOLERANCE))
            data[x, y] = (red, green, blue, feather_alpha)

        for next_x, next_y in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if 0 <= next_x < width and 0 <= next_y < height:
                queue.append((next_x, next_y))

    return pixels


def crop_to_visible_content(image: Image.Image) -> Image.Image:
    bounds = image.getchannel("A").getbbox()
    if bounds is None:
        raise ValueError("The source icon contains no visible pixels after background removal.")

    return image.crop(bounds)


def make_square_icon(image: Image.Image) -> Image.Image:
    foreground = crop_to_visible_content(image)
    padding = max(8, round(max(foreground.size) * 0.08))
    side = max(foreground.size) + padding * 2
    result = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    result.alpha_composite(
        foreground,
        ((side - foreground.width) // 2, (side - foreground.height) // 2),
    )
    return result


def create_icon(source: Path, ico_destination: Path, png_destination: Path) -> None:
    if not source.is_file():
        raise FileNotFoundError(f"Missing source icon: {source}")

    with Image.open(source) as source_image:
        prepared = make_square_icon(remove_edge_connected_background(source_image))

    png_destination.parent.mkdir(parents=True, exist_ok=True)
    ico_destination.parent.mkdir(parents=True, exist_ok=True)
    prepared.resize((256, 256), Image.Resampling.LANCZOS).save(
        png_destination,
        format="PNG",
    )
    prepared.save(
        ico_destination,
        format="ICO",
        sizes=[(size, size) for size in ICON_SIZES],
    )
    print(
        f"Created {ico_destination} and {png_destination} "
        f"from {source} with sizes: {', '.join(map(str, ICON_SIZES))}"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Remove icon backgrounds and create app assets")
    parser.add_argument("--source", type=Path, default=SOURCE)
    parser.add_argument("--ico-destination", type=Path, default=ICO_DESTINATION)
    parser.add_argument("--png-destination", type=Path, default=PNG_DESTINATION)
    arguments = parser.parse_args()
    create_icon(arguments.source, arguments.ico_destination, arguments.png_destination)


if __name__ == "__main__":
    main()