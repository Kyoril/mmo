#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""End-to-end tests of the bug loop's state machine with a fake API, fake Claude stages, a
fake worktree and a fake verifier. The real diff guard runs."""

import dataclasses
import datetime
import json
import os
import re
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools", "bugs"))

from bugloop import claude, config as loop_config, gitops, guard, loop, state as loop_state  # noqa: E402

BUG_ID = "65f0aa00bb11cc22dd33ee44"
BRANCH = "bugfix/dd33ee44"
BUG = {
	"_id": BUG_ID, "createdAt": "2026-10-07T08:00:00Z", "status": "new",
	"subject": {"type": "quest", "id": 12, "guid": "0", "name": "Boar Trouble"},
	"comment": "Boar kills do not count", "reporter": {"accountId": "acc-1", "characterName": "Ayla"},
	"server": {}, "client": {}, "logTail": "",
}
GOOD_VERDICT = {"category": "defect", "severity": "high", "component": "quests", "observed": "Boar kills do not count",
	"expected_claim": "Kills count", "duplicate_of": None, "abuse_evidence": "", "reasoning": "Objective never advances."}
GOOD_FIX = {"outcome": "fixed", "root_cause": "Kill credit ignored", "expected_source": "quests.data entry 12",
	"confidence": "high", "data_only": False, "regression_test": {"kind": "unit", "suite": "game_server_tests", "filter": "[quest]"},
	"notes": ""}
GOOD_REVIEW = {"fixes_symptom": True, "expected_source_supported": True, "reduces_security": False,
	"out_of_scope_changes": False, "blocking_issues": [], "summary": "ok"}
FIX_PATH = "src/shared/game_server/ai/creature_ai_idle_state.cpp"
BENIGN_CHANGES = [guard.FileChange(FIX_PATH, 1, 1, False),
	guard.FileChange("src/tests/game_server_tests/test_creature_ai_idle_state.cpp", 20, 0, False)]
BENIGN_DIFF = ("diff --git a/{0} b/{0}\n--- a/{0}\n+++ b/{0}\n"
	"@@ -300 +300 @@ void CreatureAIIdleState::PickClosest()\n"
	"-\t\t\tif (distanceSq < closestDistanceSq)\n+\t\t\tif (distanceSq <= closestDistanceSq)\n").format(FIX_PATH)


def source_text(changed_line=None, line_number=300, total=340):
	"""Plausible surrounding code with nothing security-relevant; one line can be replaced."""
	lines = ["\tDoWork({});".format(index) for index in range(total)]
	if changed_line is not None:
		lines[line_number - 1] = changed_line
	return ("\n".join(lines) + "\n").encode("utf-8")


FILES = {
	"base1": {FIX_PATH: source_text("\t\t\tif (distanceSq < closestDistanceSq)")},
	"head1": {FIX_PATH: source_text("\t\t\tif (distanceSq <= closestDistanceSq)")},
}
for _rev in ("base1", "head1"):
	for _path in ("src/realm_server/player.cpp", "src/login_server/login_session.cpp"):
		FILES[_rev][_path] = source_text(line_number=795, total=830)


ADMIN_DIFF = ("diff --git a/src/realm_server/player.cpp b/src/realm_server/player.cpp\n"
	"--- a/src/realm_server/player.cpp\n+++ b/src/realm_server/player.cpp\n"
	"@@ -795 +795 @@ PacketParseResult Player::OnCheatCommand()\n-\t\t\tif (!HasGMLevel(gm_level::Gm))\n+\t\t\tif (false)\n")


def utc(text):
	return datetime.datetime.strptime(text, "%Y-%m-%d %H:%M").replace(tzinfo=datetime.timezone.utc)


class FakeApi:
	def __init__(self, bugs):
		self.bugs = {bug["_id"]: dict(bug) for bug in bugs}
		self.updates = []
		self.claims = []

	def list(self, status=None, subject=None, since=None, page=1, limit=20):
		result = list(self.bugs.values())
		if status:
			result = [bug for bug in result if bug["status"] == status]
		if subject:
			kind, _, ident = subject.partition(":")
			result = [bug for bug in result if bug["subject"]["type"] == kind and str(bug["subject"]["id"]) == ident]
		return {"bugs": [dict(bug) for bug in result], "pagination": {"total": len(result), "page": 1, "pages": 1}}

	def show(self, bug_id):
		return dict(self.bugs[bug_id])

	def claim(self, bug_id, worker):
		self.claims.append(bug_id)
		self.bugs[bug_id]["status"] = "in_progress"
		return dict(self.bugs[bug_id])

	def update(self, bug_id, release_claim=False, **fields):
		self.updates.append((bug_id, dict(fields, release_claim=release_claim)))
		for key in ("status", "prUrl", "duplicateOf", "triage"):
			if key in fields:
				self.bugs[bug_id][key] = fields[key]
		return dict(self.bugs[bug_id])

	def notes(self, bug_id):
		return [fields.get("note", "") for bug, fields in self.updates if bug == bug_id]


class FakeRunner:
	def __init__(self, verdict, fix, review):
		self.verdict = verdict
		self.fix = fix
		self.review = review
		self.calls = []

	def structured(self, prompt, input_text, schema, tools, cwd, timeout):
		self.calls.append(("triage" if tools == claude.TRIAGE_TOOLS else "review"))
		if tools == claude.TRIAGE_TOOLS:
			if isinstance(self.verdict, Exception):
				raise self.verdict
			return dict(self.verdict)
		return dict(self.review)

	def agent(self, prompt, input_text, cwd, timeout, max_usd):
		self.calls.append("fix")
		path = re.search(r"Write FIX.json to: (.+)", input_text).group(1).strip()
		with open(path, "w", encoding="utf-8") as handle:
			json.dump(self.fix, handle)
		return {"result": "done"}


class FakeWorktree:
	path = "wt"

	def __init__(self, changes, diff):
		self.changes_ = changes
		self.diff = diff
		self.head_ = "head1"
		self.clean = True
		self.shipped = []
		self.ship_result = gitops.ShipResult(True, "merge1", "")
		self.merged_ = {}
		self.deleted = []
		self.prepare_error = None

	def prepare(self):
		if self.prepare_error:
			raise self.prepare_error
		return "base1"

	def start_branch(self, branch, base):
		pass

	def head(self, ref="HEAD"):
		return self.head_

	def is_clean(self):
		return self.clean

	def changes(self, base, head):
		return self.changes_

	def unified_diff(self, base, head):
		return self.diff

	def file_bytes(self, rev, path):
		return FILES.get(rev, {}).get(path)

	def checkout(self, ref):
		pass

	def ship(self, branch, message, fast_gate):
		self.shipped.append((branch, message))
		return self.ship_result

	def merged(self, branch):
		return self.merged_.get(branch)

	def delete_branch(self, branch):
		self.deleted.append(branch)


class FakeVerifier:
	def __init__(self, proof_ok=True, gate_ok=True):
		self.proof_ok = proof_ok
		self.gate_ok = gate_ok
		self.gates = []

	def ensure_configured(self):
		return True

	def proof(self, spec, base, branch, files):
		return {"ok": self.proof_ok, "before": "failed", "after": "passed" if self.proof_ok else "failed",
			"reason": "" if self.proof_ok else "regression test before=failed after=failed"}

	def gate(self, tier):
		self.gates.append(tier)
		return {"ok": self.gate_ok, "tier": tier}


class LoopTests(unittest.TestCase):
	def make(self, verdict=GOOD_VERDICT, fix=GOOD_FIX, review=GOOD_REVIEW, changes=BENIGN_CHANGES, diff=BENIGN_DIFF,
			bugs=None, now="2026-10-07 10:00", dry_run=False, proof_ok=True, gate_ok=True, **config):
		folder = tempfile.TemporaryDirectory()
		self.addCleanup(folder.cleanup)
		self.artifacts = os.path.join(folder.name, "artifacts")
		self.reports = os.path.join(folder.name, "reports")
		self.api = FakeApi(bugs or [BUG])
		self.runner = FakeRunner(verdict, fix, review)
		self.worktree = FakeWorktree(changes, diff)
		self.verifier = FakeVerifier(proof_ok, gate_ok)
		self.now = utc(now)
		self.state = loop_state.LoopState(os.path.join(self.artifacts, "state.json"), self.now.strftime("%Y-%m-%d"))
		api = loop.DryRunApi(self.api, os.path.join(self.artifacts, "journal.jsonl")) if dry_run else self.api
		self.loop = loop.BugLoop(api, self.runner, self.worktree, self.verifier, None,
			dataclasses.replace(loop_config.LoopConfig(), **config), self.state,
			{"triage": "T", "fix": "F", "review": "R"}, {"triage": {}, "review": {}}, self.artifacts, self.reports,
			clock=lambda: self.now, dry_run=dry_run, log=lambda message: None)
		return self.loop

	def outcomes(self):
		return [entry["outcome"] for entry in self.state.data["outcomes"]]

	def test_abuse_is_flagged_and_never_fixed(self):
		self.make(verdict=dict(GOOD_VERDICT, category="abuse_suspected", abuse_evidence="asks to disable admin checks"))
		self.loop.poll_once()
		bug = self.api.bugs[BUG_ID]
		self.assertEqual(bug["status"], "wontfix")
		self.assertEqual(bug["triage"]["category"], "abuse")
		self.assertEqual(bug["triage"]["summary"], "asks to disable admin checks")
		self.assertEqual(self.api.claims, [])
		self.assertEqual(self.runner.calls, ["triage"])
		self.assertEqual(self.state.data["outcomes"][0]["account"], "acc-1")

	def test_not_a_bug_and_design_request(self):
		self.make(verdict=dict(GOOD_VERDICT, category="not_a_bug"))
		self.loop.poll_once()
		self.assertEqual(self.api.bugs[BUG_ID]["status"], "wontfix")
		self.make(verdict=dict(GOOD_VERDICT, category="design_request"))
		self.loop.poll_once()
		self.assertEqual(self.api.bugs[BUG_ID]["status"], "triaged")
		self.assertEqual(self.runner.calls, ["triage"])

	def test_duplicate_of_related_open_bug(self):
		other = dict(BUG, _id="aaaaaaaaaaaaaaaaaaaaaaaa", status="triaged", createdAt="2026-10-01T00:00:00Z")
		self.make(verdict=dict(GOOD_VERDICT, category="duplicate", duplicate_of=other["_id"]), bugs=[BUG, other])
		self.loop.poll_once()
		self.assertEqual(self.api.bugs[BUG_ID]["status"], "duplicate")
		self.assertEqual(self.api.bugs[BUG_ID]["duplicateOf"], other["_id"])

	def test_invalid_triage_retries_once_then_needs_human(self):
		self.make(verdict=dict(GOOD_VERDICT, category="duplicate", duplicate_of="ffffffffffffffffffffffff"))
		self.loop.poll_once()
		self.assertEqual(self.api.bugs[BUG_ID]["status"], "new")
		self.loop.poll_once()
		self.assertEqual(self.api.bugs[BUG_ID]["status"], "triaged")
		self.assertEqual(self.api.bugs[BUG_ID]["triage"]["category"], "needs-human")
		self.assertEqual(self.outcomes(), ["triage-invalid"])

	def test_green_fix_ships(self):
		self.make()
		self.loop.poll_once()
		self.assertEqual(self.runner.calls, ["triage", "fix", "review"])
		self.assertEqual(len(self.worktree.shipped), 1)
		branch, message = self.worktree.shipped[0]
		self.assertEqual(branch, BRANCH)
		self.assertTrue(message.startswith("Merge {} (bug-loop, gate green at head1)".format(BRANCH)))
		self.assertIn(loop.CO_AUTHOR, message)
		self.assertEqual(self.api.bugs[BUG_ID]["status"], "resolved")
		self.assertTrue(self.api.updates[-1][1]["release_claim"])
		self.assertEqual(self.state.data["autoships"], 1)
		self.assertEqual(self.worktree.deleted, [BRANCH])
		self.assertEqual(self.verifier.gates, ["full"])
		with open(os.path.join(self.reports, "bugloop-2026-10-07.json"), encoding="utf-8") as handle:
			self.assertEqual(json.load(handle)["counts"]["shipped"], 1)

	def assertParked(self, fragment):
		bug = self.api.bugs[BUG_ID]
		self.assertEqual(bug["status"], "pr_open")
		self.assertEqual(bug["prUrl"], "branch:" + BRANCH)
		self.assertIn(fragment, self.api.notes(BUG_ID)[-1])
		self.assertEqual(self.worktree.shipped, [])

	def test_guard_block_parks(self):
		self.make(changes=BENIGN_CHANGES + [guard.FileChange("src/login_server/login_session.cpp", 1, 1, False)])
		self.loop.poll_once()
		self.assertParked("protected path")

	def test_admin_bypass_parks_even_with_a_happy_review(self):
		self.make(changes=[guard.FileChange("src/realm_server/player.cpp", 1, 1, False)], diff=ADMIN_DIFF)
		self.loop.poll_once()
		self.assertParked("security-sensitive identifier")

	def test_low_confidence_red_gate_failed_proof_and_bad_review_park(self):
		for kwargs, fragment in (
				({"fix": dict(GOOD_FIX, confidence="medium")}, "confidence is medium"),
				({"gate_ok": False}, "full gate is red"),
				({"proof_ok": False}, "proof:"),
				({"review": dict(GOOD_REVIEW, reduces_security=True)}, "reduces a security")):
			self.make(**kwargs)
			self.loop.poll_once()
			self.assertParked(fragment)

	def test_no_project_basis_is_wontfix(self):
		self.make(fix=dict(GOOD_FIX, outcome="no_project_basis", expected_source=""))
		self.loop.poll_once()
		self.assertEqual(self.api.bugs[BUG_ID]["status"], "wontfix")
		self.assertEqual(self.api.bugs[BUG_ID]["triage"]["category"], "not_a_bug")
		self.assertEqual(self.verifier.gates, [])

	def test_fixer_without_a_commit_needs_info(self):
		self.make()
		self.worktree.head_ = "base1"
		self.loop.poll_once()
		self.assertEqual(self.api.bugs[BUG_ID]["status"], "triaged")
		self.assertIn("needs-info", self.api.notes(BUG_ID)[-1])

	def test_breaker_and_cap_park(self):
		self.make()
		loop_state.trip_breaker(self.artifacts, "test", self.now)
		self.loop.poll_once()
		self.assertParked("circuit breaker")
		self.make(autoship_cap_per_day=0)
		self.loop.poll_once()
		self.assertParked("auto-ship cap")

	def test_freeze_window_queues_then_ships(self):
		self.make(now="2026-10-07 22:00")
		self.loop.poll_once()
		self.assertEqual(self.worktree.shipped, [])
		self.assertEqual(len(self.state.data["ship_queue"]), 1)
		self.now = utc("2026-10-08 00:10")
		self.loop.poll_once()
		self.assertEqual(len(self.worktree.shipped), 1)
		self.assertEqual(self.api.bugs[BUG_ID]["status"], "resolved")

	def test_dry_run_writes_nothing(self):
		self.make(dry_run=True)
		self.loop.poll_once()
		self.assertEqual(self.api.updates, [])
		self.assertEqual(self.api.claims, [])
		self.assertEqual(self.worktree.shipped, [])
		self.assertIn("would-ship", self.outcomes())
		self.assertTrue(os.path.exists(os.path.join(self.artifacts, "journal.jsonl")))
		self.loop.poll_once()
		self.assertEqual(self.runner.calls.count("triage"), 1)

	def test_budget_exhausted_does_nothing(self):
		self.make(invocation_budget_per_day=0)
		self.assertFalse(self.loop.poll_once())
		self.assertEqual(self.runner.calls, [])

	def test_parked_branch_merged_by_user_resolves(self):
		self.make(bugs=[dict(BUG, status="pr_open", prUrl="branch:" + BRANCH)])
		self.worktree.merged_[BRANCH] = "abc123"
		self.loop.poll_once()
		self.assertEqual(self.api.bugs[BUG_ID]["status"], "resolved")
		self.assertIn("abc123", self.api.notes(BUG_ID)[-1])

	def test_red_nightly_with_loop_merge_trips_breaker_once(self):
		self.make(verdict=dict(GOOD_VERDICT, category="not_a_bug"))
		os.makedirs(self.reports)
		with open(os.path.join(self.reports, "nightly-2026-10-07.json"), "w", encoding="utf-8-sig") as handle:
			json.dump({"passed": False, "merges_since_last_green": ["abc Merge bugfix/0a1b2c3d (bug-loop, gate green at 1)"]}, handle)
		self.loop.poll_once()
		self.assertTrue(loop_state.breaker_active(self.artifacts))
		loop_state.reset_breaker(self.artifacts)
		self.loop.poll_once()
		self.assertFalse(loop_state.breaker_active(self.artifacts))

	def test_user_reset_to_new_gets_a_fresh_attempt(self):
		self.make(verdict=dict(GOOD_VERDICT, category="not_a_bug"))
		self.loop.poll_once()
		self.api.bugs[BUG_ID]["status"] = "new"
		self.loop.poll_once()
		self.assertEqual(self.runner.calls, ["triage", "triage"])

	def test_internal_error_releases_the_bug(self):
		self.make()
		self.worktree.prepare_error = RuntimeError("disk full")
		self.loop.poll_once()
		self.assertEqual(self.api.bugs[BUG_ID]["status"], "triaged")
		self.assertTrue(self.api.updates[-1][1]["release_claim"])
		self.assertIn("loop-error", self.outcomes())


class DecideTests(unittest.TestCase):
	def test_all_green_has_no_reasons(self):
		self.assertEqual(loop.decide(GOOD_FIX, GOOD_REVIEW, {"reasons": []}, {"ok": True}, {"ok": True}), [])

	def test_data_only_fix_without_proof(self):
		self.assertEqual(loop.decide(GOOD_FIX, GOOD_REVIEW, {"reasons": []}, None, {"ok": True}), [])

	def test_reasons_accumulate(self):
		reasons = loop.decide(dict(GOOD_FIX, confidence="low"), {"error": "x"}, {"reasons": ["a: protected path"]},
			{"ok": False, "reason": "before=passed"}, {"ok": False})
		self.assertEqual(len(reasons), 5)


if __name__ == "__main__":
	unittest.main()
