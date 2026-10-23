#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the bug loop's git operations against throwaway repositories: a bare origin,
a bare submodule origin and a main clone, like H:/mmo with data/client."""

import os
import shutil
import stat
import sys
import tempfile
import threading
import unittest
from unittest import mock

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools", "bugs"))

from bugloop import gitops, merge_resolve, winlock  # noqa: E402

GOOD_REVIEW = {"keeps_ours": True, "keeps_theirs": True, "adds_unrelated": False, "problems": [], "summary": "ok"}


def git(cwd, *args):
	return gitops.run_git(cwd, *args).stdout.strip()


class FakeRunner:
	"""Stands in for ClaudeRunner: the merge agent runs `edit(worktree root)`, the review answers `review`."""

	def __init__(self, edit=None, review=None):
		self.edit = edit
		self.review = review or GOOD_REVIEW
		self.agent_inputs = []
		self.review_inputs = []

	def agent(self, prompt, input_text, cwd, timeout, max_usd):
		self.agent_inputs.append(input_text)
		if self.edit:
			self.edit(cwd)
		return {}

	def structured(self, prompt, input_text, schema, tools, cwd, timeout):
		self.review_inputs.append(input_text)
		return self.review


def write(path, text):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8", newline="\n") as handle:
		handle.write(text)


def _make_writable(func, path, _info):
	os.chmod(path, stat.S_IWRITE)
	func(path)


class GitopsTests(unittest.TestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp()
		self.addCleanup(shutil.rmtree, self.root, onerror=_make_writable)
		empty = os.path.join(self.root, "empty.gitconfig")
		write(empty, "")
		patcher = mock.patch.dict(os.environ, {
			"GIT_CONFIG_GLOBAL": empty, "GIT_CONFIG_NOSYSTEM": "1",
			"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@example.test",
			"GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@example.test",
		})
		patcher.start()
		self.addCleanup(patcher.stop)

		self.sub_origin = os.path.join(self.root, "sub.git")
		git(self.root, "init", "--bare", "-b", "master", self.sub_origin)
		self.sub_seed = os.path.join(self.root, "sub-seed")
		git(self.root, "init", "-b", "master", self.sub_seed)
		write(os.path.join(self.sub_seed, "data.txt"), "one\n")
		git(self.sub_seed, "add", "-A")
		git(self.sub_seed, "commit", "-m", "seed")
		git(self.sub_seed, "push", self.sub_origin, "master")

		self.origin = os.path.join(self.root, "origin.git")
		git(self.root, "init", "--bare", "-b", "develop", self.origin)
		self.main = os.path.join(self.root, "main")
		git(self.root, "init", "-b", "develop", self.main)
		write(os.path.join(self.main, "src", "a.cpp"), "int a = 1;\n")
		git(self.main, "submodule", "add", self.sub_origin, "data/client")
		git(self.main, "add", "-A")
		git(self.main, "commit", "-m", "base")
		git(self.main, "remote", "add", "origin", self.origin)
		git(self.main, "push", "origin", "develop")

		self.wt = gitops.Worktree(self.main, os.path.join(self.root, "wt"), submodules=("data/client",))

	def make_fix(self):
		base = self.wt.prepare()
		self.wt.start_branch("bugfix/x", base)
		sub = self.wt.sub_path("data/client")
		write(os.path.join(sub, "data.txt"), "two\n")
		git(sub, "commit", "-am", "fix data")
		write(os.path.join(self.wt.path, "src", "a.cpp"), "int a = 2;\n")
		git(self.wt.path, "add", "-A")
		git(self.wt.path, "commit", "-m", "fix code")
		return base, self.wt.head()

	def test_changes_cover_parent_and_submodule(self):
		base, head = self.make_fix()
		paths = sorted(change.path for change in self.wt.changes(base, head))
		self.assertEqual(paths, ["data/client/data.txt", "src/a.cpp"])
		self.assertTrue(self.wt.is_clean())
		text = self.wt.unified_diff(base, head)
		self.assertIn("+++ b/data/client/data.txt", text)
		self.assertIn("+++ b/src/a.cpp", text)
		self.assertIn("+int a = 2;", text)
		self.assertEqual(self.wt.file_bytes(base, "data/client/data.txt"), b"one\n")
		self.assertEqual(self.wt.file_bytes(head, "data/client/data.txt"), b"two\n")
		self.assertIsNone(self.wt.file_bytes(head, "missing.txt"))

	def test_untracked_file_is_not_clean(self):
		self.make_fix()
		write(os.path.join(self.wt.path, "stray.txt"), "x")
		self.assertFalse(self.wt.is_clean())

	def test_ship_pushes_submodule_then_develop(self):
		_, head = self.make_fix()
		sub_head = self.wt.gitlinks(head)["data/client"]
		result = self.wt.ship("bugfix/x", head, "Merge bugfix/x (bug-loop, gate green at 1)\n\nbody", lambda: self.fail("no gate needed"))
		self.assertTrue(result.ok, result.reason)
		self.assertEqual(git(self.sub_origin, "rev-parse", "master"), sub_head)
		self.assertEqual(git(self.origin, "rev-parse", "develop"), result.commit)
		self.assertEqual(self.wt.merged("bugfix/x"), result.commit)

	def test_ship_lands_submodules_on_the_gitmodules_url_not_the_main_checkout(self):
		# 2026-10-11: H:/mmo's .git/config points submodule.data/client.url at its own non-bare
		# data/client (master checked out, with local commits). The loop pushed there and git
		# refused ("branch is currently checked out", then "remote contains work that you do not
		# have"); develop on GitHub would have referenced data commits only that checkout had.
		local_sub = os.path.join(self.main, "data", "client")
		git(local_sub, "checkout", "-B", "master")
		write(os.path.join(local_sub, "local.txt"), "unpushed\n")
		git(local_sub, "add", "-A")
		git(local_sub, "commit", "-m", "local only")
		local_master = git(local_sub, "rev-parse", "master")
		git(self.main, "config", "submodule.data/client.url", local_sub)
		# A remote left behind by a loop run before the fix must be corrected, not trusted.
		self.wt.prepare()
		sub_dir = self.wt.sub_path("data/client")
		git(sub_dir, "remote", "set-url", self.wt.sub_remote, local_sub)

		_, head = self.make_fix()
		self.assertEqual(os.path.normcase(git(sub_dir, "remote", "get-url", self.wt.sub_remote)), os.path.normcase(self.sub_origin))
		sub_head = self.wt.gitlinks(head)["data/client"]
		result = self.wt.ship("bugfix/x", head, "Merge bugfix/x (bug-loop, gate green at 1)", lambda: self.fail("no gate needed"))
		self.assertTrue(result.ok, result.reason)
		self.assertEqual(git(self.sub_origin, "rev-parse", "master"), sub_head)
		self.assertEqual(git(self.origin, "rev-parse", "develop"), result.commit)
		self.assertEqual(git(local_sub, "rev-parse", "master"), local_master)
		self.assertEqual(git(local_sub, "symbolic-ref", "HEAD"), "refs/heads/master")
		# The main checkout's data/client lacks the shipped data commit now; a fresh loop worktree
		# (cloned from it through the overridden URL) must still reach origin/develop.
		fresh = gitops.Worktree(self.main, os.path.join(self.root, "wt2"), submodules=("data/client",))
		self.assertEqual(fresh.prepare(), result.commit)
		self.assertEqual(git(fresh.sub_path("data/client"), "rev-parse", "HEAD"), sub_head)

	def move_submodule_master(self, name, text):
		"""Someone pushes data to the submodule's master without bumping develop's pointer."""
		write(os.path.join(self.sub_seed, name), text)
		git(self.sub_seed, "add", "-A")
		git(self.sub_seed, "commit", "-m", "elsewhere: " + name)
		git(self.sub_seed, "push", self.sub_origin, "master")
		return git(self.sub_seed, "rev-parse", "HEAD")

	def test_ship_merges_a_submodule_master_that_moved_and_gates_it(self):
		# Bug 6ac66714: parking here sent the fix back for a refix that merged master, and master
		# had moved again by the time that refix shipped, three times in a row.
		_, head = self.make_fix()
		fix_sub = self.wt.gitlinks(head)["data/client"]
		moved = self.move_submodule_master("other.txt", "x\n")
		gated = []
		result = self.wt.ship("bugfix/x", head, "Merge bugfix/x (bug-loop, gate green at 1)", lambda: gated.append(True) or True)
		self.assertTrue(result.ok, result.reason)
		self.assertEqual(gated, [True])
		shipped = git(self.sub_origin, "rev-parse", "master")
		self.assertEqual(self.wt.gitlinks(result.commit)["data/client"], shipped)
		for commit in (fix_sub, moved):
			self.assertEqual(gitops.run_git(self.sub_origin, "merge-base", "--is-ancestor", commit, shipped, check=False).returncode, 0)
		self.assertEqual(git(self.origin, "rev-parse", "develop"), result.commit)
		self.assertEqual(self.wt.merged("bugfix/x"), result.commit)
		self.assertEqual(len(git(self.origin, "rev-list", "--parents", "-n", "1", result.commit).split()), 3)

	def test_ship_does_not_push_a_submodule_catch_up_the_gate_rejects(self):
		_, head = self.make_fix()
		moved = self.move_submodule_master("other.txt", "x\n")
		before = git(self.origin, "rev-parse", "develop")
		result = self.wt.ship("bugfix/x", head, "Merge bugfix/x", lambda: False)
		self.assertFalse(result.ok)
		self.assertIn("fast gate", result.reason)
		self.assertEqual(git(self.origin, "rev-parse", "develop"), before)
		self.assertEqual(git(self.sub_origin, "rev-parse", "master"), moved)

	def test_ship_refuses_when_submodule_master_conflicts_with_the_fix(self):
		_, head = self.make_fix()
		moved = self.move_submodule_master("data.txt", "three\n")
		before = git(self.origin, "rev-parse", "develop")
		result = self.wt.ship("bugfix/x", head, "Merge bugfix/x", lambda: self.fail("nothing to gate"))
		self.assertFalse(result.ok)
		self.assertIn("manual merge", result.reason)
		self.assertEqual(git(self.origin, "rev-parse", "develop"), before)
		self.assertEqual(git(self.sub_origin, "rev-parse", "master"), moved)
		detached = gitops.run_git(self.wt.path, "symbolic-ref", "-q", "HEAD", check=False)
		self.assertNotEqual(detached.returncode, 0)
		self.assertTrue(self.wt.is_clean())

	def test_changes_reports_non_ascii_paths_unquoted(self):
		base = self.wt.prepare()
		self.wt.start_branch("bugfix/x", base)
		write(os.path.join(self.wt.path, "src", "é.cpp"), "int e;\n")
		git(self.wt.path, "add", "-A")
		git(self.wt.path, "commit", "-m", "unicode")
		paths = [change.path for change in self.wt.changes(base, self.wt.head())]
		self.assertEqual(paths, ["src/é.cpp"])

	def test_changes_reports_removed_submodule(self):
		base = self.wt.prepare()
		self.wt.start_branch("bugfix/x", base)
		git(self.wt.path, "rm", "-f", "data/client")
		git(self.wt.path, "commit", "-m", "drop submodule")
		result = self.wt.changes(base, self.wt.head())
		self.assertIn(gitops.FileChange("data/client", 0, 0, True), result)

	def test_merged_is_subject_anchored(self):
		self.make_fix()
		write(os.path.join(self.main, "README.md"), "x\n")
		git(self.main, "add", "-A")
		git(self.main, "commit", "-m", "note\n\nMerge bugfix/x (not really)")
		self.assertIsNone(self.wt.merged("bugfix/x"))

	def test_ship_gates_the_merge_when_develop_moved(self):
		_, head = self.make_fix()
		write(os.path.join(self.main, "README.md"), "moved\n")
		git(self.main, "add", "-A")
		git(self.main, "commit", "-m", "someone else")
		git(self.main, "push", "origin", "develop")
		red = self.wt.ship("bugfix/x", head, "Merge bugfix/x", lambda: False)
		self.assertFalse(red.ok)
		self.assertIn("moved", red.reason)
		green = self.wt.ship("bugfix/x", head, "Merge bugfix/x", lambda: True)
		self.assertTrue(green.ok, green.reason)

	def test_ship_refuses_a_branch_that_moved_after_gating(self):
		_, head = self.make_fix()
		write(os.path.join(self.wt.path, "src", "a.cpp"), "int a = 3;\n")
		git(self.wt.path, "commit", "-am", "sneaked in after the gate")
		before = git(self.origin, "rev-parse", "develop")
		result = self.wt.ship("bugfix/x", head, "Merge bugfix/x", lambda: True)
		self.assertFalse(result.ok)
		self.assertIn("branch moved after gating", result.reason)
		self.assertEqual(git(self.origin, "rev-parse", "develop"), before)

	def test_ship_merges_the_gated_commit(self):
		_, head = self.make_fix()
		result = self.wt.ship("bugfix/x", head, "Merge bugfix/x", lambda: True)
		self.assertTrue(result.ok, result.reason)
		self.assertEqual(git(self.origin, "rev-parse", "develop^2"), head)

	def test_prepare_clears_a_leftover_merge(self):
		base = self.wt.prepare()
		self.wt.start_branch("bugfix/x", base)
		write(os.path.join(self.wt.path, "src", "a.cpp"), "int a = 2;\n")
		git(self.wt.path, "commit", "-am", "ours")
		git(self.main, "checkout", "-b", "other", base)
		write(os.path.join(self.main, "src", "a.cpp"), "int a = 9;\n")
		git(self.main, "commit", "-am", "theirs")
		conflict = gitops.run_git(self.wt.path, "merge", "other", check=False)
		self.assertNotEqual(conflict.returncode, 0)
		self.assertEqual(self.wt.prepare(), base)
		self.assertTrue(self.wt.is_clean())

	def test_merged_is_none_before_merge(self):
		self.make_fix()
		self.assertIsNone(self.wt.merged("bugfix/x"))

	def test_resume_branch_continues_on_the_fix_tip(self):
		base, head = self.make_fix()
		self.wt.prepare()
		self.assertEqual(self.wt.resume_branch("bugfix/x"), head)
		self.assertEqual(git(self.wt.path, "symbolic-ref", "HEAD"), "refs/heads/bugfix/x")
		self.assertEqual(git(self.wt.sub_path("data/client"), "symbolic-ref", "HEAD"), "refs/heads/bugfix/x")
		self.assertEqual(self.wt.fork_point("bugfix/x"), base)
		self.assertTrue(self.wt.is_clean())

	def test_resume_missing_branch_raises(self):
		self.wt.prepare()
		with self.assertRaises(gitops.GitError):
			self.wt.resume_branch("bugfix/missing")

	def test_branch_pushes_are_limited_to_bugfix_branches(self):
		worktree = gitops.Worktree("main", "wt")
		self.assertEqual(worktree.push_branch("develop", "abc")[0], False)
		self.assertEqual(worktree.push_branch("bugfix/../develop", "abc")[0], False)
		with self.assertRaises(ValueError):
			worktree.delete_remote_branch("master")

	def test_push_and_log_refuse_odd_commit_ids_without_running_git(self):
		worktree = gitops.Worktree("main", "wt")
		with mock.patch.object(gitops, "run_git") as run:
			for head in ("--receive-pack=x", "", "HEAD", "abc", "A" * 40):
				self.assertFalse(worktree.push_branch("bugfix/aaaaaaaa", head)[0], head)
			self.assertEqual(worktree.log_lines("", "--output=x"), [])
			self.assertEqual(worktree.log_lines("-n1", "abcdef0"), [])
			self.assertEqual(worktree.log_lines("abcdef0", ""), [])
			self.assertEqual(worktree.log_lines("abcdef0", "HEAD"), [])
			self.assertFalse(worktree.is_ancestor("--all", "abcdef0"))
			self.assertFalse(worktree.is_ancestor("abcdef0", "HEAD"))
			self.assertFalse(worktree.is_ancestor("", "abcdef0"))
			run.assert_not_called()

	def test_is_ancestor(self):
		base, head = self.make_fix()
		self.assertTrue(self.wt.is_ancestor(base, head))
		self.assertTrue(self.wt.is_ancestor(head, head))
		self.assertFalse(self.wt.is_ancestor(head, base))
		self.assertFalse(self.wt.is_ancestor("0" * 40, head))  # unknown even after a fetch

	def test_emergency_branch_push_log_and_delete(self):
		base, head = self.make_fix()
		ok, reason = self.wt.push_branch("bugfix/aaaaaaaa", head)
		self.assertEqual((ok, reason), (True, ""))
		self.assertEqual(git(self.origin, "rev-parse", "refs/heads/bugfix/aaaaaaaa"), head)
		self.assertEqual(len(self.wt.log_lines(base, head)), 1)
		self.assertEqual(len(self.wt.log_lines(base[:8], head, limit=1)), 1)
		self.assertEqual(len(self.wt.log_lines("", head)), 2)
		self.wt.delete_remote_branch("bugfix/aaaaaaaa")
		self.assertEqual(git(self.origin, "branch", "--list", "bugfix/aaaaaaaa"), "")

	def test_backup_lets_another_machine_resume_a_parked_branch(self):
		# 2026-10-13: the loop moved to a new machine; its parked branches were only local on the
		# old one, so two maintainer refix decisions failed.
		branch = "bugfix/0123abcd"
		base = self.wt.prepare()
		self.wt.start_branch(branch, base)
		sub = self.wt.sub_path("data/client")
		write(os.path.join(sub, "data.txt"), "two\n")
		git(sub, "commit", "-am", "fix data")
		git(self.wt.path, "add", "-A")
		git(self.wt.path, "commit", "-m", "fix")
		head = self.wt.head()
		sub_head = git(sub, "rev-parse", "HEAD")
		self.assertEqual(self.wt.backup_branch(branch), (True, ""))
		self.assertEqual(git(self.origin, "rev-parse", "refs/bugloop/" + branch), head)
		self.assertEqual(git(self.sub_origin, "rev-parse", "refs/bugloop/" + branch), sub_head)
		self.assertEqual(git(self.origin, "branch", "--list"), "* develop")  # no branch, so no CI run

		other_main = os.path.join(self.root, "other-main")
		git(self.root, "clone", "--recurse-submodules", self.origin, other_main)
		other = gitops.Worktree(other_main, os.path.join(self.root, "other-wt"), submodules=("data/client",))
		other.prepare()
		self.assertFalse(other.has_branch(branch))
		self.assertTrue(other.restore_branch(branch))
		self.assertFalse(other.restore_branch(branch))  # already here
		self.assertEqual(other.resume_branch(branch), head)
		self.assertEqual(git(other.sub_path("data/client"), "rev-parse", "HEAD"), sub_head)

		other.delete_backup(branch)
		self.assertEqual(gitops.run_git(self.origin, "rev-parse", "-q", "--verify", "refs/bugloop/" + branch, check=False).returncode, 1)
		self.assertEqual(gitops.run_git(self.sub_origin, "rev-parse", "-q", "--verify", "refs/bugloop/" + branch, check=False).returncode, 1)

	def test_restore_without_backup_is_false(self):
		self.wt.prepare()
		self.assertFalse(self.wt.restore_branch("bugfix/0123abcd"))
		self.assertFalse(self.wt.has_branch("bugfix/0123abcd"))

	def test_backups_are_limited_to_bugfix_branches(self):
		worktree = gitops.Worktree("main", "wt")
		with mock.patch.object(gitops, "run_git") as run:
			self.assertFalse(worktree.backup_branch("develop")[0])
			self.assertFalse(worktree.restore_branch("refs/heads/develop"))
			with self.assertRaises(ValueError):
				worktree.delete_backup("master")
			run.assert_not_called()

	# ---- merge conflicts with develop

	def move_develop_file(self, path, text):
		write(os.path.join(self.main, *path.split("/")), text)
		git(self.main, "add", "-A")
		git(self.main, "commit", "-m", "elsewhere: " + path)
		git(self.main, "push", "origin", "develop")

	def resolver(self, edit=None, review=None):
		runner = FakeRunner(edit, review)
		self.wt.resolver = merge_resolve.ConflictResolver(gitops.run_git, runner, {"merge": "m", "merge_review": "r"}, {"merge_review": {}})
		return runner

	def test_ship_resolves_a_text_conflict_and_gates_the_result(self):
		_, head = self.make_fix()
		self.move_develop_file("src/a.cpp", "int a = 1;\nint b = 1;\n")
		runner = self.resolver(edit=lambda root: write(os.path.join(root, "src", "a.cpp"), "int a = 2;\nint b = 1;\n"))
		gated = []
		result = self.wt.ship("bugfix/x", head, "Merge bugfix/x (bug-loop, gate green at 1)\n\nCo-Authored-By: t", lambda: gated.append(True) or True)
		self.assertTrue(result.ok, result.reason)
		self.assertEqual(gated, [True])
		self.assertEqual(len(runner.agent_inputs), 1)
		self.assertIn("src/a.cpp", runner.agent_inputs[0])
		self.assertIn("--- RESOLUTION\nint a = 2;\nint b = 1;\n", runner.review_inputs[0])
		self.assertEqual(self.wt.resolutions[0]["text"][0]["review"], "conflicts")
		self.assertEqual(git(self.origin, "show", "develop:src/a.cpp"), "int a = 2;\nint b = 1;")
		message = git(self.origin, "log", "-1", "--format=%B", "develop")
		self.assertIn("Conflicts with origin/develop resolved by the bug loop in: src/a.cpp", message)
		self.assertTrue(message.rstrip().endswith("Co-Authored-By: t"))
		self.assertEqual([item["path"] for item in self.wt.resolutions[0]["text"]], ["src/a.cpp"])

	def assert_refused(self, result, before, text):
		self.assertFalse(result.ok)
		self.assertTrue(result.conflict)
		self.assertIn(text, result.reason)
		self.assertEqual(git(self.origin, "rev-parse", "develop"), before)
		self.assertNotEqual(gitops.run_git(self.wt.path, "rev-parse", "-q", "--verify", "MERGE_HEAD", check=False).returncode, 0)
		self.assertTrue(self.wt.is_clean())

	def test_ship_refuses_a_resolution_the_review_rejects(self):
		_, head = self.make_fix()
		self.move_develop_file("src/a.cpp", "int a = 1;\nint b = 1;\n")
		self.resolver(edit=lambda root: write(os.path.join(root, "src", "a.cpp"), "int a = 2;\n"),
			review={"keeps_ours": True, "keeps_theirs": False, "adds_unrelated": False, "problems": ["drops int b"], "summary": ""})
		before = git(self.origin, "rev-parse", "develop")
		result = self.wt.ship("bugfix/x", head, "Merge bugfix/x", lambda: self.fail("nothing to gate"))
		self.assert_refused(result, before, "drops int b")

	def test_ship_refuses_a_resolver_that_touches_other_files(self):
		_, head = self.make_fix()
		self.move_develop_file("src/a.cpp", "int a = 1;\nint b = 1;\n")

		def edit(root):
			write(os.path.join(root, "src", "a.cpp"), "int a = 2;\nint b = 1;\n")
			write(os.path.join(root, "src", "extra.cpp"), "int sneaky = 1;\n")
		runner = self.resolver(edit=edit)
		before = git(self.origin, "rev-parse", "develop")
		result = self.wt.ship("bugfix/x", head, "Merge bugfix/x", lambda: self.fail("nothing to gate"))
		self.assert_refused(result, before, "outside the conflict")
		self.assertEqual(runner.review_inputs, [])

	def test_ship_refuses_conflict_markers_left_behind(self):
		_, head = self.make_fix()
		self.move_develop_file("src/a.cpp", "int a = 1;\nint b = 1;\n")
		self.resolver(edit=lambda root: None)
		before = git(self.origin, "rev-parse", "develop")
		result = self.wt.ship("bugfix/x", head, "Merge bugfix/x", lambda: self.fail("nothing to gate"))
		self.assert_refused(result, before, "conflict markers")

	def test_ship_without_a_resolver_reports_the_conflict(self):
		_, head = self.make_fix()
		self.move_develop_file("src/a.cpp", "int a = 1;\nint b = 1;\n")
		before = git(self.origin, "rev-parse", "develop")
		result = self.wt.ship("bugfix/x", head, "Merge bugfix/x", lambda: self.fail("nothing to gate"))
		self.assert_refused(result, before, "manual merge")

	def test_ship_merges_game_data_both_sides_changed_without_a_model(self):
		# 2026-10-22: four parked fixes could not ship because develop had changed the same .data
		# file (binary to git) in other entries.
		def entry(entry_id, value):
			payload = bytes([0x08, entry_id, 0x10, value])
			return bytes([0x0A, len(payload)]) + payload

		with open(os.path.join(self.sub_seed, "items.data"), "wb") as handle:
			handle.write(entry(1, 10) + entry(2, 20))
		git(self.sub_seed, "add", "-A")
		git(self.sub_seed, "commit", "-m", "items")
		git(self.sub_seed, "push", self.sub_origin, "master")
		sub = os.path.join(self.main, "data", "client")
		git(sub, "fetch", "origin")
		git(sub, "checkout", "--detach", "origin/master")
		git(self.main, "add", "data/client")
		git(self.main, "commit", "-m", "bump data/client")
		git(self.main, "push", "origin", "develop")

		base = self.wt.prepare()
		self.wt.start_branch("bugfix/x", base)
		wt_sub = self.wt.sub_path("data/client")
		with open(os.path.join(wt_sub, "items.data"), "wb") as handle:
			handle.write(entry(1, 11) + entry(2, 20))
		git(wt_sub, "commit", "-am", "fix item 1")
		git(self.wt.path, "add", "-A")
		git(self.wt.path, "commit", "-m", "bump data")
		head = self.wt.head()

		with open(os.path.join(self.sub_seed, "items.data"), "wb") as handle:
			handle.write(entry(1, 10) + entry(2, 22))
		git(self.sub_seed, "commit", "-am", "elsewhere: item 2")
		git(self.sub_seed, "push", self.sub_origin, "master")
		git(sub, "fetch", "origin")
		git(sub, "checkout", "--detach", "origin/master")
		git(self.main, "add", "data/client")
		git(self.main, "commit", "-m", "bump data/client again")
		git(self.main, "push", "origin", "develop")

		self.wt.resolver = merge_resolve.ConflictResolver(gitops.run_git)
		result = self.wt.ship("bugfix/x", head, "Merge bugfix/x", lambda: True)
		self.assertTrue(result.ok, result.reason)
		shipped = gitops.run_git(self.sub_origin, "show", "master:items.data", binary=True).stdout
		self.assertEqual(shipped, entry(1, 11) + entry(2, 22))
		self.assertEqual(self.wt.resolutions[0]["data"], ["data/client/items.data"])

	def test_resume_branch_resolves_a_text_conflict(self):
		self.make_fix()
		self.move_develop_file("src/a.cpp", "int a = 1;\nint b = 1;\n")
		self.resolver(edit=lambda root: write(os.path.join(root, "src", "a.cpp"), "int a = 2;\nint b = 1;\n"))
		self.wt.prepare()
		head = self.wt.resume_branch("bugfix/x")
		self.assertEqual(git(self.wt.path, "show", head + ":src/a.cpp"), "int a = 2;\nint b = 1;")
		self.assertEqual(self.wt.fork_point("bugfix/x"), git(self.origin, "rev-parse", "develop"))
		self.assertTrue(self.wt.is_clean())

	def move_develop_submodule(self, name, text):
		"""Someone else lands a data change: submodule master and develop's pointer move together."""
		write(os.path.join(self.sub_seed, name), text)
		git(self.sub_seed, "add", "-A")
		git(self.sub_seed, "commit", "-m", "elsewhere: " + name)
		git(self.sub_seed, "push", self.sub_origin, "master")
		sub = os.path.join(self.main, "data", "client")
		git(sub, "fetch", "origin")
		git(sub, "checkout", "--detach", "origin/master")
		git(self.main, "add", "data/client")
		git(self.main, "commit", "-m", "bump data/client")
		git(self.main, "push", "origin", "develop")
		return git(sub, "rev-parse", "HEAD")

	def test_resume_branch_merges_develop_including_submodule_pointers(self):
		_, old_head = self.make_fix()
		moved = self.move_develop_submodule("stump.txt", "collision\n")
		self.wt.prepare()
		head = self.wt.resume_branch("bugfix/x")
		self.assertNotEqual(head, old_head)
		self.assertEqual(self.wt.fork_point("bugfix/x"), git(self.origin, "rev-parse", "develop"))
		# The refix is judged against today's develop: the other data change is not part of it.
		paths = sorted(change.path for change in self.wt.changes(self.wt.fork_point("bugfix/x"), head))
		self.assertEqual(paths, ["data/client/data.txt", "src/a.cpp"])
		sub = self.wt.sub_path("data/client")
		self.assertEqual(gitops.run_git(sub, "merge-base", "--is-ancestor", moved, "HEAD", check=False).returncode, 0)
		self.assertEqual(git(sub, "symbolic-ref", "HEAD"), "refs/heads/bugfix/x")
		self.assertTrue(self.wt.is_clean())

	def test_resume_branch_raises_on_a_submodule_conflict_and_leaves_no_merge(self):
		self.make_fix()
		self.move_develop_submodule("data.txt", "three\n")
		self.wt.prepare()
		with self.assertRaises(gitops.GitError) as raised:
			self.wt.resume_branch("bugfix/x")
		self.assertIn("data/client", str(raised.exception))
		self.assertNotEqual(gitops.run_git(self.wt.path, "rev-parse", "-q", "--verify", "MERGE_HEAD", check=False).returncode, 0)
		self.assertTrue(self.wt.is_clean())

	def test_ship_merges_a_submodule_pointer_develop_moved(self):
		_, head = self.make_fix()
		fix_sub = self.wt.gitlinks(head)["data/client"]
		moved = self.move_develop_submodule("stump.txt", "collision\n")
		result = self.wt.ship("bugfix/x", head, "Merge bugfix/x", lambda: True)
		self.assertTrue(result.ok, result.reason)
		shipped = git(self.sub_origin, "rev-parse", "master")
		self.assertEqual(self.wt.gitlinks(result.commit)["data/client"], shipped)
		for commit in (fix_sub, moved):
			self.assertEqual(gitops.run_git(self.sub_origin, "merge-base", "--is-ancestor", commit, shipped, check=False).returncode, 0)

	def test_ship_gates_a_merge_that_had_to_merge_a_submodule(self):
		_, head = self.make_fix()
		self.move_develop_submodule("stump.txt", "collision\n")
		before = git(self.origin, "rev-parse", "develop")
		result = self.wt.ship("bugfix/x", head, "Merge bugfix/x", lambda: False)
		self.assertFalse(result.ok)
		self.assertIn("fast gate", result.reason)
		self.assertEqual(git(self.origin, "rev-parse", "develop"), before)


@unittest.skipUnless(sys.platform == "win32", "named mutexes are Windows-only")
class WinlockTests(unittest.TestCase):
	def test_single_instance_is_exclusive(self):
		name = "Local\\MMOBugLoopTest{}".format(os.getpid())
		held = threading.Event()
		release = threading.Event()

		def holder():
			handle = winlock.acquire_single_instance(name)
			self.assertIsNotNone(handle)
			held.set()
			release.wait(5)

		thread = threading.Thread(target=holder)
		thread.start()
		held.wait(5)
		self.assertIsNone(winlock.acquire_single_instance(name))
		release.set()
		thread.join()

	def test_named_mutex_is_reentrant_in_sequence(self):
		name = "Local\\MMOBugLoopGateTest{}".format(os.getpid())
		with winlock.named_mutex(name):
			pass
		with winlock.named_mutex(name):
			pass


if __name__ == "__main__":
	unittest.main()
