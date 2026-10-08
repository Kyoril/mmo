# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""The newest finished GitHub "Nightly Release" run on develop, in the shape the circuit breaker
reads: (name, {"passed": bool, "merges_since_last_green": [...]}). That workflow gates develop on
Linux before anything is published, so it is the nightly that decides what reaches players."""

import json
import subprocess

from .gitops import run_git

WORKFLOW = "nightly-release.yml"
# A run whose gate never ran (nothing new since the last release) or that was cancelled says
# nothing about develop.
VERDICTS = ("success", "failure")


def newest_nightly(runs, first_parent_log):
	"""`runs` newest first, as `gh run list --json databaseId,status,conclusion,headSha` prints
	them; `first_parent_log(green, head)` lists the develop merges after `green` (None: no green
	run is known) up to `head`."""
	finished = [run for run in runs if run.get("status") == "completed" and run.get("conclusion") in VERDICTS]
	if not finished:
		return None, None
	newest = finished[0]
	name = "nightly run {}".format(newest["databaseId"])
	if newest["conclusion"] == "success":
		return name, {"passed": True, "merges_since_last_green": []}
	green = next((run["headSha"] for run in finished[1:] if run["conclusion"] == "success"), None)
	return name, {"passed": False, "merges_since_last_green": first_parent_log(green, newest["headSha"])}


def github_nightly(main_repo, gh="gh", timeout=60):
	"""The breaker's source in production: gh (authenticated as the user) and the main checkout."""

	def list_runs():
		completed = subprocess.run([gh, "run", "list", "--workflow", WORKFLOW, "--branch", "develop", "--limit", "20",
			"--json", "databaseId,status,conclusion,headSha"], cwd=main_repo, capture_output=True, text=True,
			encoding="utf-8", errors="replace", timeout=timeout)
		if completed.returncode != 0:
			raise RuntimeError("gh run list failed: " + completed.stderr.strip()[-300:])
		return json.loads(completed.stdout)

	def first_parent_log(green, head):
		run_git(main_repo, "fetch", "origin")
		span = ["{}..{}".format(green, head)] if green else ["-20", head]
		return run_git(main_repo, "log", "--first-parent", "--format=%h %s", *span).stdout.splitlines()

	return lambda: newest_nightly(list_runs(), first_parent_log)
