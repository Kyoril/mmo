#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Keeps docker's SIGTERM reaching the servers.

Each server image starts through docker/<tier>/run_server.sh. Unless the script execs the
server, bash stays PID 1 with the server as its child, and bash as PID 1 neither forwards
SIGTERM nor dies of it. Docker then SIGKILLs the server after its stop_grace_period: the
realm never saves its characters, and every redeploy waits out the full grace period
(the failed nightly deploy of 2026-10-25). This test fails when a script starts its server
any other way.

    python tools/tests/test_server_container_signals.py
"""

import os
import re
import sys
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TIERS = ("login_server", "realm_server", "world_server")


def _commands(script):
	"""The script's command lines, without comments and the config heredoc."""
	lines = []
	in_heredoc = None
	for line in script.splitlines():
		stripped = line.strip()
		if in_heredoc:
			if stripped == in_heredoc:
				in_heredoc = None
			continue
		heredoc = re.search(r"<<-?\s*['\"]?(\w+)['\"]?", stripped)
		if heredoc:
			in_heredoc = heredoc.group(1)
		if stripped and not stripped.startswith("#"):
			lines.append(stripped)
	return lines


class ServerContainerSignalTests(unittest.TestCase):
	def test_every_run_script_execs_its_server(self):
		for tier in TIERS:
			with self.subTest(tier=tier):
				path = os.path.join(REPO_ROOT, "docker", tier, "run_server.sh")
				with open(path, encoding="utf-8") as handle:
					commands = _commands(handle.read())
				starts = [c for c in commands if re.search(r"(^|\s|/)\./{}(\s|$)".format(tier), c)]
				self.assertEqual(starts, ["exec ./{}".format(tier)],
					"{} must start the server with 'exec ./{}' so it is PID 1 and gets SIGTERM".format(path, tier))
				self.assertEqual(commands[-1], "exec ./{}".format(tier),
					"{}: nothing may follow the exec, it would never run".format(path))

	def test_every_server_image_starts_through_its_run_script(self):
		# The exec only helps if the image runs the script in exec form (no extra sh -c around it).
		for tier in TIERS:
			with self.subTest(tier=tier):
				path = os.path.join(REPO_ROOT, "Dockerfile.{}".format(tier))
				with open(path, encoding="utf-8") as handle:
					dockerfile = handle.read()
				self.assertRegex(dockerfile, r'(?m)^CMD \["/app/run_server\.sh"\]\s*$', path)
				self.assertNotRegex(dockerfile, r"(?m)^ENTRYPOINT", path)


if __name__ == "__main__":
	unittest.main()
