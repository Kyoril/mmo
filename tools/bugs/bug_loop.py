#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Autonomous bug loop over the central bug API. Operator guide: docs/bug-loop.md.

    python tools/bugs/bug_loop.py [--repo <main checkout>] watch [--dry-run] [--once]
    python tools/bugs/bug_loop.py [--repo <main checkout>] breaker on|off|status [--reason "..."]

Environment: MMO_BUG_API_KEY (reader key, required), MMO_BUG_API_URL (optional),
MMO_E2E_MYSQL_PASSWORD (the gate's E2E step).
"""

import argparse
import datetime
import json
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
# The checkout this script runs from: the runtime snapshot of origin/develop under the scheduled task.
RUNTIME_ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import bugs  # noqa: E402
from bugloop import claude, config as loop_config, data_diff, gitops, loop, notify, state as loop_state, verification, winlock  # noqa: E402


def _logger(path):
	os.makedirs(os.path.dirname(path), exist_ok=True)

	def log(message):
		line = "{} {}".format(datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"), message)
		print(line, flush=True)
		with open(path, "a", encoding="utf-8") as handle:
			handle.write(line + "\n")

	return log


def make_decoder(schema_root, repo, log, factory=None):
	"""Compiles the game-data schemas now, before any poll: the schemas come from schema_root (the
	runtime snapshot) and protoc from the main checkout's build, never from the fixer's worktree.
	Without either the loop still runs, and every data fix parks ("no decoder for game data")."""
	factory = factory or data_diff.DataDecoder
	protoc = data_diff.find_protoc(repo)
	try:
		return factory(schema_root, protoc=protoc)
	except (data_diff.DecoderUnavailable, OSError) as error:
		log("warning: game-data decoder unavailable, data fixes will park: {}".format(error))
		return None


def make_notifier(environ, log):
	"""Discord notifications are optional: no MMO_BUGLOOP_WEBHOOK, no messages."""
	return notify.Notifier(environ.get("MMO_BUGLOOP_WEBHOOK", ""), environ.get("MMO_BUGLOOP_UI_URL", ""), log=log)


def _read(relative):
	with open(os.path.join(HERE, "bugloop", relative), "r", encoding="utf-8") as handle:
		return handle.read()


def resolve_worktree(config, repo, environ):
	"""The loop's worktree: MMO_BUGLOOP_WORKTREE, else the config value, else mmo-bugloop next to the
	main checkout (the same convention as the nightly gate's mmo-nightly)."""
	path = environ.get("MMO_BUGLOOP_WORKTREE") or config.worktree
	return os.path.abspath(path) if path else os.path.join(os.path.dirname(repo), "mmo-bugloop")


def main(argv=None):
	# Bug text and tool output can hold anything; a console that cannot print it must not crash the loop.
	for stream in (sys.stdout, sys.stderr):
		if hasattr(stream, "reconfigure"):
			stream.reconfigure(errors="replace")
	parser = argparse.ArgumentParser(description="Autonomous bug loop over the central bug API")
	parser.add_argument("--repo", default=os.path.dirname(os.path.dirname(HERE)), help="main checkout: artifacts, reports, build")
	sub = parser.add_subparsers(dest="command", required=True)
	p_watch = sub.add_parser("watch", help="poll and work the backlog")
	p_watch.add_argument("--dry-run", action="store_true", help="no API writes, no ship, no push")
	p_watch.add_argument("--once", action="store_true", help="one poll, then exit")
	p_breaker = sub.add_parser("breaker", help="the auto-ship circuit breaker")
	p_breaker.add_argument("action", choices=("on", "off", "status"))
	p_breaker.add_argument("--reason", default="tripped by hand")
	args = parser.parse_args(argv)

	repo = os.path.abspath(args.repo)
	artifacts = os.path.join(repo, "artifacts", "bug-loop")
	now = loop.utcnow()
	if args.command == "breaker":
		if args.action == "on":
			loop_state.trip_breaker(artifacts, args.reason, now)
		elif args.action == "off":
			loop_state.reset_breaker(artifacts)
		print("breaker: " + ("tripped" if loop_state.breaker_active(artifacts) else "clear"))
		return 0

	api_key = os.environ.get("MMO_BUG_API_KEY")
	if not api_key:
		print("MMO_BUG_API_KEY is not set (reader key of the bug API)", file=sys.stderr)
		return 2
	instance = winlock.acquire_single_instance("Local\\MMOBugLoop")
	if instance is None:
		print("another bug loop is already running", file=sys.stderr)
		return 3

	config = loop_config.load_config(os.path.join(HERE, "bug_loop.json"))
	today = now.strftime("%Y-%m-%d")
	log = _logger(os.path.join(artifacts, "_runs", today + (".dry" if args.dry_run else "") + ".log"))
	state = loop_state.LoopState(os.path.join(artifacts, "state-dry.json" if args.dry_run else "state.json"), today)
	api = bugs.BugApi(os.environ.get("MMO_BUG_API_URL", bugs.DEFAULT_URL), api_key)
	if args.dry_run:
		api = loop.DryRunApi(api, os.path.join(artifacts, "dry-run-journal.jsonl"))
	runner = claude.ClaudeRunner(shutil.which(config.claude_exe) or config.claude_exe, config.model, on_invoke=state.count_invocation)
	worktree = gitops.Worktree(repo, resolve_worktree(config, repo, os.environ))
	verifier = verification.Verifier(worktree, os.path.join(repo, "build"), timeout=config.step_timeout_seconds)
	decoder = make_decoder(RUNTIME_ROOT, repo, log)
	prompts = {name: _read(os.path.join("prompts", name + ".md")) for name in ("triage", "fix", "review")}
	schemas = {name: json.loads(_read(os.path.join("schemas", name + ".json"))) for name in ("triage", "review")}
	bug_loop = loop.BugLoop(api, runner, worktree, verifier, decoder, config, state, prompts, schemas,
		artifacts, os.path.join(repo, "tools", "gate", "reports"), lock=winlock.named_mutex, dry_run=args.dry_run, log=log,
		notifier=make_notifier(os.environ, log))
	log("bug loop started ({}, {})".format("dry run" if args.dry_run else "live", HERE))
	log("notifications: " + ("on" if bug_loop.notifier.enabled else "off (MMO_BUGLOOP_WEBHOOK not set)"))
	if args.once:
		bug_loop.poll_once()
		return 0
	return bug_loop.watch(time.sleep)


if __name__ == "__main__":
	sys.exit(main())
