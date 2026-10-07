# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Runs each bug-loop stage as its own `claude -p` process with its own tool set. The process
boundary is the point: the triage stage reads player text and has no tools at all.

Every stage also runs without MCP servers (--strict-mcp-config) and with a scrubbed environment:
no stage sees the bug API key, and only the fixer gets the E2E database password. The fixer
additionally runs with git settings that make any push fail; the orchestrator's own git calls
(gitops, in this Python process) are unaffected."""

import json
import os
import subprocess

from .procs import run_with_tree_kill

TRIAGE_TOOLS = ""
REVIEW_TOOLS = "Read,Grep,Glob"

# Never passed to any stage.
SECRET_ENV = ("MMO_BUG_API_KEY", "MMO_BUG_API_URL")
# Passed only to the fixer, which works in the build tree; never to the stages reading player text.
FIXER_ONLY_ENV = ("MMO_E2E_MYSQL_PASSWORD",)
FIXER_DISALLOWED_TOOLS = ("Bash(git push:*)", "Bash(git -C * push:*)")
BLOCKED_PUSH_URL = "blocked://bug-loop-fixer-cannot-push"
# Command-scope git config (highest precedence) for the fixer: every push URL is unusable and no
# credential helper can hand out a token.
FIXER_GIT_CONFIG = (
	("remote.origin.pushurl", BLOCKED_PUSH_URL),
	("remote.upstream.pushurl", BLOCKED_PUSH_URL),
	("credential.helper", ""),
)


class ClaudeError(RuntimeError):
	pass


def _parse_json_text(text):
	text = (text or "").strip()
	if text.startswith("```"):
		text = text.strip("`").strip()
		if text.startswith("json"):
			text = text[4:]
	try:
		return json.loads(text)
	except ValueError:
		return None


def stage_env(base, fixer=False):
	"""The environment of one claude process, derived from `base` (the loop's own environment)."""
	env = {key: value for key, value in base.items() if key.upper() not in SECRET_ENV}
	if not fixer:
		env = {key: value for key, value in env.items() if key.upper() not in FIXER_ONLY_ENV}
		return env
	# An explicit URL (git push git@host:...) bypasses pushurl; ssh and prompts fail instead.
	env["GIT_SSH_COMMAND"] = "false"
	env["GIT_TERMINAL_PROMPT"] = "0"
	env["GCM_INTERACTIVE"] = "never"
	try:
		index = int(env.get("GIT_CONFIG_COUNT", "0"))
	except ValueError:
		index = 0
	for key, value in FIXER_GIT_CONFIG:
		env["GIT_CONFIG_KEY_{}".format(index)] = key
		env["GIT_CONFIG_VALUE_{}".format(index)] = value
		index += 1
	env["GIT_CONFIG_COUNT"] = str(index)
	return env


class ClaudeRunner:
	def __init__(self, exe, model="", run=run_with_tree_kill, on_invoke=None, environ=None):
		self.exe = exe
		self.model = model
		self.run = run
		self.on_invoke = on_invoke
		self.environ = environ

	def _command(self, extra):
		command = [self.exe, "-p", "--output-format", "json", "--strict-mcp-config"]
		if self.model:
			command += ["--model", self.model]
		return command + extra

	def _invoke(self, command, stdin_text, cwd, timeout, fixer=False):
		if self.on_invoke:
			self.on_invoke()
		env = stage_env(os.environ if self.environ is None else self.environ, fixer)
		try:
			completed = self.run(command, input=stdin_text, capture_output=True, text=True,
				encoding="utf-8", errors="replace", cwd=cwd, timeout=timeout, env=env)
		except subprocess.TimeoutExpired:
			raise ClaudeError("claude timed out after {} s".format(timeout))
		except OSError as error:
			raise ClaudeError("cannot start claude: {}".format(error))
		if completed.returncode != 0:
			raise ClaudeError("claude exited with {}: {}".format(completed.returncode, (completed.stderr or completed.stdout or "")[-2000:]))
		try:
			result = json.loads(completed.stdout)
		except ValueError:
			raise ClaudeError("claude output is not JSON: " + (completed.stdout or "")[:500])
		if not isinstance(result, dict):
			raise ClaudeError("claude output is not a JSON object")
		if result.get("is_error"):
			raise ClaudeError("claude reported an error: " + str(result.get("result"))[:2000])
		return result

	def structured(self, prompt, input_text, schema, tools, cwd, timeout):
		"""One answer validated against `schema`, with only `tools` available ("" = none)."""
		command = self._command(["--tools", tools, "--json-schema", json.dumps(schema)])
		result = self._invoke(command, prompt + "\n\n" + input_text, cwd, timeout)
		value = result.get("structured_output")
		if value is None:
			value = _parse_json_text(result.get("result"))
		if not isinstance(value, dict):
			raise ClaudeError("claude returned no structured object")
		return value

	def agent(self, prompt, input_text, cwd, timeout, max_usd):
		"""An unattended agent with all tools except git push, working in cwd (the fixer)."""
		command = self._command(["--dangerously-skip-permissions", "--max-budget-usd", str(max_usd),
			"--disallowedTools"] + list(FIXER_DISALLOWED_TOOLS))
		return self._invoke(command, prompt + "\n\n" + input_text, cwd, timeout, fixer=True)
