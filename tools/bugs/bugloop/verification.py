# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Regression proof and gate runs in the bug-loop worktree. The orchestrator verifies the
fixer's claims itself: the test must fail on the base commit and pass on the fix."""

import os
import subprocess

from .guard import TEST_PREFIXES
from .procs import run_with_tree_kill

E2E_TARGETS = ("e2e_client", "login_server", "realm_server", "world_server")


def regression_files(changes):
	return [change.path for change in changes if change.path.startswith(TEST_PREFIXES)]


class Verifier:
	def __init__(self, worktree, main_build, powershell="powershell", config="Debug", timeout=3600, run=run_with_tree_kill):
		self.worktree = worktree
		self.main_build = main_build
		self.powershell = powershell
		self.config = config
		self.timeout = timeout
		self.run = run
		self.last_output = ""
		self.last_timed_out = False

	def _run(self, command, timeout=None):
		try:
			completed = self.run(command, cwd=self.worktree.path, capture_output=True, text=True,
				encoding="utf-8", errors="replace", timeout=timeout or self.timeout)
			self.last_timed_out = False
		except subprocess.TimeoutExpired:
			self.last_output = "timed out: " + " ".join(command)
			self.last_timed_out = True
			return False
		except OSError as error:  # a missing executable is a failed step, not a loop crash
			self.last_output = "cannot run {}: {}".format(" ".join(command), error)
			self.last_timed_out = False
			return False
		self.last_output = ((completed.stdout or "") + (completed.stderr or ""))[-4000:]
		return completed.returncode == 0

	def ensure_configured(self):
		"""Configures the worktree build like the gate does (generator and MMO_* options of the
		main checkout's build)."""
		if os.path.exists(os.path.join(self.worktree.path, "build", "CMakeCache.txt")):
			return True
		script = os.path.join(self.worktree.path, "tools", "gate", "gate_worktree.ps1")
		command = ". '{0}'; Invoke-GateConfigure -Source '{1}' -MainBuild '{2}'".format(script, self.worktree.path, self.main_build)
		return self._run([self.powershell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", command])

	def build(self, targets):
		return self._run(["cmake", "--build", "build", "--config", self.config, "-t"] + list(targets))

	def _test(self, spec):
		if spec["kind"] == "unit":
			# Absolute: Windows resolves a relative executable against the parent's cwd, not cwd=.
			command = [os.path.abspath(os.path.join(self.worktree.path, "bin", self.config, spec["suite"] + ".exe"))]
			if spec.get("filter"):
				command.append(spec["filter"])
			return command, [spec["suite"]]
		return ([self.powershell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
			os.path.join("tools", "e2e", "e2e_run.ps1"), "-Scenario", spec["scenario"]], list(E2E_TARGETS))

	def _attempt(self, spec):
		command, targets = self._test(spec)
		if not self.build(targets):
			if self.last_timed_out:
				return "timeout"
			return "build_failed"
		if self._run(command):
			return "passed"
		return "timeout" if self.last_timed_out else "failed"

	def proof(self, spec, base, head, files):
		"""Runs the test with the fix's test files on `base` (must fail) and on `head` (must pass).
		`head` is the exact commit that is later gated and shipped, checked out detached."""
		if spec.get("kind") not in ("unit", "e2e"):
			return {"ok": False, "before": "", "after": "", "reason": "no runnable regression test"}
		if not files:
			return {"ok": False, "before": "", "after": "",
				"reason": "the fix changes no file under " + ", ".join(TEST_PREFIXES)}
		# A test file the fix deleted does not exist at head; checking it out would fail.
		present = set(self.worktree.git("ls-tree", "-r", "-z", "--name-only", head, "--", *files).split("\0"))
		existing = [path for path in files if path in present]
		self.worktree.checkout(base)
		on_head = False
		try:
			if existing:
				self.worktree.git("checkout", head, "--", *existing)
			before = self._attempt(spec)
			self.worktree.checkout(head)
			on_head = True
			after = self._attempt(spec)
		finally:
			if not on_head:
				self.worktree.checkout(head)
		ok = before in ("failed", "build_failed") and after == "passed"
		return {"ok": ok, "before": before, "after": after,
			"reason": "" if ok else "regression test before={} after={}".format(before, after),
			"output_tail": self.last_output[-2000:]}

	def gate(self, tier):
		report = os.path.join(self.worktree.path, "tools", "gate", "last_report.json")
		# A report from an earlier run must never be mistaken for this run's verdict.
		if os.path.exists(report):
			os.remove(report)
		ok = self._run([self.powershell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
			os.path.join(self.worktree.path, "tools", "gate", "verify.ps1"), "-Tier", tier])
		return {"ok": ok, "tier": tier, "report": report if os.path.exists(report) else "", "output_tail": self.last_output[-2000:]}
