# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""The bug loop's state machine. It is the only component that writes to the bug API, to
develop or to origin; the Claude stages only return data, and the guard decides alone."""

import contextlib
import datetime
import hashlib
import json
import os
import re
import traceback
import urllib.error

from . import ci, github, guard, inputs, notify, state as loop_state, verdicts, verification
from .claude import REVIEW_TOOLS, TRIAGE_TOOLS, ClaudeError

CO_AUTHOR = "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
# watch() returns this at the UTC day boundary; the scheduled task restarts the loop from a
# fresh origin/develop snapshot (new loop code, new protobuf schemas).
RESTART_EXIT_CODE = 75
# A change set is exempt from the regression-proof requirement only when every non-test path is
# data; the fixer's own `data_only` claim is never trusted.
# Client UI Lua under data/client/Interface/ cannot cheat the server, so it needs no proof either.
DATA_ONLY_ROOTS = ("data/editor/data/", "data/client/Locales/", "data/client/Interface/")
# Statuses of bugs parked for the maintainer.
PARKED_STATUSES = ("pr_open", "needs_decision")
# The web UI shows the candidate diff; the API stores at most this much.
REVIEW_DIFF_LIMIT = 128 * 1024
TRUNCATION_MARKER = "\n... (diff truncated for the web UI)"
# Guided refixes a maintainer may request per bug.
MAX_REFIX = 3
# Every feature parks for the maintainer: only a hash-bound "ship" decision merges one.
FEATURE_REASON = "feature: shipping needs the maintainer's approval"
# Bug ids are Mongo ObjectIds; they become paths and branch names, so nothing else is accepted.
BUG_ID = re.compile(r"^[0-9a-f]{24}$")
# While develop is red in CI nothing but the emergency ticket ships; everything else queues.
RED_REASON = "develop is red"
CI_NAMES = {"push": "Linux Servers", "nightly": "Nightly Release"}
# The triage verdict written for an emergency ticket (for the operator; the loop reads its context from state).
EMERGENCY_VERDICT = {"category": "defect", "severity": "critical", "component": "ci", "observed": "develop is red in CI",
	"expected_claim": "develop is green in CI", "duplicate_of": None, "abuse_evidence": "",
	"reasoning": "written by the bug loop from a red CI run"}


_CI_ACCEPT = {
	"push": lambda run: run.get("event") == "push" and run.get("head_branch") == "develop",
	"nightly": lambda run: run.get("event") in ("schedule", "workflow_dispatch"),
}


def valid_bug_id(bug_id):
	return isinstance(bug_id, str) and BUG_ID.match(bug_id) is not None


def proof_exempt(changes):
	production = [change for change in changes if guard.is_production(change.path)]
	return bool(production) and all(change.path.startswith(DATA_ONLY_ROOTS) for change in production)


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

	def put_review_diff(self, bug_id, diff, actor="unknown"):
		self._journal("review-diff", bug_id, {"length": len(diff), "actor": actor})
		return {"reviewDiffLength": len(diff)}


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
			artifacts_dir, report_dir, clock=utcnow, lock=contextlib.nullcontext, dry_run=False, log=print, notifier=None,
			github=None):
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
		self.notifier = notify.Notifier("", log=log) if dry_run or notifier is None else notifier
		# GitHub Actions client of the CI watch; None turns the watch off.
		self.github = github
		self._backfilled = False

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
		today = now.strftime("%Y-%m-%d")
		self.state.roll(today)
		if self.state.data.get("pending_summary"):
			if not self.dry_run:
				self._send_daily_summary(self.state.data["pending_summary"])
			self.state.data["pending_summary"] = None
		self._check_nightly_breaker(now)
		worked = False
		if not self.dry_run:
			# First, so a red develop is known before anything can ship in this poll.
			self._watch_ci(now)
			self._backfill_review_diffs()
			worked = self._handle_decisions() or worked
			self._release_stale_claims()
			self._reconcile_parked()
		self._ship_queued(now)
		worked = self._triage_new() or worked
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
				sleep(min(self.config.poll_seconds, self.config.ci_poll_seconds) if self.github else self.config.poll_seconds)

	def _safe_release(self, bug_id, note, status="triaged", **fields):
		try:
			self._release(bug_id, status, note, **fields)
		except Exception:
			self.log(traceback.format_exc())

	# ---- triage

	def _triage_new(self):
		listing = self.api.list(status="new", limit=100)
		worked = False
		for summary in sorted(listing.get("bugs", []), key=lambda bug: bug.get("createdAt") or ""):
			bug_id = summary.get("_id")
			if not valid_bug_id(bug_id):
				self.log("skipping a bug with a malformed id: {!r}".format(bug_id)[:200])
				continue
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
		ship_queue_ids = {item["bug"] for item in self.state.data["ship_queue"]}
		for bug_id, outcome in list(self.state.data["attempts"].items()):
			if outcome != "fix-started":
				continue
			if bug_id in ship_queue_ids:
				continue
			try:
				bug = self.api.show(bug_id)
				if bug.get("status") != "in_progress" or bug.get("claimedBy") != self.config.worker:
					continue
				# A bug parked before (an interrupted refix) goes back to the maintainer, not to triage.
				parked = (bug.get("prUrl") or "").startswith("branch:")
				note = "needs-info: the bug loop was interrupted while fixing; see artifacts/bug-loop/" + bug_id
				if self.state.is_feature(bug_id) and not parked:
					# An interrupted first feature run goes back to where `implement` can be chosen again.
					self._release(bug_id, "triaged", note + "; decide again", triage={"category": "design_request"})
				else:
					self._release(bug_id, "pr_open" if parked else "triaged", note)
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

	def _fix(self, bug_id, guidance=None, feature=None):
		self.state.drop_fix(bug_id)
		if not valid_bug_id(bug_id):
			self.log("dropping a queued fix with a malformed bug id: {!r}".format(bug_id)[:200])
			return
		self.state.mark_attempted(bug_id, "fix-started")
		self.state.save()
		bug = self._read(bug_id, "report.json")
		verdict = self._read(bug_id, "triage.json")
		# A guided refix that fails goes back to the maintainer, who can only decide on parked bugs;
		# a failed feature run goes back to where `implement` can be chosen again.
		failed_status = "pr_open" if guidance else "triaged"
		fresh_feature = bool(feature) and not guidance
		failed_fields = {"triage": {"category": "design_request"}} if fresh_feature else {}
		again = "; decide again" if fresh_feature else ""
		previous = self._previous_attempt(bug_id) if guidance else None
		try:
			self.api.claim(bug_id, self.config.worker)
		except urllib.error.HTTPError as error:
			if error.code != 409:
				raise
			if fresh_feature:
				# Nothing was relabelled yet, so the bug keeps the status and category `implement` accepts.
				self._update(bug_id, note="could not start implementing: claimed elsewhere; decide again")
			self._finish(bug_id, "claimed-elsewhere")
			return
		if fresh_feature:
			# Relabel only once the bug is ours. The description lives in the loop state: the fixer
			# can write the artifacts folder. A stale `branch:` prUrl from an earlier run is cleared,
			# so an interrupted run goes back to where `implement` can be chosen, not to a parked review.
			self.state.add_feature(bug_id, feature)
			self.state.save()
			self._update(bug_id, triage={"category": "feature"}, prUrl="", note="accepted as a feature by the maintainer")
		branch = "bugfix/" + short_id(bug_id)
		base = self.worktree.prepare()
		if not self.verifier.ensure_configured():
			self._release(bug_id, failed_status, "needs-info: build configure failed in the bug-loop worktree" + again, **failed_fields)
			self._finish(bug_id, "needs-info", reason="build configure failed")
			return
		if guidance:
			try:
				start = self.worktree.resume_branch(branch)
				base = self.worktree.fork_point(branch)
			except Exception as error:
				self._release(bug_id, "pr_open", "cannot refix: the branch {} is gone or unusable ({}); decide again".format(branch, str(error)[:200]))
				self._finish(bug_id, "needs-info", reason="branch unusable")
				return
		else:
			self.worktree.start_branch(branch, base)
			start = base
		fix_path = os.path.join(self._bug_dir(bug_id), "FIX.json")
		if os.path.exists(fix_path):
			os.remove(fix_path)
		try:
			result = self.runner.agent(self.prompts["fix"],
				inputs.build_fix_input(bug, verdict, branch, fix_path, guidance=guidance, previous=previous, feature=feature),
				self.worktree.path, self.config.fix_timeout_seconds, self.config.fix_max_usd)
			self._write(bug_id, "fix_result.json", result)
			fix = verdicts.load_fix(fix_path)
		except (ClaudeError, verdicts.VerdictError) as error:
			self._release(bug_id, failed_status, "needs-info: the fix stage failed: " + str(error)[:500] + again, **failed_fields)
			self._finish(bug_id, "needs-info", reason=str(error)[:300])
			return
		if fix["outcome"] == "no_project_basis" and feature:
			self._release(bug_id, failed_status,
				"needs-info: the fixer found no way to implement the feature: " + fix["root_cause"][:1000] + again, **failed_fields)
			self._finish(bug_id, "needs-info", reason="no_project_basis")
			return
		if fix["outcome"] == "no_project_basis":
			self._release(bug_id, "wontfix", "not a bug: nothing in the project defines the expected behaviour. " + fix["root_cause"][:1000],
				triage={"category": "not_a_bug"})
			self._finish(bug_id, "wontfix-no-basis")
			return
		if fix["outcome"] != "fixed":
			self._release(bug_id, failed_status, "needs-info ({}): {}".format(fix["outcome"], fix["root_cause"][:1000]) + again, **failed_fields)
			self._finish(bug_id, "needs-info", reason=fix["outcome"])
			return
		head = self.worktree.head()
		if head == start or not self.worktree.is_clean():
			self._release(bug_id, failed_status, "needs-info: the fixer reported a fix but left no new clean commit" + again, **failed_fields)
			self._finish(bug_id, "needs-info", reason="no clean commit")
			return
		# Everything below judges `head`; the branch must point there, or a later ship would merge
		# something nobody guarded.
		if self.worktree.head(branch) != head:
			self._release(bug_id, failed_status, "needs-info: the fixer moved the branch away from the checked-out commit" + again, **failed_fields)
			self._finish(bug_id, "needs-info", reason="fixer moved the branch")
			return
		self._verify_and_ship(bug_id, verdict, fix, branch, base, head, guidance=guidance, feature=feature)

	def _verify_and_ship(self, bug_id, verdict, fix, branch, base, head, guidance=None, feature=None):
		changes = self.worktree.changes(base, head)
		diff_text = self.worktree.unified_diff(base, head)
		self._write(bug_id, "diff.patch", diff_text)
		guard_result = guard.evaluate(changes, diff_text,
			load_data=lambda path: (self.worktree.file_bytes(base, path), self.worktree.file_bytes(head, path)),
			decoder=self.decoder, max_lines=self.config.max_changed_lines, max_entries=self.config.max_changed_data_entries)
		self._write(bug_id, "guard.json", guard_result)
		if len(diff_text) > inputs.DIFF_LIMIT:
			# The reviewer would only see a truncated diff; nothing that large ships unreviewed.
			reasons = ["diff too large for review ({} characters, limit {})".format(len(diff_text), inputs.DIFF_LIMIT)]
			reasons += ["guard: " + reason for reason in guard_result["reasons"]]
			if self.state.is_feature(bug_id):
				reasons.append(FEATURE_REASON)
			self._write(bug_id, "decision.json", {"reasons": reasons, "head": head})
			self._park(bug_id, branch, reasons)
			return
		review = self._review(bug_id, verdict, fix, diff_text, guard_result["reasons"], guidance=guidance, feature=feature)
		with self.lock():
			proof = None
			if fix["regression_test"]["kind"] == "none":
				if not proof_exempt(changes):
					proof = {"ok": False, "reason": "a code change needs a regression test"}
			else:
				proof = self.verifier.proof(fix["regression_test"], base, head, verification.regression_files(changes))
				self._write(bug_id, "proof.json", proof)
			self.worktree.checkout(head)
			gate = self.verifier.gate("full")
			self._write(bug_id, "gate.json", gate)
		reasons = decide(fix, review, guard_result, proof, gate)
		if self.state.is_feature(bug_id):
			green = not reasons
			reasons = reasons + [FEATURE_REASON]
		else:
			green = False
		self._write(bug_id, "decision.json", {"reasons": reasons, "head": head})
		if reasons:
			self._park(bug_id, branch, reasons, review)
			# Pinged once, when a fresh implement run first parks green; guided refixes stay quiet.
			if green and feature and not guidance:
				self.notifier.send(notify.feature_ready_message(self.notifier, bug_id, self._summary(bug_id), branch))
			return
		self._try_ship(bug_id, branch, verdict["observed"][:200], head)

	def _review(self, bug_id, verdict, fix, diff_text, guard_reasons, guidance=None, feature=None):
		try:
			review = self.runner.structured(self.prompts["review"], inputs.build_review_input(verdict, fix, diff_text, guard_reasons, guidance=guidance, feature=feature),
				self.schemas["review"], REVIEW_TOOLS, self.worktree.path, self.config.step_timeout_seconds)
		except ClaudeError as error:
			review = {"error": str(error)[:500]}
		self._write(bug_id, "review.json", review)
		return review

	# ---- park and ship

	def _park(self, bug_id, branch, reasons, review=None):
		question = ""
		if isinstance(review, dict) and isinstance(review.get("design_question"), str):
			question = review["design_question"].strip()[:2000]
		self._update(bug_id, status="needs_decision" if question else "pr_open", prUrl="branch:" + branch,
			designQuestion=question, note="parked for review: " + "; ".join(reasons)[:1800], release_claim=True)
		self._upload_diff(bug_id)
		self._finish(bug_id, "parked", branch=branch, reasons=reasons[:10], decision_needed=bool(question))
		if question:
			self.notifier.send(notify.design_question_message(self.notifier, bug_id, self._summary(bug_id), question, branch))

	# ---- maintainer decisions

	def _handle_decisions(self):
		try:
			pending = self.api.list(decision_pending=True, limit=20).get("bugs", [])
		except Exception:
			self.log(traceback.format_exc())
			return False
		worked = False
		for summary in pending:
			bug_id = summary.get("_id")
			if not valid_bug_id(bug_id):
				self.log("skipping a decision with a malformed bug id: {!r}".format(bug_id)[:200])
				continue
			decision = summary.get("decision") or {}
			# An API that predates decisions ignores decisionPending and lists every bug.
			if not isinstance(decision, dict) or not decision.get("action") or decision.get("consumedAt"):
				continue
			action = decision.get("action")
			guidance = (decision.get("guidance") or "").strip()
			if action in ("refix", "implement") and not self.state.budget_left(self.config.invocation_budget_per_day, needed=2):
				continue  # stays pending until tomorrow's budget
			if action == "refix" and not guidance:
				# Without guidance a refix would be a fresh fix that resets the branch.
				try:
					self._update(bug_id, decisionConsumed=True)
					self._update(bug_id, status="needs_decision", note="refix needs guidance; decide again")
					self._finish(bug_id, "refix-without-guidance")
				except Exception:
					self.log(traceback.format_exc())
				worked = True
				continue
			if action == "implement" and (not guidance or not self._has_triage_artifacts(bug_id)):
				try:
					self._update(bug_id, decisionConsumed=True)
					if not guidance:
						self._update(bug_id, note="implement needs a description; decide again")
						self._finish(bug_id, "implement-without-description")
					else:
						self._update(bug_id, note="cannot implement: triage artifacts missing in artifacts/bug-loop/" + bug_id)
						self._finish(bug_id, "implement-without-artifacts")
				except Exception:
					self.log(traceback.format_exc())
				worked = True
				continue
			try:
				# Consume first: a crash below must not replay the action.
				self._update(bug_id, decisionConsumed=True)
			except Exception:
				self.log(traceback.format_exc())
				continue
			try:
				if action == "discard":
					self._discard(bug_id, guidance)
				elif action == "ship":
					self._ship_by_decision(bug_id, decision)
				elif action == "refix":
					self._refix(bug_id, guidance)
				elif action == "implement":
					self._implement(bug_id, guidance)
				else:
					self.log("unknown decision action {!r} for {}".format(action, bug_id)[:200])
			except Exception:
				self.log(traceback.format_exc())
				if action == "implement":
					# No branch is parked yet: back to where `implement` can be chosen again.
					self._safe_release(bug_id, "the bug loop failed to implement the feature; see artifacts/bug-loop/" + bug_id + "; decide again",
						status="triaged", triage={"category": "design_request"})
				else:
					self._safe_release(bug_id, "the bug loop failed to carry out the maintainer decision; see artifacts/bug-loop/" + bug_id, status="pr_open")
				self._finish(bug_id, "loop-error")
			worked = True
		return worked

	def _drop_queued_ship(self, bug_id):
		"""A later discard or refix overrides an earlier ship decision still waiting out the freeze."""
		queue = self.state.data["ship_queue"]
		kept = [item for item in queue if item.get("bug") != bug_id]
		if len(kept) != len(queue):
			self.state.data["ship_queue"] = kept
			self.state.save()

	def _discard(self, bug_id, guidance):
		self._drop_queued_ship(bug_id)
		self.worktree.delete_branch("bugfix/" + short_id(bug_id))
		self._release(bug_id, "wontfix", "discarded by maintainer: " + (guidance or "(no reason given)"))
		self._finish(bug_id, "discarded")

	def _refix(self, bug_id, guidance):
		self._drop_queued_ship(bug_id)
		if self.state.refix_count(bug_id) >= MAX_REFIX:
			self._update(bug_id, status="needs_decision",
				note="refix limit reached ({} guided refixes); finish it by hand".format(MAX_REFIX))
			self.notifier.send(notify.refix_limit_message(self.notifier, bug_id, MAX_REFIX))
			self._finish(bug_id, "refix-limit")
			return
		self.state.count_refix(bug_id)
		# A feature accepted before descriptions were kept has none; it still parks via is_feature.
		feature = self.state.feature_description(bug_id) if self.state.is_feature(bug_id) else None
		self._fix(bug_id, guidance=guidance, feature=feature)

	def _has_triage_artifacts(self, bug_id):
		folder = os.path.join(self.artifacts_dir, bug_id)
		return all(os.path.exists(os.path.join(folder, name)) for name in ("report.json", "triage.json"))

	def _implement(self, bug_id, description):
		"""The maintainer accepted a rejected report as a feature: implement it, never auto-ship it.
		_fix relabels the bug as a feature only after its claim succeeds."""
		self._fix(bug_id, feature=description)

	def _ship_by_decision(self, bug_id, decision):
		branch = "bugfix/" + short_id(bug_id)
		try:
			head = self._read(bug_id, "decision.json").get("head")
		except (OSError, ValueError):
			head = None
		try:
			current = self.worktree.head(branch)
		except Exception:
			current = None
		if not head or current != head:
			self._park(bug_id, branch, ["branch moved since it was parked (or no candidate commit is recorded); decide again"])
			return
		# The maintainer approved a diff, not a commit id: ship only if the diff uploaded for the
		# recorded commit is the one they saw (the review diff is writable with the reader key).
		approved = (decision or {}).get("diffSha256")
		uploaded = self._review_diff_text(bug_id)
		if not approved or uploaded is None or hashlib.sha256(uploaded.encode("utf-8")).hexdigest() != approved:
			self._park(bug_id, branch, ["the diff you approved does not match the candidate commit; decide again"])
			return
		if loop_state.breaker_active(self.artifacts_dir):
			self._park(bug_id, branch, ["the circuit breaker is tripped"])
			return
		summary = self._summary(bug_id)
		if any(item.get("bug") == bug_id for item in self.state.data["ship_queue"]):
			self._update(bug_id, note="already queued to ship after the freeze window")
			return
		self._watch_ci(self.clock())
		if self._develop_red() and bug_id != self.state.emergency_ticket():
			self.state.enqueue_ship(bug_id, branch, summary, head, by_maintainer=True)
			self.state.mark_attempted(bug_id, "ship-queued")
			self._update(bug_id, note="maintainer approved {}; {}: ships once CI is green".format(branch, RED_REASON))
			self.state.record(bug_id, "ship-queued", branch=branch, reason=RED_REASON)
			return
		if loop_state.in_freeze(self.clock(), self.config.freeze_start_utc, self.config.freeze_end_utc):
			self.state.enqueue_ship(bug_id, branch, summary, head, by_maintainer=True)
			self.state.mark_attempted(bug_id, "ship-queued")
			self._update(bug_id, note="maintainer approved {}; ships after the nightly freeze window".format(branch))
			self.state.record(bug_id, "ship-queued", branch=branch)
			return
		self._ship(bug_id, branch, summary, head, by_maintainer=True)

	def _previous_attempt(self, bug_id):
		previous = {}
		try:
			fix = self._read(bug_id, "FIX.json")
			previous["fix"] = {key: fix.get(key) for key in ("root_cause", "expected_source", "confidence", "regression_test", "notes")}
		except (OSError, ValueError):
			pass
		try:
			previous["park_reasons"] = self._read(bug_id, "decision.json").get("reasons", [])
		except (OSError, ValueError):
			pass
		return previous

	def _summary(self, bug_id):
		try:
			return str(self._read(bug_id, "triage.json").get("observed", ""))[:200]
		except (OSError, ValueError):
			return ""

	def _review_diff_text(self, bug_id):
		"""The exact review diff text uploaded for the bug's candidate commit, or None without one.
		A ship decision carries the SHA-256 of this text."""
		path = os.path.join(self.artifacts_dir, bug_id, "diff.patch")
		if not os.path.exists(path):
			return None
		with open(path, "r", encoding="utf-8", errors="replace") as handle:
			text = handle.read()
		if len(text) > REVIEW_DIFF_LIMIT:
			text = text[:REVIEW_DIFF_LIMIT - len(TRUNCATION_MARKER)] + TRUNCATION_MARKER
		return text

	def _upload_diff(self, bug_id):
		try:
			text = self._review_diff_text(bug_id)
			if text is None:
				return
			self.api.put_review_diff(bug_id, text, actor=self.config.worker)
		except Exception:  # the diff is a convenience for the maintainer, never a reason to stop
			self.log(traceback.format_exc())

	def _backfill_review_diffs(self):
		"""Bugs parked before the web UI showed diffs get theirs once per process start."""
		if self._backfilled:
			return
		self._backfilled = True
		for status in PARKED_STATUSES:
			try:
				parked = self.api.list(status=status, limit=100).get("bugs", [])
			except Exception:
				self.log(traceback.format_exc())
				continue
			for bug in parked:
				bug_id = bug.get("_id")
				if not valid_bug_id(bug_id):
					continue
				try:
					if not (self.api.show(bug_id).get("reviewDiff") or ""):
						self._upload_diff(bug_id)
				except Exception:
					self.log(traceback.format_exc())

	def _send_daily_summary(self, snapshot):
		try:
			waiting = self.api.list(awaiting_decision=True, limit=1).get("pagination", {}).get("total", 0)
		except Exception:
			self.log(traceback.format_exc())
			waiting = None
		self.notifier.send(notify.daily_summary(snapshot, self.config.invocation_budget_per_day, waiting))

	def _ship_blockers(self, by_maintainer=False):
		blockers = []
		if loop_state.breaker_active(self.artifacts_dir):
			blockers.append("the circuit breaker is tripped")
		if not by_maintainer and not self.state.autoship_left(self.config.autoship_cap_per_day):
			blockers.append("the daily auto-ship cap is reached")
		return blockers

	def _try_ship(self, bug_id, branch, summary, head):
		blockers = self._ship_blockers()
		if self.state.is_feature(bug_id):
			blockers.append(FEATURE_REASON)
		if blockers:
			self._park(bug_id, branch, blockers)
			return
		if self.dry_run:
			self._finish(bug_id, "would-ship", branch=branch)
			return
		# A fix can take an hour and more; look at CI again (rate-limited) before deciding.
		self._watch_ci(self.clock())
		if self._develop_red() and bug_id != self.state.emergency_ticket():
			self.state.enqueue_ship(bug_id, branch, summary, head)
			self.state.mark_attempted(bug_id, "ship-queued")
			self._update(bug_id, note="fix ready on {}; {}: ships once CI is green".format(branch, RED_REASON))
			self.state.record(bug_id, "ship-queued", branch=branch, reason=RED_REASON)
			return
		if loop_state.in_freeze(self.clock(), self.config.freeze_start_utc, self.config.freeze_end_utc):
			self.state.enqueue_ship(bug_id, branch, summary, head)
			self.state.mark_attempted(bug_id, "ship-queued")
			self._update(bug_id, note="fix ready on {}; ships after the nightly freeze window".format(branch))
			self.state.record(bug_id, "ship-queued", branch=branch)
			return
		self._ship(bug_id, branch, summary, head)

	def _ship(self, bug_id, branch, summary, head, by_maintainer=False):
		label = "maintainer decision" if by_maintainer else "gate green at " + head[:8]
		message = "Merge {} (bug-loop, {})\n\nBug {}: {}\n\n{}".format(branch, label, bug_id, summary, CO_AUTHOR)
		with self.lock():
			result = self.worktree.ship(branch, head, message, lambda: self.verifier.gate("fast")["ok"])
		if not result.ok:
			self._park(bug_id, branch, ["ship: " + result.reason])
			return
		if not by_maintainer:
			self.state.count_autoship()
		outcome = "shipped-by-maintainer" if by_maintainer else "shipped"
		# The merge is on develop now: nothing below may undo that by releasing the bug.
		try:
			self.worktree.delete_branch(branch)
			self._update(bug_id, status="resolved", release_claim=True,
				note="shipped by the bug loop{} in {}; reaches players with the next nightly deploy".format(
					" on the maintainer's decision" if by_maintainer else "", result.commit))
			self._finish(bug_id, outcome, branch=branch, commit=result.commit)
			self.notifier.send(notify.shipped_message(self.notifier, bug_id, result.commit, by_maintainer))
		except Exception:
			self.log(traceback.format_exc())
			self._finish(bug_id, outcome, branch=branch, commit=result.commit, bookkeeping_failed=True)

	def _ship_queued(self, now):
		if loop_state.in_freeze(now, self.config.freeze_start_utc, self.config.freeze_end_utc):
			return
		self._watch_ci(now)
		pending = self.state.take_ship_queue()
		kept = []
		if self._develop_red():
			# Only the emergency ticket ships while develop is red; the rest waits untouched.
			ticket = self.state.emergency_ticket()
			kept = [item for item in pending if item.get("bug") != ticket]
			pending = [item for item in pending if item.get("bug") == ticket]
			self.state.data["ship_queue"] = kept + list(pending)
		while pending:
			item = pending.pop(0)
			try:
				try:
					by_maintainer = bool(item.get("by_maintainer"))
					blockers = self._ship_blockers(by_maintainer)
					if not valid_bug_id(item.get("bug")) or not item.get("head"):
						self.log("dropping a malformed ship-queue item: {!r}".format(item)[:300])
					elif not by_maintainer and self.state.is_feature(item["bug"]):
						# Defence in depth: only a maintainer decision ships a feature.
						self._park(item["bug"], item["branch"], [FEATURE_REASON])
					elif blockers:
						self._park(item["bug"], item["branch"], blockers)
					else:
						self._ship(item["bug"], item["branch"], item["summary"], item["head"], by_maintainer=by_maintainer)
				except Exception:
					self.log(traceback.format_exc())
					self._park(item["bug"], item["branch"], ["ship error, see the runner log"])
			except BaseException:
				# Even parking failed: keep this item and the rest for the next poll. The queue may
				# already hold them (saved after the previous item); never list one twice.
				ours = kept + [item] + pending
				self.state.data["ship_queue"] = ours + [extra for extra in self.state.data["ship_queue"] if extra not in ours]
				raise
			self.state.data["ship_queue"] = kept + list(pending)
			self.state.save()

	# ---- CI watch

	def _develop_red(self):
		return self.state.ci_phase() is not None

	def _emergency_due(self):
		"""An open red phase whose emergency ticket still has fix attempts left (Task 6 acts on it)."""
		phase = self.state.ci_phase()
		return phase is not None and not phase.get("parked") and phase.get("attempts", 0) < self.config.emergency_attempts

	def _watch_ci(self, now):
		"""Reads the newest completed develop runs of both workflows at most every ci_poll_seconds.
		A red one opens a phase (one emergency ticket); all green closes it. GitHub trouble leaves
		the phase as it is."""
		if self.github is None or self.dry_run:
			return
		ci_state = self.state.data["ci"]
		last = ci_state.get("last_check")
		if last and (now - datetime.datetime.fromisoformat(last)).total_seconds() < self.config.ci_poll_seconds:
			return
		ci_state["last_check"] = now.isoformat()
		try:
			lists = {
				"push": self.github.runs(self.config.ci_push_workflow, branch="develop"),
				"nightly": self.github.runs(self.config.ci_nightly_workflow),
			}
			newest = {key: ci.newest_completed(lists[key], _CI_ACCEPT[key]) for key in lists}
			colours = {key: ci.colour(run) for key, run in newest.items() if run}
			ci_state["colours"] = colours
			red = {key: newest[key] for key, colour in colours.items() if colour == "red"}
			phase = self.state.ci_phase()
			if red and phase is None:
				self._open_ci_phase(now, red, lists)
			elif red:
				self._refresh_ci_phase(red, lists)
			elif phase is not None and all(colours.get(key) == "green" for key in phase["red_runs"]):
				# Only a green run of every workflow that was red ends the phase; a workflow GitHub
				# lists no completed run for (renamed, all in progress) proves nothing.
				self._close_ci_phase(newest)
			self._restart_nightly(newest, colours)
		except github.GitHubError as error:
			self.log("ci watch: " + str(error))
		except BaseException:
			# Not a GitHub answer (the bug API, say): look again on the next poll instead of in ci_poll_seconds.
			ci_state["last_check"] = last
			raise
		self.state.save()

	def _ci_context(self, key, run, runs):
		jobs = self.github.jobs(run["id"])
		job, step = ci.failing_step(jobs)
		excerpt = ""
		if job:
			try:
				excerpt = ci.excerpt(self.github.job_log(job["id"]))
			except github.GitHubError as error:
				self.log("ci watch: " + str(error))
		# Suspects: develop's first-parent commits since the newest green run before the red one.
		green_sha = ""
		older = runs[runs.index(run) + 1:] if run in runs else []
		for candidate in older:
			if _CI_ACCEPT[key](candidate) and ci.colour(candidate) == "green":
				green_sha = candidate.get("head_sha") or ""
				break
		suspects = self.worktree.log_lines(green_sha, run.get("head_sha") or "")
		return {"workflow": CI_NAMES[key], "key": key, "run_id": run["id"], "run_url": run.get("html_url", ""),
			"red_sha": run.get("head_sha") or "", "step": step, "excerpt": excerpt, "suspects": suspects}

	def _open_ci_phase(self, now, red, lists):
		key = "push" if "push" in red else "nightly"
		context = self._ci_context(key, red[key], lists[key])
		summary = "{} is red on develop at {}{}".format(context["workflow"], context["red_sha"][:8],
			": " + context["step"] if context["step"] else "")
		ticket = self.api.create_system(summary, context["excerpt"] or "(no log)", context["red_sha"], context["run_url"],
			actor=self.config.worker)
		self.state.data["ci"]["phase"] = {"since": now.isoformat(), "ticket": ticket,
			"red_runs": {name: run["id"] for name, run in red.items()}, "ci": context, "attempts": 0,
			"nightly_dispatched": [], "parked": False, "notified_exhausted": False}
		if valid_bug_id(ticket):
			self._write(ticket, "report.json", {"_id": ticket, "comment": summary, "subject": {"type": "generic", "id": 0}})
			self._write(ticket, "triage.json", EMERGENCY_VERDICT)
		else:
			self.log("ci watch: the bug API returned a malformed ticket id {!r}".format(ticket)[:200])
		self.log("ci watch: develop is red ({}), emergency ticket {}".format(context["run_url"], ticket))
		self.state.record(ticket, "ci-red", workflow=context["workflow"], run=context["run_url"])
		self.notifier.send(notify.ci_red_message(self.notifier, ticket, context["workflow"], context["step"], context["run_url"]))

	def _refresh_ci_phase(self, red, lists):
		phase = self.state.ci_phase()
		ticket = phase["ticket"]
		for key, run in red.items():
			if phase["red_runs"].get(key) == run["id"]:
				continue
			phase["red_runs"][key] = run["id"]
			context = self._ci_context(key, run, lists[key])
			phase["ci"] = context
			self._update(ticket, logTail=context["excerpt"] or "(no log)", note="still red: " + context["run_url"])
			if key == "nightly" and run.get("head_sha") in phase["nightly_dispatched"]:
				# The restarted nightly failed again.
				phase["attempts"] += 1
				phase["ci"]["retry"] = context["excerpt"]

	def _close_ci_phase(self, newest):
		phase = self.state.ci_phase()
		ticket = phase["ticket"]
		ci_state = self.state.data["ci"]
		pending = ci_state["pending"]
		if pending:
			try:
				self.worktree.delete_remote_branch(pending["branch"])
			except Exception:
				self.log(traceback.format_exc())
			ci_state["pending"] = None
		green = newest.get("push") or newest.get("nightly")
		try:
			self._update(ticket, status="resolved", release_claim=True,
				note="develop is green again in CI at {}".format(((green or {}).get("head_sha") or "")[:8]))
		except Exception:
			self.log(traceback.format_exc())
		ci_state["phase"] = None
		self.state.record(ticket, "ci-green")
		self.log("ci watch: develop is green again, ticket {} resolved".format(ticket))
		self.notifier.send(notify.ci_green_message([CI_NAMES[key] for key in newest if newest[key]]))

	def _restart_nightly(self, newest, colours):
		"""A red Nightly Release reruns once a newer develop commit is green in the push workflow."""
		phase = self.state.ci_phase()
		if phase is None or colours.get("nightly") != "red" or colours.get("push") != "green":
			return
		sha = newest["push"].get("head_sha") or ""
		if not sha or sha == newest["nightly"].get("head_sha") or sha in phase["nightly_dispatched"]:
			return
		self.github.dispatch(self.config.ci_nightly_workflow, "develop")
		phase["nightly_dispatched"].append(sha)
		self.log("ci watch: restarted the Nightly Release for " + sha[:8])
		self._update(phase["ticket"], note="restarted the Nightly Release for " + sha[:8])

	# ---- housekeeping

	def _reconcile_parked(self):
		for status in PARKED_STATUSES:
			for bug in self.api.list(status=status, limit=100).get("bugs", []):
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
			self.notifier.send(notify.breaker_message("nightly {} is red and includes bug-loop merges".format(name)))

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
