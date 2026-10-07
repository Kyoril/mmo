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

	def test_every_stage_runs_without_mcp_servers(self):
		run = FakeRun(result(structured_output={}))
		runner = claude.ClaudeRunner("claude", run=run, environ={})
		runner.structured("p", "i", {}, claude.TRIAGE_TOOLS, ".", 1)
		runner.structured("p", "i", {}, claude.REVIEW_TOOLS, ".", 1)
		runner.agent("p", "i", ".", 1, 1.0)
		for command, _ in run.calls:
			self.assertIn("--strict-mcp-config", command)

	def test_triage_and_review_get_no_secrets(self):
		environ = {"PATH": "p", "MMO_BUG_API_KEY": "k", "MMO_BUG_API_URL": "u", "MMO_E2E_MYSQL_PASSWORD": "pw"}
		run = FakeRun(result(structured_output={}))
		runner = claude.ClaudeRunner("claude", run=run, environ=environ)
		runner.structured("p", "i", {}, claude.TRIAGE_TOOLS, ".", 1)
		runner.structured("p", "i", {}, claude.REVIEW_TOOLS, ".", 1)
		for _, kwargs in run.calls:
			self.assertEqual(kwargs["env"], {"PATH": "p"})
		self.assertEqual(environ["MMO_BUG_API_KEY"], "k")

	def test_fixer_env_blocks_pushes_and_keeps_only_the_e2e_password(self):
		environ = {"PATH": "p", "MMO_BUG_API_KEY": "k", "MMO_BUG_API_URL": "u", "MMO_E2E_MYSQL_PASSWORD": "pw"}
		run = FakeRun(result(result="done"))
		claude.ClaudeRunner("claude", run=run, environ=environ).agent("p", "i", ".", 1, 1.0)
		command, kwargs = run.calls[0]
		env = kwargs["env"]
		self.assertNotIn("MMO_BUG_API_KEY", env)
		self.assertNotIn("MMO_BUG_API_URL", env)
		self.assertEqual(env["MMO_E2E_MYSQL_PASSWORD"], "pw")
		self.assertEqual(env["GIT_TERMINAL_PROMPT"], "0")
		self.assertEqual(env["GIT_SSH_COMMAND"], "false")
		config = {env["GIT_CONFIG_KEY_{}".format(index)]: env["GIT_CONFIG_VALUE_{}".format(index)]
			for index in range(int(env["GIT_CONFIG_COUNT"]))}
		self.assertEqual(config, {"remote.origin.pushurl": claude.BLOCKED_PUSH_URL,
			"remote.upstream.pushurl": claude.BLOCKED_PUSH_URL, "credential.helper": ""})
		at = command.index("--disallowedTools")
		self.assertEqual(command[at + 1:at + 3], ["Bash(git push:*)", "Bash(git -C * push:*)"])

	def test_fixer_git_config_appends_to_existing_entries(self):
		env = claude.stage_env({"GIT_CONFIG_COUNT": "1", "GIT_CONFIG_KEY_0": "core.x", "GIT_CONFIG_VALUE_0": "y"}, fixer=True)
		self.assertEqual(env["GIT_CONFIG_KEY_0"], "core.x")
		self.assertEqual(env["GIT_CONFIG_KEY_1"], "remote.origin.pushurl")
		self.assertEqual(env["GIT_CONFIG_COUNT"], "4")

	def test_orchestrator_environment_is_untouched(self):
		before = dict(os.environ)
		claude.stage_env(os.environ, fixer=True)
		self.assertEqual(dict(os.environ), before)
		self.assertNotIn(claude.BLOCKED_PUSH_URL, " ".join(os.environ.values()))

	def test_blocked_push_url_really_fails(self):
		"""A push with the fixer's environment fails even though origin is a reachable local repo."""
		import shutil
		import tempfile
		with tempfile.TemporaryDirectory() as folder:
			origin = os.path.join(folder, "origin.git")
			clone = os.path.join(folder, "clone")
			base = {key: value for key, value in os.environ.items() if not key.upper().startswith("GIT_")}
			base.update({"GIT_CONFIG_NOSYSTEM": "1", "GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@example.test",
				"GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@example.test"})
			git = shutil.which("git")
			subprocess.run([git, "init", "-q", "--bare", origin], check=True, env=base)
			subprocess.run([git, "init", "-q", clone], check=True, env=base)
			subprocess.run([git, "-C", clone, "commit", "-q", "--allow-empty", "-m", "x"], check=True, env=base)
			subprocess.run([git, "-C", clone, "remote", "add", "origin", origin], check=True, env=base)
			blocked = subprocess.run([git, "-C", clone, "push", "origin", "HEAD:refs/heads/develop"],
				capture_output=True, text=True, env=claude.stage_env(base, fixer=True))
			self.assertNotEqual(blocked.returncode, 0)
			allowed = subprocess.run([git, "-C", clone, "push", "-q", "origin", "HEAD:refs/heads/develop"],
				capture_output=True, text=True, env=base)
			self.assertEqual(allowed.returncode, 0, allowed.stderr)

	def test_every_invocation_is_counted(self):
		counter = []
		run = FakeRun(result(structured_output={}))
		runner = claude.ClaudeRunner("claude", run=run, on_invoke=lambda: counter.append(1))
		runner.structured("p", "i", {}, "", ".", 1)
		runner.agent("p", "i", ".", 1, 1.0)
		self.assertEqual(len(counter), 2)


if __name__ == "__main__":
	unittest.main()
