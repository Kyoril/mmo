#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the bug loop's tree-killing subprocess runner with real processes."""

import ctypes
import os
import subprocess
import sys
import tempfile
import time
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools", "bugs"))

from bugloop import procs  # noqa: E402

# The child starts a sleeping grandchild that inherits the output pipes, records its pid, and sleeps.
CHILD = (
	"import subprocess, sys, time\n"
	"grandchild = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(120)'])\n"
	"open(sys.argv[1], 'w').write(str(grandchild.pid))\n"
	"print('started', flush=True)\n"
	"time.sleep(120)\n"
)


def alive(pid):
	if os.name == "nt":
		synchronize, query = 0x00100000, 0x1000
		kernel = ctypes.windll.kernel32
		handle = kernel.OpenProcess(synchronize | query, False, pid)
		if not handle:
			return False
		try:
			return kernel.WaitForSingleObject(handle, 0) != 0
		finally:
			kernel.CloseHandle(handle)
	try:
		os.kill(pid, 0)
	except OSError:
		return False
	return True


class TreeKillTests(unittest.TestCase):
	def test_timeout_kills_child_and_grandchild(self):
		with tempfile.TemporaryDirectory() as folder:
			pid_file = os.path.join(folder, "grandchild.pid")
			started = time.monotonic()
			with self.assertRaises(subprocess.TimeoutExpired):
				procs.run_with_tree_kill([sys.executable, "-c", CHILD, pid_file], capture_output=True, text=True, timeout=5)
			# subprocess.run would block here until the grandchild's 120 s sleep released the pipes.
			self.assertLess(time.monotonic() - started, 60)
			with open(pid_file, encoding="utf-8") as handle:
				grandchild = int(handle.read())
			deadline = time.monotonic() + 10
			while alive(grandchild) and time.monotonic() < deadline:
				time.sleep(0.2)
			self.assertFalse(alive(grandchild))

	def test_signature_matches_subprocess_run(self):
		completed = procs.run_with_tree_kill([sys.executable, "-c", "import sys; print(sys.stdin.read().upper())"],
			input="hi", capture_output=True, text=True, encoding="utf-8", timeout=60)
		self.assertEqual(completed.returncode, 0)
		self.assertEqual(completed.stdout.strip(), "HI")
		with self.assertRaises(subprocess.CalledProcessError):
			procs.run_with_tree_kill([sys.executable, "-c", "raise SystemExit(3)"], check=True, timeout=60)


if __name__ == "__main__":
	unittest.main()
