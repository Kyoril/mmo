# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""The patch root served by nginx: releases/<sha>/ and a `current` symlink to one of them."""

import os
import shutil
from pathlib import Path


class PatchDir:
	def __init__(self, root):
		self.root = Path(root)
		self.releases = self.root / "releases"

	def release_path(self, sha):
		return self.releases / sha

	def tmp_path(self, sha):
		return self.releases / (sha + ".tmp")

	def has_release(self, sha):
		return bool(sha) and self.release_path(sha).is_dir()

	def current_sha(self):
		link = self.root / "current"
		if not link.is_symlink():
			return None
		return Path(os.readlink(link)).name

	def flip_current(self, sha):
		"""Atomically points `current` at releases/<sha>."""
		if not self.has_release(sha):
			raise FileNotFoundError(str(self.release_path(sha)))
		staging = self.root / "current.new"
		if os.path.lexists(staging):
			staging.unlink()
		# Relative, so the link resolves the same on the host (nginx) and in the container.
		os.symlink(os.path.join("releases", sha), staging)
		os.replace(staging, self.root / "current")

	def prune(self, keep, protect):
		"""Deletes all but the `keep` newest releases, never one in `protect`. Returns removed shas."""
		if not self.releases.is_dir():
			return []
		releases = [p for p in self.releases.iterdir() if p.is_dir() and not p.name.endswith(".tmp")]
		releases.sort(key=lambda p: p.stat().st_mtime, reverse=True)
		removed = []
		for path in releases[keep:]:
			if path.name in protect:
				continue
			shutil.rmtree(path)
			removed.append(path.name)
		return removed
