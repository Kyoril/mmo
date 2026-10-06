# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Shared test doubles for the deployer tests."""

import dataclasses
import datetime
import json
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
