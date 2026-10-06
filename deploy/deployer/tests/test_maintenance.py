# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

import datetime
import itertools
import tempfile
import unittest

from mmo_deployer.backup import BackupError
from mmo_deployer.maintenance import EXIT_GRACE_S, VERIFY_TIMEOUT_S, Maintenance, Outcome
from mmo_deployer.state import State
from mmo_deployer.web import HttpError

from .fakes import FakeApi, FakeClock, FakeNotifier, FakePatchDir, FakePortainer, make_config


class MaintenanceTests(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.cfg = make_config(self.tmp.name)
		self.clock = FakeClock(datetime.datetime(2026, 10, 7, 4, 45, tzinfo=datetime.timezone.utc))
		self.portainer = FakePortainer("old")
		self.realm_a = FakeApi("realm-a")
		self.realm_b = FakeApi("realm-b")
		self.login = FakeApi("login")
		self.patchdir = FakePatchDir(self.tmp.name, {"old", "new"}, current="old")
		self.backups = []
		self.notifier = FakeNotifier()
		self.state = State(live="old", staged="new")

	def tearDown(self):
		self.tmp.cleanup()

	def make(self, backup=None, cfg=None):
		return Maintenance(cfg or self.cfg, [self.realm_a, self.realm_b], self.login, self.portainer,
			self.patchdir, backup or self.backups.append, self.notifier, self.clock)

	def test_nothing_staged(self):
		self.assertEqual(self.make().run(State(live="old")), Outcome.NOTHING)
		self.assertEqual(self.make().run(State(live="old", staged="old")), Outcome.NOTHING)
		self.assertEqual(self.realm_a.calls, [])

	def test_happy_path(self):
		outcome = self.make().run(self.state)
		self.assertEqual(outcome, Outcome.DEPLOYED)
		self.assertIn(("shutdown", 900), self.realm_a.calls)
		self.assertIn(("shutdown", 900), self.realm_b.calls)
		self.assertEqual(self.backups, ["new"])
		self.assertEqual(self.patchdir.flips, ["new"])
		self.assertEqual(self.portainer.deploys, ["new"])
		self.assertTrue(any("Maintenance started" in m for m in self.notifier.messages))
		self.assertEqual(self.state, State(live="old", staged="new"))

	def test_preflight_realm_down_aborts_before_shutdown(self):
		self.realm_b.health = lambda: False
		self.assertEqual(self.make().run(self.state), Outcome.ABORTED)
		self.assertNotIn(("shutdown", 900), self.realm_a.calls)
		self.assertEqual(self.portainer.deploys, [])
		self.assertTrue(any("realm-b" in m for m in self.notifier.messages))

	def test_preflight_portainer_down_aborts(self):
		self.portainer.ping_error = OSError("refused")
		self.assertEqual(self.make().run(self.state), Outcome.ABORTED)
		self.assertNotIn(("shutdown", 900), self.realm_a.calls)

	def test_preflight_missing_release_aborts(self):
		self.patchdir.release_set.discard("new")
		self.assertEqual(self.make().run(self.state), Outcome.ABORTED)

	def test_partial_schedule_failure_cancels_accepted_realms(self):
		self.realm_b.schedule_error = HttpError(503, "unavailable", "x")
		self.assertEqual(self.make().run(self.state), Outcome.ABORTED)
		self.assertIn("cancel", self.realm_a.calls)
		self.assertEqual(self.portainer.deploys, [])
		self.assertEqual(self.patchdir.flips, [])

	def test_dry_run_changes_nothing(self):
		cfg = make_config(self.tmp.name, dry_run=True)
		self.assertEqual(self.make(cfg=cfg).run(self.state), Outcome.DRY_RUN)
		self.assertNotIn(("shutdown", 900), self.realm_a.calls)
		self.assertEqual(self.portainer.deploys, [])
		self.assertTrue(any("dry-run" in m for m in self.notifier.messages))

	def test_containers_that_never_exit_do_not_block_forever(self):
		self.portainer.states = {"realm_server_01": "running", "world_node_01": "running"}
		self.assertEqual(self.make().run(self.state), Outcome.DEPLOYED)
		self.assertGreaterEqual(self.clock.slept, self.cfg.countdown_s + EXIT_GRACE_S)

	def test_backup_failure_restarts_current_release(self):
		def failing_backup(sha):
			raise BackupError("disk full")
		self.assertEqual(self.make(backup=failing_backup).run(self.state), Outcome.ABORTED)
		self.assertEqual(self.patchdir.flips, [])
		self.assertEqual(self.portainer.deploys, ["old"])
		self.assertTrue(any("disk full" in m for m in self.notifier.messages))

	def test_failed_verification_rolls_back(self):
		self.realm_a.health = lambda: self.portainer.tag != "new"
		self.assertEqual(self.make().run(self.state), Outcome.ROLLED_BACK)
		self.assertEqual(self.patchdir.flips, ["new", "old"])
		self.assertEqual(self.portainer.deploys, ["new", "old"])

	def test_crash_looping_realm_is_rolled_back(self):
		flaky = itertools.cycle([True, False])
		self.realm_a.health = lambda: self.portainer.tag != "new" or next(flaky)
		self.assertEqual(self.make().run(self.state), Outcome.ROLLED_BACK)
		self.assertEqual(self.portainer.deploys, ["new", "old"])

	def test_redeploy_error_attempts_rollback(self):
		self.portainer.redeploy_error = OSError("timeout")
		self.assertEqual(self.make().run(self.state), Outcome.FAILED)
		self.assertEqual(self.portainer.deploys, ["new", "old"])

	def test_no_previous_release_means_failed(self):
		self.realm_a.health = lambda: self.portainer.tag != "new"
		self.assertEqual(self.make().run(State(live=None, staged="new")), Outcome.FAILED)

	def test_rollback_that_also_fails(self):
		# Healthy for the preflight; unhealthy after any redeploy, including the rollback.
		self.realm_a.health = lambda: not self.portainer.deploys
		self.assertEqual(self.make().run(self.state), Outcome.FAILED)

	def test_verification_gives_up_after_timeout(self):
		self.realm_a.health = lambda: self.portainer.tag != "new"
		start = self.clock.now()
		self.make().run(self.state)
		self.assertGreaterEqual((self.clock.now() - start).total_seconds(), VERIFY_TIMEOUT_S)


if __name__ == "__main__":
	unittest.main()
