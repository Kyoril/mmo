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

from bugloop import gitops, winlock  # noqa: E402


def git(cwd, *args):
	return gitops.run_git(cwd, *args).stdout.strip()


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

	def test_unpublished_gitlinks_names_data_develop_was_pushed_before(self):
		self.wt.prepare()
		self.assertEqual(self.wt.unpublished_gitlinks(), [])
		# Someone commits data, pushes develop with the new pointer, but not the data.
		sub = os.path.join(self.main, "data", "client")
		write(os.path.join(sub, "data.txt"), "local only\n")
		git(sub, "commit", "-am", "local data")
		sha = git(sub, "rev-parse", "HEAD")
		git(self.main, "commit", "-am", "bump data")
		git(self.main, "push", "origin", "develop")
		self.assertEqual(self.wt.unpublished_gitlinks(), [("data/client", sha)])
		with self.assertRaises(gitops.GitError):
			self.wt.prepare()  # what the check spares the loop
		git(sub, "push", self.sub_origin, "HEAD:master")
		self.assertEqual(self.wt.unpublished_gitlinks(), [])
		self.wt.prepare()
		self.assertEqual(self.wt.gitlinks("HEAD")["data/client"], sha)

	def test_unpublished_gitlinks_skips_a_worktree_not_set_up_yet(self):
		self.assertEqual(self.wt.unpublished_gitlinks(), [])

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
