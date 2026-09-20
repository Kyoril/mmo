# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Compares two client screenshots for the rendering regression checkpoint.

Prints the largest per-channel difference and the share of pixels beyond the noise tolerance,
optionally writes a 16x amplified difference image, and exits 0 on PASS.
Requires Pillow (python -m pip install pillow).
"""
import argparse
import sys

from PIL import Image, ImageChops


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline")
    parser.add_argument("candidate")
    parser.add_argument("--diff", help="write an amplified difference image here")
    parser.add_argument("--tolerance", type=int, default=2,
                        help="per-channel difference (8-bit levels) treated as dither noise")
    parser.add_argument("--max-fraction", type=float, default=0.005,
                        help="share of pixels allowed beyond the tolerance")
    args = parser.parse_args()

    baseline = Image.open(args.baseline).convert("RGB")
    candidate = Image.open(args.candidate).convert("RGB")
    if baseline.size != candidate.size:
        print(f"FAIL: size mismatch {baseline.size} vs {candidate.size}")
        return 1

    diff = ImageChops.difference(baseline, candidate)
    pixels = list(diff.getdata())
    largest = max(max(pixel) for pixel in pixels)
    over = sum(1 for pixel in pixels if max(pixel) > args.tolerance)
    fraction = over / len(pixels)
    print(f"max channel difference: {largest}; pixels over tolerance: {over} ({fraction:.4%})")

    if args.diff:
        diff.point(lambda value: min(255, value * 16)).save(args.diff)

    passed = fraction <= args.max_fraction
    print("PASS" if passed else "FAIL")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
