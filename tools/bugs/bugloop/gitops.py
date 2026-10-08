# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Git operations of the bug loop in its own worktree (by default ../mmo-bugloop next to the main
checkout). The fixer works in the worktree but never ships; only the orchestrator calls ship()."""

import collections
import os
import re
import subprocess

from .guard import FileChange

_EMERGENCY_BRANCH = re.compile(r"bugfix/[0-9a-f]{8}")
SUBMODULES = ("data/client", "data/editor")
ShipResult = collections.namedtuple("ShipResult", "ok commit reason")


class GitError(RuntimeError):
	pass


def run_git(cwd, *args, check=True, binary=False):
	# Submodule URLs in worktrees can be local paths; file transport must be allowed.
	command = ["git", "-c", "protocol.file.allow=always", "-C", cwd] + list(args)
	if binary:
		completed = subprocess.run(command, capture_output=True)
	else:
		completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
	if check and completed.returncode != 0:
		stderr = completed.stderr.decode("utf-8", "replace") if binary else completed.stderr
		raise GitError("git {} failed ({}): {}".format(" ".join(args), completed.returncode, stderr.strip()[-2000:]))
	return completed


def _count(text):
	return 0 if text == "-" else int(text)


def _numstat(output, prefix=""):
	changes = []
	for record in output.split("\0"):
		if not record:
			continue
		added, removed, path = record.split("\t", 2)
		changes.append(FileChange(prefix + path, _count(added), _count(removed), added == "-"))
	return changes


class Worktree:
	def __init__(self, main_repo, path, submodules=SUBMODULES, remote="origin", sub_remote="upstream", develop="develop", master="master"):
		self.main_repo = main_repo
		self.path = path
		self.submodules = tuple(submodules)
		self.remote = remote
		self.sub_remote = sub_remote
		self.develop = develop
		self.master = master

	@property
	def target(self):
		return "{}/{}".format(self.remote, self.develop)

	def git(self, *args, cwd=None, check=True):
		return run_git(cwd or self.path, *args, check=check).stdout.strip()

	def sub_path(self, sub):
		return os.path.join(self.path, *sub.split("/"))

	def _sync_submodules(self):
		self.git("submodule", "update", "--init", "--force")
		for sub in self.submodules:
			self.git("clean", "-fd", cwd=self.sub_path(sub))

	def _ensure_sub_remote(self, sub):
		url = run_git(self.main_repo, "config", "--get", "submodule.{}.url".format(sub)).stdout.strip()
		sub_dir = self.sub_path(sub)
		if run_git(sub_dir, "remote", "get-url", self.sub_remote, check=False).returncode == 0:
			self.git("remote", "set-url", self.sub_remote, url, cwd=sub_dir)
		else:
			self.git("remote", "add", self.sub_remote, url, cwd=sub_dir)
		self.git("fetch", self.sub_remote, cwd=sub_dir)

	def prepare(self):
		"""A fresh checkout of origin/develop; returns its commit."""
		run_git(self.main_repo, "fetch", self.remote, "--prune")
		run_git(self.main_repo, "worktree", "prune")
		if not os.path.exists(os.path.join(self.path, ".git")):
			run_git(self.main_repo, "worktree", "add", "--detach", self.path, self.target)
		# A killed run can leave a merge, rebase or cherry-pick in progress; checkout would refuse.
		for operation in ("merge", "rebase", "cherry-pick"):
			run_git(self.path, operation, "--abort", check=False)
		self.checkout(self.target)
		self.git("clean", "-fd")
		for sub in self.submodules:
			self._ensure_sub_remote(sub)
		return self.head()

	def checkout(self, ref):
		self.git("checkout", "--detach", "--force", ref)
		self._sync_submodules()

	def start_branch(self, branch, base):
		self.git("checkout", "--force", "-B", branch, base)
		self._sync_submodules()
		for sub in self.submodules:
			self.git("checkout", "-B", branch, cwd=self.sub_path(sub))

	def resume_branch(self, branch):
		"""Checks out an existing fix branch at its tip for another round; returns its head."""
		self.git("checkout", "--force", branch)
		self._sync_submodules()
		for sub in self.submodules:
			self.git("checkout", "-B", branch, cwd=self.sub_path(sub))
		return self.head()

	def fork_point(self, branch):
		"""Where the fix branch left origin/develop; the base for diffs, guard and proof."""
		return self.git("merge-base", self.target, branch)

	def head(self, ref="HEAD"):
		return self.git("rev-parse", ref)

	def is_clean(self):
		if self.git("status", "--porcelain"):
			return False
		return all(not self.git("status", "--porcelain", cwd=self.sub_path(sub)) for sub in self.submodules)

	def gitlinks(self, rev):
		links = {}
		for line in self.git("ls-tree", rev, "--", *self.submodules).splitlines():
			meta, path = line.split("\t", 1)
			links[path] = meta.split()[2]
		return links

	def _changed_links(self, base, head):
		old, new = self.gitlinks(base), self.gitlinks(head)
		return [(sub, old.get(sub), new[sub]) for sub in self.submodules if sub in new and old.get(sub) != new[sub]]

	def changes(self, base, head):
		output = self.git("diff", "--numstat", "-z", "--no-renames", base, head)
		result = [change for change in _numstat(output) if change.path not in self.submodules]
		for sub, old, new in self._changed_links(base, head):
			if old is None:
				result.append(FileChange(sub, 0, 0, True))
				continue
			result += _numstat(self.git("diff", "--numstat", "-z", "--no-renames", old, new, cwd=self.sub_path(sub)), sub + "/")
		new_links = self.gitlinks(head)
		for sub in self.gitlinks(base):
			if sub not in new_links:
				result.append(FileChange(sub, 0, 0, True))
		return result

	def unified_diff(self, base, head):
		parts = [self.git("-c", "core.quotePath=false", "diff", "-U0", "--no-color", "--no-renames", "--ignore-submodules=all",
			"--src-prefix=a/", "--dst-prefix=b/", base, head)]
		for sub, old, new in self._changed_links(base, head):
			if old is not None:
				parts.append(self.git("-c", "core.quotePath=false", "diff", "-U0", "--no-color", "--no-renames",
					"--src-prefix=a/{}/".format(sub), "--dst-prefix=b/{}/".format(sub), old, new, cwd=self.sub_path(sub)))
		return "\n".join(part for part in parts if part)

	def file_bytes(self, rev, path):
		repo, relative = self.path, path
		for sub in self.submodules:
			if path.startswith(sub + "/"):
				link = self.gitlinks(rev).get(sub)
				if link is None:
					return None
				repo, relative, rev = self.sub_path(sub), path[len(sub) + 1:], link
				break
		completed = run_git(repo, "show", "{}:{}".format(rev, relative), check=False, binary=True)
		return completed.stdout if completed.returncode == 0 else None

	def ship(self, branch, head, message, fast_gate):
		"""Merges `head`, the commit that was guarded, proved and gated, onto the current
		origin/develop and pushes: changed submodules first (fast-forward of their master only),
		then develop. Refuses when `branch` no longer points at `head`. fast_gate() runs only when
		develop moved since the branch was cut, because then the merge result was never gated."""
		current = run_git(self.path, "rev-parse", "--verify", "-q", branch + "^{commit}", check=False).stdout.strip()
		if current != head:
			return ShipResult(False, "", "branch moved after gating ({} is at {}, gated {})".format(branch, current[:8] or "nothing", head[:8]))
		run_git(self.main_repo, "fetch", self.remote)
		tip = self.head(self.target)
		fork = self.git("merge-base", tip, head)
		moved = fork != tip
		pushes = []
		for sub, _, sha in self._changed_links(fork, head):
			sub_dir = self.sub_path(sub)
			self.git("fetch", self.sub_remote, cwd=sub_dir)
			master = "{}/{}".format(self.sub_remote, self.master)
			if run_git(sub_dir, "merge-base", "--is-ancestor", master, sha, check=False).returncode != 0:
				self.git("checkout", "--detach", "--force", tip)
				return ShipResult(False, "", "{}: {} is not an ancestor of {}; a submodule merge is needed".format(sub, master, sha[:8]))
			pushes.append((sub_dir, sha))
		self.git("checkout", "--detach", "--force", tip)
		merge = run_git(self.path, "merge", "--no-ff", "-m", message, head, check=False)
		if merge.returncode != 0:
			run_git(self.path, "merge", "--abort", check=False)
			return ShipResult(False, "", "merge conflict with " + self.target)
		self._sync_submodules()
		if moved and not fast_gate():
			return ShipResult(False, "", self.target + " moved and the fast gate on the merge is red")
		for sub_dir, sha in pushes:
			push = run_git(sub_dir, "push", self.sub_remote, "{}:refs/heads/{}".format(sha, self.master), check=False)
			if push.returncode != 0:
				return ShipResult(False, "", "push to {} failed: {}".format(sub_dir, push.stderr.strip()[-300:]))
		commit = self.head()
		push = run_git(self.path, "push", self.remote, "HEAD:refs/heads/" + self.develop, check=False)
		if push.returncode != 0:
			return ShipResult(False, "", "push of develop failed: " + push.stderr.strip()[-300:])
		run_git(self.main_repo, "fetch", self.remote)
		return ShipResult(True, commit, "")

	def merged(self, branch):
		"""The merge commit of `branch` on develop or origin/develop, found by its message
		(both /ship and the loop write `Merge <branch> (...)`), or None."""
		for ref in (self.develop, self.target):
			# git's --grep anchors ^ at every message line; the subject is checked here instead.
			found = run_git(self.main_repo, "log", "--first-parent", "--format=%H%x09%s",
				"--grep=Merge {} ".format(re.escape(branch)), "-E", ref, check=False)
			if found.returncode != 0:
				continue
			for line in found.stdout.splitlines():
				commit, _, subject = line.partition("\t")
				if subject.startswith("Merge {} ".format(branch)):
					return commit
		return None

	def delete_branch(self, branch):
		run_git(self.main_repo, "branch", "-D", branch, check=False)

	def push_branch(self, branch, head):
		"""Pushes an emergency branch for CI verification; nothing but bugfix/<id8> ever goes out here."""
		if not _EMERGENCY_BRANCH.fullmatch(branch):
			return False, "refusing to push " + branch
		push = run_git(self.path, "push", "--force", self.remote, "{}:refs/heads/{}".format(head, branch), check=False)
		if push.returncode != 0:
			return False, "push of {} failed: {}".format(branch, push.stderr.strip()[-300:])
		return True, ""

	def delete_remote_branch(self, branch):
		if not _EMERGENCY_BRANCH.fullmatch(branch):
			raise ValueError("refusing to delete " + branch)
		run_git(self.path, "push", self.remote, "--delete", branch, check=False)

	def log_lines(self, base, head, limit=30):
		rng = "{}..{}".format(base, head) if base else head
		out = run_git(self.main_repo, "log", "--first-parent", "--format=%h %s", "-n", str(limit), rng, check=False)
		return [line for line in out.stdout.splitlines() if line.strip()]
