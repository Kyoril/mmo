# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Stands in for update_compiler in tests: writes a list.txt naming every source file.

FAKE_COMPILER_FAIL=1 makes it fail; FAKE_COMPILER_OMIT=<name> leaves one file out.
"""

import argparse
import os
import sys

parser = argparse.ArgumentParser()
parser.add_argument("-s", required=True)
parser.add_argument("-o", required=True)
parser.add_argument("-c")
parser.add_argument("-j")
args = parser.parse_args()

if os.environ.get("FAKE_COMPILER_FAIL"):
	sys.stderr.write("simulated compiler failure\n")
	sys.exit(3)
data = os.path.join(args.s, "..", "..", "data", "client")
if not os.path.isdir(data):
	sys.stderr.write("data/client missing at {}\n".format(data))
	sys.exit(4)
omit = os.environ.get("FAKE_COMPILER_OMIT", "")
os.makedirs(os.path.join(args.o, "Data"), exist_ok=True)
with open(os.path.join(args.o, "list.txt"), "w", encoding="utf-8") as out:
	out.write("version = 1\n")
	for name in sorted(os.listdir(args.s)):
		if name != omit:
			out.write('name = "{}"\n'.format(name))
