# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""`deployer <command>`: `run` is the service; every other command talks to it via request files."""

import argparse
import json
import logging
import os
import sys
from dataclasses import asdict
from pathlib import Path

from .app import TICK_S, Deployer, write_request
from .backup import dump_databases
from .clients import GitHubClient, Notifier, PortainerClient, RestApi
from .clock import Clock
from .config import ConfigError, load_config
from .maintenance import Maintenance
from .patchdir import PatchDir
from .stage import Stager
from .state import load_state
from .web import Http

REQUEST_COMMANDS = ("pause", "resume", "stage-now", "deploy-now", "rollback", "unbad")


def build_deployer(cfg, clock=None, backup=None, compiler_command=None, github_api="https://api.github.com"):
	clock = clock or Clock(cfg.timezone)
	http = Http(timeout=60)
	notifier = Notifier(cfg.notify_webhook, cfg.notify_format, http)
	github = GitHubClient(cfg.github_repo, cfg.github_token, http, github_api)
	patchdir = PatchDir(cfg.patch_root)
	stager = Stager(cfg, github, patchdir, compiler_command)
	realms = [RestApi(realm.name, realm.url, realm.password, http) for realm in cfg.realms]
	login = RestApi("login", cfg.login_url, cfg.login_password, http)
	# Portainer answers the stack update only after pulling and starting every image.
	portainer = PortainerClient(cfg.portainer_url, cfg.portainer_token, cfg.portainer_stack_id, cfg.portainer_endpoint_id, Http(timeout=900))
	if backup is None:
		backups_dir = Path(cfg.state_dir) / "backups"

		def backup(sha):
			label = "{}-{}".format(clock.now().strftime("%Y%m%d-%H%M%S"), sha[:8])
			dump_databases(cfg, backups_dir / label)

	maintenance = Maintenance(cfg, realms, login, portainer, patchdir, backup, notifier, clock)
	return Deployer(cfg, github, stager, maintenance, patchdir, notifier, clock)


def main(argv=None, env=None):
	parser = argparse.ArgumentParser(prog="deployer", description=__doc__)
	sub = parser.add_subparsers(dest="command")
	sub.add_parser("run", help="run the service (default)")
	sub.add_parser("status", help="print state.json")
	sub.add_parser("pause", help="skip staging and maintenance until resumed")
	sub.add_parser("resume")
	sub.add_parser("stage-now", help="poll GitHub on the next tick")
	deploy_now = sub.add_parser("deploy-now", help="run the maintenance now (with countdown)")
	deploy_now.add_argument("sha", nargs="?")
	sub.add_parser("rollback", help="return to the previous live release immediately")
	unbad = sub.add_parser("unbad", help="allow a rolled-back release to be staged again")
	unbad.add_argument("sha")
	args = parser.parse_args(argv)
	command = args.command or "run"

	logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
	try:
		cfg = load_config(os.environ if env is None else env)
	except ConfigError as error:
		print("configuration error: {}".format(error), file=sys.stderr)
		return 2

	if command == "run":
		build_deployer(cfg).run_forever()
		return 0
	if command == "status":
		state = asdict(load_state(Path(cfg.state_dir) / "state.json"))
		state["history"] = state["history"][-10:]
		print(json.dumps(state, indent=2))
		return 0
	request_args = [getattr(args, "sha")] if getattr(args, "sha", None) else []
	write_request(cfg.state_dir, command, request_args)
	print("queued '{}'; the deployer applies it within {} s (see 'deployer status').".format(command, TICK_S))
	return 0
