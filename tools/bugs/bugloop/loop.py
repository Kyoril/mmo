# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""The bug loop's state machine. It is the only component that writes to the bug API, to
develop or to origin; the Claude stages only return data, and the guard decides alone."""

import contextlib
import datetime
import json
import os
import traceback
import urllib.error

from . import guard, inputs, state as loop_state, verdicts, verification
from .claude import REVIEW_TOOLS, TRIAGE_TOOLS, ClaudeError

CO_AUTHOR = "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
# watch() returns this at the UTC day boundary; the scheduled task restarts the loop from a
# fresh origin/develop snapshot (new loop code, new protobuf schemas).
RESTART_EXIT_CODE = 75
# A change set is exempt from the regression-proof requirement only when every non-test path is
# data; the fixer's own `data_only` claim is never trusted.
DATA_ONLY_ROOTS = ("data/editor/data/", "data/client/Locales/", "data/client/Interface/")


def proof_exempt(changes):
	return all(change.path.startswith(DATA_ONLY_ROOTS) for change in changes if not change.path.startswith(guard.TEST_PREFIXES))


def short_id(bug_id):
	"""The last 8 hex digits of a Mongo ObjectId: its counter, unlike the timestamp prefix."""
	return bug_id[-8:]


def utcnow():
	return datetime.datetime.now(datetime.timezone.utc)


class DryRunApi:
	"""Reads through to the real API; records writes in a journal instead of sending them."""

	def __init__(self, api, journal_path):
		self.api = api
		self.journal_path = journal_path

	def list(self, *args, **kwargs):
		return self.api.list(*args, **kwargs)

	def show(self, bug_id):
		return self.api.show(bug_id)

	def _journal(self, action, bug_id, fields):
		os.makedirs(os.path.dirname(self.journal_path), exist_ok=True)
		with open(self.journal_path, "a", encoding="utf-8") as handle:
			entry = {"at": utcnow().isoformat(), "action": action, "bug": bug_id, "fields": fields}
			handle.write(json.dumps(entry, ensure_ascii=False, default=str) + "\n")

	def claim(self, bug_id, worker):
		self._journal("claim", bug_id, {"worker": worker})
		return {"_id": bug_id}

	def update(self, bug_id, release_claim=False, **fields):
		self._journal("update", bug_id, dict(fields, release_claim=release_claim))
		return {"_id": bug_id}


def decide(fix, review, guard_result, proof, gate):
	"""Every reason that forbids auto-ship. Empty means eligible; the breaker, the daily cap
	and the freeze window are checked at ship time."""
	reasons = []
	if fix.get("confidence") != "high":
		reasons.append("fixer confidence is {}".format(fix.get("confidence")))
	reasons.extend(verdicts.review_blockers(review))
	reasons.extend("guard: " + reason for reason in guard_result["reasons"])
	if proof is not None and not proof["ok"]:
		reasons.append("proof: " + proof["reason"])
	if not gate["ok"]:
		reasons.append("the full gate is red")
	return reasons


class BugLoop:
	def __init__(self, api, runner, worktree, verifier, decoder, config, state, prompts, schemas,
			artifacts_dir, report_dir, clock=utcnow, lock=contextlib.nullcontext, dry_run=False, log=print):
		self.api = api
		self.runner = runner
		self.worktree = worktree
		self.verifier = verifier
		self.decoder = decoder
		self.config = config
		self.state = state
		self.prompts = prompts
		self.schemas = schemas
		self.artifacts_dir = artifacts_dir
		self.report_dir = report_dir
		self.clock = clock
		self.lock = lock
		self.dry_run = dry_run
		self.log = log

	# ---- small helpers

	def _bug_dir(self, bug_id):
		path = os.path.join(self.artifacts_dir, bug_id)
		os.makedirs(path, exist_ok=True)
		return path

	def _write(self, bug_id, name, value):
		with open(os.path.join(self._bug_dir(bug_id), name), "w", encoding="utf-8") as handle:
			if isinstance(value, str):
				handle.write(value)
			else:
				json.dump(value, handle, indent=1, ensure_ascii=False, default=str)

	def _read(self, bug_id, name):
		with open(os.path.join(self._bug_dir(bug_id), name), "r", encoding="utf-8") as handle:
			return json.load(handle)

	def _update(self, bug_id, **fields):
		return self.api.update(bug_id, actor=self.config.worker, **fields)

	def _release(self, bug_id, status, note, **fields):
		return self._update(bug_id, status=status, note=note[:1900], release_claim=True, **fields)

	def _finish(self, bug_id, outcome, **details):
		self.state.mark_attempted(bug_id, outcome)
		self.state.record(bug_id, outcome, **details)
		self.log("bug {}: {} {}".format(bug_id, outcome, json.dumps(details, default=str) if details else ""))

	# ---- one poll

	def poll_once(self):
		"""Returns True when it did work, so the caller polls again without sleeping."""
		now = self.clock()
		self.state.roll(now.strftime("%Y-%m-%d"))
		self._check_nightly_breaker(now)
		if not self.dry_run:
			self._release_stale_claims()
			self._reconcile_parked()
		self._ship_queued(now)
		worked = self._triage_new()
		bug_id = self.state.next_fix()
		if bug_id is not None and self.state.budget_left(self.config.invocation_budget_per_day, needed=2):
			try:
				self._fix(bug_id)
			except Exception:  # the loop must survive one bad bug
				self.log(traceback.format_exc())
				self._safe_release(bug_id, "needs-info: the bug loop hit an internal error; see artifacts/bug-loop/" + bug_id)
				self._finish(bug_id, "loop-error")
			worked = True
		self._write_daily_report()
		self.state.save()
		return worked

	def watch(self, sleep):
		day = self.clock().strftime("%Y-%m-%d")
		while True:
			try:
				worked = self.poll_once()
			except Exception:  # API or network trouble: log, sleep, retry
				self.log(traceback.format_exc())
				worked = False
			if self.clock().strftime("%Y-%m-%d") != day:
				return RESTART_EXIT_CODE
			if not worked:
				sleep(self.config.poll_seconds)

	def _safe_release(self, bug_id, note):
		try:
			self._release(bug_id, "triaged", note)
		except Exception:
			self.log(traceback.format_exc())

	# ---- triage

	def _triage_new(self):
		listing = self.api.list(status="new", limit=100)
		worked = False
		for summary in sorted(listing.get("bugs", []), key=lambda bug: bug.get("createdAt") or ""):
			bug_id = summary["_id"]
			if self.state.in_fix_queue(bug_id):
				continue
			if self.state.attempted(bug_id):
				# Live, every finished bug leaves `new`; seeing it again means the user reset it.
				if self.dry_run:
					continue
				self.state.forget(bug_id)
			if not self.state.budget_left(self.config.invocation_budget_per_day):
				break
			try:
				self._triage(bug_id)
			except Exception:  # one unreadable bug must not wedge the loop
				self.log(traceback.format_exc())
				self._triage_crashed(bug_id)
			worked = True
		return worked

	def _triage_crashed(self, bug_id):
		if self.state.triage_failed(bug_id) < 2:
			return
		try:
			self._update(bug_id, note="the bug loop could not triage this bug (internal error); needs a human")
		except Exception:
			self.log(traceback.format_exc())
		self._finish(bug_id, "loop-error")

	def _release_stale_claims(self):
		"""A crash or kill mid-fix leaves the bug claimed; give it back once."""
		for bug_id, outcome in list(self.state.data["attempts"].items()):
			if outcome != "fix-started":
				continue
			try:
				bug = self.api.show(bug_id)
				if bug.get("status") != "in_progress" or bug.get("claimedBy") != self.config.worker:
					continue
				self._release(bug_id, "triaged",
					"needs-info: the bug loop was interrupted while fixing; see artifacts/bug-loop/" + bug_id)
				self._finish(bug_id, "interrupted")
			except Exception:
				self.log(traceback.format_exc())

	def _related(self, bug):
		subject = bug.get("subject") or {}
		listing = self.api.list(subject="{}:{}".format(subject.get("type", "generic"), subject.get("id", 0)), limit=50)
		return [other for other in listing.get("bugs", [])
			if other.get("_id") != bug.get("_id") and other.get("status") in verdicts.OPEN_STATUSES]

	def _triage(self, bug_id):
		bug = self.api.show(bug_id)
		self._write(bug_id, "report.json", bug)
		related = self._related(bug)
		try:
			raw = self.runner.structured(self.prompts["triage"], inputs.build_triage_input(bug, related),
				self.schemas["triage"], TRIAGE_TOOLS, self._bug_dir(bug_id), self.config.step_timeout_seconds)
			verdict = verdicts.parse_verdict(raw, [other["_id"] for other in related])
		except (ClaudeError, verdicts.VerdictError) as error:
			self._triage_failed(bug_id, error)
			return
		self._write(bug_id, "triage.json", verdict)
		self._apply_verdict(bug, verdict)

	def _triage_failed(self, bug_id, error):
		self._write(bug_id, "triage_error.txt", str(error))
		if self.state.triage_failed(bug_id) >= 2:
			self._update(bug_id, status="triaged", triage={"category": "needs-human"}, note="triage failed twice: " + str(error)[:500])
			self._finish(bug_id, "triage-invalid", reason=str(error)[:300])
		else:
			self._update(bug_id, note="triage-invalid, retrying once: " + str(error)[:500])

	def _apply_verdict(self, bug, verdict):
		bug_id = bug["_id"]
		category = verdict["category"]
		triage = {"category": category, "severity": verdict["severity"], "component": verdict["component"],
			"summary": verdict["observed"][:500]}
		if category == "duplicate":
			self._update(bug_id, status="duplicate", duplicateOf=verdict["duplicate_of"], triage=triage,
				note="duplicate: " + verdict["reasoning"][:1000])
			self._finish(bug_id, "duplicate", of=verdict["duplicate_of"])
		elif category == "abuse_suspected":
			triage["category"] = "abuse"
			triage["summary"] = verdict["abuse_evidence"][:500]
			self._update(bug_id, status="wontfix", triage=triage, note="abuse suspected: " + verdict["abuse_evidence"][:1500])
			self._finish(bug_id, "abuse", account=(bug.get("reporter") or {}).get("accountId"))
		elif category == "not_a_bug":
			self._update(bug_id, status="wontfix", triage=triage, note="not a bug: " + verdict["reasoning"][:1500])
			self._finish(bug_id, "wontfix-not-a-bug")
		elif category == "design_request":
			self._update(bug_id, status="triaged", triage=triage, note="design request, parked for the team: " + verdict["reasoning"][:1500])
			self._finish(bug_id, "design-request")
		else:
			self._update(bug_id, status="triaged", triage=triage, note="queued for an automated fix (severity {})".format(verdict["severity"]))
			self.state.enqueue_fix(bug_id, verdict["severity"], bug.get("createdAt") or "")
			self.state.record(bug_id, "queued", severity=verdict["severity"])

	# ---- fix

	def _fix(self, bug_id):
		self.state.drop_fix(bug_id)
		self.state.mark_attempted(bug_id, "fix-started")
		self.state.save()
		bug = self._read(bug_id, "report.json")
		verdict = self._read(bug_id, "triage.json")
		try:
			self.api.claim(bug_id, self.config.worker)
		except urllib.error.HTTPError as error:
			if error.code != 409:
				raise
			self._finish(bug_id, "claimed-elsewhere")
			return
		branch = "bugfix/" + short_id(bug_id)
		base = self.worktree.prepare()
		self.verifier.ensure_configured()
		self.worktree.start_branch(branch, base)
		fix_path = os.path.join(self._bug_dir(bug_id), "FIX.json")
		if os.path.exists(fix_path):
			os.remove(fix_path)
		try:
			result = self.runner.agent(self.prompts["fix"], inputs.build_fix_input(bug, verdict, branch, fix_path),
				self.worktree.path, self.config.fix_timeout_seconds, self.config.fix_max_usd)
			self._write(bug_id, "fix_result.json", result)
			fix = verdicts.load_fix(fix_path)
		except (ClaudeError, verdicts.VerdictError) as error:
			self._release(bug_id, "triaged", "needs-info: the fix stage failed: " + str(error)[:500])
			self._finish(bug_id, "needs-info", reason=str(error)[:300])
			return
		if fix["outcome"] == "no_project_basis":
			self._release(bug_id, "wontfix", "not a bug: nothing in the project defines the expected behaviour. " + fix["root_cause"][:1000],
				triage={"category": "not_a_bug"})
			self._finish(bug_id, "wontfix-no-basis")
			return
		if fix["outcome"] != "fixed":
			self._release(bug_id, "triaged", "needs-info ({}): {}".format(fix["outcome"], fix["root_cause"][:1000]))
			self._finish(bug_id, "needs-info", reason=fix["outcome"])
			return
		head = self.worktree.head()
		if head == base or not self.worktree.is_clean():
			self._release(bug_id, "triaged", "needs-info: the fixer reported a fix but left no clean commit")
			self._finish(bug_id, "needs-info", reason="no clean commit")
			return
		self._verify_and_ship(bug_id, verdict, fix, branch, base, head)

	def _verify_and_ship(self, bug_id, verdict, fix, branch, base, head):
		changes = self.worktree.changes(base, head)
		diff_text = self.worktree.unified_diff(base, head)
		self._write(bug_id, "diff.patch", diff_text)
		guard_result = guard.evaluate(changes, diff_text,
			load_data=lambda path: (self.worktree.file_bytes(base, path), self.worktree.file_bytes(head, path)),
			decoder=self.decoder, max_lines=self.config.max_changed_lines, max_entries=self.config.max_changed_data_entries)
		self._write(bug_id, "guard.json", guard_result)
		review = self._review(bug_id, verdict, fix, diff_text, guard_result["reasons"])
		with self.lock():
			proof = None
			if fix["regression_test"]["kind"] == "none":
				if not proof_exempt(changes):
					proof = {"ok": False, "reason": "a code change needs a regression test"}
			else:
				proof = self.verifier.proof(fix["regression_test"], base, branch, verification.regression_files(changes))
				self._write(bug_id, "proof.json", proof)
			self.worktree.checkout(branch)
			gate = self.verifier.gate("full")
			self._write(bug_id, "gate.json", gate)
		reasons = decide(fix, review, guard_result, proof, gate)
		self._write(bug_id, "decision.json", {"reasons": reasons, "head": head})
		if reasons:
			self._park(bug_id, branch, reasons)
			return
		self._try_ship(bug_id, branch, verdict["observed"][:200], head)

	def _review(self, bug_id, verdict, fix, diff_text, guard_reasons):
		try:
			review = self.runner.structured(self.prompts["review"], inputs.build_review_input(verdict, fix, diff_text, guard_reasons),
				self.schemas["review"], REVIEW_TOOLS, self.worktree.path, self.config.step_timeout_seconds)
		except ClaudeError as error:
			review = {"error": str(error)[:500]}
		self._write(bug_id, "review.json", review)
		return review

	# ---- park and ship

	def _park(self, bug_id, branch, reasons):
		self._update(bug_id, status="pr_open", prUrl="branch:" + branch,
			note="parked for review: " + "; ".join(reasons)[:1800], release_claim=True)
		self._finish(bug_id, "parked", branch=branch, reasons=reasons[:10])

	def _ship_blockers(self):
		blockers = []
		if loop_state.breaker_active(self.artifacts_dir):
			blockers.append("the circuit breaker is tripped")
		if not self.state.autoship_left(self.config.autoship_cap_per_day):
			blockers.append("the daily auto-ship cap is reached")
		return blockers

	def _try_ship(self, bug_id, branch, summary, head):
		blockers = self._ship_blockers()
		if blockers:
			self._park(bug_id, branch, blockers)
			return
		if self.dry_run:
			self._finish(bug_id, "would-ship", branch=branch)
			return
		if loop_state.in_freeze(self.clock(), self.config.freeze_start_utc, self.config.freeze_end_utc):
			self.state.enqueue_ship(bug_id, branch, summary, head)
			self._update(bug_id, note="fix ready on {}; ships after the nightly freeze window".format(branch))
			self.state.record(bug_id, "ship-queued", branch=branch)
			return
		self._ship(bug_id, branch, summary, head)

	def _ship(self, bug_id, branch, summary, head):
		message = "Merge {} (bug-loop, gate green at {})\n\nBug {}: {}\n\n{}".format(branch, head[:8], bug_id, summary, CO_AUTHOR)
		with self.lock():
			result = self.worktree.ship(branch, message, lambda: self.verifier.gate("fast")["ok"])
		if not result.ok:
			self._park(bug_id, branch, ["ship: " + result.reason])
			return
		self.state.count_autoship()
		# The merge is on develop now: nothing below may undo that by releasing the bug.
		try:
			self.worktree.delete_branch(branch)
			self._update(bug_id, status="resolved", release_claim=True,
				note="shipped by the bug loop in {}; reaches players with the next nightly deploy".format(result.commit))
			self._finish(bug_id, "shipped", branch=branch, commit=result.commit)
		except Exception:
			self.log(traceback.format_exc())
			self._finish(bug_id, "shipped", branch=branch, commit=result.commit, bookkeeping_failed=True)

	def _ship_queued(self, now):
		if loop_state.in_freeze(now, self.config.freeze_start_utc, self.config.freeze_end_utc):
			return
		pending = self.state.take_ship_queue()
		while pending:
			item = pending.pop(0)
			try:
				try:
					blockers = self._ship_blockers()
					if blockers:
						self._park(item["bug"], item["branch"], blockers)
					else:
						self._ship(item["bug"], item["branch"], item["summary"], item["head"])
				except Exception:
					self.log(traceback.format_exc())
					self._park(item["bug"], item["branch"], ["ship error, see the runner log"])
			except BaseException:
				# Even parking failed: keep this item and the rest for the next poll.
				self.state.data["ship_queue"] = [item] + pending + self.state.data["ship_queue"]
				raise
			self.state.data["ship_queue"] = list(pending)
			self.state.save()

	# ---- housekeeping

	def _reconcile_parked(self):
		for bug in self.api.list(status="pr_open", limit=100).get("bugs", []):
			pr = bug.get("prUrl") or ""
			if not pr.startswith("branch:"):
				continue
			commit = self.worktree.merged(pr[len("branch:"):])
			if commit:
				self._update(bug["_id"], status="resolved", note="merged into develop in " + commit)
				self.state.record(bug["_id"], "merged-by-user", commit=commit)

	def _check_nightly_breaker(self, now):
		try:
			name, report = loop_state.newest_nightly(self.report_dir)
		except Exception:  # a malformed report must not stop the loop
			self.log(traceback.format_exc())
			return
		if name and name not in self.state.data["seen_red_reports"] and loop_state.red_nightly_blames_loop(report):
			self.state.data["seen_red_reports"].append(name)
			loop_state.trip_breaker(self.artifacts_dir, "nightly {} is red and includes bug-loop merges".format(name), now)
			self.log("circuit breaker tripped by " + name)

	def _write_daily_report(self):
		day = self.state.data["day"]
		counts = {}
		for entry in self.state.data["outcomes"]:
			counts[entry["outcome"]] = counts.get(entry["outcome"], 0) + 1
		report = {
			"day": day,
			"dry_run": self.dry_run,
			"counts": counts,
			"outcomes": self.state.data["outcomes"],
			"invocations": self.state.data["invocations"],
			"autoships": self.state.data["autoships"],
			"breaker": loop_state.breaker_active(self.artifacts_dir),
			"fix_queue": len(self.state.data["fix_queue"]),
			"ship_queue": len(self.state.data["ship_queue"]),
		}
		os.makedirs(self.report_dir, exist_ok=True)
		name = "bugloop-{}{}.json".format("dry-" if self.dry_run else "", day)
		with open(os.path.join(self.report_dir, name), "w", encoding="utf-8") as handle:
			json.dump(report, handle, indent=1, ensure_ascii=False, default=str)
