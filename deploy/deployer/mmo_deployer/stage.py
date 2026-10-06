# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Turns a nightly GitHub release into releases/<sha>/ on the patch root."""

import hashlib
import logging
import os
import shutil
import subprocess
import zipfile
from pathlib import Path

from .web import HttpError

log = logging.getLogger("deployer")


class StageError(Exception):
	pass


def select_release(releases, state):
	"""The newest release, unless it is live, staged or bad.

	Only the newest is considered: falling back to an older one could go backwards.
	"""
	if not releases:
		return None
	newest = releases[0]
	commit = newest["target_commitish"]
	if commit in (state.live, state.staged) or commit in state.bad:
		return None
	return newest


def sha256_file(path):
	digest = hashlib.sha256()
	with open(path, "rb") as handle:
		for chunk in iter(lambda: handle.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


class Stager:
	def __init__(self, cfg, github, patchdir, compiler_command=None, run=subprocess.run):
		self.cfg = cfg
		self.github = github
		self.patchdir = patchdir
		self.compiler_command = compiler_command or [cfg.update_compiler]
		self.run = run
		# The workspace mirrors the repo layout, so source.txt's ../../data/client resolves.
		self.workspace = Path(cfg.state_dir) / "workspace"
		self.data_checkout = self.workspace / "data" / "client"
		self.patch_source = self.workspace / "deploy" / "patch"
		self.downloads = Path(cfg.state_dir) / "downloads"

	def stage(self, release):
		"""Builds releases/<sha>/ for `release` and returns its release.json. Raises StageError."""
		commit = release["target_commitish"]
		tmp = self.patchdir.tmp_path(commit)
		try:
			return self._stage(release, commit, tmp)
		except StageError:
			raise
		except (OSError, KeyError, ValueError, HttpError, zipfile.BadZipFile) as error:
			raise StageError("{}: {}".format(type(error).__name__, error)) from error
		finally:
			if tmp.exists():
				shutil.rmtree(tmp, ignore_errors=True)

	def _stage(self, release, commit, tmp):
		self.downloads.mkdir(parents=True, exist_ok=True)
		manifest = self.github.fetch_manifest(release, str(self.downloads))
		if manifest.get("schema") != 1:
			raise StageError("unsupported release.json schema {}".format(manifest.get("schema")))
		if manifest["commit"] != commit:
			raise StageError("release.json names {} but the release targets {}".format(manifest["commit"], commit))
		gate = manifest.get("gate")
		if not gate or not gate.get("passed"):
			raise StageError("release.json carries no green gate report")

		zip_path = self.downloads / "client-bin.zip"
		self.github.download_asset(release, "client-bin.zip", str(zip_path))
		if sha256_file(zip_path) != manifest["assets"]["client-bin.zip"]["sha256"]:
			raise StageError("client-bin.zip checksum mismatch")

		self._checkout_data(manifest["data_client_commit"])

		if self.patch_source.exists():
			shutil.rmtree(self.patch_source)
		self.patch_source.mkdir(parents=True)
		with zipfile.ZipFile(zip_path) as archive:
			archive.extractall(self.patch_source)
			binaries = [name for name in archive.namelist() if name != "source.txt"]
		if not (self.patch_source / "source.txt").is_file():
			raise StageError("client-bin.zip contains no source.txt")

		if tmp.exists():
			shutil.rmtree(tmp)
		self.patchdir.releases.mkdir(parents=True, exist_ok=True)
		self._run(self.compiler_command + [
			"-s", str(self.patch_source), "-o", str(tmp), "-c", "zlib", "-j", str(self.cfg.compiler_threads)])
		self._sanity_check(tmp, binaries)

		final = self.patchdir.release_path(commit)
		if final.exists():
			shutil.rmtree(final)
		os.replace(tmp, final)
		log.info("staged %s into %s", commit, final)
		return manifest

	def _checkout_data(self, data_commit):
		if not (self.data_checkout / ".git").exists():
			self.data_checkout.parent.mkdir(parents=True, exist_ok=True)
			self._run(["git", "clone", "--quiet", "--no-checkout", self.cfg.data_repo_url, str(self.data_checkout)])
		git = ["git", "-C", str(self.data_checkout)]
		self._run(git + ["fetch", "--quiet", "origin", data_commit])
		self._run(git + ["checkout", "--quiet", "--force", data_commit])
		self._run(git + ["clean", "-ffdxq"])

	def _sanity_check(self, output, binaries):
		listing = output / "list.txt"
		if not listing.is_file() or listing.stat().st_size == 0:
			raise StageError("update_compiler produced no list.txt")
		text = listing.read_text(encoding="utf-8", errors="replace")
		for name in binaries:
			if name not in text:
				raise StageError("list.txt does not mention {}".format(name))

	def _run(self, command):
		result = self.run(command, capture_output=True, text=True)
		if result.returncode != 0:
			detail = (result.stderr or result.stdout or "").strip()[-2000:]
			raise StageError("{} failed ({}): {}".format(" ".join(command[:3]), result.returncode, detail))
