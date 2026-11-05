#!/usr/bin/env python3
"""Compose the 320x176 XMB icon from the user's own extracted title art.

The repository ships no game art, so the icon is built from the player's copy
like every other asset: the title screen sky with the title logo on top.

Usage: make_icon.py --work <work dir>
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image

ICON_SIZE = (320, 176)
SKY_REGION = (0, 0, 1920, 1440)  # the sky occupies the top-left of title.jpg
LOGO_HEIGHT = 156


def cover(image: Image.Image, size: tuple[int, int]) -> Image.Image:
    """Scale to fill size, cropping the overflow evenly."""
    scale = max(size[0] / image.width, size[1] / image.height)
    scaled = image.resize((round(image.width * scale), round(image.height * scale)), Image.LANCZOS)
    left = (scaled.width - size[0]) // 2
    top = (scaled.height - size[1]) // 2
    return scaled.crop((left, top, left + size[0], top + size[1]))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--work", type=Path, required=True)
    work = parser.parse_args().work
    raw = work / "assets_raw"
    sky = Image.open(raw / "title" / "title.jpg").convert("RGBA").crop(SKY_REGION)
    icon = cover(sky, ICON_SIZE)
    logo = Image.open(raw / "images" / "ui" / "title_logo" / "eng.png").convert("RGBA")
    logo = logo.resize((round(logo.width * LOGO_HEIGHT / logo.height), LOGO_HEIGHT), Image.LANCZOS)
    icon.alpha_composite(logo, ((ICON_SIZE[0] - logo.width) // 2, (ICON_SIZE[1] - logo.height) // 2))
    out = work / "icon" / "ICON0.PNG"
    out.parent.mkdir(parents=True, exist_ok=True)
    icon.convert("RGB").save(out)
    print(f"icon: {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
