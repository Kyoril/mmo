#!/usr/bin/env python3
"""Derive a terrain layer height map from its albedo texture.

Height based terrain blending needs a per-texel height for each splat layer so that one
layer can poke through another instead of cross-fading into it. Hand painting one for every
tileset texture is a lot of art, so this produces a usable starting point from the albedo
that already exists: blurred luminance, stretched to the full range.

    python tools/derive_height_map.py data/client/Textures/Tilesets/T_Dirt_01_C.htex

Writes <input>_h.htex next to the input unless --output says otherwise. Output is
uncompressed single channel (R8) with a full mip chain, which is what the terrain material
samples. Luminance is deliberately NOT linearised: a perceptual response is a better guess
at "how high does this read" than a photometric one, and this is art direction rather than
measurement. Expect to replace it by hand wherever it guesses wrong - it reads mortar lines
and gaps as low and highlights as high, which is usually but not always the right sense.

The editor's texture import dialog does the same thing at import time for new textures; this
tool exists for the textures that were already imported.
"""
import argparse
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
HTEX_TOOL = REPO_ROOT / ".claude" / "skills" / "mmo-material-editor" / "scripts" / "htex_tool.py"

HTEX_MAGIC = b"HTEX"
HTEX_VERSION = 0x0100
HTEX_FORMAT_R8 = 6      # tex::v1_0::PixelFormat::R8
HTEX_HEADER_SIZE = 142
HTEX_MAX_MIPS = 16


def decode_to_image(path: Path):
    from PIL import Image

    if path.suffix.lower() != ".htex":
        return Image.open(path).convert("RGB")

    with tempfile.TemporaryDirectory() as tmp:
        png = Path(tmp) / "decoded.png"
        result = subprocess.run(
            [sys.executable, str(HTEX_TOOL), "preview", str(path), "--output", str(png)],
            capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit("could not decode {}:\n{}{}".format(path, result.stdout, result.stderr))
        return Image.open(png).convert("RGB")


def derive(image, blur_radius: int, size: int):
    from PIL import Image, ImageFilter

    if size and (image.width > size or image.height > size):
        image = image.resize((size, size), Image.LANCZOS)

    # Rec. 709 luminance.
    grey = image.convert("L")

    # Two box passes approximate a Gaussian and stay cheap; the terrain samples this tiling,
    # so edges wrapping rather than clamping would be marginally better, but at these radii
    # the difference is invisible.
    for _ in range(2):
        if blur_radius > 0:
            grey = grey.filter(ImageFilter.BoxBlur(blur_radius))

    # Stretch to the full range: a low contrast albedo would otherwise give a height map too
    # flat to separate anything, and heightScale would have to be cranked to compensate.
    low, high = grey.getextrema()
    if high > low:
        scale = 255.0 / (high - low)
        grey = grey.point(lambda v: int(min(255, max(0, (v - low) * scale))))
    return grey


def build_mips(image):
    from PIL import Image

    mips = [image]
    while (mips[-1].width > 1 or mips[-1].height > 1) and len(mips) < HTEX_MAX_MIPS:
        prev = mips[-1]
        mips.append(prev.resize((max(1, prev.width // 2), max(1, prev.height // 2)), Image.BOX))
    return mips


def write_htex(path: Path, mips) -> None:
    payloads = [mip.tobytes() for mip in mips]

    header = bytearray()
    header.extend(HTEX_MAGIC)
    header.extend(struct.pack("<I", HTEX_VERSION))
    header.extend(struct.pack("<B", HTEX_FORMAT_R8))
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


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", type=Path, help="Source albedo (.htex or an image file)")
    parser.add_argument("--output", type=Path, help="Destination .htex (default: <input>_h.htex)")
    parser.add_argument("--blur", type=int, default=2, help="Blur radius in texels (0 disables)")
    parser.add_argument("--size", type=int, default=512,
                        help="Clamp the longest side to this; height data needs far less "
                             "resolution than albedo. 0 keeps the source size.")
    args = parser.parse_args()

    output = args.output or args.input.with_name(args.input.stem + "_h.htex")

    image = decode_to_image(args.input)
    grey = derive(image, args.blur, args.size)
    mips = build_mips(grey)
    write_htex(output, mips)

    print("{} -> {}  ({}x{}, R8, {} mips)".format(
        args.input.name, output, mips[0].width, mips[0].height, len(mips)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
