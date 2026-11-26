#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Decides which steps of the fast gate a change needs, from the files it changes.

    python tools/gate/select_steps.py [--base origin/develop]

The fast gate ran every step for every change: a one-line fix in a Python tool waited half an
hour for the Windows and Linux C++ builds and their test suites. Now a change runs what it can
affect:

    only documentation (*.md, docs/)       protocol check only
    tools/<area>/... (Python tools)        the tool tests of that area
    tools/gate/, tools/tests/ helpers      every tool test
    data/client, data/editor               every tool test (the data tests read them)
    deploy/                                the deployer tests
    anything else (src/, deps/, cmake/,
    CMakeLists.txt, .github/, ...)         every step, as before

The files are the merge base with --base (origin/develop) up to HEAD, plus uncommitted ones. If
git cannot answer, every step runs. The full tier (nightly, release) never uses this: it runs
everything. Prints a JSON object:

    {"cpp": bool, "tool_tests": "all" | "none" | [module, ...], "deployer": bool,
     "files": int, "reason": str}
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys

TESTS_DIR = os.path.join("tools", "tests")
# Changes in these parts of tools/ can affect any tool test.
ALL_TOOL_TESTS = ("tools/gate/", "tools/tests/__init__.py")
# Data the tool tests read.
DATA_ROOTS = ("data/client", "data/editor")
DOC_SUFFIXES = (".md",)
DOC_ROOTS = ("docs/",)
QUOTES = "[\"'/\\\\]"


def changed_files(base: str, cwd: str = ".") -> list[str] | None:
	"""Files changed since the merge base with `base`, committed or not; None when git cannot tell."""
	def git(*args):
		proc = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace")
		if proc.returncode != 0:
			raise RuntimeError(proc.stderr.strip())
		return proc.stdout

	try:
		merge_base = git("merge-base", "HEAD", base).strip()
		files = set(git("diff", "--name-only", merge_base, "HEAD").splitlines())
		for line in git("status", "--porcelain", "--untracked-files=all").splitlines():
			path = line[3:].strip().strip('"')
			if " -> " in path:
				path = path.split(" -> ", 1)[1]
			files.add(path)
	except (OSError, RuntimeError):
		return None
	return sorted(path.replace("\\", "/") for path in files if path)


def _area(path: str) -> str | None:
	"""The area of a tools/ file: its first directory under tools/, or the stem of a top-level script."""
	parts = path.split("/")
	if len(parts) < 2 or parts[0] != "tools":
		return None
	if len(parts) == 2:
		return os.path.splitext(parts[1])[0]
	return parts[1]


def _imports(source: str, module: str) -> bool:
	name = re.escape(module)
	return re.search(r"(^|\s)(import|from)\s+" + name + r"(\s|$|\.|,)", source, re.MULTILINE) is not None


def _mentions(source: str, area: str) -> bool:
	"""True when a test's source names the area as a path segment or a quoted name ("bugs",
	tools/world, parents[1] / "world") or imports the module of a top-level script."""
	name = re.escape(area)
	return re.search(QUOTES + name + "[\"'/\\\\.]", source) is not None or _imports(source, area)


def _read(path: str) -> str:
	with open(path, encoding="utf-8", errors="replace") as handle:
		return handle.read()


def tool_test_modules(areas: set[str], tests_dir: str = TESTS_DIR) -> list[str]:
	"""Test modules that name one of `areas`, themselves or through a helper module of tests_dir they
	import (world_fixtures puts tools/world on the path for most world tests)."""
	names = sorted(os.listdir(tests_dir))
	helpers = {name[:-3]: _read(os.path.join(tests_dir, name)) for name in names
		if name.endswith(".py") and not name.startswith("test_")}
	modules = []
	for name in names:
		if not (name.startswith("test_") and name.endswith(".py")):
			continue
		source = _read(os.path.join(tests_dir, name))
		used = [text for helper, text in helpers.items() if _imports(source, helper)]
		if any(_mentions(text, area) for text in [source] + used for area in areas):
			modules.append(name[:-3])
	return modules


def select(files: list[str] | None, tests_dir: str = TESTS_DIR) -> dict:
	everything = {"cpp": True, "tool_tests": "all", "deployer": True}
	if files is None:
		return dict(everything, files=0, reason="git could not list the changed files: every step runs")
	deployer = False
	all_tools = False
	areas: set[str] = set()
	own_tests: set[str] = set()
	unknown = []
	for path in files:
		if path.endswith(DOC_SUFFIXES) or path.startswith(DOC_ROOTS):
			continue
		if path.startswith(DATA_ROOTS) or path.startswith(ALL_TOOL_TESTS):
			all_tools = True
		elif path.startswith("tools/tests/"):
			name = os.path.basename(path)
			if name.startswith("test_") and name.endswith(".py"):
				own_tests.add(name[:-3])
			elif name.endswith(".py"):
				# A helper (world_fixtures): every test that imports it.
				areas.add(name[:-3])
			else:
				all_tools = True
		elif path.startswith("tools/"):
			areas.add(_area(path))
		elif path.startswith("deploy/"):
			deployer = True
		else:
			unknown.append(path)
	if unknown:
		return dict(everything, files=len(files), reason="changes outside tools/, data/, deploy/ and docs ({}{}): every step runs".format(
			", ".join(unknown[:3]), ", ..." if len(unknown) > 3 else ""))
	if all_tools:
		tool_tests = "all"
	else:
		modules = set(tool_test_modules(areas, tests_dir))
		modules |= {name for name in own_tests if os.path.exists(os.path.join(tests_dir, name + ".py"))}
		tool_tests = sorted(modules) or "none"
	parts = []
	if tool_tests == "all":
		parts.append("every tool test")
	elif tool_tests != "none":
		parts.append("{} tool test module(s) for {}".format(len(tool_tests), ", ".join(sorted(areas | own_tests))))
	if deployer:
		parts.append("the deployer tests")
	reason = "no C++ change: " + (", ".join(parts) if parts else "nothing beyond the protocol check")
	return {"cpp": False, "tool_tests": tool_tests, "deployer": deployer, "files": len(files), "reason": reason}


def main(argv=None) -> int:
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--base", default="origin/develop")
	args = parser.parse_args(argv)
	print(json.dumps(select(changed_files(args.base))))
	return 0


if __name__ == "__main__":
	sys.exit(main())
