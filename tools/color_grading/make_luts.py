#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Generate strip colour-grading LUTs for the environment profile system.

A strip LUT of edge N is a single 2D texture, N*N texels wide and N texels high, that packs
an N^3 colour cube the way Unreal's legacy colour grading does: blue selects which N x N
"slice" of the strip to read (`slice = x // N`), red selects the column inside that slice
(`column = x % N`), and green selects the row (`y`). The tonemap pass
(`src/shared/deferred_shading/shaders/PS_Tonemap.hlsl`, addressing mirrored in
`src/shared/deferred_shading/color_grading.h`) samples this strip at mip 0 with bilinear,
clamped filtering and blends the two slices nearest the input blue value. See
docs/color-grading.md for the full pipeline and the authoring workflow.

Textures are read back by the engine unchanged (no sRGB decode), so an 8-bit neutral LUT
must store round(value * 255) at every texel - values are not gamma-encoded or decoded again.

Commands:
    python tools/color_grading/make_luts.py neutral --size 32 --out-dir data/client/Textures/ColorGrading
        Writes NeutralLUT32.png and NeutralLUT32.htex: the identity grade, i.e. every texel
        maps its own (r, g, b) coordinate back to itself. This is what an artist pastes into
        an image editor before grading (see docs/color-grading.md).

    python tools/color_grading/make_luts.py test-grade --size 32 --out <path.htex>
        Writes a strong, warm, high-contrast grade as an .htex, for verification only - never
        shipped as game content.
"""
import argparse
import struct
from pathlib import Path

import numpy as np

# mmo::tex::v1_0::PixelFormat, see src/shared/tex_v1_0/magic.h.
HTEX_FORMAT_RGBA = 1

HTEX_MAGIC = b"HTEX"
HTEX_VERSION = 0x0100
HTEX_HEADER_SIZE = 142
HTEX_MAX_MIPS = 16


def neutral(size: int) -> np.ndarray:
    """Returns the identity strip LUT of edge `size` as an (size, size*size, 3) uint8 array.

    Texel (x, y) is (column / (N - 1), y / (N - 1), slice / (N - 1)) * 255, rounded, with
    `slice = x // N` and `column = x % N` - i.e. sampling the strip at (r, g, b) returns
    (r, g, b) back, unchanged.
    """
    n = size
    scale = float(n - 1)
    columns = np.arange(n * n, dtype=np.float64) % n
    slices = np.arange(n * n, dtype=np.float64) // n
    rows = np.arange(n, dtype=np.float64)

    red = np.round(columns / scale * 255.0)
    blue = np.round(slices / scale * 255.0)
    green = np.round(rows / scale * 255.0)

    image = np.empty((n, n * n, 3), dtype=np.uint8)
    image[:, :, 0] = red[np.newaxis, :]
    image[:, :, 1] = green[:, np.newaxis]
    image[:, :, 2] = blue[np.newaxis, :]
    return image


def test_grade(size: int) -> np.ndarray:
    """Returns a strong warm, high-contrast strip LUT of edge `size` as uint8 (verification only).

    Applied to the identity grid coordinates: r' = r^0.85 * 1.1, g' = g, b' = b^1.2 * 0.85,
    clamped to [0, 1] before quantizing to 8 bits.
    """
    n = size
    scale = float(n - 1)
    columns = np.arange(n * n, dtype=np.float64) % n
    slices = np.arange(n * n, dtype=np.float64) // n
    rows = np.arange(n, dtype=np.float64)

    r = (columns / scale)[np.newaxis, :]
    g = (rows / scale)[:, np.newaxis]
    b = (slices / scale)[np.newaxis, :]

    r_graded = np.clip(np.power(r, 0.85) * 1.1, 0.0, 1.0)
    g_graded = np.broadcast_to(g, (n, n * n))
    b_graded = np.clip(np.power(b, 1.2) * 0.85, 0.0, 1.0)

    image = np.empty((n, n * n, 3), dtype=np.uint8)
    image[:, :, 0] = np.round(r_graded * 255.0)
    image[:, :, 1] = np.round(g_graded * 255.0)
    image[:, :, 2] = np.round(b_graded * 255.0)
    return image


def to_rgba(rgb: np.ndarray) -> np.ndarray:
    """Appends an opaque alpha channel to an (H, W, 3) uint8 array."""
    height, width, _ = rgb.shape
    rgba = np.empty((height, width, 4), dtype=np.uint8)
    rgba[:, :, :3] = rgb
    rgba[:, :, 3] = 255
    return rgba


def build_mips(rgba: np.ndarray) -> list:
    """Builds a full RGBA8 mip chain down to 1x1, box-filtering each level from the previous one."""
    from PIL import Image

    mips = [Image.fromarray(rgba, mode="RGBA")]
    while (mips[-1].width > 1 or mips[-1].height > 1) and len(mips) < HTEX_MAX_MIPS:
        prev = mips[-1]
        mips.append(prev.resize((max(1, prev.width // 2), max(1, prev.height // 2)), Image.BOX))
    return mips


def write_htex(path: Path, mips) -> None:
    """Writes an uncompressed RGBA8 .htex with a full mip chain.

    Follows the header layout of `write_htex` in tools/derive_height_map.py (magic HTEX,
    version 0x0100, 142-byte header: format/hasMips/width/height then 16 mip offsets and 16
    mip lengths), but with the RGBA8 pixel format instead of R8.
    """
    payloads = [mip.tobytes() for mip in mips]

    header = bytearray()
    header.extend(HTEX_MAGIC)
    header.extend(struct.pack("<I", HTEX_VERSION))
    header.extend(struct.pack("<B", HTEX_FORMAT_RGBA))
    header.extend(struct.pack("<B", 1 if len(payloads) > 1 else 0))
    header.extend(struct.pack("<H", mips[0].width))
    header.extend(struct.pack("<H", mips[0].height))

    offsets = [0] * HTEX_MAX_MIPS
    lengths = [0] * HTEX_MAX_MIPS
    cursor = HTEX_HEADER_SIZE
    for index, payload in enumerate(payloads[:HTEX_MAX_MIPS]):
        offsets[index] = cursor
        lengths[index] = len(payload)
        cursor += len(payload)

    for offset in offsets:
        header.extend(struct.pack("<I", offset))
    for length in lengths:
        header.extend(struct.pack("<I", length))

    if len(header) != HTEX_HEADER_SIZE:
        raise ValueError("HTEX header size mismatch: {}".format(len(header)))

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as out:
        out.write(header)
        for payload in payloads[:HTEX_MAX_MIPS]:
            out.write(payload)


def cmd_neutral(args) -> int:
    from PIL import Image

    rgb = neutral(args.size)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    png_path = out_dir / "NeutralLUT{}.png".format(args.size)
    Image.fromarray(rgb, mode="RGB").save(png_path, "PNG")

    htex_path = out_dir / "NeutralLUT{}.htex".format(args.size)
    mips = build_mips(to_rgba(rgb))
    write_htex(htex_path, mips)

    print("{} ({}x{})".format(png_path, rgb.shape[1], rgb.shape[0]))
    print("{} ({}x{}, RGBA, {} mips)".format(htex_path, mips[0].width, mips[0].height, len(mips)))
    return 0


def cmd_test_grade(args) -> int:
    rgb = test_grade(args.size)
    out_path = Path(args.out)
    mips = build_mips(to_rgba(rgb))
    write_htex(out_path, mips)
    print("{} ({}x{}, RGBA, {} mips) - verification only, never shipped".format(
        out_path, mips[0].width, mips[0].height, len(mips)))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    subparsers = parser.add_subparsers(dest="command", required=True)

    neutral_parser = subparsers.add_parser("neutral", help="Write the identity strip LUT (PNG + htex)")
    neutral_parser.add_argument("--size", type=int, default=32, help="LUT edge size N (default 32)")
    neutral_parser.add_argument("--out-dir", type=str, required=True, help="Output directory")
    neutral_parser.set_defaults(func=cmd_neutral)

    grade_parser = subparsers.add_parser("test-grade", help="Write a strong test grade (htex only)")
    grade_parser.add_argument("--size", type=int, default=32, help="LUT edge size N (default 32)")
    grade_parser.add_argument("--out", type=str, required=True, help="Output .htex path")
    grade_parser.set_defaults(func=cmd_test_grade)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
