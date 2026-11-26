#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Runs the tool tests (tools/tests/test_*.py) in parallel processes for the gate.

    python tools/gate/run_tool_tests.py [--jobs N] [--start-dir tools/tests]

The tests mostly wait: on git subprocesses, PowerShell, file I/O. Run one after the other in a
single process they took 37 minutes of the fast gate (22 s of CPU) on 2026-11-26; in parallel the
step takes as long as its slowest shard.

A shard is one test class, so a setUpClass fixture is built once per shard as before. A class
without a setUpClass of its own has independent tests and is split into chunks of CHUNK tests,
so one slow class (the bug loop's git tests, 42 tests of ~30 s each) does not decide the step's
length. Every shard runs from the repository root with the start directory on sys.path, exactly
like `python -m unittest discover -s tools/tests`. Exit code 1 when any test fails or errors.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import io
import json
import os
import subprocess
import sys
import time
import unittest

CHUNK = 3
# Shard durations of the last run, so the slowest start first and the step ends with short ones.
TIMES_FILE = os.path.join("tools", "gate", "logs", "tool_test_times.json")


def _load_times():
	try:
		with open(TIMES_FILE, encoding="utf-8") as handle:
			return json.load(handle)
	except (OSError, ValueError):
		return {}


def _save_times(timings):
	try:
		os.makedirs(os.path.dirname(TIMES_FILE), exist_ok=True)
		with open(TIMES_FILE, "w", encoding="utf-8") as handle:
			json.dump({ids[0]: round(seconds, 1) for seconds, ids in timings}, handle, indent=0, sort_keys=True)
	except OSError:
		pass


def _iter_tests(suite):
	for item in suite:
		if isinstance(item, unittest.TestSuite):
			yield from _iter_tests(item)
		else:
			yield item


def plan_shards(suite, chunk=CHUNK):
	"""Lists of test ids, one list per shard, in discovery order. Import errors become shards of
	their own (unittest's _FailedTest), so a module that does not import still fails the step."""
	classes = {}
	for test in _iter_tests(suite):
		cls = type(test)
		classes.setdefault(cls, []).append(test)
	shards = []
	for cls, tests in classes.items():
		ids = [test.id() for test in tests]
		independent = "setUpClass" not in vars(cls) and "tearDownClass" not in vars(cls) \
			and not cls.__module__.startswith("unittest.")
		if independent:
			shards.extend(ids[index:index + chunk] for index in range(0, len(ids), chunk))
		else:
			shards.append(ids)
	return shards


def _run_shard(ids, start_dir, root):
	started = time.time()
	proc = subprocess.run([sys.executable, os.path.abspath(__file__), "--worker", "--start-dir", start_dir] + ids,
		cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace")
	return ids, proc.returncode, proc.stdout + proc.stderr, time.time() - started


def _worker(start_dir, ids):
	sys.path.insert(0, os.path.abspath(start_dir))
	suite = unittest.defaultTestLoader.loadTestsFromNames(ids)
	result = unittest.TextTestRunner(stream=sys.stderr, verbosity=1).run(suite)
	return 0 if result.wasSuccessful() else 1


def main(argv=None):
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--start-dir", default=os.path.join("tools", "tests"))
	parser.add_argument("--jobs", type=int, default=min(16, os.cpu_count() or 4))
	parser.add_argument("--pattern", default="test_*.py")
	parser.add_argument("--modules", default="", help="comma-separated test modules to run instead of discovering all")
	parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
	parser.add_argument("ids", nargs="*", help=argparse.SUPPRESS)
	args = parser.parse_args(argv)
	if args.worker:
		return _worker(args.start_dir, args.ids)

	root = os.getcwd()
	sys.path.insert(0, os.path.abspath(args.start_dir))
	if args.modules:
		suite = unittest.defaultTestLoader.loadTestsFromNames([name for name in args.modules.split(",") if name])
	else:
		suite = unittest.defaultTestLoader.discover(args.start_dir, pattern=args.pattern)
	# A module that does not import cannot be re-run by name in a worker: report its error here.
	broken = [test for test in _iter_tests(suite) if type(test).__module__.startswith("unittest.")]
	if broken:
		stream = io.StringIO()
		unittest.TextTestRunner(stream=stream, verbosity=1).run(unittest.TestSuite(broken))
		print("FAILED to import:\n" + stream.getvalue(), flush=True)
	shards = [shard for shard in plan_shards(suite) if not shard[0].startswith("unittest.")]
	total = sum(len(shard) for shard in shards)
	print("tool tests: {} tests in {} shards, {} jobs".format(total, len(shards), args.jobs), flush=True)
	started = time.time()
	failed = []
	timings = []
	# The slowest shards of the last run first; unknown ones by size.
	times = _load_times()
	order = sorted(shards, key=lambda shard: (times.get(shard[0], 0.0), len(shard)), reverse=True)
	with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
		futures = [pool.submit(_run_shard, shard, args.start_dir, root) for shard in order]
		for future in concurrent.futures.as_completed(futures):
			ids, code, output, seconds = future.result()
			timings.append((seconds, ids))
			if code != 0:
				failed.append(ids)
				print("FAILED shard ({:.0f}s): {}\n{}".format(seconds, ", ".join(ids), output), flush=True)
	_save_times(timings)
	print("slowest shards:")
	for seconds, ids in sorted(timings, reverse=True)[:5]:
		print("  {:6.1f}s  {}{}".format(seconds, ids[0], " (+{} more)".format(len(ids) - 1) if len(ids) > 1 else ""))
	print("Ran {} tests in {:.1f}s".format(total + len(broken), time.time() - started))
	if failed or broken:
		print("FAILED ({} of {} shards, {} module(s) that do not import)".format(len(failed), len(shards), len(broken)))
		return 1
	print("OK")
	return 0


if __name__ == "__main__":
	sys.exit(main())
