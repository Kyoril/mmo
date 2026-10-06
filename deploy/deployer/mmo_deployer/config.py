# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Deployer configuration, read once from environment variables."""

import datetime
import json
from dataclasses import dataclass
from pathlib import Path

REQUIRED = (
	"GITHUB_REPO", "DATA_REPO_URL",
	"PORTAINER_URL", "PORTAINER_TOKEN", "PORTAINER_STACK_ID", "PORTAINER_ENDPOINT_ID",
	"REALMS", "LOGIN_URL", "LOGIN_PASSWORD",
	"MYSQL_HOST", "MYSQL_USER", "MYSQL_PASSWORD", "BACKUP_DATABASES",
)
NOTIFY_FORMATS = ("discord", "slack")


class ConfigError(Exception):
	pass


@dataclass(frozen=True)
class RealmEndpoint:
	name: str
	url: str
	password: str


@dataclass(frozen=True)
class Config:
	github_repo: str
	github_token: str
	release_prefix: str
	data_repo_url: str
	update_compiler: str
	compiler_threads: int
	portainer_url: str
	portainer_token: str
	portainer_stack_id: int
	portainer_endpoint_id: int
	wait_services: tuple
	realms: tuple
	login_url: str
	login_password: str
	mysql_host: str
	mysql_user: str
	mysql_password: str
	backup_databases: tuple
	maintenance_at: datetime.time
	timezone: str
	countdown_s: int
	poll_interval_s: int
	keep_releases: int
	keep_backups: int
	notify_webhook: str
	notify_format: str
	dry_run: bool
	patch_root: Path
	state_dir: Path


def _csv(text):
	return tuple(item.strip() for item in text.split(",") if item.strip())


def _parse_time(text):
	hours, minutes = text.split(":")
	return datetime.time(int(hours), int(minutes))


def _parse_realms(text):
	try:
		realms = tuple(RealmEndpoint(r["name"], r["url"].rstrip("/"), r["password"]) for r in json.loads(text))
	except (ValueError, KeyError, TypeError, AttributeError) as error:
		raise ConfigError("REALMS must be a JSON list of {{name, url, password}}: {}".format(error))
	if not realms:
		raise ConfigError("REALMS must name at least one realm")
	return realms


def load_config(env):
	missing = [name for name in REQUIRED if not env.get(name)]
	if missing:
		raise ConfigError("missing environment variables: " + ", ".join(missing))
	realms = _parse_realms(env["REALMS"])
	notify_format = env.get("NOTIFY_FORMAT", "discord")
	if notify_format not in NOTIFY_FORMATS:
		raise ConfigError("NOTIFY_FORMAT must be one of " + ", ".join(NOTIFY_FORMATS))
	try:
		return Config(
			github_repo=env["GITHUB_REPO"],
			github_token=env.get("GITHUB_TOKEN", ""),
			release_prefix=env.get("RELEASE_PREFIX", "nightly-"),
			data_repo_url=env["DATA_REPO_URL"],
			update_compiler=env.get("UPDATE_COMPILER", "/app/update_compiler"),
			compiler_threads=int(env.get("COMPILER_THREADS", "2")),
			portainer_url=env["PORTAINER_URL"].rstrip("/"),
			portainer_token=env["PORTAINER_TOKEN"],
			portainer_stack_id=int(env["PORTAINER_STACK_ID"]),
			portainer_endpoint_id=int(env["PORTAINER_ENDPOINT_ID"]),
			wait_services=_csv(env.get("WAIT_SERVICES", "realm_server_01,world_node_01")),
			realms=realms,
			login_url=env["LOGIN_URL"].rstrip("/"),
			login_password=env["LOGIN_PASSWORD"],
			mysql_host=env["MYSQL_HOST"],
			mysql_user=env["MYSQL_USER"],
			mysql_password=env["MYSQL_PASSWORD"],
			backup_databases=_csv(env["BACKUP_DATABASES"]),
			maintenance_at=_parse_time(env.get("MAINTENANCE_AT", "04:45")),
			timezone=env.get("TZ", "Europe/Berlin"),
			countdown_s=int(env.get("COUNTDOWN_S", "900")),
			poll_interval_s=int(env.get("POLL_INTERVAL_S", "900")),
			keep_releases=int(env.get("KEEP_RELEASES", "3")),
			keep_backups=int(env.get("KEEP_BACKUPS", "7")),
			notify_webhook=env.get("NOTIFY_WEBHOOK", ""),
			notify_format=notify_format,
			dry_run=env.get("DRY_RUN", "0") == "1",
			patch_root=Path(env.get("PATCH_ROOT", "/srv/mmo-patch")),
			state_dir=Path(env.get("STATE_DIR", "/state")),
		)
	except ValueError as error:
		raise ConfigError(str(error))
