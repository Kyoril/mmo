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

from . import ci_runs, github, gitops, guard, inputs, notify, state as loop_state, status as loop_status, verdicts, verification
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
# A refix whose branch cannot take today's develop restarts from develop; the fixer is told so.
RESTART_NOTE = ("the earlier fix branch could not be merged with today's develop ({}). This attempt starts from "
	"today's develop: re-apply the earlier change (earlier_diff) on the current code, adapting it where develop "
	"changed the same places, together with any guidance.")
# The guidance of an automatic re-application after a ship conflict nothing could resolve.
REAPPLY_GUIDANCE = ("From the bug loop, not the maintainer: this fix could not be merged with today's develop ({}). "
	"Re-apply the same fix on today's develop: keep its intent and its regression test, adapt it to what develop "
	"changed in the same places, and change nothing else.")
# How much of the earlier diff a restarted refix sees.
EARLIER_DIFF_LIMIT = 40000
# Every feature parks for the maintainer: only a hash-bound "ship" decision merges one.
FEATURE_REASON = "feature: shipping needs the maintainer's approval"
# A bug the loop gave up on (needs-info) and that only the maintainer can move on: the web UI lists
# this category, the daily summary counts it. Setting the bug back to `new` triages it again.
NEEDS_INFO_CATEGORY = "needs_info"
NEEDS_INFO_HINT = "; waiting for the maintainer: set the status to new to retry, or close it"
# How the bug API records a maintainer's "Implement as feature" decision in the bug history.
IMPLEMENT_HISTORY_PREFIX = "decision: implement: "
# Bug ids are Mongo ObjectIds; they become paths and branch names, so nothing else is accepted.
BUG_ID = re.compile(r"^[0-9a-f]{24}$")
# While develop is red in CI nothing but the emergency ticket ships; everything else queues.
RED_REASON = "develop is red"
# The pre-ship CI check failed (not GitHub: the bug API, say): the CI state is unknown, so nothing ships.
CI_UNKNOWN_REASON = "CI check failed"
# The breaker stops every ship. A maintainer's ship decision waits for it in the ship queue instead
# of being used up; an auto-ship parks.
BREAKER_REASON = "the circuit breaker is tripped"
# The emergency ticket auto-ships only through its CI-verified path (_check_pending_ci).
EMERGENCY_REASON = "emergency fix: ships only after a green Linux CI run or on the maintainer's decision"
# Statuses of a GitHub run that has not finished yet.
ACTIVE_RUN_STATUSES = ("queued", "in_progress", "waiting", "pending", "requested")
CI_NAMES = {"push": "Linux Servers", "nightly": "Nightly Release"}
# The triage verdict written for an emergency ticket (for the operator; the loop reads its context from state).
EMERGENCY_VERDICT = {"category": "defect", "severity": "critical", "component": "ci", "observed": "develop is red in CI",
	"expected_claim": "develop is green in CI", "duplicate_of": None, "abuse_evidence": "",
	"reasoning": "written by the bug loop from a red CI run"}


_CI_ACCEPT = {
	"push": lambda run: run.get("event") == "push" and run.get("head_branch") == "develop",
	"nightly": lambda run: run.get("event") in ("schedule", "workflow_dispatch"),
}
# Outcomes that already send a dedicated message, so no status message repeats them. The CI
# outcomes are covered by the "develop is red/green", "Emergency fix shipped/needs you" messages.
ANNOUNCED_ELSEWHERE = ("shipped", "shipped-by-maintainer", "refix-limit", "shipped-emergency",
	"ci-pending", "ci-failed", "emergency-failed", "ci-red", "ci-green")
# Per-attempt outcomes of the open phase's emergency ticket: the CI messages say what matters,
# once per event, never per attempt.
EMERGENCY_QUIET = ("fix-started", "interrupted", "needs-info", "loop-error", "claimed-elsewhere")
# The note in gate.json of a candidate that parks for other reasons: its gate run would change
# nothing (a ship decision gates the merge anyway), so it is skipped to get to the next bug sooner.
GATE_SKIPPED = "skipped: the candidate parks for other reasons; a ship decision runs the fast gate on the merge"


class WorktreeUnavailable(RuntimeError):
	"""The fix worktree could not be brought to origin/develop (fetch, checkout or submodule sync
	failed: develop pushed before its submodule commit, say). It is the loop's fault, not the bug's,
	and is raised before the bug is touched: the bug stays queued and no attempt is used."""


def valid_bug_id(bug_id):
	return isinstance(bug_id, str) and BUG_ID.match(bug_id) is not None


def proof_exempt(changes):
	production = [change for change in changes if guard.is_production(change.path)]
	return bool(production) and all(change.path.startswith(DATA_ONLY_ROOTS) for change in production)


def short_id(bug_id):
	"""The last 8 hex digits of a Mongo ObjectId: its counter, unlike the timestamp prefix."""
	return bug_id[-8:]


def _nightly_run_id(name):
	"""The GitHub run id in a breaker source name ("nightly run 123", see ci.newest_nightly), or None."""
	prefix = "nightly run "
	if not isinstance(name, str) or not name.startswith(prefix):
		return None
	suffix = name[len(prefix):]
	# ASCII digits only: str.isdigit() accepts "²", which int() then rejects.
	if not re.fullmatch(r"[0-9]+", suffix):
		return None
	return int(suffix)


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
	and the freeze window are checked at ship time. `gate` None: not run."""
	reasons = []
	# Medium confidence ships when a regression test proves the fix (fails before, passes after).
	proven = proof is not None and proof["ok"]
	if fix.get("confidence") != "high" and not (fix.get("confidence") == "medium" and proven):
		reasons.append("fixer confidence is {}".format(fix.get("confidence")))
	reasons.extend(verdicts.review_blockers(review))
	reasons.extend("guard: " + reason for reason in guard_result["reasons"])
	if proof is not None and not proof["ok"]:
		reasons.append("proof: " + proof["reason"])
	if gate is not None and not gate["ok"]:
		reasons.append("the fast gate is red")
	return reasons


class BugLoop:
	def __init__(self, api, runner, worktree, verifier, decoder, config, state, prompts, schemas,
			artifacts_dir, report_dir, clock=utcnow, lock=contextlib.nullcontext, dry_run=False, log=print, notifier=None,
			github=None, nightly=lambda: (None, None)):
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
		# () -> (name, report) of the newest finished nightly; see ci.newest_nightly.
		self.nightly = nightly
		self.clock = clock
		self.lock = lock
		self.dry_run = dry_run
		self.log = log
		self.notifier = notify.Notifier("", log=log) if dry_run or notifier is None else notifier
		# GitHub Actions client of the CI watch; None turns the watch off.
		self.github = github
		self._backfilled = False
		# status.json: what the loop does right now, for `bug_loop.py status`.
		self._status_name = loop_status.DRY_STATUS_FILE if dry_run else loop_status.STATUS_FILE
		self._started_at = clock().isoformat()
		self._stage_key = None
		self._stage_since = self._started_at

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

	def _finish(self, bug_id, outcome, announce=True, **details):
		self.state.mark_attempted(bug_id, outcome)
		self.state.record(bug_id, outcome, **details)
		self._count_for_mode(outcome)
		self.log("bug {}: {} {}".format(bug_id, outcome, json.dumps(details, default=str) if details else ""))
		if announce:
			self._announce(bug_id, outcome, details)

	def _announce(self, bug_id, outcome, details=None):
		"""A status-change message. Outcomes with a message of their own stay quiet here."""
		details = details or {}
		if not self.config.notify_status_changes or not self.notifier.enabled or outcome in ANNOUNCED_ELSEWHERE:
			return
		if outcome == "parked" and details.get("decision_needed"):
			return  # the design question message says it
		if outcome in EMERGENCY_QUIET and bug_id == self.state.emergency_ticket():
			return
		try:
			self.notifier.send(notify.status_message(self.notifier, bug_id, outcome, self._summary(bug_id), details))
		except Exception:  # a notification must never stop the loop
			self.log(traceback.format_exc())

	# ---- status and mode

	def _stage(self, stage, bug_id=None, branch=None, detail="", until=None):
		"""Rewrites status.json. A status that cannot be written never stops the loop."""
		try:
			now = self.clock()
			if (stage, bug_id) != self._stage_key:
				self._stage_key = (stage, bug_id)
				self._stage_since = now.isoformat()
			outcomes = self.state.data["outcomes"]
			next_fix = self.state.next_fix()
			snapshot = {
				"pid": os.getpid(),
				"dry_run": self.dry_run,
				"started_at": self._started_at,
				"updated_at": now.isoformat(),
				"stage": stage,
				"stage_since": self._stage_since,
				"bug": bug_id,
				"branch": branch,
				"summary": self._summary(bug_id) if bug_id else "",
				"detail": detail,
				"until": until.isoformat() if until else None,
				"mode": self.state.data.get("mode"),
				# The next bug to fix first, then the rest in queue order.
				"fix_queue": sorted((item["bug"] for item in self.state.data["fix_queue"]), key=lambda bug: bug != next_fix),
				"ship_queue": len(self.state.data["ship_queue"]),
				"invocations": self.state.data["invocations"],
				"budget": self.config.invocation_budget_per_day,
				"autoships": self.state.data["autoships"],
				"autoship_cap": self.config.autoship_cap_per_day,
				"breaker": loop_state.breaker_active(self.artifacts_dir),
				"develop_red": self._develop_red(),
				"last_outcome": outcomes[-1] if outcomes else None,
			}
			loop_status.write(self.artifacts_dir, snapshot, self._status_name)
		except Exception:
			self.log(traceback.format_exc())

	def _update_mode(self):
		"""busy while bugs are queued and the budget allows a fix, paused while they are queued but
		the budget is used up, idle with an empty queue. Each change is logged and announced once;
		the mode lives in the state, so a restart does not announce it again."""
		queue = len(self.state.data["fix_queue"])
		reason = ""
		if not queue:
			name = "idle"
		elif not self.state.budget_left(self.config.invocation_budget_per_day, needed=2):
			name = "paused"
			reason = "daily Claude budget used up ({} of {}); resumes at 00:00 UTC".format(
				self.state.data["invocations"], self.config.invocation_budget_per_day)
		else:
			name = "busy"
		previous = self.state.data.get("mode") or {}
		if previous.get("name") == name:
			return
		now = self.clock()
		self.state.data["mode"] = {"name": name, "since": now.isoformat(), "reason": reason, "counts": {}}
		self.log("mode: {} -> {}{}".format(previous.get("name") or "none", name, " ({})".format(reason) if reason else ""))
		# The first mode after an upgrade is only recorded: nothing changed for the maintainer.
		if self.dry_run or not previous.get("name") or not self.config.notify_status_changes:
			return
		poll = min(self.config.poll_seconds, self.config.ci_poll_seconds) if self.github else self.config.poll_seconds
		try:
			self.notifier.send(notify.mode_message(name, previous, now, queue, reason, max(1, poll // 60)))
		except Exception:  # a notification must never stop the loop
			self.log(traceback.format_exc())

	def _count_for_mode(self, outcome):
		"""Counts a finished bug for the current mode; the idle message reports the busy stretch."""
		mode = self.state.data.get("mode")
		if mode is not None:
			counts = mode.setdefault("counts", {})
			counts[outcome] = counts.get(outcome, 0) + 1

	# ---- one poll

	def poll_once(self):
		"""Returns True when it did work, so the caller polls again without sleeping."""
		now = self.clock()
		today = now.strftime("%Y-%m-%d")
		self.state.roll(today)
		self._stage("polling")
		if self.state.data.get("pending_summary"):
			if not self.dry_run:
				self._send_daily_summary(self.state.data["pending_summary"])
			self.state.data["pending_summary"] = None
		self._check_nightly_breaker(now)
		worked = False
		if not self.dry_run:
			# First, so a red develop is known before anything can ship in this poll. A failure that is
			# not GitHub's (the bug API, say) must not stop triage and decisions: _watch_ci left
			# last_check reset, so every ship of this poll rechecks and stays held ("CI check failed").
			try:
				self._watch_ci(now)
			except Exception:
				self.log(traceback.format_exc())
			self._backfill_review_diffs()
			worked = self._handle_decisions() or worked
			self._release_stale_claims()
			self._reconcile_parked()
		self._ship_queued(now)
		worked = self._triage_new() or worked
		# Before the fix below, which can take an hour: a backlog that just appeared is announced now.
		self._update_mode()
		if not self.dry_run and self.github is not None:
			self._check_pending_ci(now)
			# The emergency ticket goes before every other bug; the ordinary fix waits for the next poll.
			due = self._emergency_due()
			if due and not self.state.budget_left(self.config.invocation_budget_per_day, needed=2):
				self._notify_budget_exhausted(now)
			elif due and self._worktree_waiting(now):
				# The emergency ticket still goes first: nothing else is fixed until the worktree works.
				return self._end_poll(worked)
			elif due:
				ticket = self.state.emergency_ticket()
				try:
					self._emergency_fix()
				except WorktreeUnavailable:
					return self._end_poll(worked)
				except Exception:  # the loop must survive a bad emergency run too
					self.log(traceback.format_exc())
					self._delay_emergency_retry()
					self._safe_release(ticket, "needs-info: the bug loop hit an internal error on the emergency fix; see artifacts/bug-loop/{}{}".format(
						ticket, self._retry_note()))
					self._finish(ticket, "loop-error")
				return self._end_poll(True)
		bug_id = self.state.next_fix()
		if bug_id is not None and not self._worktree_waiting(now) and self.state.budget_left(self.config.invocation_budget_per_day, needed=2):
			try:
				self._fix(bug_id)
				worked = True
			except WorktreeUnavailable:
				pass
			except Exception:  # the loop must survive one bad bug
				self.log(traceback.format_exc())
				self._safe_release(bug_id, "needs-info: the bug loop hit an internal error; see artifacts/bug-loop/" + bug_id)
				self._finish(bug_id, "loop-error")
				worked = True
		return self._end_poll(worked)

	def _end_poll(self, worked):
		self._update_mode()
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
				seconds = min(self.config.poll_seconds, self.config.ci_poll_seconds) if self.github else self.config.poll_seconds
				self._stage("sleeping", until=self.clock() + datetime.timedelta(seconds=seconds))
				sleep(seconds)

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
				elif not parked and bug_id != self.state.emergency_ticket():
					self._release(bug_id, "triaged", note + NEEDS_INFO_HINT, triage={"category": NEEDS_INFO_CATEGORY})
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
		self._stage("triaging", bug_id)
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
			self._update(bug_id, status="triaged", triage={"category": NEEDS_INFO_CATEGORY},
				note="triage failed twice: " + str(error)[:500] + NEEDS_INFO_HINT)
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
			self._announce(bug_id, "queued", {"severity": verdict["severity"]})

	# ---- fix

	def _fix(self, bug_id, guidance=None, feature=None, emergency=None):
		"""`emergency` is the CI context of the red phase when bug_id is its emergency ticket: the
		fixer then sees only the loop's CI task and log, and a locally green fix goes to CI, not develop."""
		if not valid_bug_id(bug_id):
			self.state.drop_fix(bug_id)
			self.log("dropping a queued fix with a malformed bug id: {!r}".format(bug_id)[:200])
			return
		if emergency is not None and bug_id != self.state.emergency_ticket():
			raise ValueError("an emergency fix for {} which is not the recorded emergency ticket".format(bug_id))
		# Before the bug is touched (queue, claim, attempt): a worktree that cannot reach develop
		# raises WorktreeUnavailable and leaves everything as it was for a later poll.
		self._stage("preparing", bug_id)
		base = self._prepare_worktree()
		self.state.drop_fix(bug_id)
		self.state.mark_attempted(bug_id, "fix-started")
		self.state.save()
		if emergency is not None:
			# The ticket is the loop's own; no API field or player text reaches the fixer.
			bug = {"_id": bug_id}
			verdict = EMERGENCY_VERDICT
		else:
			self._ensure_artifacts(bug_id)
			bug = self._read(bug_id, "report.json")
			verdict = self._read(bug_id, "triage.json")
		# A guided refix that fails goes back to the maintainer, who can only decide on parked bugs;
		# a failed feature run goes back to where `implement` can be chosen again; a failed
		# emergency run goes back to `triaged`, where the loop retries it while attempts remain;
		# any other failed fix goes to the maintainer's needs-info list (the loop never polls `triaged`).
		failed_status = "pr_open" if guidance else "triaged"
		fresh_feature = bool(feature) and not guidance
		if fresh_feature:
			failed_fields = {"triage": {"category": "design_request"}}
			again = "; decide again"
		elif emergency is not None:
			failed_fields = {}
			again = self._retry_note()
		elif not guidance:
			failed_fields = {"triage": {"category": NEEDS_INFO_CATEGORY}}
			again = NEEDS_INFO_HINT
		else:
			failed_fields = {}
			again = ""
		resume = bool(guidance) or bool(emergency and emergency.get("resume"))
		previous = self._previous_attempt(bug_id) if resume else None
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
		self._announce(bug_id, "refix-started" if guidance else "implement-started" if feature else "fix-started", {"branch": branch})
		if not self.verifier.ensure_configured():
			self._release(bug_id, failed_status, "needs-info: build configure failed in the bug-loop worktree" + again, **failed_fields)
			self._finish(bug_id, "needs-info", reason="build configure failed")
			return
		start = None
		if resume and emergency is None and not self._branch_available(branch):
			# Parked on another machine before backups existed, or never parked (a needs-info bug
			# the maintainer sent back with guidance): the guidance starts a fresh branch.
			self.log("bug {}: no {} here or on origin; the guided refix starts from develop".format(bug_id, branch))
			resume = False
		if resume:
			try:
				start = self.worktree.resume_branch(branch)
				base = self.worktree.fork_point(branch)
				self._write_resolutions(bug_id)
			except Exception as error:
				if emergency is None:
					# The branch cannot take today's develop (a conflict nothing could resolve): the fixer
					# re-applies the earlier change on a fresh branch from develop, with its diff in hand.
					self.log("bug {}: cannot bring {} up to date ({}); the refix starts from develop".format(bug_id, branch, str(error)[:300]))
					previous = dict(previous or {}, restarted_from_develop=RESTART_NOTE.format(str(error)[:500]),
						earlier_diff=self._earlier_diff(bug_id))
				else:
					# The emergency branch is only a convenience: start again from develop.
					self.log("emergency fix: cannot resume {} ({}); starting fresh".format(branch, str(error)[:200]))
					previous = None
				start = None
		if start is None:
			self.worktree.start_branch(branch, base)
			start = base
		fix_path = os.path.join(self._bug_dir(bug_id), "FIX.json")
		if os.path.exists(fix_path):
			os.remove(fix_path)
		if emergency is not None:
			fix_input = inputs.build_emergency_fix_input(bug_id, emergency, branch, fix_path, previous=previous)
		else:
			fix_input = inputs.build_fix_input(bug, verdict, branch, fix_path, guidance=guidance, previous=previous, feature=feature)
		self._stage("fixing", bug_id, branch, detail="guided refix" if guidance else "feature" if feature else
			"emergency fix" if emergency is not None else "")
		try:
			result = self.runner.agent(self.prompts["fix"], fix_input,
				self.worktree.path, self.config.fix_timeout_seconds, self.config.fix_max_usd)
			self._write(bug_id, "fix_result.json", result)
			fix = verdicts.load_fix(fix_path)
		except (ClaudeError, verdicts.VerdictError) as error:
			self._release(bug_id, failed_status, "needs-info: the fix stage failed: " + str(error)[:500] + again, **failed_fields)
			self._finish(bug_id, "needs-info", reason=str(error)[:300])
			return
		if fix["outcome"] == "no_project_basis" and emergency is not None:
			# A red CI run is never "not a bug".
			self._release(bug_id, failed_status, "needs-info: the fixer found no fix for the CI failure: " + fix["root_cause"][:1000] + again)
			self._finish(bug_id, "needs-info", reason="no_project_basis")
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
		self._verify_and_ship(bug_id, verdict, fix, branch, base, head, guidance=guidance, feature=feature, emergency=emergency)

	def _verify_and_ship(self, bug_id, verdict, fix, branch, base, head, guidance=None, feature=None, emergency=None):
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
			# An emergency park says it in its "needs you" message only.
			self._park(bug_id, branch, reasons, announce=emergency is None)
			if emergency is not None:
				self._park_emergency(bug_id, "parked: " + reasons[0])
			return
		self._stage("reviewing", bug_id, branch)
		review = self._review(bug_id, verdict, fix, diff_text, guard_result["reasons"], guidance=guidance, feature=feature,
			ci_failure=(emergency.get("excerpt") or "(no log)") if emergency is not None else None)
		self._stage("waiting-for-gate-lock", bug_id, branch)
		with self.lock():
			self._stage("proving", bug_id, branch)
			proof = None
			if fix["regression_test"]["kind"] == "none":
				if not proof_exempt(changes):
					proof = {"ok": False, "reason": "a code change needs a regression test"}
			else:
				proof = self.verifier.proof(fix["regression_test"], base, head, verification.regression_files(changes))
				if emergency is not None and not proof["ok"] and proof.get("after") == "passed":
					# The CI failure does not reproduce on Windows; the Linux CI run on the pushed head is the proof.
					proof = dict(proof, ok=True, waived="the test passed before the fix on Windows; the Linux CI run is the proof")
				self._write(bug_id, "proof.json", proof)
			# A candidate that parks anyway skips the gate; emergency fixes and features always run it
			# (the CI push and the "Feature ready" ping rely on a green result).
			doomed = decide(fix, review, guard_result, proof, None)
			if doomed and emergency is None and not self.state.is_feature(bug_id):
				gate = None
				self._write(bug_id, "gate.json", {"ok": None, "tier": "fast", "skipped": GATE_SKIPPED})
			else:
				self._stage("gating", bug_id, branch)
				self.worktree.checkout(head)
				# E2E needs the local MySQL and runs again in the nightly before anything is deployed.
				gate = self.verifier.gate("fast")
				self._write(bug_id, "gate.json", gate)
		reasons = decide(fix, review, guard_result, proof, gate)
		if emergency is not None:
			self._write(bug_id, "decision.json", {"reasons": reasons, "head": head})
			if reasons:
				self._park(bug_id, branch, reasons, review, announce=False)
				self._park_emergency(bug_id, "parked: " + reasons[0])
				return
			# Locally green: CI on the exact head decides, never _try_ship.
			self._push_for_ci(bug_id, branch, base, head)
			return
		if self.state.is_feature(bug_id):
			green = not reasons
			reasons = reasons + [FEATURE_REASON]
		else:
			green = False
		self._write(bug_id, "decision.json", {"reasons": reasons, "head": head})
		if reasons:
			ready = green and feature and not guidance
			# The "Feature ready" ping replaces the status message of this park.
			self._park(bug_id, branch, reasons, review, announce=not ready)
			# Pinged once, when a fresh implement run first parks green; guided refixes stay quiet.
			if ready:
				self.notifier.send(notify.feature_ready_message(self.notifier, bug_id, self._summary(bug_id), branch))
			return
		self._try_ship(bug_id, branch, verdict["observed"][:200], head)

	def _review(self, bug_id, verdict, fix, diff_text, guard_reasons, guidance=None, feature=None, ci_failure=None):
		try:
			review = self.runner.structured(self.prompts["review"],
				inputs.build_review_input(verdict, fix, diff_text, guard_reasons, guidance=guidance, feature=feature, ci_failure=ci_failure),
				self.schemas["review"], REVIEW_TOOLS, self.worktree.path, self.config.step_timeout_seconds)
		except ClaudeError as error:
			review = {"error": str(error)[:500]}
		self._write(bug_id, "review.json", review)
		return review

	# ---- park and ship

	def _park(self, bug_id, branch, reasons, review=None, announce=True):
		question = ""
		if isinstance(review, dict) and isinstance(review.get("design_question"), str):
			question = review["design_question"].strip()[:2000]
		self._update(bug_id, status="needs_decision" if question else "pr_open", prUrl="branch:" + branch,
			designQuestion=question, note="parked for review: " + "; ".join(reasons)[:1800], release_claim=True)
		self._upload_diff(bug_id)
		self._backup_branch(bug_id, branch)
		self._finish(bug_id, "parked", announce=announce, branch=branch, reasons=reasons[:10], decision_needed=bool(question))
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
			# A maintainer decision supersedes a pending CI check of the same bug (the emergency ticket).
			self._drop_pending_check(bug_id)
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
		self._delete_backup("bugfix/" + short_id(bug_id))
		if bug_id == self.state.emergency_ticket():
			# No more automatic attempts on a ticket the maintainer discarded.
			self.state.ci_phase()["parked"] = True
			self.state.save()
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
		self._recover_feature(bug_id)
		# A feature accepted before descriptions were kept has none; it still parks via is_feature.
		feature = self.state.feature_description(bug_id) if self.state.is_feature(bug_id) else None
		self._fix(bug_id, guidance=guidance, feature=feature)

	def _has_triage_artifacts(self, bug_id):
		try:
			self._ensure_artifacts(bug_id)
		except Exception:
			self.log(traceback.format_exc())
		folder = os.path.join(self.artifacts_dir, bug_id)
		return all(os.path.exists(os.path.join(folder, name)) for name in ("report.json", "triage.json"))

	# ---- state another machine's loop left behind

	def _ensure_artifacts(self, bug_id):
		"""report.json and triage.json are local files; a bug triaged by a loop on another machine
		has neither here. The report comes from the API again, the triage verdict is rebuilt from the
		triage the API keeps (no new model call, so the category cannot change under the maintainer)."""
		folder = os.path.join(self.artifacts_dir, bug_id)
		report_path = os.path.join(folder, "report.json")
		triage_path = os.path.join(folder, "triage.json")
		if os.path.exists(report_path) and os.path.exists(triage_path):
			return
		bug = self.api.show(bug_id)
		if not os.path.exists(report_path):
			self._write(bug_id, "report.json", bug)
		triage = bug.get("triage") if isinstance(bug.get("triage"), dict) else {}
		if not os.path.exists(triage_path) and triage.get("category"):
			summary = str(triage.get("summary") or "")
			self._write(bug_id, "triage.json", {
				"category": triage.get("category"), "severity": triage.get("severity") or "medium",
				"component": triage.get("component") or "unknown", "observed": summary,
				"expected_claim": "(not kept; triaged on another machine, see the triage summary)",
				"duplicate_of": None, "abuse_evidence": "", "reasoning": "(not kept; triaged on another machine)",
				"recovered_from_api": True})

	def _recover_feature(self, bug_id):
		"""A feature's description lives in the state of the loop that accepted it. Elsewhere it is
		taken from the bug's history: the newest "implement" decision the maintainer key wrote."""
		if self.state.is_feature(bug_id):
			return
		try:
			bug = self.api.show(bug_id)
		except Exception:
			self.log(traceback.format_exc())
			return
		if (bug.get("triage") or {}).get("category") != "feature":
			return
		description = None
		for entry in bug.get("history") or []:
			change = entry.get("change") if isinstance(entry, dict) else None
			if entry.get("actor") == "maintainer" and isinstance(change, str) and change.startswith(IMPLEMENT_HISTORY_PREFIX):
				description = change[len(IMPLEMENT_HISTORY_PREFIX):].strip()[:4000] or None
		self.state.add_feature(bug_id, description)
		self.state.save()
		self.log("bug {}: feature description {} from the bug history".format(bug_id, "recovered" if description else "not found"))

	def _branch_available(self, branch):
		"""The fix branch exists here, or is restored from its backup on origin."""
		try:
			return self.worktree.has_branch(branch) or self.worktree.restore_branch(branch)
		except Exception:
			self.log(traceback.format_exc())
			return False

	def _backup_branch(self, bug_id, branch):
		if self.dry_run:
			return
		try:
			ok, reason = self.worktree.backup_branch(branch)
			if not ok:
				self.log("bug {}: {}".format(bug_id, reason))
		except Exception:  # a backup is a convenience for another machine, never a reason to stop
			self.log(traceback.format_exc())

	def _delete_backup(self, branch):
		if self.dry_run:
			return
		try:
			self.worktree.delete_backup(branch)
		except Exception:
			self.log(traceback.format_exc())

	def _implement(self, bug_id, description):
		"""The maintainer accepted a rejected report as a feature: implement it, never auto-ship it.
		_fix relabels the bug as a feature only after its claim succeeds."""
		self._fix(bug_id, feature=description)

	def _ship_by_decision(self, bug_id, decision):
		branch = "bugfix/" + short_id(bug_id)
		try:
			head = self._read(bug_id, "decision.json").get("head")
		except (OSError, ValueError):
			head = self._recover_candidate(bug_id, branch)
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
		summary = self._summary(bug_id)
		if any(item.get("bug") == bug_id for item in self.state.data["ship_queue"]):
			self._update(bug_id, note="already queued to ship")
			return
		if loop_state.breaker_active(self.artifacts_dir):
			# The decision stays valid: it ships once the breaker is cleared (by hand, or by the next
			# green nightly when the loop tripped it), so the maintainer never has to decide again.
			self._queue_held(bug_id, branch, summary, head, BREAKER_REASON, "maintainer approved {}".format(branch), by_maintainer=True)
			return
		reason = self._ci_hold(bug_id)
		if reason:
			self._queue_held(bug_id, branch, summary, head, reason, "maintainer approved {}".format(branch), by_maintainer=True)
			return
		if loop_state.in_freeze(self.clock(), self.config.freeze_start_utc, self.config.freeze_end_utc):
			self.state.enqueue_ship(bug_id, branch, summary, head, by_maintainer=True)
			self.state.mark_attempted(bug_id, "ship-queued")
			self._update(bug_id, note="maintainer approved {}; ships after the nightly freeze window".format(branch))
			self.state.record(bug_id, "ship-queued", branch=branch)
			self._announce(bug_id, "ship-queued", {"branch": branch})
			return
		self._ship(bug_id, branch, summary, head, by_maintainer=True)

	def _recover_candidate(self, bug_id, branch):
		"""A bug parked on another machine has no decision.json and no diff.patch here. Its branch
		comes from the backup on origin; the candidate is the branch tip and the review diff is
		rebuilt from it, so the approved hash still has to match what is shipped."""
		if not self._branch_available(branch):
			return None
		head = self.worktree.head(branch)
		self._write(bug_id, "diff.patch", self.worktree.unified_diff(self.worktree.fork_point(branch), head))
		self._write(bug_id, "decision.json", {"reasons": ["recovered from the branch backup on origin"], "head": head})
		return head

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
		try:
			needs_info = self.api.list(status="triaged", triage_category=NEEDS_INFO_CATEGORY, limit=1).get("pagination", {}).get("total", 0)
		except Exception:
			self.log(traceback.format_exc())
			needs_info = None
		self.notifier.send(notify.daily_summary(snapshot, self.config.invocation_budget_per_day, waiting, needs_info))

	def _ship_blockers(self, by_maintainer=False):
		blockers = []
		if loop_state.breaker_active(self.artifacts_dir):
			blockers.append(BREAKER_REASON)
		if not by_maintainer and not self.state.autoship_left(self.config.autoship_cap_per_day):
			blockers.append("the daily auto-ship cap is reached")
		return blockers

	def _try_ship(self, bug_id, branch, summary, head):
		blockers = self._ship_blockers()
		if self.state.is_feature(bug_id):
			blockers.append(FEATURE_REASON)
		if bug_id == self.state.emergency_ticket():
			# A guided refix of the emergency ticket: only a green Linux CI run (the emergency path)
			# or the maintainer's ship decision takes it to develop.
			blockers.append(EMERGENCY_REASON)
		if blockers:
			self._park(bug_id, branch, blockers)
			return
		if self.dry_run:
			self._finish(bug_id, "would-ship", branch=branch)
			return
		reason = self._ci_hold(bug_id)
		if reason:
			self._queue_held(bug_id, branch, summary, head, reason, "fix ready on {}".format(branch))
			return
		if loop_state.in_freeze(self.clock(), self.config.freeze_start_utc, self.config.freeze_end_utc):
			self.state.enqueue_ship(bug_id, branch, summary, head)
			self.state.mark_attempted(bug_id, "ship-queued")
			self._update(bug_id, note="fix ready on {}; ships after the nightly freeze window".format(branch))
			self.state.record(bug_id, "ship-queued", branch=branch)
			self._announce(bug_id, "ship-queued", {"branch": branch})
			return
		self._ship(bug_id, branch, summary, head)

	def _recheck_ci(self):
		"""The CI check right before a ship decision (a fix can take an hour and more); rate-limited.
		False when it failed for another reason than GitHub: the CI state is unknown then."""
		try:
			self._watch_ci(self.clock())
			return True
		except Exception:
			self.log(traceback.format_exc())
			return False

	def _ci_hold(self, bug_id):
		"""Why `bug_id` must not ship now because of CI, or "" when it may."""
		if not self._recheck_ci():
			return CI_UNKNOWN_REASON
		if self._develop_red() and bug_id != self.state.emergency_ticket():
			return RED_REASON
		return ""

	def _queue_held(self, bug_id, branch, summary, head, reason, what, by_maintainer=False):
		self.state.enqueue_ship(bug_id, branch, summary, head, by_maintainer=by_maintainer)
		self.state.mark_attempted(bug_id, "ship-queued")
		self.state.record(bug_id, "ship-queued", branch=branch, reason=reason)
		self.state.save()
		if reason == BREAKER_REASON:
			when = "ships once the breaker is cleared"
		else:
			when = "ships once CI is green" if reason == RED_REASON else "ships once CI is confirmed green"
		try:
			self._update(bug_id, note="{}; {}: {}".format(what, reason, when))
		except Exception:  # the fix is queued; a lost note must not throw it away
			self.log(traceback.format_exc())

	def _ship(self, bug_id, branch, summary, head, by_maintainer=False, emergency=False):
		"""`emergency`: the emergency ticket's fix after a green Linux CI run on exactly `head`; it
		ships outside the daily cap."""
		if emergency and (bug_id != self.state.emergency_ticket() or branch != "bugfix/" + short_id(bug_id)):
			raise ValueError("refusing an emergency ship for {!r}: not the recorded emergency ticket".format(bug_id)[:200])
		if emergency:
			label = "emergency fix, Linux CI green on " + head[:8]
		else:
			label = "maintainer decision" if by_maintainer else "gate green at " + head[:8]
		message = "Merge {} (bug-loop, {})\n\nBug {}: {}\n\n{}".format(branch, label, bug_id, summary, CO_AUTHOR)
		self._stage("shipping", bug_id, branch, detail=label)
		with self.lock():
			result = self.worktree.ship(branch, head, message, lambda: self.verifier.gate("fast")["ok"])
		resolved = self._write_resolutions(bug_id)
		if not result.ok:
			reapply = getattr(result, "conflict", False) and not emergency and self._may_reapply(bug_id)
			self._park(bug_id, branch, ["ship: " + result.reason], announce=not emergency and not reapply)
			if emergency:
				self._park_emergency(bug_id, "ship: " + result.reason)
			elif reapply:
				self._reapply(bug_id, result.reason)
			return
		if not by_maintainer and not emergency:
			self.state.count_autoship()
		outcome = "shipped-emergency" if emergency else ("shipped-by-maintainer" if by_maintainer else "shipped")
		if emergency or bug_id == self.state.emergency_ticket():
			# Also a maintainer ship of the emergency ticket: develop's own CI run now decides.
			self._await_develop(result.commit)
		# The merge is on develop now: nothing below may undo that by releasing the bug.
		try:
			self.worktree.delete_branch(branch)
			self._delete_backup(branch)
			if emergency:
				self._update(bug_id, status="resolved", release_claim=True,
					note="emergency fix shipped by the bug loop in {} after {} was green on {}; waiting for CI on develop".format(
						result.commit, CI_NAMES["push"], head[:8]))
			else:
				self._update(bug_id, status="resolved", release_claim=True,
					note="shipped by the bug loop{} in {}{}; reaches players with the next nightly deploy".format(
						" on the maintainer's decision" if by_maintainer else "", result.commit,
						" (merge conflicts with develop resolved in {})".format(", ".join(resolved[:5])) if resolved else ""))
			self._finish(bug_id, outcome, branch=branch, commit=result.commit)
			if emergency:
				self.notifier.send(notify.emergency_shipped_message(self.notifier, bug_id, result.commit))
			else:
				self.notifier.send(notify.shipped_message(self.notifier, bug_id, result.commit, by_maintainer))
		except Exception:
			self.log(traceback.format_exc())
			self._finish(bug_id, outcome, branch=branch, commit=result.commit, bookkeeping_failed=True)

	def _write_resolutions(self, bug_id):
		"""Records the merge conflicts the last ship or refix merge resolved (merge.json); returns the files."""
		reports = list(getattr(self.worktree, "resolutions", None) or [])
		if not reports:
			return []
		self._write(bug_id, "merge.json", reports)
		return [path for report in reports for path in report.get("data", []) + [item.get("path") for item in report.get("text", [])]]

	def _earlier_diff(self, bug_id):
		text = self._review_diff_text(bug_id) or ""
		return text[:EARLIER_DIFF_LIMIT] + ("\n... (truncated)" if len(text) > EARLIER_DIFF_LIMIT else "")

	def _may_reapply(self, bug_id):
		return (bug_id != self.state.emergency_ticket() and self.state.reapply_count(bug_id) < self.config.max_reapply
			and self.state.budget_left(self.config.invocation_budget_per_day, needed=2))

	def _reapply(self, bug_id, reason):
		"""A ship ran into a conflict with develop nothing could resolve: the fixer re-applies the fix on
		today's develop at once (the parked branch stays the backup until then). The new candidate goes
		through guard, review, proof and gate again: it auto-ships only if it qualifies on its own,
		otherwise it parks and a ship decision is needed again."""
		self.state.count_reapply(bug_id)
		self.state.save()
		self.log("bug {}: re-applying the fix on today's develop after: {}".format(bug_id, reason[:300]))
		try:
			self._recover_feature(bug_id)
			feature = self.state.feature_description(bug_id) if self.state.is_feature(bug_id) else None
			self._fix(bug_id, guidance=REAPPLY_GUIDANCE.format(reason[:500]), feature=feature)
		except Exception:  # the bug is parked already; it waits for the maintainer as before
			self.log(traceback.format_exc())

	def _ship_queued(self, now):
		if loop_state.in_freeze(now, self.config.freeze_start_utc, self.config.freeze_end_utc):
			return
		if self.state.data["ship_queue"] and not self._recheck_ci():
			return  # CI state unknown: everything stays queued
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
					elif blockers and by_maintainer:
						# Only the breaker holds a maintainer's ship: it keeps its place until the breaker is cleared.
						kept.append(item)
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
		"""An open phase, or a red colour from the last read (defence in depth: a phase that could not
		be opened yet still holds ships). Without a GitHub client (no token, dry run) nothing could
		ever close a stored phase, so it is ignored (bug_loop.py warns at startup)."""
		if self.github is None:
			return False
		if self.state.ci_phase() is not None:
			return True
		return "red" in (self.state.data["ci"].get("colours") or {}).values()

	def _emergency_due(self):
		"""True when the emergency ticket should get a fix attempt now: a red phase that is not parked,
		has no CI verification pending, is not waiting for develop's own run after a ship, has attempts
		left (running out notifies the maintainer once) and whose ticket is `triaged` in the API (a
		maintainer who discards or parks it stops the retries)."""
		phase = self.state.ci_phase()
		if not phase:
			return False
		if phase.get("awaiting_develop"):
			deadline = phase.get("awaiting_deadline")
			if not phase.get("parked") and deadline and self.clock() >= datetime.datetime.fromisoformat(deadline):
				# develop's run on the merge never finished (cancelled, say): leave it to the maintainer, once.
				self._park_emergency(phase["ticket"], "develop's CI did not finish on the emergency merge {} within {} minutes".format(
					phase["awaiting_develop"][:8], self._awaiting_minutes(phase)))
			return False
		if phase.get("parked") or self.state.data["ci"]["pending"]:
			return False
		if self._emergency_retry_pending(phase):
			return False
		if not valid_bug_id(phase.get("ticket")):
			return False
		if phase.get("attempts", 0) >= self.config.emergency_attempts:
			if not phase.get("notified_exhausted"):
				phase["notified_exhausted"] = True
				self.state.save()
				self.notifier.send(notify.emergency_needs_you_message(self.notifier, phase["ticket"],
					"{} attempts failed; develop stays red".format(phase["attempts"])))
			return False
		try:
			status = self.api.show(phase["ticket"]).get("status")
		except Exception:
			self.log(traceback.format_exc())
			return False
		return status == "triaged"

	def _emergency_fix(self):
		phase = self.state.ci_phase()
		phase["attempts"] = phase.get("attempts", 0) + 1
		self.state.save()
		# A failed verification resumes the pushed branch; after a ship (or the first time) it starts fresh.
		context = dict(phase["ci"], resume=bool(phase.get("resume")))
		try:
			self._fix(phase["ticket"], emergency=context)
		except WorktreeUnavailable:
			# Nothing was attempted: the fixer never started.
			phase["attempts"] -= 1
			self.state.save()
			raise

	def _prepare_worktree(self):
		"""worktree.prepare(); a git failure starts (or extends) the worktree back-off and raises
		WorktreeUnavailable. The first success after a failure ends it."""
		now = self.clock()
		try:
			base = self.worktree.prepare()
		except gitops.GitError as error:
			self._worktree_failed(now, str(error))
			raise WorktreeUnavailable(str(error)) from error
		down = self.state.data.get("worktree_down")
		if down:
			self.log("worktree works again (down since {})".format(down.get("since")))
			self.state.data["worktree_down"] = None
			self.state.save()
			if down.get("notified"):
				self.notifier.send(notify.worktree_up_message())
		return base

	def _worktree_failed(self, now, error):
		"""No retry before the next CI poll interval: an immediate retry fails the same way. The
		maintainer hears of it once, after worktree_escalate_minutes."""
		down = self.state.data.get("worktree_down") or {"since": now.isoformat(), "notified": False}
		down["error"] = error[-2000:]
		down["retry_at"] = (now + datetime.timedelta(seconds=self.config.ci_poll_seconds)).isoformat()
		self.state.data["worktree_down"] = down
		minutes = int((now - datetime.datetime.fromisoformat(down["since"])).total_seconds() // 60)
		self.log("worktree unavailable for {} min, retrying after {}: {}".format(minutes, down["retry_at"], error[-500:]))
		if not down["notified"] and minutes >= self.config.worktree_escalate_minutes:
			down["notified"] = True
			self.notifier.send(notify.worktree_down_message(minutes, error))
		self.state.save()

	def _worktree_waiting(self, now):
		"""True while the worktree back-off holds every fix (emergency or queued)."""
		down = self.state.data.get("worktree_down")
		return bool(down) and now < datetime.datetime.fromisoformat(down["retry_at"])

	def _notify_budget_exhausted(self, now):
		"""An emergency attempt is due but the daily invocation budget is used up: tell the maintainer
		once per phase and UTC day instead of waiting silently for the day to roll."""
		phase = self.state.ci_phase()
		today = now.strftime("%Y-%m-%d")
		if phase is None or phase.get("budget_notified") == today:
			return
		phase["budget_notified"] = today
		self.state.save()
		tomorrow = (now + datetime.timedelta(days=1)).strftime("%Y-%m-%d")
		self.notifier.send(notify.emergency_needs_you_message(self.notifier, phase["ticket"],
			"invocation budget exhausted; develop stays red until {} 00:00 UTC or until you act".format(tomorrow)))

	def _delay_emergency_retry(self):
		"""An emergency attempt ended in an internal loop error: the next one waits
		emergency_retry_minutes instead of starting in the very next poll."""
		phase = self.state.ci_phase()
		if phase is None:
			return
		retry_at = self.clock() + datetime.timedelta(minutes=self.config.emergency_retry_minutes)
		phase["retry_after"] = retry_at.isoformat()
		self.state.save()

	def _emergency_retry_pending(self, phase):
		retry_after = phase.get("retry_after")
		return bool(retry_after) and self.clock() < datetime.datetime.fromisoformat(retry_after)

	def _retry_note(self):
		phase = self.state.ci_phase()
		if not phase:
			return ""
		attempts, limit = phase.get("attempts", 0), self.config.emergency_attempts
		if attempts < limit and self._emergency_retry_pending(phase):
			return "; the bug loop retries at {} UTC (attempt {} of {})".format(
				datetime.datetime.fromisoformat(phase["retry_after"]).strftime("%H:%M"), attempts, limit)
		if attempts < limit:
			return "; the bug loop retries (attempt {} of {})".format(attempts, limit)
		return "; attempt {} of {}, none left".format(attempts, limit)

	def _park_emergency(self, bug_id, reason):
		"""The emergency ticket is parked for the maintainer: no more automatic attempts this phase."""
		phase = self.state.ci_phase()
		if phase is not None and phase.get("ticket") == bug_id:
			phase["parked"] = True
			self.state.save()
		self.notifier.send(notify.emergency_needs_you_message(self.notifier, bug_id, reason))

	def _push_for_ci(self, bug_id, branch, base, head):
		"""Pushes the locally green emergency fix (exactly `head`) to origin for the Linux CI run and
		records the pending check. The only branch push the loop makes."""
		phase = self.state.ci_phase()
		if phase is None or not valid_bug_id(bug_id) or bug_id != self.state.emergency_ticket():
			raise ValueError("refusing to push for {!r}: not the recorded emergency ticket".format(bug_id)[:200])
		if branch != "bugfix/" + short_id(bug_id):
			raise ValueError("refusing to push {!r} for the emergency ticket".format(branch)[:200])
		# A moved gitlink shows up as the submodule's own files, or as the bare submodule path.
		roots = tuple(sub + "/" for sub in gitops.SUBMODULES)
		submodule_changes = [change.path for change in self.worktree.changes(base, head)
			if change.path in gitops.SUBMODULES or change.path.startswith(roots)]
		if submodule_changes:
			reason = "the emergency fix changes a data submodule ({}); CI cannot verify an unpublished submodule commit".format(submodule_changes[0])
		elif self.worktree.head(branch) != head:
			reason = "the branch moved away from the gated commit"
		else:
			ok, reason = self.worktree.push_branch(branch, head)
			if ok:
				deadline = self.clock() + datetime.timedelta(minutes=self.config.ci_wait_minutes)
				self.state.data["ci"]["pending"] = {"bug": bug_id, "branch": branch, "head": head, "deadline": deadline.isoformat()}
				phase["resume"] = True
				self.state.save()
				self._update(bug_id, status="pr_open", prUrl="branch:" + branch, release_claim=True,
					note="emergency fix pushed; verifying in {} on {}".format(CI_NAMES["push"], head[:8]))
				self._finish(bug_id, "ci-pending", branch=branch, head=head)
				return
		self._park(bug_id, branch, [reason], announce=False)
		self._park_emergency(bug_id, "parked: " + reason)

	def _check_pending_ci(self, now):
		"""Looks at the CI run of the pushed emergency branch (never blocks): green ships it, red or
		the deadline retries or, out of attempts, leaves it to the maintainer. The origin branch is
		deleted whatever the outcome."""
		ci_state = self.state.data["ci"]
		pending = ci_state["pending"]
		if not pending:
			return
		phase = self.state.ci_phase()
		bug_id, branch, head = pending.get("bug"), pending.get("branch"), pending.get("head")
		if phase is None or bug_id != phase.get("ticket"):
			# Defence in depth: _close_ci_phase drops the check together with the phase.
			ci_state["pending"] = None
			self._delete_remote(branch)
			self.state.save()
			return
		try:
			ticket = self.api.show(bug_id)
		except Exception:
			self.log(traceback.format_exc())
			return  # look again on the next poll
		if ticket.get("status") != "pr_open" or ticket.get("prUrl") != "branch:" + branch:
			# Someone (the maintainer) moved the ticket since the push: the check is stale.
			self.log("ci watch: dropping the CI check of {}: the ticket is {} now".format(bug_id, ticket.get("status")))
			ci_state["pending"] = None
			self._delete_remote(branch)
			self.state.save()
			return
		result = None
		try:
			runs = [item for item in self.github.runs(self.config.ci_push_workflow, branch=branch)
				if item.get("head_sha") == head and item.get("head_branch") == branch and item.get("event") == "push"]
			result = ci_runs.newest_completed(runs, lambda item: True)
		except github.GitHubError as error:
			self.log("ci watch: " + str(error))
		colour = ci_runs.colour(result) if result else None
		expired = now >= datetime.datetime.fromisoformat(pending["deadline"])
		if colour is None and not expired:
			return
		if colour == "green":
			exempt = self._breaker_allows_emergency()
			if loop_state.breaker_active(self.artifacts_dir) and not exempt:
				reason = "Linux CI green on {}, but the circuit breaker is tripped".format(head[:8])
				ci_state["pending"] = None
				self._park_emergency(bug_id, reason)
				self._safe_park(bug_id, branch, [reason], announce=False)
				self._delete_remote(branch)
				self.state.save()
				return
			if self._ci_hold(bug_id):
				return  # the CI state is unknown: wait, the check stays pending
			if self.state.data["ci"]["pending"] is not pending:
				return  # the recheck found develop green and closed the phase (and the check)
			ci_state["pending"] = None
			self.state.save()
			if exempt:
				# The breaker stays tripped for everything else; the maintainer resets it as always.
				message = "shipping despite the breaker: it was tripped by the red nightly this emergency fix repairs"
				self.log("ci watch: {} ({})".format(message, bug_id))
				try:
					self._update(bug_id, note=message)
				except Exception:
					self.log(traceback.format_exc())
			try:
				self._ship(bug_id, branch, self._summary(bug_id), head, emergency=True)
			except Exception:
				self.log(traceback.format_exc())
				self._park_emergency(bug_id, "the ship failed after Linux CI was green on " + head[:8])
				self._safe_park(bug_id, branch, ["Linux CI green on {}, but the ship failed (see the runner log)".format(head[:8])], announce=False)
			finally:
				self._delete_remote(branch)
				self.state.save()
			return
		retry = "timed out after {} minutes without a CI result".format(self.config.ci_wait_minutes)
		if result is not None:
			retry = "CI run failed: " + result.get("html_url", "")
			try:
				job, _ = ci_runs.failing_step(self.github.jobs(result["id"]))
				if job:
					retry = ci_runs.excerpt(self.github.job_log(job["id"])) or retry
			except github.GitHubError as error:
				self.log("ci watch: " + str(error))
		phase["ci"]["retry"] = retry
		try:
			if phase.get("attempts", 0) >= self.config.emergency_attempts:
				# Out of attempts: park the last candidate so the maintainer can decide on its diff.
				self._park(bug_id, branch, ["emergency verification failed, no attempts left: " + retry[:300]], announce=False)
				self.state.record(bug_id, "emergency-failed", branch=branch, head=head)
				# The one "needs you" for running out (_emergency_due would send it otherwise).
				phase["notified_exhausted"] = True
				self._park_emergency(bug_id, "{} attempts failed; develop stays red".format(phase["attempts"]))
			else:
				# Back to `triaged`, where _emergency_due picks it up again (on the same branch).
				self._update(bug_id, status="triaged", prUrl="", note="emergency verification failed; retrying: " + retry[:500])
				self.state.record(bug_id, "ci-failed", branch=branch, head=head)
		except Exception:
			# The bug API is down: the check stays pending, so the next poll does this again.
			self.log(traceback.format_exc())
			self.state.save()
			return
		ci_state["pending"] = None
		self._delete_remote(branch)
		self.state.save()

	def _await_develop(self, commit):
		"""The emergency fix is merged in `commit`: no attempt starts until develop's own CI run on it
		(or a descendant) is red (_refresh_ci_phase) or green (_close_ci_phase), or the wait times out
		(_emergency_due). A later attempt starts on a fresh branch; a maintainer ship un-parks."""
		phase = self.state.ci_phase()
		if phase is None:
			return
		phase["resume"] = False
		phase["parked"] = False
		phase["notified_exhausted"] = False
		phase["awaiting_develop"] = commit
		phase["awaiting_deadline"] = (self.clock() + datetime.timedelta(minutes=self._awaiting_minutes(phase))).isoformat()
		self.state.save()

	def _awaiting_minutes(self, phase):
		"""How long to wait for develop after an emergency ship: a red phase with a red nightly is
		decided by a Nightly Release, which runs far longer than the push workflow."""
		if "nightly" in (phase.get("red_runs") or {}):
			return self.config.ci_nightly_wait_minutes
		return self.config.ci_wait_minutes

	def _descends(self, ancestor, commit):
		try:
			return bool(self.worktree.is_ancestor(ancestor, commit))
		except Exception:
			self.log(traceback.format_exc())
			return False

	def _drop_pending_check(self, bug_id):
		pending = self.state.data["ci"]["pending"]
		if pending and pending.get("bug") == bug_id:
			self.state.data["ci"]["pending"] = None
			self._delete_remote(pending.get("branch"))
			self.state.save()

	def _safe_park(self, bug_id, branch, reasons, announce=True):
		try:
			self._park(bug_id, branch, reasons, announce=announce)
		except Exception:
			self.log(traceback.format_exc())

	def _delete_remote(self, branch):
		try:
			self.worktree.delete_remote_branch(branch)
		except Exception:
			self.log(traceback.format_exc())

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
			newest = {key: ci_runs.newest_completed(lists[key], _CI_ACCEPT[key]) for key in lists}
			colours = {key: ci_runs.colour(run) for key, run in newest.items() if run}
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
			self._restart_nightly(newest, colours, lists["nightly"])
		except github.GitHubError as error:
			self.log("ci watch: " + str(error))
		except BaseException:
			# Not a GitHub answer (the bug API, say): look again on the next poll instead of in ci_poll_seconds.
			ci_state["last_check"] = last
			raise
		self.state.save()

	def _ci_context(self, key, run, runs, best_effort=False):
		"""The context of a red run. `best_effort`: a GitHub error reading the jobs leaves the step
		and excerpt empty and the context `incomplete` (refilled by _refresh_ci_phase) instead of raising."""
		incomplete = False
		try:
			jobs = self.github.jobs(run["id"])
		except github.GitHubError as error:
			if not best_effort:
				raise
			self.log("ci watch: " + str(error))
			jobs, incomplete = [], True
		job, step = ci_runs.failing_step(jobs)
		excerpt = ""
		if job:
			try:
				excerpt = ci_runs.excerpt(self.github.job_log(job["id"]))
			except github.GitHubError as error:
				self.log("ci watch: " + str(error))
				incomplete = True
		# Suspects: develop's first-parent commits since the newest green run before the red one.
		green_sha = ""
		older = runs[runs.index(run) + 1:] if run in runs else []
		for candidate in older:
			if _CI_ACCEPT[key](candidate) and ci_runs.colour(candidate) == "green":
				green_sha = candidate.get("head_sha") or ""
				break
		suspects = self.worktree.log_lines(green_sha, run.get("head_sha") or "")
		return {"workflow": CI_NAMES[key], "key": key, "run_id": run["id"], "run_url": run.get("html_url", ""),
			"red_sha": run.get("head_sha") or "", "step": step, "excerpt": excerpt, "suspects": suspects,
			"incomplete": incomplete}

	@staticmethod
	def _log_tail(context):
		if context.get("excerpt"):
			return context["excerpt"]
		return "(log unavailable)" if context.get("incomplete") else "(no log)"

	def _open_ci_phase(self, now, red, lists):
		# Nothing GitHub says about the jobs may stop the phase from opening: develop is red either way.
		key = "push" if "push" in red else "nightly"
		context = self._ci_context(key, red[key], lists[key], best_effort=True)
		summary = "{} is red on develop at {}{}".format(context["workflow"], context["red_sha"][:8],
			": " + context["step"] if context["step"] else "")
		ticket = self.api.create_system(summary, self._log_tail(context), context["red_sha"], context["run_url"],
			actor=self.config.worker)
		self.state.data["ci"]["phase"] = {"since": now.isoformat(), "ticket": ticket,
			"red_runs": {name: run["id"] for name, run in red.items()}, "ci": context, "attempts": 0,
			"nightly_red_runs": [red["nightly"]["id"]] if "nightly" in red else [],
			"nightly_dispatched": [], "parked": False, "notified_exhausted": False, "counted_runs": []}
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
		seen_nightlies = phase.setdefault("nightly_red_runs", [])
		if "nightly" in red and red["nightly"]["id"] not in seen_nightlies:
			seen_nightlies.append(red["nightly"]["id"])
		for key, run in red.items():
			if phase["red_runs"].get(key) == run["id"]:
				self._refill_ci_context(phase, key, run, lists[key])
				continue
			# The run counts as seen only once its context is read, so a GitHub error retries it; the
			# attempt is counted once per run however often that happens.
			failed_restart = key == "nightly" and run.get("head_sha") in phase["nightly_dispatched"]
			counted = phase.setdefault("counted_runs", [])
			if failed_restart and run["id"] not in counted:
				# The restarted nightly failed again.
				phase["attempts"] += 1
				counted.append(run["id"])
			context = self._ci_context(key, run, lists[key])
			if failed_restart:
				context["retry"] = context["excerpt"]
			phase["ci"] = context
			# The ticket notes are best-effort: a bug API error (a deleted ticket answers 404 forever)
			# must not keep the run unseen and abort every poll.
			if phase.get("awaiting_develop") and self._descends(phase["awaiting_develop"], run.get("head_sha") or ""):
				# develop is still (or again) red with the emergency fix in it: the ticket goes back
				# to `triaged`, where _emergency_due retries it while attempts remain. An older run
				# finishing after the ship proves nothing about the fix.
				phase["awaiting_develop"] = None
				phase["awaiting_deadline"] = None
				try:
					self._update(ticket, status="triaged", prUrl="", logTail=self._log_tail(context),
						note="still red after the emergency fix: " + context["run_url"])
				except Exception:
					self.log(traceback.format_exc())
					# Without `triaged` no attempt starts: say so once instead of staying red silently.
					self._park_emergency(ticket, "develop is still red after the emergency fix ({}), and the ticket "
						"could not be set back to triaged".format(context["run_url"]))
			else:
				self._safe_note(ticket, logTail=self._log_tail(context), note="still red: " + context["run_url"])
			phase["red_runs"][key] = run["id"]

	def _refill_ci_context(self, phase, key, run, runs):
		"""A phase opened while GitHub could not list the run's jobs has no excerpt yet: read it again."""
		context = phase.get("ci") or {}
		if not context.get("incomplete") or context.get("run_id") != run["id"]:
			return
		try:
			fresh = self._ci_context(key, run, runs)
		except github.GitHubError as error:
			self.log("ci watch: " + str(error))
			return
		if "retry" in context:
			fresh["retry"] = context["retry"]
		phase["ci"] = fresh
		if not fresh["incomplete"]:
			self._safe_note(phase["ticket"], logTail=self._log_tail(fresh), note="CI log read: " + fresh["run_url"])

	def _safe_note(self, bug_id, **fields):
		try:
			self._update(bug_id, **fields)
		except Exception:
			self.log(traceback.format_exc())

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

	def _restart_nightly(self, newest, colours, nightly_runs):
		"""A red Nightly Release reruns once a newer develop commit is green in the push workflow,
		unless a nightly run is already queued or running."""
		phase = self.state.ci_phase()
		if phase is None or colours.get("nightly") != "red" or colours.get("push") != "green":
			return
		if any(run.get("status") in ACTIVE_RUN_STATUSES for run in nightly_runs):
			return
		sha = newest["push"].get("head_sha") or ""
		if not sha or sha == newest["nightly"].get("head_sha") or sha in phase["nightly_dispatched"]:
			return
		self.github.dispatch(self.config.ci_nightly_workflow, "develop")
		phase["nightly_dispatched"].append(sha)
		if phase.get("awaiting_develop"):
			# The restarted nightly is the run that decides now; its wait starts with the dispatch.
			phase["awaiting_deadline"] = (self.clock() + datetime.timedelta(minutes=self._awaiting_minutes(phase))).isoformat()
		self.log("ci watch: restarted the Nightly Release for " + sha[:8])
		self._safe_note(phase["ticket"], note="restarted the Nightly Release for " + sha[:8])

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
					self._delete_backup(pr[len("branch:"):])
					self.state.record(bug["_id"], "merged-by-user", commit=commit)
					self._announce(bug["_id"], "merged-by-user", {"commit": commit})

	def _check_nightly_breaker(self, now):
		try:
			name, report = self.nightly()
		except Exception:  # an unreachable GitHub or a malformed answer must not stop the loop
			self.log(traceback.format_exc())
			return
		if self.state.data.get("breaker_source") and not loop_state.breaker_active(self.artifacts_dir):
			# The maintainer reset the breaker: what tripped it last time is history.
			self.state.data["breaker_source"] = None
			self.state.save()
		if name and report and report.get("passed") and self._breaker_tripped_by_loop() 				and _nightly_run_id(name) != self.state.data["breaker_source"]["nightly_run"]:
			# A newer Nightly Release is green: develop no longer has what tripped the breaker. Only
			# the loop's own trip clears itself; a hold set by hand (or on top of the trip) stays.
			tripped_by = self.state.data["breaker_source"]["nightly_run"]
			loop_state.reset_breaker(self.artifacts_dir)
			self.state.data["breaker_source"] = None
			self.state.save()
			self.log("circuit breaker cleared: {} is green (tripped by nightly run {})".format(name, tripped_by))
			self.notifier.send(notify.breaker_cleared_message("{} is green".format(name)))
		if name and name not in self.state.data["seen_red_reports"] and loop_state.red_nightly_blames_loop(report):
			self.state.data["seen_red_reports"].append(name)
			if loop_state.trip_breaker(self.artifacts_dir, "nightly {} is red and includes bug-loop merges".format(name), now):
				run_id = _nightly_run_id(name)
				# Only a trip by this run is attributed; a breaker already set (by hand, say) stays unattributed.
				self.state.data["breaker_source"] = None if run_id is None else {
					"nightly_run": run_id, "text": loop_state.breaker_text(self.artifacts_dir)}
			self.state.save()
			self.log("circuit breaker tripped by " + name)
			self.notifier.send(notify.breaker_message("nightly {} is red and includes bug-loop merges".format(name)))

	def _breaker_allows_emergency(self):
		"""True when the tripped breaker must not hold a green emergency fix: the loop tripped it
		itself for a red Nightly Release run that belongs to the current red phase, so the fix
		repairs exactly what tripped it. A manual trip, a trip for another run, or a breaker file
		rewritten since the trip keeps blocking."""
		if not self._breaker_tripped_by_loop():
			return False
		run_id = self.state.data["breaker_source"]["nightly_run"]
		phase = self.state.ci_phase()
		return phase is not None and run_id in (phase.get("nightly_red_runs") or [])

	def _breaker_tripped_by_loop(self):
		"""True when the breaker is tripped and the loop tripped it itself for a red Nightly Release
		run, with nothing added since: a manual trip, or a breaker file rewritten (`breaker on` on
		top of the trip, a reset and a new trip) is the maintainer's."""
		if not loop_state.breaker_active(self.artifacts_dir):
			return False
		source = self.state.data.get("breaker_source") or {}
		return source.get("nightly_run") is not None and source.get("text") == loop_state.breaker_text(self.artifacts_dir)

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
