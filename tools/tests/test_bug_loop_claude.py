#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the bug loop's claude -p runner. No real claude process is started."""

import json
import os
import subprocess
import sys
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools", "bugs"))

from bugloop import claude  # noqa: E402


class FakeRun:
	def __init__(self, stdout="", returncode=0, stderr="", raises=None):
		self.stdout = stdout
		self.returncode = returncode
		self.stderr = stderr
		self.raises = raises
		self.calls = []

	def __call__(self, command, **kwargs):
		self.calls.append((command, kwargs))
		if self.raises:
			raise self.raises
		return subprocess.CompletedProcess(command, self.returncode, self.stdout, self.stderr)


def result(**fields):
	return json.dumps(dict({"type": "result", "is_error": False, "result": ""}, **fields))


class RunnerTests(unittest.TestCase):
	def test_structured_triage_has_no_tools_and_a_schema(self):
		run = FakeRun(result(structured_output={"category": "defect"}))
		runner = claude.ClaudeRunner("claude", run=run)
		value = runner.structured("PROMPT", "INPUT", {"type": "object"}, claude.TRIAGE_TOOLS, "C:/cwd", 60)
		self.assertEqual(value, {"category": "defect"})
		command, kwargs = run.calls[0]
		self.assertEqual(command[:4], ["claude", "-p", "--output-format", "json"])
		tools_at = command.index("--tools")
		self.assertEqual(command[tools_at + 1], "")
		self.assertEqual(json.loads(command[command.index("--json-schema") + 1]), {"type": "object"})
		self.assertNotIn("--dangerously-skip-permissions", command)
		self.assertEqual(kwargs["input"], "PROMPT\n\nINPUT")
		self.assertEqual(kwargs["cwd"], "C:/cwd")
		self.assertEqual(kwargs["timeout"], 60)

	def test_structured_falls_back_to_fenced_json_in_result(self):
		run = FakeRun(result(result='```json\n{"ok": true}\n```'))
		self.assertEqual(claude.ClaudeRunner("claude", run=run).structured("p", "i", {}, "", ".", 1), {"ok": True})

	def test_structured_without_object_is_an_error(self):
		run = FakeRun(result(result="I think it is a defect."))
		with self.assertRaises(claude.ClaudeError):
			claude.ClaudeRunner("claude", run=run).structured("p", "i", {}, "", ".", 1)

	def test_failures_become_claude_errors(self):
		cases = [
			FakeRun("", returncode=1, stderr="boom"),
			FakeRun(result(is_error=True, result="rate limited")),
			FakeRun("not json"),
			FakeRun(raises=subprocess.TimeoutExpired("claude", 1)),
		]
		for run in cases:
			with self.assertRaises(claude.ClaudeError):
				claude.ClaudeRunner("claude", run=run).structured("p", "i", {}, "", ".", 1)

	def test_agent_skips_permissions_and_caps_budget(self):
		run = FakeRun(result(result="done"))
		value = claude.ClaudeRunner("claude", model="opus", run=run).agent("p", "i", "C:/wt", 99, 20.0)
		self.assertEqual(value["result"], "done")
		command = run.calls[0][0]
		self.assertIn("--dangerously-skip-permissions", command)
		self.assertEqual(command[command.index("--max-budget-usd") + 1], "20.0")
		self.assertEqual(command[command.index("--model") + 1], "opus")
		self.assertNotIn("--tools", command)

	def test_every_invocation_is_counted(self):
		counter = []
		run = FakeRun(result(structured_output={}))
		runner = claude.ClaudeRunner("claude", run=run, on_invoke=lambda: counter.append(1))
		runner.structured("p", "i", {}, "", ".", 1)
		runner.agent("p", "i", ".", 1, 1.0)
		self.assertEqual(len(counter), 2)


if __name__ == "__main__":
	unittest.main()
