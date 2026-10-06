# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""The deployer loop: stage new releases, run the nightly maintenance, apply CLI requests.

Only this loop writes state.json. CLI commands drop request files that the next tick
applies, so a command can never be overwritten by a tick that was already running.
"""

import datetime
import json
import logging
import os
import time
from pathlib import Path

from .backup import prune_backups
from .maintenance import Outcome
from .news import add_patch_note
from .stage import StageError, select_release
from .state import load_state, record, save_state
from .web import HttpError

log = logging.getLogger("deployer")

TICK_S = 10
MAINTENANCE_WINDOW_S = 3600


def _short(sha):
	return sha[:8] if sha else "none"


def write_request(state_dir, command, args):
	requests = Path(state_dir) / "requests"
	requests.mkdir(parents=True, exist_ok=True)
	name = "{}-{}".format(time.time_ns(), command)
	tmp = requests / (name + ".tmp")
	tmp.write_text(json.dumps({"command": command, "args": list(args)}), encoding="utf-8")
	os.replace(tmp, requests / (name + ".json"))


class Deployer:
	def __init__(self, cfg, github, stager, maintenance, patchdir, notifier, clock):
		self.cfg = cfg
		self.github = github
		self.stager = stager
		self.maintenance = maintenance
		self.patchdir = patchdir
		self.notifier = notifier
		self.clock = clock
		self.state_path = Path(cfg.state_dir) / "state.json"
		self.requests_dir = Path(cfg.state_dir) / "requests"
		self.next_poll = None
		self.stage_failures = set()

	def run_forever(self):
		state = load_state(self.state_path)
		self.recover_interrupted_maintenance(state)
		self.notifier.send("Deployer started (live {}, staged {}, dry run {}).".format(
			_short(state.live), _short(state.staged), self.cfg.dry_run))
		while True:
			try:
				self.tick()
			except Exception:
				# Keep the service alive; the next tick starts from state.json again.
				log.exception("tick failed")
			self.clock.sleep(TICK_S)

	def recover_interrupted_maintenance(self, state):
		"""A maintenance marker at startup means the deployer died mid-maintenance: pause, never guess."""
		marker = state.maintenance_in_progress
		if not marker:
			return False
		sha = marker.get("commit") if isinstance(marker, dict) else None
		state.paused = True
		state.maintenance_in_progress = None
		record(state, self.clock.now(), "interrupted_maintenance", commit=sha)
		save_state(self.state_path, state)
		self.notifier.send(
			"MANUAL INTERVENTION REQUIRED: the deployer restarted during maintenance for {}; the game stack may be down. "
			"The deployer is paused. See docs/deployment.md (manual recovery).".format(_short(sha)))
		return True

	def tick(self):
		state = load_state(self.state_path)
		self._handle_requests(state)
		now = self.clock.now()
		if not state.paused and (self.next_poll is None or now >= self.next_poll):
			self.next_poll = now + datetime.timedelta(seconds=self.cfg.poll_interval_s)
			self.stage_newest(state)
			save_state(self.state_path, state)
		now = self.clock.now()
		if self.maintenance_due(state, now):
			state.last_maintenance_date = now.date().isoformat()
			# Saved first, so a crash mid-maintenance cannot repeat it the same night.
			save_state(self.state_path, state)
			self.run_maintenance(state)
		save_state(self.state_path, state)

	def maintenance_due(self, state, now):
		if state.paused or not state.staged or state.staged == state.live:
			return False
		if state.last_maintenance_date == now.date().isoformat():
			return False
		start = now.replace(hour=self.cfg.maintenance_at.hour, minute=self.cfg.maintenance_at.minute, second=0, microsecond=0)
		return start <= now < start + datetime.timedelta(seconds=MAINTENANCE_WINDOW_S)

	def stage_newest(self, state):
		try:
			releases = self.github.list_releases(self.cfg.release_prefix)
		except (HttpError, OSError, ValueError, KeyError) as error:
			log.warning("could not list releases: %s", error)
			return
		release = select_release(releases, state)
		if release is not None:
			self._stage(state, release)

	def _stage(self, state, release):
		commit = release["target_commitish"]
		try:
			manifest = self.stager.stage(release)
		except StageError as error:
			log.error("staging %s failed: %s", release["tag_name"], error)
			if commit not in self.stage_failures:
				self.stage_failures.add(commit)
				self.notifier.send("Staging {} failed; retrying every poll, live release unchanged: {}".format(release["tag_name"], error))
			return False
		self.stage_failures.discard(commit)
		state.staged = commit
		state.staged_tag = release["tag_name"]
		state.staged_changes = list(manifest.get("changes") or [])
		record(state, self.clock.now(), "staged", commit=commit, tag=release["tag_name"])
		# Releases that never go live (replaced while staged, or rolled back) would pile up otherwise.
		try:
			self.patchdir.prune(self.cfg.keep_releases, {sha for sha in (state.live, state.previous_live, state.staged) if sha})
		except OSError as error:
			log.warning("pruning releases after staging failed: %s", error)
			self.notifier.send("post-stage cleanup failed: {}".format(error))
		lines = "\n".join("- " + change for change in state.staged_changes[:20])
		self.notifier.send("Staged {} ({} changes) for the {} maintenance.\n{}".format(
			release["tag_name"], len(state.staged_changes), self.cfg.maintenance_at.strftime("%H:%M"), lines))
		return True

	def run_maintenance(self, state):
		sha = state.staged
		if sha and sha != state.live:
			# Durable before anything touches the stack, so a restart mid-maintenance is noticed.
			state.maintenance_in_progress = {"commit": sha, "started": self.clock.now().isoformat()}
			save_state(self.state_path, state)
		try:
			outcome = self.maintenance.run(state)
		except Exception:
			# After the realm shutdown an escaping exception must never leave the stack silently down.
			log.exception("maintenance crashed")
			outcome = Outcome.FAILED
		# Cleared together with the outcome: every save below persists both at once.
		state.maintenance_in_progress = None
		now = self.clock.now()
		record(state, now, "maintenance", outcome=outcome, commit=sha)
		if outcome == Outcome.DEPLOYED:
			self._promote(state, now)
		elif outcome == Outcome.DRY_RUN:
			# Dry runs take a real backup every night; keep them within the same retention.
			try:
				prune_backups(Path(self.cfg.state_dir) / "backups", self.cfg.keep_backups)
			except OSError as error:
				log.warning("pruning backups after the dry run failed: %s", error)
		elif outcome in (Outcome.ROLLED_BACK, Outcome.FAILED):
			state.bad.append(sha)
			state.staged = None
			state.staged_tag = None
			state.staged_changes = []
			if outcome == Outcome.ROLLED_BACK:
				self.notifier.send("Release {} was rolled back; {} is live again. It will not be staged again (deployer unbad {} to retry).".format(
					_short(sha), _short(state.live), sha))
			else:
				state.paused = True
				self.notifier.send("MANUAL INTERVENTION REQUIRED: maintenance for {} did not complete and the stack could not be recovered automatically. The deployer is paused; the release is marked bad (deployer unbad {} after fixing).".format(_short(sha), sha))
		save_state(self.state_path, state)
		return outcome

	def _promote(self, state, now):
		state.previous_live = state.live
		state.live = state.staged
		changes = state.staged_changes
		state.staged = None
		state.staged_tag = None
		state.staged_changes = []
		# Durable before any best-effort cleanup, so a crash cannot forget the promotion.
		save_state(self.state_path, state)
		news = self.patchdir.root / "launcher" / "launcher.json"
		steps = (
			lambda: self.patchdir.prune(self.cfg.keep_releases, {sha for sha in (state.live, state.previous_live) if sha}),
			lambda: prune_backups(Path(self.cfg.state_dir) / "backups", self.cfg.keep_backups),
			lambda: add_patch_note(news, now.strftime("%Y-%m-%d"), changes),
		)
		for index, step in enumerate(steps):
			try:
				result = step()
			except OSError as error:
				log.warning("post-deploy cleanup step failed: %s", error)
				self.notifier.send("post-deploy cleanup step failed: {}".format(error))
				continue
			if index == 2 and not result:
				log.warning("no launcher.json at %s; patch notes not published", news)
		lines = "\n".join("- " + change for change in changes[:20])
		self.notifier.send("Release {} is live.\n{}".format(_short(state.live), lines))

	def _handle_requests(self, state):
		if not self.requests_dir.is_dir():
			return
		for path in sorted(self.requests_dir.glob("*.json")):
			try:
				request = json.loads(path.read_text(encoding="utf-8"))
				command = request["command"]
				args = list(request.get("args") or [])
			except (ValueError, KeyError, TypeError, OSError) as error:
				log.error("dropping malformed request %s: %s", path.name, error)
				command = None
			# Removed before it is applied: a request that raises must never be retried.
			path.unlink(missing_ok=True)
			if command is not None:
				try:
					self._apply_request(state, command, args)
				except Exception as error:
					log.exception("request %s failed", command)
					self.notifier.send("Request {} failed: {}".format(command, type(error).__name__))
			save_state(self.state_path, state)

	def _apply_request(self, state, command, args):
		record(state, self.clock.now(), "request", command=command, args=args)
		if command == "pause":
			state.paused = True
		elif command == "resume":
			state.paused = False
		elif command == "unbad":
			state.bad = [sha for sha in state.bad if sha != args[0]]
		elif command == "stage-now":
			self.next_poll = None
		elif command == "deploy-now":
			self._deploy_now(state, args[0] if args else None)
		elif command == "rollback":
			self._manual_rollback(state)
		else:
			log.error("unknown request %s", command)

	def _deploy_now(self, state, sha):
		if sha and sha != state.staged:
			try:
				releases = self.github.list_releases(self.cfg.release_prefix)
			except (HttpError, OSError, ValueError, KeyError) as error:
				self.notifier.send("deploy-now: could not list releases: {}".format(error))
				return
			matches = [r for r in releases if r["target_commitish"] == sha]
			if not matches:
				self.notifier.send("deploy-now: no {} release for {}".format(self.cfg.release_prefix, sha))
				return
			if not self._stage(state, matches[0]):
				return
		if not state.staged or state.staged == state.live:
			self.notifier.send("deploy-now: nothing new is staged")
			return
		# Same guard as the scheduled path: no second maintenance in this window.
		state.last_maintenance_date = self.clock.now().date().isoformat()
		save_state(self.state_path, state)
		self.run_maintenance(state)

	def _manual_rollback(self, state):
		target = state.previous_live
		if not target:
			self.notifier.send("rollback: there is no previous release to return to")
			return
		self.notifier.send("Manual rollback from {} to {}: servers restart now.".format(_short(state.live), _short(target)))
		if self.maintenance.rollback_to(target):
			state.bad.append(state.live)
			state.live = target
			state.previous_live = None
			self.notifier.send("Rollback done: {} is live.".format(_short(target)))
		else:
			state.paused = True
			self.notifier.send("MANUAL INTERVENTION REQUIRED: rollback to {} failed. The deployer is paused.".format(_short(target)))
