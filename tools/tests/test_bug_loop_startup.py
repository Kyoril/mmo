#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Startup of tools/bugs/bug_loop.py: the game-data decoder is built eagerly from the runtime
snapshot's schemas with the main checkout's protoc, and its absence only disables data fixes."""

import importlib.util
import os
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MODULE_PATH = os.path.join(REPO_ROOT, "tools", "bugs", "bug_loop.py")
_spec = importlib.util.spec_from_file_location("bug_loop_cli", MODULE_PATH)
bug_loop = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(bug_loop)


class BreakerCommandTests(unittest.TestCase):
	def test_breaker_on_always_changes_a_tripped_breaker(self):
		with tempfile.TemporaryDirectory() as repo:
			artifacts = os.path.join(repo, "artifacts", "bug-loop")
			bug_loop.loop_state.trip_breaker(artifacts, "nightly run 10 is red", bug_loop.loop.utcnow())
			path = os.path.join(artifacts, bug_loop.loop_state.BREAKER_FILE)
			with open(path, encoding="utf-8") as handle:
				before = handle.read()
			self.assertEqual(bug_loop.main(["--repo", repo, "breaker", "on", "--reason", "hold"]), 0)
			with open(path, encoding="utf-8") as handle:
				after = handle.read()
			self.assertTrue(after.startswith(before))
			self.assertTrue(after.rstrip("\n").endswith("hold (set by hand)"))


class StartupTests(unittest.TestCase):
	def test_decoder_is_built_from_the_runtime_schemas_with_main_protoc(self):
		with tempfile.TemporaryDirectory() as repo:
			protoc = os.path.join(repo, "build", "_deps", "protobuf-build", "Release", "protoc.exe")
			os.makedirs(os.path.dirname(protoc))
			open(protoc, "wb").close()
			calls = []
			decoder = bug_loop.make_decoder("H:/runtime", repo, calls.append,
				factory=lambda root, protoc: calls.append((root, protoc)) or "decoder")
			self.assertEqual(decoder, "decoder")
			self.assertEqual(calls, [("H:/runtime", protoc)])

	def test_missing_protoc_runs_without_decoder(self):
		messages = []
		with tempfile.TemporaryDirectory() as folder:
			decoder = bug_loop.make_decoder(folder, folder, messages.append)
		self.assertIsNone(decoder)
		self.assertIn("decoder unavailable", messages[0])

	def test_runtime_root_is_the_checkout_of_the_script(self):
		self.assertEqual(os.path.normcase(bug_loop.RUNTIME_ROOT), os.path.normcase(REPO_ROOT))


class WorktreePathTests(unittest.TestCase):
	REPO = os.path.join(tempfile.gettempdir(), "checkouts", "mmo")

	def config(self, worktree=""):
		return bug_loop.loop_config.LoopConfig(worktree=worktree)

	def test_default_is_a_sibling_of_the_main_checkout(self):
		path = bug_loop.resolve_worktree(self.config(), self.REPO, {})
		self.assertEqual(path, os.path.join(os.path.dirname(self.REPO), "mmo-bugloop"))

	def test_environment_variable_wins(self):
		other = os.path.join(tempfile.gettempdir(), "elsewhere", "loop")
		path = bug_loop.resolve_worktree(self.config("ignored"), self.REPO, {"MMO_BUGLOOP_WORKTREE": other})
		self.assertEqual(path, os.path.abspath(other))

	def test_config_value_is_used_without_environment_variable(self):
		other = os.path.join(tempfile.gettempdir(), "configured")
		self.assertEqual(bug_loop.resolve_worktree(self.config(other), self.REPO, {}), os.path.abspath(other))

	def test_checked_in_config_holds_no_machine_path(self):
		config = bug_loop.loop_config.load_config(os.path.join(REPO_ROOT, "tools", "bugs", "bug_loop.json"))
		self.assertEqual(config.worktree, "")


class NotifierWiringTests(unittest.TestCase):
	def test_notifier_reads_webhook_and_ui_url(self):
		notifier = bug_loop.make_notifier({"MMO_BUGLOOP_WEBHOOK": "https://hook", "MMO_BUGLOOP_UI_URL": "https://ui/"}, print)
		self.assertTrue(notifier.enabled)
		self.assertEqual(notifier.bug_link("a"), "https://ui/bugs/a")

	def test_no_webhook_means_silent(self):
		self.assertFalse(bug_loop.make_notifier({}, print).enabled)

	def test_notifications_log_line(self):
		on = bug_loop.make_notifier({"MMO_BUGLOOP_WEBHOOK": "https://hook"}, print)
		off = bug_loop.make_notifier({}, print)
		self.assertEqual(bug_loop.notifications_status(on, dry_run=False), "notifications: on")
		self.assertEqual(bug_loop.notifications_status(off, dry_run=False), "notifications: off (MMO_BUGLOOP_WEBHOOK not set)")
		# A dry run never sends, whether or not the variable is set.
		self.assertEqual(bug_loop.notifications_status(on, dry_run=True), "notifications: off (dry run)")
		self.assertEqual(bug_loop.notifications_status(off, dry_run=True), "notifications: off (dry run)")


class GitHubWiringTests(unittest.TestCase):
	def test_token_and_github_origin_turn_the_watch_on(self):
		messages = []
		client = bug_loop.make_github({"MMO_BUGLOOP_GITHUB_TOKEN": "t0ken"}, "git@github.com:Kyoril/mmo.git", messages.append)
		self.assertEqual(client.base, "https://api.github.com/repos/Kyoril/mmo")
		self.assertFalse(any("t0ken" in message for message in messages))

	def test_no_token_means_no_watch(self):
		messages = []
		self.assertIsNone(bug_loop.make_github({}, "git@github.com:Kyoril/mmo.git", messages.append))
		self.assertTrue(messages[0].startswith("ci watch: off ("))

	def test_non_github_origin_means_no_watch(self):
		messages = []
		self.assertIsNone(bug_loop.make_github({"MMO_BUGLOOP_GITHUB_TOKEN": "t"}, "https://gitlab.com/a/b.git", messages.append))
		self.assertTrue(messages[0].startswith("ci watch: off ("))

	def test_stored_phase_without_the_watch_is_warned_about(self):
		class State:
			def __init__(self, phase):
				self.phase = phase

			def ci_phase(self):
				return self.phase

		phase = {"ticket": "c1" + "0" * 22}
		self.assertEqual(bug_loop.stored_phase_warning(State(phase), None),
			"ci watch: off, but state holds a red phase for ticket c1{}; ships are not held".format("0" * 22))
		self.assertIsNone(bug_loop.stored_phase_warning(State(phase), object()))
		self.assertIsNone(bug_loop.stored_phase_warning(State(None), None))


if __name__ == "__main__":
	unittest.main()
