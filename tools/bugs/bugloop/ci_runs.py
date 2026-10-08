# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Pure evaluation of GitHub Actions runs for the bug loop's CI watch: run colours and a bounded
excerpt of a failing job's log. The excerpt is untrusted data for the fixer."""

import re

RED = ("failure", "timed_out", "startup_failure")
_TIMESTAMP = re.compile(r"^\d{4}-\d\d-\d\dT[\d:.]+Z ?")
_MARKERS = re.compile(r"FAILED|\*\*\*Exception|\berror\b|Error:|double free|SIGABRT|SIGSEGV|Assertion|tests failed|##\[error\]", re.IGNORECASE)
TAIL_LINES = 60


def colour(run):
	if run.get("status") != "completed":
		return None
	if run.get("conclusion") in RED:
		return "red"
	if run.get("conclusion") == "success":
		return "green"
	return None


def newest_completed(runs, accept):
	"""The newest run (GitHub lists newest first) that `accept`s and has a colour."""
	for run in runs:
		if accept(run) and colour(run):
			return run
	return None


def failing_step(jobs):
	for job in jobs:
		if job.get("conclusion") in RED:
			for step in job.get("steps") or []:
				if step.get("conclusion") in RED:
					return job, step.get("name", "")
			return job, ""
	return None, ""


def excerpt(log_text, limit=8000):
	lines = [_TIMESTAMP.sub("", line.rstrip()) for line in (log_text or "").splitlines()]
	keep = set()
	for index, line in enumerate(lines):
		if _MARKERS.search(line):
			# The marker line and the two after it (an abort's reason and the test name follow it).
			keep.update(range(index, min(len(lines), index + 3)))
	picked = [lines[index] for index in sorted(keep)] if keep else lines[-TAIL_LINES:]
	text = "\n".join(picked)
	if len(text) > limit:
		text = text[-limit:]
	return text
