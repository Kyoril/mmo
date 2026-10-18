#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""The bug loop's status view: status.json rendering, the process check, the busy/idle/paused
Discord messages and the `bug_loop.py status` command."""

import contextlib
import datetime
import importlib.util
import io
import json
import os
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools", "bugs"))

from bugloop import notify, status  # noqa: E402

MODULE_PATH = os.path.join(REPO_ROOT, "tools", "bugs", "bug_loop.py")
_spec = importlib.util.spec_from_file_location("bug_loop_cli_status", MODULE_PATH)
bug_loop = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(bug_loop)

NOW = datetime.datetime(2026, 10, 18, 12, 0, tzinfo=datetime.timezone.utc)


def at(minutes_ago):
	return (NOW - datetime.timedelta(minutes=minutes_ago)).isoformat()


SNAPSHOT = {
	"pid": 4242, "dry_run": False, "started_at": at(180), "updated_at": at(1), "stage": "gating", "stage_since": at(12),
	"bug": "6ad3d6d2e34a31a4b36cd451", "branch": "bugfix/b36cd451", "summary": "Marauders attack the waypost",
	"detail": "", "until": None, "mode": {"name": "busy", "since": at(90), "reason": "", "counts": {}},
	"fix_queue": ["a" * 24, "b" * 24, "c" * 24, "d" * 24], "ship_queue": 0, "invocations": 35, "budget": 400,
	"autoships": 2, "autoship_cap": 20, "breaker": False, "develop_red": False,
	"last_outcome": {"bug": "6ad32c23e34a31a4b36ccfdd", "outcome": "needs-info"},
}


class RenderTests(unittest.TestCase):
	def test_a_running_loop_shows_mode_stage_bug_queue_and_budget(self):
		text = status.render(SNAPSHOT, NOW, alive=True)
		self.assertIn("running (pid 4242, live, up 3.0 h)", text)
		self.assertIn("busy: works the backlog without waiting since 90 min ago", text)
		self.assertIn("running the fast gate on the candidate for 12 min", text)
		self.assertIn("6ad3d6d2e34a31a4b36cd451 (bugfix/b36cd451)", text)
		self.assertIn("Marauders attack the waypost", text)
		self.assertIn("queue: 4 bug(s) to fix, next {}, {}, {}".format("a" * 24, "b" * 24, "c" * 24), text)
		self.assertIn("35 of 400 Claude invocations, 2 of 20 auto-ships", text)
		self.assertIn("last:  6ad32c23e34a31a4b36ccfdd needs-info", text)
		self.assertNotIn("BREAKER", text)

	def test_a_dead_process_says_not_running(self):
		text = status.render(SNAPSHOT, NOW, alive=False)
		self.assertTrue(text.startswith("bug loop: NOT RUNNING (pid 4242 is gone; last update 60 s ago)"))

	def test_sleeping_shows_the_next_poll(self):
		snapshot = dict(SNAPSHOT, stage="sleeping", stage_since=at(2), bug=None, until=(NOW + datetime.timedelta(seconds=180)).isoformat(),
			mode={"name": "paused", "since": at(30), "reason": "daily Claude budget used up (40 of 40); resumes at 00:00 UTC"})
		text = status.render(snapshot, NOW, alive=True)
		self.assertIn("sleeping until the next poll for 2 min, next poll in 180 s", text)
		self.assertIn("paused: bugs wait, but the loop cannot work them now (daily Claude budget used up (40 of 40)", text)
		self.assertNotIn("bug:", text)

	def test_breaker_and_red_develop_are_called_out(self):
		text = status.render(dict(SNAPSHOT, breaker=True, develop_red=True, ship_queue=2), NOW, alive=True)
		self.assertIn("BREAKER tripped", text)
		self.assertIn("develop is red", text)
		self.assertIn("ships: 2 queued", text)

	def test_no_status_file_yet(self):
		self.assertIn("no status yet", status.render(None, NOW, alive=False))


class FileTests(unittest.TestCase):
	def test_write_then_read_round_trips_and_leaves_no_temp_file(self):
		with tempfile.TemporaryDirectory() as folder:
			status.write(folder, SNAPSHOT)
			self.assertEqual(status.read(folder), SNAPSHOT)
			self.assertEqual(os.listdir(folder), [status.STATUS_FILE])

	def test_an_unreadable_file_reads_as_none(self):
		with tempfile.TemporaryDirectory() as folder:
			with open(os.path.join(folder, status.STATUS_FILE), "w", encoding="utf-8") as handle:
				handle.write("{broken")
			self.assertIsNone(status.read(folder))

	def test_pid_alive(self):
		self.assertTrue(status.pid_alive(os.getpid()))
		self.assertFalse(status.pid_alive(0))
		self.assertFalse(status.pid_alive(None))
		self.assertFalse(status.pid_alive(2 ** 31 - 2))


class ModeMessageTests(unittest.TestCase):
	def test_busy(self):
		self.assertEqual(notify.mode_message("busy", {"name": "idle"}, NOW, 3, "", 5),
			"**Bug loop busy** — 3 bug(s) queued; working through them without waiting")

	def test_paused_quotes_the_reason(self):
		text = notify.mode_message("paused", {"name": "busy"}, NOW, 7, "daily Claude budget used up (400 of 400); resumes at 00:00 UTC", 5)
		self.assertIn("daily Claude budget used up (400 of 400)", text)
		self.assertIn("7 bug(s) wait", text)

	def test_idle_after_busy_reports_the_stretch(self):
		previous = {"name": "busy", "since": at(150), "counts": {"shipped": 2, "shipped-by-maintainer": 1, "parked": 4, "needs-info": 1}}
		text = notify.mode_message("idle", previous, NOW, 0, "", 5)
		self.assertIn("polling for new reports every 5 min", text)
		self.assertIn("This busy stretch (2.5 h): shipped 3, parked 4, needs-info 1", text)

	def test_idle_after_paused_has_no_stretch(self):
		text = notify.mode_message("idle", {"name": "paused", "since": at(10), "counts": {"parked": 1}}, NOW, 0, "", 5)
		self.assertNotIn("stretch", text)


class StatusCommandTests(unittest.TestCase):
	def run_command(self, repo, *extra):
		out = io.StringIO()
		with contextlib.redirect_stdout(out):
			code = bug_loop.main(["--repo", repo, "status"] + list(extra))
		return code, out.getvalue()

	def test_a_live_loop_exits_0_and_prints_its_status(self):
		with tempfile.TemporaryDirectory() as repo:
			status.write(os.path.join(repo, "artifacts", "bug-loop"), dict(SNAPSHOT, pid=os.getpid()))
			code, text = self.run_command(repo)
			self.assertEqual(code, 0)
			self.assertIn("bug loop: running", text)
			self.assertIn("bugfix/b36cd451", text)

	def test_a_dead_loop_exits_1(self):
		with tempfile.TemporaryDirectory() as repo:
			status.write(os.path.join(repo, "artifacts", "bug-loop"), dict(SNAPSHOT, pid=2 ** 31 - 2))
			code, text = self.run_command(repo)
			self.assertEqual(code, 1)
			self.assertIn("NOT RUNNING", text)

	def test_json_prints_the_raw_snapshot(self):
		with tempfile.TemporaryDirectory() as repo:
			status.write(os.path.join(repo, "artifacts", "bug-loop"), SNAPSHOT)
			code, text = self.run_command(repo, "--json")
			self.assertEqual(code, 0)
			self.assertEqual(json.loads(text), SNAPSHOT)

	def test_dry_run_reads_the_shadow_status(self):
		with tempfile.TemporaryDirectory() as repo:
			status.write(os.path.join(repo, "artifacts", "bug-loop"), dict(SNAPSHOT, dry_run=True), status.DRY_STATUS_FILE)
			code, text = self.run_command(repo, "--dry-run", "--json")
			self.assertTrue(json.loads(text)["dry_run"])


if __name__ == "__main__":
	unittest.main()
