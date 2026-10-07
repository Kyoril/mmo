# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Runs each bug-loop stage as its own `claude -p` process with its own tool set. The process
boundary is the point: the triage stage reads player text and has no tools at all."""

import json
import subprocess

TRIAGE_TOOLS = ""
REVIEW_TOOLS = "Read,Grep,Glob"


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


class ClaudeRunner:
	def __init__(self, exe, model="", run=subprocess.run, on_invoke=None):
		self.exe = exe
		self.model = model
		self.run = run
		self.on_invoke = on_invoke

	def _command(self, extra):
		command = [self.exe, "-p", "--output-format", "json"]
		if self.model:
			command += ["--model", self.model]
		return command + extra

	def _invoke(self, command, stdin_text, cwd, timeout):
		if self.on_invoke:
			self.on_invoke()
		try:
			completed = self.run(command, input=stdin_text, capture_output=True, text=True,
				encoding="utf-8", errors="replace", cwd=cwd, timeout=timeout)
		except subprocess.TimeoutExpired:
			raise ClaudeError("claude timed out after {} s".format(timeout))
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
		"""An unattended agent with all tools, working in cwd (the fixer)."""
		command = self._command(["--dangerously-skip-permissions", "--max-budget-usd", str(max_usd)])
		return self._invoke(command, prompt + "\n\n" + input_text, cwd, timeout)
