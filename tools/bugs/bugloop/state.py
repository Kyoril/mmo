# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Persistent bug-loop state (artifacts/bug-loop/state.json): per-day counters, attempts, the
fix and ship queues. Also the freeze window and the circuit breaker file."""

import copy
import json
import os

from .verdicts import SEVERITIES

BREAKER_FILE = "BREAKER"

_DEFAULT = {
	"day": "",
	"invocations": 0,
	"autoships": 0,
	"outcomes": [],
	"attempts": {},
	"triage_failures": {},
	"fix_queue": [],
	"ship_queue": [],
	"seen_red_reports": [],
	"refix_rounds": {},
	"pending_summary": None,
	# Maintainer-accepted features: bug id -> the accepted description. It lives here, not in
	# the bug's artifacts folder, because the fixer can write that folder.
	"features": {},
	# CI watch: the newest colour per workflow, the current red phase and a pending emergency verification.
	"ci": {"last_check": "", "colours": {}, "phase": None, "pending": None},
}


class LoopState:
	def __init__(self, path, today):
		self.path = path
		self.data = copy.deepcopy(_DEFAULT)
		if os.path.exists(path):
			with open(path, "r", encoding="utf-8") as handle:
				self.data.update(json.load(handle))
		if isinstance(self.data["features"], list):
			# State from before descriptions were kept: still features, without a description.
			self.data["features"] = {bug_id: None for bug_id in self.data["features"]}
		for key, value in copy.deepcopy(_DEFAULT["ci"]).items():
			# data.update replaced the whole dict from an old file: fill what it lacked.
			self.data["ci"].setdefault(key, value)
		if not self.data["day"]:
			self.data["day"] = today
		self.roll(today)

	def roll(self, today):
		"""Starts a new UTC day: counters reset, attempts and queues stay."""
		if self.data["day"] != today:
			# Kept so the daily summary survives a restart after midnight.
			self.data["pending_summary"] = {"day": self.data["day"], "invocations": self.data["invocations"],
				"autoships": self.data["autoships"], "outcomes": self.data["outcomes"]}
			self.data["day"] = today
			self.data["invocations"] = 0
			self.data["autoships"] = 0
			self.data["outcomes"] = []

	def save(self):
		os.makedirs(os.path.dirname(self.path), exist_ok=True)
		temp = self.path + ".tmp"
		with open(temp, "w", encoding="utf-8") as handle:
			json.dump(self.data, handle, indent=1, sort_keys=True)
		os.replace(temp, self.path)

	def budget_left(self, cap, needed=1):
		return self.data["invocations"] + needed <= cap

	def count_invocation(self):
		self.data["invocations"] += 1

	def autoship_left(self, cap):
		return self.data["autoships"] < cap

	def count_autoship(self):
		self.data["autoships"] += 1

	def attempted(self, bug_id):
		return bug_id in self.data["attempts"]

	def mark_attempted(self, bug_id, outcome):
		self.data["attempts"][bug_id] = outcome

	def forget(self, bug_id):
		"""A bug the user set back to `new` gets a fresh attempt."""
		self.data["attempts"].pop(bug_id, None)
		self.data["triage_failures"].pop(bug_id, None)

	def triage_failed(self, bug_id):
		count = self.data["triage_failures"].get(bug_id, 0) + 1
		self.data["triage_failures"][bug_id] = count
		return count

	def record(self, bug_id, outcome, **details):
		entry = {"bug": bug_id, "outcome": outcome}
		entry.update(details)
		self.data["outcomes"].append(entry)

	def enqueue_fix(self, bug_id, severity, created_at):
		if not self.in_fix_queue(bug_id):
			self.data["fix_queue"].append({"bug": bug_id, "severity": severity, "createdAt": created_at})

	def in_fix_queue(self, bug_id):
		return any(item["bug"] == bug_id for item in self.data["fix_queue"])

	def next_fix(self):
		"""Most severe first, then oldest."""
		if not self.data["fix_queue"]:
			return None

		def rank(item):
			severity = item["severity"]
			return (SEVERITIES.index(severity) if severity in SEVERITIES else len(SEVERITIES), item["createdAt"])

		return min(self.data["fix_queue"], key=rank)["bug"]

	def drop_fix(self, bug_id):
		self.data["fix_queue"] = [item for item in self.data["fix_queue"] if item["bug"] != bug_id]

	def enqueue_ship(self, bug_id, branch, summary, head, by_maintainer=False):
		self.data["ship_queue"].append({"bug": bug_id, "branch": branch, "summary": summary, "head": head,
			"by_maintainer": by_maintainer})

	def take_ship_queue(self):
		queue = self.data["ship_queue"]
		self.data["ship_queue"] = []
		return queue

	def refix_count(self, bug_id):
		return self.data["refix_rounds"].get(bug_id, 0)

	def count_refix(self, bug_id):
		self.data["refix_rounds"][bug_id] = self.refix_count(bug_id) + 1

	def ci_phase(self):
		return self.data["ci"]["phase"]

	def emergency_ticket(self):
		phase = self.data["ci"]["phase"]
		return phase.get("ticket") if phase else None

	def add_feature(self, bug_id, description=None):
		"""A maintainer-accepted feature: no round of this bug ever ships on its own. The
		description is the trusted FEATURE REQUEST text for this and every later round."""
		self.data["features"][bug_id] = description

	def is_feature(self, bug_id):
		return bug_id in self.data["features"]

	def feature_description(self, bug_id):
		"""The accepted description, or None (not a feature, or accepted before it was kept)."""
		return self.data["features"].get(bug_id)


def _minutes(text):
	hours, minutes = text.split(":")
	return int(hours) * 60 + int(minutes)


def in_freeze(now_utc, start, end):
	"""True inside [start, end] (HH:MM, UTC, inclusive); the window may cross midnight."""
	minute = now_utc.hour * 60 + now_utc.minute
	first, last = _minutes(start), _minutes(end)
	if first <= last:
		return first <= minute <= last
	return minute >= first or minute <= last


def breaker_active(artifacts_dir):
	return os.path.exists(os.path.join(artifacts_dir, BREAKER_FILE))


def trip_breaker(artifacts_dir, reason, now_utc):
	path = os.path.join(artifacts_dir, BREAKER_FILE)
	if os.path.exists(path):
		return
	os.makedirs(artifacts_dir, exist_ok=True)
	with open(path, "w", encoding="utf-8") as handle:
		handle.write("{}Z {}\n".format(now_utc.strftime("%Y-%m-%dT%H:%M:%S"), reason))


def reset_breaker(artifacts_dir):
	path = os.path.join(artifacts_dir, BREAKER_FILE)
	if os.path.exists(path):
		os.remove(path)


def red_nightly_blames_loop(report):
	"""A red nightly whose suspect merges include a bug-loop merge (`Merge bugfix/...`)."""
	if not report or report.get("passed") is not False:
		return False
	return any("Merge bugfix/" in str(line) for line in report.get("merges_since_last_green") or [])
