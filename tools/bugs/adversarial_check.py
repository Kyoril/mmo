#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Runs the triage stage on the adversarial fixtures against the real model. Not part of the
gate, because it costs model calls. Run it after every change to prompts/triage.md.

    python tools/bugs/adversarial_check.py
"""

import glob
import json
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from bugloop import claude, config as loop_config, inputs, verdicts  # noqa: E402


def main():
	config = loop_config.load_config(os.path.join(HERE, "bug_loop.json"))
	runner = claude.ClaudeRunner(shutil.which(config.claude_exe) or config.claude_exe, config.model)
	with open(os.path.join(HERE, "bugloop", "prompts", "triage.md"), "r", encoding="utf-8") as handle:
		prompt = handle.read()
	with open(os.path.join(HERE, "bugloop", "schemas", "triage.json"), "r", encoding="utf-8") as handle:
		schema = json.load(handle)
	failures = 0
	for path in sorted(glob.glob(os.path.join(HERE, "adversarial", "*.json"))):
		with open(path, "r", encoding="utf-8") as handle:
			fixture = json.load(handle)
		with tempfile.TemporaryDirectory() as cwd:
			try:
				verdict = verdicts.parse_verdict(runner.structured(prompt, inputs.build_triage_input(fixture["bug"], []),
					schema, claude.TRIAGE_TOOLS, cwd, config.step_timeout_seconds))
				category = verdict["category"]
			except (claude.ClaudeError, verdicts.VerdictError) as error:
				category = "error: {}".format(error)
		ok = category in fixture["expect"]
		failures += 0 if ok else 1
		print("{:5} {:26} {} (expected {})".format("ok" if ok else "FAIL", fixture["name"], category, " / ".join(fixture["expect"])))
	return 1 if failures else 0


if __name__ == "__main__":
	sys.exit(main())
