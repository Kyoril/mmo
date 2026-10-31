# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Turns a nightly GitHub release into releases/<sha>/ on the patch root."""

import hashlib
import json
import logging
import os
import shutil
import subprocess
import zipfile
from pathlib import Path

from .web import HttpError

log = logging.getLogger("deployer")

GIT_TIMEOUT_S = 1800
COMPILER_TIMEOUT_S = 3 * 3600
# Abort a fetch that stalls below 1 KB/s for 5 minutes instead of hanging the deployer.
GIT_OPTIONS = ["-c", "http.lowSpeedLimit=1000", "-c", "http.lowSpeedTime=300"]


LAUNCHER_BINARY = "Launcher.exe"
LAUNCHER_MANIFEST = "launcher-release.json"
# The macOS app bundle; releases published before it existed do not carry it.
MACOS_CLIENT_ASSET = "client-macos.zip"
# Cooked client content (material shaders for every platform) laid over the data checkout.
OVERLAY_ASSET = "client-overlay.zip"
OVERLAY_MANIFEST = "overlay.json"


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

		zip_path = self._download_verified(release, manifest, "client-bin.zip")
		macos_zip = None
		if MACOS_CLIENT_ASSET in manifest["assets"]:
			macos_zip = self._download_verified(release, manifest, MACOS_CLIENT_ASSET)
		overlay_zip = None
		if OVERLAY_ASSET in manifest["assets"]:
			overlay_zip = self._download_verified(release, manifest, OVERLAY_ASSET)

		self._checkout_data(manifest["data_client_commit"])
		cook_warnings = self._apply_overlay(overlay_zip) if overlay_zip is not None else []

		if self.patch_source.exists():
			shutil.rmtree(self.patch_source)
		self.patch_source.mkdir(parents=True)
		with zipfile.ZipFile(zip_path) as archive:
			archive.extractall(self.patch_source)
			binaries = [name for name in archive.namelist() if name != "source.txt"]
		if not (self.patch_source / "source.txt").is_file():
			raise StageError("client-bin.zip contains no source.txt")
		if macos_zip is not None:
			binaries += self._extract_macos_client(macos_zip)
		launcher_version = self._pin_launcher()

		if tmp.exists():
			shutil.rmtree(tmp)
		self.patchdir.releases.mkdir(parents=True, exist_ok=True)
		self._run(self.compiler_command + [
			"-s", str(self.patch_source), "-o", str(tmp), "-c", "zlib", "-j", str(self.cfg.compiler_threads)],
			COMPILER_TIMEOUT_S)
		self._sanity_check(tmp, binaries)

		final = self.patchdir.release_path(commit)
		if final.exists():
			shutil.rmtree(final)
		os.replace(tmp, final)
		log.info("staged %s into %s (launcher %s)", commit, final, launcher_version or "from the nightly")
		return dict(manifest, launcher_version=launcher_version, cook_warnings=cook_warnings)

	def _download_verified(self, release, manifest, name):
		"""Downloads asset `name` and checks it against release.json; returns its path."""
		path = self.downloads / name
		self.github.download_asset(release, name, str(path))
		if sha256_file(path) != manifest["assets"][name]["sha256"]:
			raise StageError("{} checksum mismatch".format(name))
		return path

	def _extract_macos_client(self, zip_path):
		"""Unpacks client-macos.zip next to the Windows binaries; returns its top-level names.

		It holds only mmo_client.app. File modes are not kept: the patch format has none, the
		updater marks the bundle's executables itself. Raises StageError.
		"""
		with zipfile.ZipFile(zip_path) as archive:
			names = archive.namelist()
			top_level = sorted({name.split("/", 1)[0] for name in names})
			if top_level != ["mmo_client.app"]:
				raise StageError("{} must hold only mmo_client.app, not {}".format(MACOS_CLIENT_ASSET, ", ".join(top_level)))
			archive.extractall(self.patch_source)
		return top_level

	def _apply_overlay(self, zip_path):
		"""Lays client-overlay.zip over the data checkout; returns the cook warnings it carries.

		Every file except overlay.json replaces its counterpart, then each path in overlay.json's
		"delete" list is removed. The next _checkout_data resets the checkout. Raises StageError.
		"""
		root = self.data_checkout.resolve()

		def inside(relative):
			target = (root / relative).resolve()
			if target != root and root not in target.parents:
				raise StageError("{} names a path outside the data checkout: {}".format(OVERLAY_ASSET, relative))
			return target

		with zipfile.ZipFile(zip_path) as archive:
			names = archive.namelist()
			if OVERLAY_MANIFEST not in names:
				raise StageError("{} contains no {}".format(OVERLAY_ASSET, OVERLAY_MANIFEST))
			try:
				overlay = json.loads(archive.read(OVERLAY_MANIFEST).decode("utf-8"))
			except ValueError as error:
				raise StageError("{} is not valid JSON: {}".format(OVERLAY_MANIFEST, error))
			if overlay.get("schema") != 1:
				raise StageError("unsupported {} schema {}".format(OVERLAY_MANIFEST, overlay.get("schema")))
			files = [name for name in names if name != OVERLAY_MANIFEST and not name.endswith("/")]
			deletions = [inside(path) for path in overlay.get("delete") or []]
			for name in files:
				target = inside(name)
				target.parent.mkdir(parents=True, exist_ok=True)
				with archive.open(name) as source, open(target, "wb") as sink:
					shutil.copyfileobj(source, sink)
		for target in deletions:
			if target.is_dir():
				shutil.rmtree(target)
			elif target.exists():
				target.unlink()
		warnings = list(overlay.get("warnings") or [])
		log.info("applied %s: %d files, %d deletions, %d cook warnings", OVERLAY_ASSET, len(files), len(deletions), len(warnings))
		return warnings

	def _pin_launcher(self):
		"""Replaces the nightly's Launcher.exe with the newest launcher release; returns its version.

		The launcher has its own release cycle: a rebuilt launcher differs in bytes every night,
		and every changed Launcher.exe makes every player's launcher update itself. Until the
		first launcher release exists the nightly's own build ships. Raises StageError.
		"""
		release = self.github.newest_versioned_release(self.cfg.launcher_prefix)
		if release is None:
			log.warning("no %s* release; shipping the nightly's launcher build", self.cfg.launcher_prefix)
			return None
		info = self.github.fetch_manifest(release, str(self.downloads), LAUNCHER_MANIFEST)
		binary = self.downloads / LAUNCHER_BINARY
		self.github.download_asset(release, LAUNCHER_BINARY, str(binary))
		if sha256_file(binary) != info["sha256"]:
			raise StageError("{} of {} checksum mismatch".format(LAUNCHER_BINARY, release["tag_name"]))
		shutil.copyfile(binary, self.patch_source / LAUNCHER_BINARY)
		return str(info["version"])

	def _checkout_data(self, data_commit):
		if not (self.data_checkout / ".git").exists():
			self.data_checkout.parent.mkdir(parents=True, exist_ok=True)
			self._git(["clone", "--quiet", "--no-checkout", self.cfg.data_repo_url, str(self.data_checkout)])
		here = ["-C", str(self.data_checkout)]
		self._git(here + ["fetch", "--quiet", "origin", data_commit])
		self._git(here + ["checkout", "--quiet", "--force", data_commit])
		self._git(here + ["clean", "-ffdxq"])

	def _git(self, args):
		# Never wait for credentials on a terminal nobody is watching.
		env = dict(os.environ, GIT_TERMINAL_PROMPT="0")
		subcommand = args[2] if args[0] == "-C" else args[0]
		self._run(["git"] + GIT_OPTIONS + args, GIT_TIMEOUT_S, env, "git " + subcommand)

	def _sanity_check(self, output, binaries):
		listing = output / "list.txt"
		if not listing.is_file() or listing.stat().st_size == 0:
			raise StageError("update_compiler produced no list.txt")
		text = listing.read_text(encoding="utf-8", errors="replace")
		for name in binaries:
			if name not in text:
				raise StageError("list.txt does not mention {}".format(name))

	def _run(self, command, timeout, env=None, name=None):
		name = name or " ".join(command[:3])
		try:
			result = self.run(command, capture_output=True, text=True, timeout=timeout, env=env)
		except subprocess.TimeoutExpired:
			raise StageError("{} timed out after {} s".format(name, timeout))
		if result.returncode != 0:
			detail = (result.stderr or result.stdout or "").strip()[-2000:]
			raise StageError("{} failed ({}): {}".format(name, result.returncode, detail))
