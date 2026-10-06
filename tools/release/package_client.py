#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Packs the client binaries named by deploy/patch/source.txt into client-bin.zip.

The zip carries source.txt itself, so the deployer compiles the patch with exactly the
layout of the commit it deploys.
"""

import argparse
import re
import sys
import zipfile
from pathlib import Path

# A top-level file entry: from = "name.exe" / "name.dll" (no path separators).
_BINARY = re.compile(r'from\s*=\s*"([^"/\\]+\.(?:exe|dll))"')


def binaries_from_source(text):
	return sorted(set(_BINARY.findall(text)))


def package(bin_dir, source_txt, out_zip):
	text = Path(source_txt).read_text(encoding="utf-8")
	names = binaries_from_source(text)
	if not names:
		raise ValueError("{} names no binaries".format(source_txt))
	missing = [name for name in names if not (Path(bin_dir) / name).is_file()]
	if missing:
		raise FileNotFoundError("missing in {}: {}".format(bin_dir, ", ".join(missing)))
	with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED) as archive:
		archive.write(source_txt, "source.txt")
		for name in names:
			archive.write(Path(bin_dir) / name, name)
	return names


def main(argv=None):
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--bin-dir", required=True)
	parser.add_argument("--source", required=True)
	parser.add_argument("--out", required=True)
	args = parser.parse_args(argv)
	names = package(args.bin_dir, args.source, args.out)
	print("packed {} into {}".format(", ".join(names), args.out))
	return 0


if __name__ == "__main__":
	sys.exit(main())
