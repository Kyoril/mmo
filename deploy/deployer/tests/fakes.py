# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Shared test doubles for the deployer tests."""

import dataclasses
import datetime
import hashlib
import io
import json
import os
import subprocess
import tempfile
import zipfile
from pathlib import Path

from mmo_deployer.config import load_config

BASE_ENV = {
	"GITHUB_REPO": "Kyoril/mmo",
	"DATA_REPO_URL": "https://github.com/Kyoril/mmo-data",
	"PORTAINER_URL": "https://portainer.local",
	"PORTAINER_TOKEN": "token",
	"PORTAINER_STACK_ID": "7",
	"PORTAINER_ENDPOINT_ID": "2",
	"REALMS": json.dumps([{"name": "realm01", "url": "http://mmo-dev-realm-01:8092/", "password": "pw"}]),
	"LOGIN_URL": "http://mmo-dev-login:8090",
	"LOGIN_PASSWORD": "pw",
	"MYSQL_HOST": "host.docker.internal",
	"MYSQL_USER": "deployer",
	"MYSQL_PASSWORD": "secret",
	"BACKUP_DATABASES": "mmo_login,mmo_realm_01",
}


def make_config(tmp, **overrides):
	cfg = load_config(BASE_ENV)
	return dataclasses.replace(cfg, patch_root=Path(tmp) / "patch", state_dir=Path(tmp) / "state", **overrides)


class FakeClock:
	def __init__(self, start):
		self.current = start
		self.slept = 0

	def now(self):
		return self.current

	def sleep(self, seconds):
		self.current += datetime.timedelta(seconds=seconds)
		self.slept += seconds


def _can_symlink():
	with tempfile.TemporaryDirectory() as tmp:
		try:
			os.symlink(tmp, os.path.join(tmp, "link"))
			return True
		except (OSError, NotImplementedError):
			return False


# Windows without developer mode cannot create symlinks; the Linux CI gate runs these tests.
CAN_SYMLINK = _can_symlink()

SOURCE_TXT = (
	'version = 0\nroot = (type = "fs", from = ".", to = "", entries = {\n'
	'\t(type = "fs", from = "mmo_client.exe")\n'
	'\t(type = "fs", from = "Launcher.exe")\n'
	'\t(type = "fs", from = "../../data/client", to = "Data")\n})\n'
)


def git(repo, *args):
	return subprocess.run(
		["git", "-C", str(repo), "-c", "user.name=Test", "-c", "user.email=test@example.com"] + list(args),
		check=True, capture_output=True, text=True).stdout.strip()


def make_git_repo(path, files):
	"""Creates a repo at `path` with one commit holding `files` ({relative path: text}); returns its sha."""
	os.makedirs(path, exist_ok=True)
	git(path, "init", "-q")
	for name, text in files.items():
		full = os.path.join(path, name)
		os.makedirs(os.path.dirname(full), exist_ok=True)
		with open(full, "w", encoding="utf-8") as handle:
			handle.write(text)
	git(path, "add", "-A")
	git(path, "commit", "-q", "-m", "data")
	return git(path, "rev-parse", "HEAD")


def make_release_assets(commit, data_commit, binaries=None, source_txt=SOURCE_TXT, changes=None, sha_override=None):
	"""Returns {asset name: bytes} for a release: client-bin.zip and a matching release.json."""
	binaries = binaries if binaries is not None else {"mmo_client.exe": b"client", "Launcher.exe": b"launcher"}
	buffer = io.BytesIO()
	with zipfile.ZipFile(buffer, "w") as archive:
		if source_txt is not None:
			archive.writestr("source.txt", source_txt)
		for name, data in binaries.items():
			archive.writestr(name, data)
	zip_bytes = buffer.getvalue()
	manifest = {
		"schema": 1,
		"commit": commit,
		"image_tag": commit,
		"data_client_commit": data_commit,
		"data_editor_commit": "0" * 40,
		"created_at": "2026-10-07T00:50:00Z",
		"assets": {"client-bin.zip": {"sha256": sha_override or hashlib.sha256(zip_bytes).hexdigest(), "size": len(zip_bytes)}},
		"gate": {"passed": True, "steps": []},
		"changes": changes if changes is not None else ["fix(loot): roll on the right table"],
	}
	return {"client-bin.zip": zip_bytes, "release.json": json.dumps(manifest).encode("utf-8")}


class FakeGitHub:
	def __init__(self):
		self.releases = []
		self.assets = {}
		self.list_error = None

	def add_release(self, tag, commit, assets, created_at):
		release = {
			"tag_name": tag, "target_commitish": commit, "created_at": created_at, "draft": False,
			"assets": [{"name": name, "id": index} for index, name in enumerate(sorted(assets))],
		}
		for name, data in assets.items():
			self.assets[(tag, name)] = data
		self.releases.append(release)
		return release

	def list_releases(self, prefix):
		if self.list_error:
			raise self.list_error
		matching = [r for r in self.releases if r["tag_name"].startswith(prefix)]
		return sorted(matching, key=lambda r: r["created_at"], reverse=True)

	def download_asset(self, release, name, dest):
		key = (release["tag_name"], name)
		if key not in self.assets:
			raise KeyError(name)
		with open(dest, "wb") as handle:
			handle.write(self.assets[key])

	def fetch_manifest(self, release, scratch_dir):
		return json.loads(self.assets[(release["tag_name"], "release.json")].decode("utf-8"))


class FakeApi:
	def __init__(self, name, health=None, schedule_error=None):
		self.name = name
		self.health = health or (lambda: True)
		self.schedule_error = schedule_error
		self.schedule_result = True
		self.calls = []

	def uptime(self):
		self.calls.append("uptime")
		if not self.health():
			raise OSError("connection refused")
		return 10

	def schedule_shutdown(self, delay):
		self.calls.append(("shutdown", delay))
		if self.schedule_error:
			raise self.schedule_error
		return self.schedule_result

	def cancel_shutdown(self):
		self.calls.append("cancel")


class FakePortainer:
	def __init__(self, tag="old"):
		self.tag = tag
		self.deploys = []
		self.states = {"realm_server_01": "exited", "world_node_01": "exited"}
		self.ping_error = None
		self.redeploy_error = None
		self.redeploy_error_tags = None

	def ping(self):
		if self.ping_error:
			raise self.ping_error

	def current_tag(self):
		return self.tag

	def redeploy(self, tag):
		self.deploys.append(tag)
		if self.redeploy_error and (self.redeploy_error_tags is None or tag in self.redeploy_error_tags):
			raise self.redeploy_error
		self.tag = tag

	def service_states(self):
		return dict(self.states)


class FakeNotifier:
	def __init__(self):
		self.messages = []

	def send(self, text):
		self.messages.append(text)


class FakePatchDir:
	def __init__(self, root, releases, current=None):
		self.root = Path(root)
		self.release_set = set(releases)
		self.current = current
		self.flips = []
		self.pruned = []

	def has_release(self, sha):
		return sha in self.release_set

	def flip_current(self, sha):
		if sha not in self.release_set:
			raise FileNotFoundError(sha)
		self.flips.append(sha)
		self.current = sha

	def prune(self, keep, protect):
		self.pruned.append((keep, set(protect)))
		return []
