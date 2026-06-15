#!/usr/bin/env python3
"""Inspect MMO HTEX files and render mip levels to PNG."""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from pathlib import Path

from PIL import Image


FORMAT_NAMES = {
    0: "RGB",
    1: "RGBA",
    2: "DXT1 / BC1",
    3: "DXT5 / BC3",
    4: "Float RGB",
    5: "Float RGBA",
    6: "R8",
    7: "RG8",
    8: "BC4",
    9: "BC5",
}


def read_htex(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < 142:
        raise ValueError(f"{path} is too small to be an HTEX v1.0 file")
    if data[:4] != b"HTEX":
        raise ValueError(f"{path} has magic {data[:4]!r}, expected b'HTEX'")

    version, = struct.unpack_from("<I", data, 4)
    pixel_format, has_mips, width, height = struct.unpack_from("<BBHH", data, 8)
    offsets = list(struct.unpack_from("<16I", data, 14))
    lengths = list(struct.unpack_from("<16I", data, 78))

    mips = []
    for index, (offset, length) in enumerate(zip(offsets, lengths)):
        if not offset or not length:
            continue
        mip_width = max(1, width >> index)
        mip_height = max(1, height >> index)
        if offset + length > len(data):
            raise ValueError(
                f"Mip {index} extends beyond EOF: offset={offset}, length={length}, file={len(data)}"
            )
        mips.append(
            {
                "index": index,
                "width": mip_width,
                "height": mip_height,
                "offset": offset,
                "length": length,
            }
        )

    return {
        "path": str(path),
        "version": version,
        "version_hex": f"0x{version:04x}",
        "format_id": pixel_format,
        "format": FORMAT_NAMES.get(pixel_format, f"Unknown ({pixel_format})"),
        "has_mips_flag": bool(has_mips),
        "width": width,
        "height": height,
        "header_size": 142,
        "file_size": len(data),
        "mip_count": len(mips),
        "mips": mips,
        "_bytes": data,
    }


def rgb565(value: int) -> tuple[int, int, int]:
    r = ((value >> 11) & 31) * 255 // 31
    g = ((value >> 5) & 63) * 255 // 63
    b = (value & 31) * 255 // 31
    return r, g, b


def decode_bc_alpha(block: bytes) -> list[int]:
    a0, a1 = block[0], block[1]
    bits = int.from_bytes(block[2:8], "little")
    values = [a0, a1]
    if a0 > a1:
        values.extend(((7 - i) * a0 + i * a1) // 7 for i in range(1, 7))
    else:
        values.extend(((5 - i) * a0 + i * a1) // 5 for i in range(1, 5))
        values.extend((0, 255))
    return [values[(bits >> (3 * i)) & 7] for i in range(16)]


def decode_bc1_colors(block: bytes, force_four_color: bool = False) -> list[tuple[int, int, int, int]]:
    c0, c1, bits = struct.unpack_from("<HHI", block)
    r0, g0, b0 = rgb565(c0)
    r1, g1, b1 = rgb565(c1)
    palette = [(r0, g0, b0, 255), (r1, g1, b1, 255)]
    if c0 > c1 or force_four_color:
        palette.extend(
            [
                ((2 * r0 + r1) // 3, (2 * g0 + g1) // 3, (2 * b0 + b1) // 3, 255),
                ((r0 + 2 * r1) // 3, (g0 + 2 * g1) // 3, (b0 + 2 * b1) // 3, 255),
            ]
        )
    else:
        palette.extend(
            [
                ((r0 + r1) // 2, (g0 + g1) // 2, (b0 + b1) // 2, 255),
                (0, 0, 0, 0),
            ]
        )
    return [palette[(bits >> (2 * i)) & 3] for i in range(16)]


def decode_blocks(payload: bytes, width: int, height: int, pixel_format: int) -> bytes:
    if pixel_format == 2:
        block_size = 8
    elif pixel_format in (3, 9):
        block_size = 16
    elif pixel_format == 8:
        block_size = 8
    else:
        raise ValueError(f"Unsupported block format {pixel_format}")

    blocks_x = max(1, (width + 3) // 4)
    blocks_y = max(1, (height + 3) // 4)
    expected = blocks_x * blocks_y * block_size
    if len(payload) < expected:
        raise ValueError(f"Compressed mip has {len(payload)} bytes, expected at least {expected}")

    rgba = bytearray(width * height * 4)
    for by in range(blocks_y):
        for bx in range(blocks_x):
            start = (by * blocks_x + bx) * block_size
            block = payload[start : start + block_size]
            if pixel_format == 2:
                pixels = decode_bc1_colors(block)
            elif pixel_format == 3:
                alpha = decode_bc_alpha(block[:8])
                colors = decode_bc1_colors(block[8:], force_four_color=True)
                pixels = [(r, g, b, alpha[i]) for i, (r, g, b, _) in enumerate(colors)]
            elif pixel_format == 8:
                channel = decode_bc_alpha(block)
                pixels = [(v, v, v, 255) for v in channel]
            else:
                red = decode_bc_alpha(block[:8])
                green = decode_bc_alpha(block[8:])
                pixels = []
                for r, g in zip(red, green):
                    nx = r / 127.5 - 1.0
                    ny = g / 127.5 - 1.0
                    nz = math.sqrt(max(0.0, 1.0 - nx * nx - ny * ny))
                    pixels.append((r, g, round((nz * 0.5 + 0.5) * 255), 255))

            for py in range(4):
                y = by * 4 + py
                if y >= height:
                    continue
                for px in range(4):
                    x = bx * 4 + px
                    if x >= width:
                        continue
                    dst = (y * width + x) * 4
                    rgba[dst : dst + 4] = bytes(pixels[py * 4 + px])
    return bytes(rgba)


def decode_mip(info: dict, mip_index: int) -> Image.Image:
    mip = next((item for item in info["mips"] if item["index"] == mip_index), None)
    if mip is None:
        raise ValueError(f"Mip {mip_index} is not present")
    payload = info["_bytes"][mip["offset"] : mip["offset"] + mip["length"]]
    width, height = mip["width"], mip["height"]
    pixel_format = info["format_id"]

    if pixel_format in (2, 3, 8, 9):
        rgba = decode_blocks(payload, width, height, pixel_format)
        return Image.frombytes("RGBA", (width, height), rgba)
    if pixel_format in (0, 1):
        pixel_count = width * height
        if len(payload) == pixel_count * 4:
            return Image.frombytes("RGBA", (width, height), payload)
        if len(payload) == pixel_count * 3:
            return Image.frombytes("RGB", (width, height), payload).convert("RGBA")
        raise ValueError(f"Unexpected RGB(A) payload length {len(payload)}")
    if pixel_format == 6:
        return Image.frombytes("L", (width, height), payload).convert("RGBA")
    if pixel_format == 7:
        rgba = bytearray(width * height * 4)
        for index in range(width * height):
            r, g = payload[index * 2 : index * 2 + 2]
            nx = r / 127.5 - 1.0
            ny = g / 127.5 - 1.0
            nz = math.sqrt(max(0.0, 1.0 - nx * nx - ny * ny))
            rgba[index * 4 : index * 4 + 4] = bytes(
                (r, g, round((nz * 0.5 + 0.5) * 255), 255)
            )
        return Image.frombytes("RGBA", (width, height), bytes(rgba))
    if pixel_format in (4, 5):
        channels = 3 if pixel_format == 4 else 4
        values = struct.unpack(f"<{width * height * channels}f", payload)
        rgba = bytearray(width * height * 4)
        for index in range(width * height):
            source = values[index * channels : (index + 1) * channels]
            converted = [round(max(0.0, min(1.0, value)) * 255) for value in source]
            if channels == 3:
                converted.append(255)
            rgba[index * 4 : index * 4 + 4] = bytes(converted)
        return Image.frombytes("RGBA", (width, height), bytes(rgba))
    raise ValueError(f"Cannot preview HTEX pixel format {info['format']}")


def public_info(info: dict) -> dict:
    return {key: value for key, value in info.items() if key != "_bytes"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    inspect_parser = subparsers.add_parser("inspect", help="Print HTEX metadata")
    inspect_parser.add_argument("input", type=Path)
    inspect_parser.add_argument("--json", action="store_true")

    preview_parser = subparsers.add_parser("preview", help="Decode an HTEX mip to PNG")
    preview_parser.add_argument("input", type=Path)
    preview_parser.add_argument("--output", "-o", required=True, type=Path)
    preview_parser.add_argument("--mip", type=int, default=0)

    args = parser.parse_args()
    try:
        info = read_htex(args.input)
        if args.command == "inspect":
            if args.json:
                print(json.dumps(public_info(info), indent=2))
            else:
                print(f"{args.input}: {info['width']}x{info['height']} {info['format']}")
                print(
                    f"HTEX {info['version_hex']}, {info['mip_count']} stored mip(s), "
                    f"{info['file_size']} bytes"
                )
                for mip in info["mips"]:
                    print(
                        f"  mip {mip['index']}: {mip['width']}x{mip['height']}, "
                        f"offset {mip['offset']}, {mip['length']} bytes"
                    )
        else:
            image = decode_mip(info, args.mip)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            image.save(args.output, "PNG")
            print(f"Wrote {args.output} ({image.width}x{image.height})")
    except (OSError, ValueError, struct.error) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
