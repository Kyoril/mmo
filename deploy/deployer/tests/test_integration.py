# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""A whole night with real clients, real git, a real PatchDir and HTTP stubs for every remote."""

import dataclasses
import datetime
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

from mmo_deployer.cli import build_deployer
from mmo_deployer.config import RealmEndpoint
from mmo_deployer.state import State, load_state, save_state

from .fakes import CAN_SYMLINK, FakeClock, make_config, make_git_repo, make_release_assets
from .stub_server import StubServer

FAKE_COMPILER = [sys.executable, os.path.join(os.path.dirname(__file__), "fake_update_compiler.py")]
NEW = "a" * 40
OLD = "b" * 40


@unittest.skipUnless(CAN_SYMLINK, "needs symlink support (runs in the Linux CI gate)")
class FullNight(unittest.TestCase):
	def test_stage_then_deploy(self):
		with tempfile.TemporaryDirectory() as tmp, StubServer() as stub:
			root = Path(tmp)
			data_commit = make_git_repo(root / "mmo-data", {"Worlds/map.txt": "terrain"})
			assets = make_release_assets(NEW, data_commit, changes=["fix(loot): roll on the right table"])

			stub.route("GET", "/repos/Kyoril/mmo/releases", (200, [{
				"tag_name": "nightly-20261007-aaaaaaaa", "target_commitish": NEW, "created_at": "2026-10-07T00:50:00Z",
				"draft": False, "assets": [{"name": "client-bin.zip", "id": 1}, {"name": "release.json", "id": 2}]}]))
			stub.route("GET", "/repos/Kyoril/mmo/releases/assets/1", (200, assets["client-bin.zip"]))
			stub.route("GET", "/repos/Kyoril/mmo/releases/assets/2", (200, assets["release.json"]))
			for prefix in ("/realm", "/login"):
				stub.route("GET", prefix + "/uptime", (200, {"uptime": 5}))
			stub.route("POST", "/realm/shutdown", (200, {"status": "SUCCESS", "delay": 900}))
			stub.route("GET", "/api/stacks/7", (200, {"Id": 7, "Name": "mmo", "Env": [{"name": "MMO_TAG", "value": OLD}]}))
			stub.route("GET", "/api/stacks/7/file", (200, {"StackFileContent": "services: {}\n"}))
			stub.route("PUT", "/api/stacks/7", (200, {"Id": 7}))

			def containers(request):
				# Exited after the countdown, running again once the stack update went through.
				state = "running" if stub.find("PUT", "/api/stacks/7") else "exited"
				return 200, [
					{"Names": ["/" + service], "State": state, "Labels": {"com.docker.compose.service": service}}
					for service in ("realm_server_01", "world_node_01")]

			stub.route("GET", "/api/endpoints/2/docker/containers/json", containers)

			cfg = dataclasses.replace(
				make_config(tmp, data_repo_url=str(root / "mmo-data")),
				portainer_url=stub.url,
				realms=(RealmEndpoint("realm01", stub.url + "/realm", "pw"),),
				login_url=stub.url + "/login",
			)
			cfg.state_dir.mkdir(parents=True)
			(cfg.patch_root / "releases" / OLD).mkdir(parents=True)
			os.symlink(os.path.join("releases", OLD), cfg.patch_root / "current")
			(cfg.patch_root / "launcher").mkdir()
			(cfg.patch_root / "launcher" / "launcher.json").write_text(json.dumps({"version": 1, "patches": []}), encoding="utf-8")
			save_state(cfg.state_dir / "state.json", State(live=OLD))

			clock = FakeClock(datetime.datetime(2026, 10, 7, 1, 0, tzinfo=datetime.timezone.utc))
			backups = []
			deployer = build_deployer(cfg, clock=clock, backup=backups.append, compiler_command=FAKE_COMPILER, github_api=stub.url)

			deployer.tick()
			self.assertEqual(load_state(cfg.state_dir / "state.json").staged, NEW)
			self.assertTrue((cfg.patch_root / "releases" / NEW / "list.txt").is_file())
			self.assertEqual(os.readlink(cfg.patch_root / "current"), os.path.join("releases", OLD))

			clock.current = datetime.datetime(2026, 10, 7, 4, 45, tzinfo=datetime.timezone.utc)
			deployer.tick()

			state = load_state(cfg.state_dir / "state.json")
			self.assertEqual((state.live, state.previous_live, state.staged), (NEW, OLD, None))
			self.assertEqual(os.readlink(cfg.patch_root / "current"), os.path.join("releases", NEW))
			self.assertEqual(backups, [NEW])
			self.assertEqual(stub.find("POST", "/realm/shutdown")[0].form(), {"delay": "900"})
			put = stub.find("PUT", "/api/stacks/7")[0].json()
			self.assertIn({"name": "MMO_TAG", "value": NEW}, put["env"])
			news = json.loads((cfg.patch_root / "launcher" / "launcher.json").read_text(encoding="utf-8"))
			self.assertIn("Roll on the right table", news["patches"][0]["body"])


if __name__ == "__main__":
	unittest.main()
