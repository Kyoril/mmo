# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""One maintenance run: preflight → shutdown → backup → flip → redeploy → verify → rollback.

Anything that fails before the shutdown leaves the live release untouched; anything that
fails after it rolls back to the previous release.
"""

import datetime
import http.client
import logging

from .backup import BackupError
from .web import HttpError

log = logging.getLogger("deployer")

VERIFY_TIMEOUT_S = 300
EXIT_GRACE_S = 1800
POLL_S = 15
# A crash-looping server can answer once between restarts.
HEALTHY_ROUNDS = 2

_REMOTE_ERRORS = (HttpError, OSError, ValueError, KeyError, http.client.HTTPException, TypeError, AttributeError)


class Outcome:
	NOTHING = "nothing"
	DRY_RUN = "dry_run"
	DEPLOYED = "deployed"
	ABORTED = "aborted"
	ROLLED_BACK = "rolled_back"
	FAILED = "failed"


class Abort(Exception):
	"""Stopped before anything changed on the live stack.

	Outcome.ABORTED means: release not deployed, live release running."""


def _short(sha):
	return sha[:8] if sha else "none"


class Maintenance:
	def __init__(self, cfg, realms, login, portainer, patchdir, backup, notifier, clock):
		self.cfg = cfg
		self.realms = list(realms)
		self.login = login
		self.portainer = portainer
		self.patchdir = patchdir
		self.backup = backup
		self.notifier = notifier
		self.clock = clock

	def run(self, state):
		sha = state.staged
		if not sha or sha == state.live:
			return Outcome.NOTHING
		try:
			self._preflight(sha)
		except Abort as error:
			self.notifier.send("Maintenance for {} aborted, nothing changed: {}".format(_short(sha), error))
			return Outcome.ABORTED

		if self.cfg.dry_run:
			return self._dry_run(sha)

		try:
			self._schedule_shutdowns()
		except Abort as error:
			self.notifier.send("Maintenance for {} aborted, nothing changed: {}".format(_short(sha), error))
			return Outcome.ABORTED
		self.notifier.send("Maintenance started: {} goes live after a {} min countdown.".format(_short(sha), self.cfg.countdown_s // 60))

		self._wait_for_exit()

		try:
			self.backup(sha)
		except (BackupError, OSError) as error:
			self.notifier.send("Database backup failed ({}); restarting the stack on the current release {}.".format(error, _short(state.live)))
			if not state.live:
				self.notifier.send("MANUAL INTERVENTION REQUIRED: no live release to restart after the backup failure.")
				return Outcome.FAILED
			try:
				self.portainer.redeploy(state.live)
				restarted = self._verify()
			except _REMOTE_ERRORS as restart_error:
				log.error("restart on %s failed: %s", state.live, restart_error)
				restarted = False
			if restarted:
				return Outcome.ABORTED
			self.notifier.send("MANUAL INTERVENTION REQUIRED: restart on {} failed or is unhealthy.".format(_short(state.live)))
			return Outcome.FAILED

		try:
			self.patchdir.flip_current(sha)
			self.portainer.redeploy(sha)
			healthy = self._verify()
		except _REMOTE_ERRORS as error:
			log.error("deploying %s failed: %s", sha, error)
			healthy = False
		if healthy:
			return Outcome.DEPLOYED

		self.notifier.send("Release {} failed its health check; rolling back to {}.".format(_short(sha), _short(state.live)))
		return Outcome.ROLLED_BACK if self.rollback_to(state.live) else Outcome.FAILED

	def _dry_run(self, sha):
		"""Changes nothing on the stack, but proves the container query and the real backup."""
		try:
			log.info("[dry-run] container states: %s", self.portainer.service_states())
		except _REMOTE_ERRORS as error:
			log.warning("[dry-run] could not read container states: %s", error)
		try:
			self.backup(sha)
			backup_result = "backup OK"
		except (BackupError, OSError) as error:
			backup_result = "backup FAILED: {}".format(error)
		self.notifier.send("[dry-run] would deploy {} after a {} s countdown on {}; {}".format(
			_short(sha), self.cfg.countdown_s, ", ".join(r.name for r in self.realms), backup_result))
		return Outcome.DRY_RUN

	def rollback_to(self, sha):
		"""Points patch and stack back at `sha`. True once it is healthy again."""
		if not self.patchdir.has_release(sha):
			log.error("cannot roll back: release %s is not on disk", sha)
			return False
		try:
			self.patchdir.flip_current(sha)
			self.portainer.redeploy(sha)
		except _REMOTE_ERRORS as error:
			log.error("rollback to %s failed: %s", sha, error)
			return False
		return self._verify()

	def _preflight(self, sha):
		if not self.patchdir.has_release(sha):
			raise Abort("staged release {} is missing on disk".format(_short(sha)))
		for api in [self.login] + self.realms:
			try:
				api.uptime()
			except _REMOTE_ERRORS as error:
				raise Abort("{} is not answering: {}".format(api.name, error))
		try:
			self.portainer.ping()
		except _REMOTE_ERRORS as error:
			raise Abort("Portainer is not answering: {}".format(error))

	def _schedule_shutdowns(self):
		accepted = []
		for realm in self.realms:
			try:
				# False means a shutdown is already due (not ours); never cancel that one.
				if realm.schedule_shutdown(self.cfg.countdown_s):
					accepted.append(realm)
			except _REMOTE_ERRORS as error:
				for other in accepted:
					try:
						other.cancel_shutdown()
					except _REMOTE_ERRORS as cancel_error:
						log.error("could not cancel the shutdown on %s: %s", other.name, cancel_error)
				raise Abort("could not schedule the shutdown on {}: {}".format(realm.name, error))

	def _wait_for_exit(self):
		deadline = self.clock.now() + datetime.timedelta(seconds=self.cfg.countdown_s + EXIT_GRACE_S)
		while self.clock.now() < deadline:
			try:
				states = self.portainer.service_states()
				if all(service in states and states[service] != "running" for service in self.cfg.wait_services):
					return True
			except _REMOTE_ERRORS as error:
				log.warning("could not read container states: %s", error)
			self.clock.sleep(POLL_S)
		# The stack update sends SIGTERM, which the realm treats as a graceful immediate shutdown.
		log.warning("servers still running after the countdown; the redeploy will stop them")
		return False

	def _healthy(self):
		"""Login and every realm answer, and every waited-for container (the world node too) runs."""
		for api in [self.login] + self.realms:
			try:
				api.uptime()
			except _REMOTE_ERRORS:
				return False
		try:
			states = self.portainer.service_states()
		except _REMOTE_ERRORS as error:
			log.warning("could not read container states: %s", error)
			return False
		for service in self.cfg.wait_services:
			if states.get(service) != "running":
				log.info("%s is %s, not running", service, states.get(service, "missing"))
				return False
		return True

	def _verify(self):
		deadline = self.clock.now() + datetime.timedelta(seconds=VERIFY_TIMEOUT_S)
		streak = 0
		while True:
			streak = streak + 1 if self._healthy() else 0
			if streak >= HEALTHY_ROUNDS:
				return True
			if self.clock.now() >= deadline:
				return False
			self.clock.sleep(POLL_S)
