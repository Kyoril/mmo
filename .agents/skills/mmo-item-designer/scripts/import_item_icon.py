#!/usr/bin/env python3
"""Import a PNG item icon into the engine HTEX format using color compression."""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

from PIL import Image


HTEX_VERSION = 0x100
HTEX_FORMAT_DXT1 = 2
HTEX_FORMAT_DXT5 = 3
DDS_MAGIC = b"DDS "
DEFAULT_TEXCONV = Path(r"C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\Utilities\bin\x64\texconv.exe")


def find_texconv(explicit: str | None = None) -> Path:
    if explicit:
        candidate = Path(explicit)
        if candidate.is_file():
            return candidate
        raise FileNotFoundError(f"texconv was not found at {candidate}")

    candidates = [
        DEFAULT_TEXCONV,
        Path(r"C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\Utilities\bin\x86\texconv.exe"),
        Path(r"C:\Program Files\FFXIV TexTools\FFXIV_TexTools\converters\texconv.exe"),
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError("Unable to locate texconv.exe")


def choose_format(source: Path, requested: str) -> tuple[str, int]:
    if requested == "dxt1":
        return "DXT1", HTEX_FORMAT_DXT1
    if requested == "dxt5":
        return "DXT5", HTEX_FORMAT_DXT5

    image = Image.open(source).convert("RGBA")
    try:
        alpha_extrema = image.getchannel("A").getextrema()
    finally:
        image.close()
    if alpha_extrema == (255, 255):
        return "DXT1", HTEX_FORMAT_DXT1
    return "DXT5", HTEX_FORMAT_DXT5


def run_texconv(texconv: Path, source: Path, output_dir: Path, dds_format: str, mip_levels: int) -> Path:
    command = [
        str(texconv),
        "-nologo",
        "-ft",
        "DDS",
        "-if",
        "TRIANGLE_DITHER",
        "-mf",
        "BOX",
        "-f",
        dds_format,
        "-m",
        str(mip_levels),
        "-o",
        str(output_dir),
        str(source),
    ]
    subprocess.run(command, check=True)
    output = output_dir / (source.stem + ".DDS")
    if not output.is_file():
        output = output_dir / (source.stem + ".dds")
    if not output.is_file():
        raise FileNotFoundError("texconv did not produce the expected DDS output")
    return output


def parse_dds(path: Path, expected_htex_format: int) -> tuple[int, int, list[bytes]]:
    data = path.read_bytes()
    if len(data) < 128 or data[:4] != DDS_MAGIC:
        raise ValueError(f"{path} is not a DDS file")

    header_size = struct.unpack_from("<I", data, 4)[0]
    if header_size != 124:
        raise ValueError(f"Unexpected DDS header size {header_size}")

    height = struct.unpack_from("<I", data, 12)[0]
    width = struct.unpack_from("<I", data, 16)[0]
    mip_count = struct.unpack_from("<I", data, 28)[0] or 1
    four_cc = data[84:88]

    if expected_htex_format == HTEX_FORMAT_DXT1 and four_cc != b"DXT1":
        raise ValueError(f"DDS format mismatch: expected DXT1, got {four_cc!r}")
    if expected_htex_format == HTEX_FORMAT_DXT5 and four_cc != b"DXT5":
        raise ValueError(f"DDS format mismatch: expected DXT5, got {four_cc!r}")

    block_size = 8 if four_cc == b"DXT1" else 16
    payload_offset = 128
    payloads: list[bytes] = []
    cursor = payload_offset
    mip_width = width
    mip_height = height
    for _ in range(mip_count):
        blocks_x = max(1, (mip_width + 3) // 4)
        blocks_y = max(1, (mip_height + 3) // 4)
        mip_size = blocks_x * blocks_y * block_size
        payload = data[cursor: cursor + mip_size]
        if len(payload) != mip_size:
            raise ValueError("DDS payload ended unexpectedly while reading mip data")
        payloads.append(payload)
        cursor += mip_size
        mip_width = max(1, mip_width // 2)
        mip_height = max(1, mip_height // 2)

    return width, height, payloads


def write_htex(path: Path, width: int, height: int, payloads: list[bytes], htex_format: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    header = bytearray()
    header.extend(b"HTEX")
    header.extend(struct.pack("<I", HTEX_VERSION))
    header.extend(struct.pack("<B", htex_format))
    header.extend(struct.pack("<B", 1 if len(payloads) > 1 else 0))
    header.extend(struct.pack("<H", width))
    header.extend(struct.pack("<H", height))

    header_size = 142
    offsets = [0] * 16
    lengths = [0] * 16
    cursor = header_size
    for index, payload in enumerate(payloads[:16]):
        offsets[index] = cursor
        lengths[index] = len(payload)
        cursor += len(payload)

    for offset in offsets:
        header.extend(struct.pack("<I", offset))
    for length in lengths:
        header.extend(struct.pack("<I", length))

    if len(header) != header_size:
        raise ValueError(f"HTEX header size mismatch: {len(header)}")

    with path.open("wb") as handle:
        handle.write(header)
        for payload in payloads[:16]:
            handle.write(payload)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input_png")
    parser.add_argument("output_htex")
    parser.add_argument("--format", choices=["auto", "dxt1", "dxt5"], default="auto")
    parser.add_argument("--mips", type=int, default=0, help="Pass-through texconv mip count; 0 = full chain")
    parser.add_argument("--texconv", default=None)
    parser.add_argument("--keep-dds", action="store_true")
    args = parser.parse_args()

    input_png = Path(args.input_png).resolve()
    output_htex = Path(args.output_htex).resolve()
    texconv = find_texconv(args.texconv)
    dds_format, htex_format = choose_format(input_png, args.format)

    with tempfile.TemporaryDirectory(prefix="mmo_item_icon_") as temp_dir:
        temp_root = Path(temp_dir)
        dds_path = run_texconv(texconv, input_png, temp_root, dds_format, args.mips)
        width, height, payloads = parse_dds(dds_path, htex_format)
        write_htex(output_htex, width, height, payloads, htex_format)
        if args.keep_dds:
            shutil.copy2(dds_path, output_htex.with_suffix(".dds"))

    print(f"OK: wrote {output_htex} ({width}x{height}, {dds_format}, {len(payloads)} mip(s))")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
