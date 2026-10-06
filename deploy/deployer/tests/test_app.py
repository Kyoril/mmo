# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

import datetime
import json
import tempfile
import unittest
from pathlib import Path

from mmo_deployer.app import Deployer, write_request
from mmo_deployer.maintenance import Outcome
from mmo_deployer.stage import StageError
from mmo_deployer.state import State, load_state, save_state

from .fakes import FakeClock, FakeGitHub, FakeMaintenance, FakeNotifier, FakePatchDir, FakeStager, make_config

UTC = datetime.timezone.utc
NEW = "n" * 40
OLD = "o" * 40


def at(hour, minute, day=7):
	return datetime.datetime(2026, 10, day, hour, minute, tzinfo=UTC)


class AppTests(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.cfg = make_config(self.tmp.name)
		self.cfg.state_dir.mkdir(parents=True)
		self.clock = FakeClock(at(1, 0))
		self.github = FakeGitHub()
		self.github.add_release("nightly-20261007-nnnnnnnn", NEW, {}, "2026-10-07T00:50:00Z")
		self.stager = FakeStager()
		self.maintenance = FakeMaintenance()
		self.patchdir = FakePatchDir(self.tmp.name, {OLD, NEW}, current=OLD)
		self.notifier = FakeNotifier()
		self.state_path = self.cfg.state_dir / "state.json"
		save_state(self.state_path, State(live=OLD))
		self.app = Deployer(self.cfg, self.github, self.stager, self.maintenance, self.patchdir, self.notifier, self.clock)

	def tearDown(self):
		self.tmp.cleanup()

	def state(self):
		return load_state(self.state_path)

	def test_tick_stages_newest_release(self):
		self.app.tick()
		state = self.state()
		self.assertEqual(state.staged, NEW)
		self.assertEqual(state.staged_tag, "nightly-20261007-nnnnnnnn")
		self.assertEqual(state.staged_changes, ["fix(loot): roll on the right table"])
		self.assertTrue(any("Staged" in m for m in self.notifier.messages))
		self.assertEqual(self.maintenance.runs, [])

	def test_poll_interval_is_respected(self):
		self.app.tick()
		self.stager.staged.clear()
		save_state(self.state_path, State(live=OLD))
		self.clock.sleep(60)
		self.app.tick()
		self.assertEqual(self.stager.staged, [])

	def test_stage_failure_notifies_once(self):
		self.stager.error = StageError("git fetch failed")
		self.app.tick()
		self.clock.sleep(self.cfg.poll_interval_s)
		self.app.tick()
		self.assertEqual(self.stager.staged, [NEW, NEW])
		self.assertEqual(sum("Staging" in m for m in self.notifier.messages), 1)
		self.assertIsNone(self.state().staged)
		self.assertEqual(self.state().live, OLD)

	def test_listing_error_is_survivable(self):
		self.github.list_error = OSError("dns")
		self.app.tick()
		self.assertIsNone(self.state().staged)

	def test_maintenance_runs_in_window_and_promotes(self):
		news = Path(self.tmp.name) / "launcher" / "launcher.json"
		news.parent.mkdir()
		news.write_text(json.dumps({"version": 1, "patches": []}), encoding="utf-8")
		self.app.tick()
		self.clock.current = at(4, 46)
		self.app.tick()
		state = self.state()
		self.assertEqual(self.maintenance.runs, [NEW])
		self.assertEqual(state.live, NEW)
		self.assertEqual(state.previous_live, OLD)
		self.assertIsNone(state.staged)
		self.assertEqual(state.last_maintenance_date, "2026-10-07")
		self.assertEqual(self.patchdir.pruned, [(3, {NEW, OLD})])
		self.assertEqual(json.loads(news.read_text(encoding="utf-8"))["patches"][0]["title"], "Update 2026-10-07")
		self.assertTrue(any("is live" in m for m in self.notifier.messages))

	def test_maintenance_runs_once_per_day(self):
		self.app.tick()
		self.clock.current = at(4, 46)
		self.maintenance.outcome = Outcome.ABORTED
		self.app.tick()
		self.clock.current = at(4, 50)
		self.app.tick()
		self.assertEqual(self.maintenance.runs, [NEW])

	def test_restart_in_the_afternoon_does_not_trigger_maintenance(self):
		save_state(self.state_path, State(live=OLD, staged=NEW))
		self.clock.current = at(14, 0)
		self.app.tick()
		self.assertEqual(self.maintenance.runs, [])
		self.clock.current = at(4, 50, day=8)
		self.app.tick()
		self.assertEqual(self.maintenance.runs, [NEW])

	def test_nothing_staged_means_no_maintenance(self):
		self.github.releases.clear()
		self.clock.current = at(4, 46)
		self.app.tick()
		self.assertEqual(self.maintenance.runs, [])

	def test_rolled_back_release_is_marked_bad(self):
		self.maintenance.outcome = Outcome.ROLLED_BACK
		save_state(self.state_path, State(live=OLD, staged=NEW))
		self.clock.current = at(4, 46)
		self.app.tick()
		state = self.state()
		self.assertEqual(state.bad, [NEW])
		self.assertIsNone(state.staged)
		self.assertEqual(state.live, OLD)
		self.assertFalse(state.paused)

	def test_failed_release_pauses(self):
		self.maintenance.outcome = Outcome.FAILED
		save_state(self.state_path, State(live=OLD, staged=NEW))
		self.clock.current = at(4, 46)
		self.app.tick()
		self.assertTrue(self.state().paused)
		self.assertTrue(any("MANUAL INTERVENTION" in m for m in self.notifier.messages))

	def test_paused_skips_staging_and_maintenance(self):
		save_state(self.state_path, State(live=OLD, staged=NEW, paused=True))
		self.clock.current = at(4, 46)
		self.app.tick()
		self.assertEqual(self.stager.staged, [])
		self.assertEqual(self.maintenance.runs, [])

	def test_pause_request_is_applied_and_persisted(self):
		write_request(self.cfg.state_dir, "pause", [])
		self.app.tick()
		self.assertTrue(self.state().paused)
		self.assertEqual(list((self.cfg.state_dir / "requests").glob("*.json")), [])
		write_request(self.cfg.state_dir, "resume", [])
		self.app.tick()
		self.assertFalse(self.state().paused)

	def test_deploy_now_outside_window(self):
		write_request(self.cfg.state_dir, "deploy-now", [NEW])
		self.clock.current = at(13, 0)
		self.app.tick()
		self.assertEqual(self.maintenance.runs, [NEW])
		self.assertEqual(self.state().live, NEW)

	def test_deploy_now_unknown_sha(self):
		write_request(self.cfg.state_dir, "deploy-now", ["x" * 40])
		self.app.next_poll = self.clock.now() + datetime.timedelta(hours=1)
		self.app.tick()
		self.assertEqual(self.maintenance.runs, [])
		self.assertTrue(any("no nightly- release" in m for m in self.notifier.messages))

	def test_manual_rollback(self):
		save_state(self.state_path, State(live=NEW, previous_live=OLD))
		write_request(self.cfg.state_dir, "rollback", [])
		self.app.tick()
		state = self.state()
		self.assertEqual(self.maintenance.rollbacks, [OLD])
		self.assertEqual(state.live, OLD)
		self.assertIsNone(state.previous_live)
		self.assertEqual(state.bad, [NEW])

	def test_manual_rollback_without_previous(self):
		write_request(self.cfg.state_dir, "rollback", [])
		self.app.tick()
		self.assertEqual(self.maintenance.rollbacks, [])

	def test_unbad(self):
		save_state(self.state_path, State(live=OLD, bad=[NEW]))
		write_request(self.cfg.state_dir, "unbad", [NEW])
		self.app.next_poll = self.clock.now() + datetime.timedelta(hours=1)
		self.app.tick()
		self.assertEqual(self.state().bad, [])

	def test_maintenance_crash_pauses(self):
		class Crashing(FakeMaintenance):
			def run(self, state):
				raise RuntimeError("boom")

		self.app.maintenance = Crashing()
		save_state(self.state_path, State(live=OLD, staged=NEW))
		self.clock.current = at(4, 46)
		self.app.tick()
		state = self.state()
		self.assertTrue(state.paused)
		self.assertIsNone(state.staged)
		self.assertIn(NEW, state.bad)
		self.assertTrue(any("MANUAL INTERVENTION" in m for m in self.notifier.messages))

	def test_malformed_request_is_dropped(self):
		requests = self.cfg.state_dir / "requests"
		requests.mkdir()
		(requests / "1-broken.json").write_text("{nope", encoding="utf-8")
		self.app.tick()
		self.assertEqual(list(requests.glob("*.json")), [])


if __name__ == "__main__":
	unittest.main()
