#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Warns when a branch edits packet payloads without touching a protocol version.

tools/protocol_version_check.py is a hard gate, but it can only see the files that DEFINE
the protocol -- opcode lists, framing, cipher. The other half of a wire format lives in the
handlers: every io::write / io::read call scattered across the three server tiers and the
client. Adding a uint32 to a packet body there is just as breaking as adding an opcode, and
no fingerprint of any reasonable set of files will notice it.

So this tool does not try to decide. It reports: here are the payload edits on this branch,
and here is whether a protocol version moved. A human (or the reviewing agent) decides
whether the two are consistent.

It is ADVISORY and always exits 0. That is deliberate. The signal is inherently noisy --
plenty of io::write edits are internal or database-side and break nothing -- and a noisy
hard failure is one people learn to route around, taking the real failures with it.

Usage:
    python tools/gate/serialization_warning.py            # against develop
    python tools/gate/serialization_warning.py --base main
"""

import argparse
import json
import os
import re
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MANIFEST_REL = "src/shared/protocol_fingerprint.json"

# Calls that put bytes on, or take bytes off, a packet. Anything matching these in a diff
# is a candidate payload change.
SERIALIZATION_CALL = re.compile(
	r"io::(write|read)(_dynamic_range|_range|_container|_limited_string|_packed_guid)?\s*[<(]"
	r"|packet\.Start\s*\(")

# Files whose protocol content is already covered by the hard check. Flagging them here
# would just duplicate a failure the gate has already produced.
COVERED_PREFIXES = (
	"src/shared/auth_protocol/",
	"src/shared/game_protocol/",
)

DIFF_FILE_HEADER = re.compile(r"^\+\+\+ b/(.+)$")


def git(args):
	"""Runs a git command in the repo, returning stdout ('' if git itself failed)."""
	try:
		result = subprocess.run(
			["git", "-C", REPO_ROOT] + args,
			stdout=subprocess.PIPE, stderr=subprocess.PIPE,
			universal_newlines=True, errors="replace")
	except OSError:
		return None
	if result.returncode != 0:
		return None
	return result.stdout


def collect_payload_edits(base):
	"""Returns {path: (added, removed)} counting serialization lines changed per file."""
	# Three dots: the changes this branch introduced, not changes that merely landed on the
	# base since it was cut.
	diff = git(["diff", "--unified=0", "%s...HEAD" % (base,), "--", "src/"])
	if diff is None:
		return None

	edits = {}
	current = None
	for line in diff.splitlines():
		header = DIFF_FILE_HEADER.match(line)
		if header:
			current = header.group(1)
			continue
		if current is None or line.startswith("+++") or line.startswith("---"):
			continue
		if any(current.startswith(prefix) for prefix in COVERED_PREFIXES):
			continue
		if not (line.startswith("+") or line.startswith("-")):
			continue
		if not SERIALIZATION_CALL.search(line):
			continue

		added, removed = edits.get(current, (0, 0))
		if line.startswith("+"):
			edits[current] = (added + 1, removed)
		else:
			edits[current] = (added, removed + 1)

	return edits


def read_versions_at(ref):
	"""Protocol versions recorded in the manifest at a given ref, or None if absent."""
	if ref is None:
		path = os.path.join(REPO_ROOT, MANIFEST_REL)
		if not os.path.isfile(path):
			return None
		with open(path, "r", encoding="utf-8-sig") as handle:
			payload = json.load(handle)
	else:
		raw = git(["show", "%s:%s" % (ref, MANIFEST_REL)])
		if raw is None:
			return None
		try:
			payload = json.loads(raw)
		except ValueError:
			return None

	return dict((key, value.get("version")) for key, value in payload.items()
		if isinstance(value, dict) and "version" in value)


def main():
	parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
	parser.add_argument("--base", default="develop",
		help="branch this work will merge into (default: develop)")
	args = parser.parse_args()

	edits = collect_payload_edits(args.base)
	if edits is None:
		print("serialization warning: could not diff against '%s' - skipping." % (args.base,))
		return 0

	if not edits:
		return 0

	base_versions = read_versions_at(args.base)
	head_versions = read_versions_at(None)

	bumped = []
	if base_versions and head_versions:
		for protocol, version in sorted(head_versions.items()):
			if base_versions.get(protocol) != version:
				bumped.append("%s %s -> %s" % (protocol, base_versions.get(protocol), version))

	total = sum(added + removed for added, removed in edits.values())
	print("=" * 78)
	print("PACKET PAYLOAD ADVISORY -- %d serialization line(s) changed in %d file(s)"
		% (total, len(edits)))
	print("=" * 78)

	for path in sorted(edits):
		added, removed = edits[path]
		print("  %-64s +%-3d -%d" % (path, added, removed))

	print("")
	if bumped:
		print("A protocol version moved on this branch (%s)." % (", ".join(bumped),))
		print("Confirm it covers these edits; nothing further is needed if it does.")
	elif base_versions is None or head_versions is None:
		print("Could not read the protocol manifest on both sides, so no version comparison")
		print("was made. Check by hand whether these edits change a packet's wire format.")
	else:
		print("No protocol version was bumped on this branch.")
		print("")
		print("If any of these edits changed what a packet CONTAINS -- a field added, widened,")
		print("reordered or removed -- then a peer built against the old format will misparse")
		print("it and be admitted anyway, because nothing in the handshake can tell.")
		print("")
		print("If so: bump the constant in src/shared/auth_protocol/auth_protocol.h and/or")
		print("src/shared/game_protocol/game_protocol.h, then run")
		print("  python tools/protocol_version_check.py --update")
		print("")
		print("If these edits are internal (database rows, disk formats, in-process buffers),")
		print("no bump is needed and this advisory can be ignored.")

	print("=" * 78)
	return 0


if __name__ == "__main__":
	sys.exit(main())
