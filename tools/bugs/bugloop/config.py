# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Configuration of the bug loop: tools/bugs/bug_loop.json over built-in defaults."""

import dataclasses
import json
import os


@dataclasses.dataclass(frozen=True)
class LoopConfig:
	worker: str = "bug-loop"
	poll_seconds: int = 1800
	autoship_cap_per_day: int = 5
	invocation_budget_per_day: int = 40
	freeze_start_utc: str = "21:30"
	freeze_end_utc: str = "23:59"
	max_changed_lines: int = 150
	max_changed_data_entries: int = 5
	# Empty: the worktree is resolved at startup (MMO_BUGLOOP_WORKTREE, else a sibling of the main
	# checkout), so no machine-specific path lives in the repository.
	worktree: str = ""
	claude_exe: str = "claude"
	model: str = ""
	fix_timeout_seconds: int = 5400
	fix_max_usd: float = 20.0
	step_timeout_seconds: int = 3600
	ci_poll_seconds: int = 300
	ci_push_workflow: str = "ccpp.yml"
	ci_nightly_workflow: str = "nightly-release.yml"
	ci_wait_minutes: int = 60
	# The wait for develop after an emergency ship when the Nightly Release decides (its gate alone can take 180 min).
	ci_nightly_wait_minutes: int = 240
	emergency_attempts: int = 3
	# A fix worktree that cannot reach origin/develop (fetch, checkout, submodule sync) is retried
	# every ci_poll_seconds without consuming an attempt; the maintainer hears of it once it has
	# lasted this long.
	worktree_escalate_minutes: int = 60
	# Post a Discord message on every bug status change, not only ships, pings and the summary.
	notify_status_changes: bool = True


def load_config(path):
	"""Reads a JSON object from path (a missing file means defaults). Unknown keys are an
	error, so a typo cannot silently fall back to a default."""
	values = {}
	if os.path.exists(path):
		with open(path, "r", encoding="utf-8") as handle:
			values = json.load(handle)
	known = {field.name for field in dataclasses.fields(LoopConfig)}
	unknown = sorted(set(values) - known)
	if unknown:
		raise ValueError("unknown bug loop config keys: " + ", ".join(unknown))
	return LoopConfig(**values)
