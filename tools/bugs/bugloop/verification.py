# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Regression proof and gate runs in the bug-loop worktree. The orchestrator verifies the
fixer's claims itself: the test must fail on the base commit and pass on the fix."""

import os
import subprocess

from .guard import TEST_PREFIXES

E2E_TARGETS = ("e2e_client", "login_server", "realm_server", "world_server")


def regression_files(changes):
	return [change.path for change in changes if change.path.startswith(TEST_PREFIXES)]


class Verifier:
	def __init__(self, worktree, main_build, powershell="powershell", config="Debug", timeout=3600, run=subprocess.run):
		self.worktree = worktree
		self.main_build = main_build
		self.powershell = powershell
		self.config = config
		self.timeout = timeout
		self.run = run
		self.last_output = ""

	def _run(self, command, timeout=None):
		try:
			completed = self.run(command, cwd=self.worktree.path, capture_output=True, text=True,
				encoding="utf-8", errors="replace", timeout=timeout or self.timeout)
		except subprocess.TimeoutExpired:
			self.last_output = "timed out: " + " ".join(command)
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
			command = [os.path.join("bin", self.config, spec["suite"] + ".exe")]
			if spec.get("filter"):
				command.append(spec["filter"])
			return command, [spec["suite"]]
		return ([self.powershell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
			os.path.join("tools", "e2e", "e2e_run.ps1"), "-Scenario", spec["scenario"]], list(E2E_TARGETS))

	def _attempt(self, spec):
		command, targets = self._test(spec)
		if not self.build(targets):
			return "build_failed"
		return "passed" if self._run(command) else "failed"

	def proof(self, spec, base, branch, files):
		if spec.get("kind") not in ("unit", "e2e"):
			return {"ok": False, "before": "", "after": "", "reason": "no runnable regression test"}
		if not files:
			return {"ok": False, "before": "", "after": "",
				"reason": "the fix changes no file under " + ", ".join(TEST_PREFIXES)}
		self.worktree.checkout(base)
		self.worktree.git("checkout", branch, "--", *files)
		before = self._attempt(spec)
		self.worktree.checkout(branch)
		after = self._attempt(spec)
		ok = before in ("failed", "build_failed") and after == "passed"
		return {"ok": ok, "before": before, "after": after,
			"reason": "" if ok else "regression test before={} after={}".format(before, after)}

	def gate(self, tier):
		report = os.path.join(self.worktree.path, "tools", "gate", "last_report.json")
		# A report from an earlier run must never be mistaken for this run's verdict.
		if os.path.exists(report):
			os.remove(report)
		ok = self._run([self.powershell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
			os.path.join(self.worktree.path, "tools", "gate", "verify.ps1"), "-Tier", tier])
		return {"ok": ok, "tier": tier, "report": report if os.path.exists(report) else "", "output_tail": self.last_output[-2000:]}
