#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Fails when the network wire surface changed without the matching protocol version bump.

Bumping mmo::auth::ProtocolVersion / mmo::game::ProtocolVersion is what makes a server
reject a client it can no longer talk to. It has always been a step someone had to
remember, and it has been forgotten -- the symptom is not a clean rejection but a peer
that authenticates and then misparses everything after it.

This tool removes the remembering. It records a fingerprint of the wire-defining sources
next to the version they belong to (src/shared/protocol_fingerprint.json) and fails the
build when the two disagree.

What it can and cannot see
--------------------------
It fingerprints the files that DEFINE the protocol: the opcode enums, the shared wire
enums, the packet framing and the game cipher. So it catches added, removed, renumbered
and reordered opcodes -- reordering matters because several enums rely on implicit values
-- along with any change to how a packet is framed or encrypted.

It cannot see a payload change: adding a uint32 to a packet body is a handler edit in a
.cpp file somewhere across three server tiers, and hashing that surface would mean hashing
half the codebase. That gap is covered by review instead, via the serialization warning in
tools/gate/serialization_warning.py. Read this tool as a floor, not a ceiling.

Comments and whitespace are normalized away, so documenting an opcode does not trip the
check. That is deliberate: a check that cries wolf over doc edits teaches everyone to run
--update without thinking, which is the same as having no check at all.

Usage:
    python tools/protocol_version_check.py            # verify; exit 1 on mismatch
    python tools/protocol_version_check.py --update   # re-record after a deliberate bump
"""

import argparse
import hashlib
import json
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST_PATH = os.path.join(REPO_ROOT, "src", "shared", "protocol_fingerprint.json")

# The files whose content defines what goes on the wire, per protocol. The version constant
# lives in the first entry of each list.
#
# The framing (.._incoming_packet / .._outgoing_packet) and the cipher are in here for the
# same reason the opcode lists are: a peer that disagrees about either cannot read a single
# packet, and that is precisely what a version handshake exists to catch early.
PROTOCOLS = {
	"auth": {
		"constant": "mmo::auth::ProtocolVersion",
		"files": [
			"src/shared/auth_protocol/auth_protocol.h",
			"src/shared/auth_protocol/auth_protocol.cpp",
			"src/shared/auth_protocol/auth_incoming_packet.h",
			"src/shared/auth_protocol/auth_incoming_packet.cpp",
			"src/shared/auth_protocol/auth_outgoing_packet.h",
			"src/shared/auth_protocol/auth_outgoing_packet.cpp",
		],
	},
	"game": {
		"constant": "mmo::game::ProtocolVersion",
		"files": [
			"src/shared/game_protocol/game_protocol.h",
			"src/shared/game_protocol/game_protocol.cpp",
			"src/shared/game_protocol/game_incoming_packet.h",
			"src/shared/game_protocol/game_incoming_packet.cpp",
			"src/shared/game_protocol/game_outgoing_packet.h",
			"src/shared/game_protocol/game_outgoing_packet.cpp",
			"src/shared/game_protocol/game_crypt.h",
			"src/shared/game_protocol/game_crypt.cpp",
		],
	},
}

VERSION_PATTERN = re.compile(
	r"constexpr\s+uint32\s+ProtocolVersion\s*=\s*(0[xX][0-9a-fA-F]+|\d+)\s*;")


def strip_comments(text):
	"""Removes // and /* */ comments without touching string or character literals.

	A plain regex would happily eat the '//' inside a string literal and, worse, would
	corrupt any literal containing '/*'. The wire headers are mostly comment-free today,
	but a fingerprint tool that can silently mangle its own input is not one to trust.
	"""
	out = []
	i = 0
	n = len(text)
	while i < n:
		c = text[i]
		if c == '"' or c == "'":
			quote = c
			out.append(c)
			i += 1
			while i < n:
				if text[i] == "\\" and i + 1 < n:
					out.append(text[i:i + 2])
					i += 2
					continue
				out.append(text[i])
				if text[i] == quote:
					i += 1
					break
				i += 1
			continue
		if c == "/" and i + 1 < n and text[i + 1] == "/":
			while i < n and text[i] != "\n":
				i += 1
			continue
		if c == "/" and i + 1 < n and text[i + 1] == "*":
			i += 2
			while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
				i += 1
			i += 2
			# Newlines inside the comment are gone; a separator keeps tokens from fusing.
			out.append(" ")
			continue
		out.append(c)
		i += 1
	return "".join(out)


def normalize(text):
	"""Reduces a source file to the part a peer on the other end of the socket can observe.

	Comments, indentation, blank lines and the ProtocolVersion definition itself all go:
	the first three carry no wire meaning, and the last one is the thing being guarded, not
	part of what it guards.
	"""
	text = strip_comments(text)
	lines = []
	for line in text.splitlines():
		if VERSION_PATTERN.search(line):
			continue
		collapsed = " ".join(line.split())
		if collapsed:
			lines.append(collapsed)
	return "\n".join(lines)


def read_source(rel_path):
	path = os.path.join(REPO_ROOT, rel_path)
	if not os.path.isfile(path):
		return None
	# utf-8-sig: several sources in this repo carry a BOM, and it must not reach the hash.
	with open(path, "r", encoding="utf-8-sig") as handle:
		return handle.read()


def sha256(text):
	return "sha256:" + hashlib.sha256(text.encode("utf-8")).hexdigest()


def parse_enum_value(raw, previous):
	"""Resolves one enumerator's value, mirroring how the compiler assigns implicit ones.

	Implicit values are the reason this matters: several opcode lists stop numbering part
	way through (realm_world_packet does exactly that), so inserting an entry in the middle
	silently renumbers everything after it. That is a breaking change that looks like an
	addition in a diff, and it is the kind this check exists to catch.
	"""
	if raw is None:
		if isinstance(previous, int):
			return previous + 1
		return "<implicit after %s>" % (previous,)

	raw = raw.strip()
	try:
		return int(raw, 0)
	except ValueError:
		# Not a plain literal (an expression, or a reference to another constant). Keep the
		# text verbatim: it still compares deterministically, which is all that is needed.
		return raw


def parse_enumerators(body):
	entries = []
	previous = -1
	for member in body.split(","):
		member = member.strip()
		if not member:
			continue
		if "=" in member:
			name, _, raw = member.partition("=")
			value = parse_enum_value(raw, previous)
		else:
			name = member
			value = parse_enum_value(None, previous)
		name = name.strip()
		if not name:
			continue
		entries.append((name, value))
		previous = value
	return entries


NAMESPACE_ENUM = re.compile(
	r"namespace\s+(\w+)\s*\{\s*enum\s+Type\s*\{([^{}]*?)\}\s*;", re.DOTALL)
ENUM_CLASS = re.compile(
	r"enum\s+class\s+(\w+)\s*(?::\s*[\w:]+\s*)?\{([^{}]*?)\}\s*;", re.DOTALL)


def extract_symbols(protocol, text):
	"""Lists every wire enumerator in a file as 'protocol::scope::Name = value'.

	Recorded alongside the hash so that a failure can say WHICH opcode moved, and so that
	the manifest diff in a review reads as a description of the protocol change rather than
	as one unreadable hash swapping for another.
	"""
	stripped = strip_comments(text)
	symbols = []

	for match in NAMESPACE_ENUM.finditer(stripped):
		scope = match.group(1)
		for name, value in parse_enumerators(match.group(2)):
			symbols.append("%s::%s::%s = %s" % (protocol, scope, name, value))

	for match in ENUM_CLASS.finditer(stripped):
		scope = match.group(1)
		for name, value in parse_enumerators(match.group(2)):
			symbols.append("%s::%s::%s = %s" % (protocol, scope, name, value))

	return sorted(symbols)


def read_version(rel_path, text):
	match = VERSION_PATTERN.search(text)
	if not match:
		raise SystemExit(
			"ERROR: no 'constexpr uint32 ProtocolVersion' found in %s.\n"
			"       Either the constant was renamed or PROTOCOLS in %s is out of date."
			% (rel_path, os.path.relpath(__file__, REPO_ROOT)))
	return int(match.group(1), 0)


def snapshot():
	"""Computes the current state of every tracked protocol."""
	result = {}
	for protocol, spec in PROTOCOLS.items():
		version = None
		file_hashes = {}
		symbols = []
		combined = []

		for rel_path in spec["files"]:
			text = read_source(rel_path)
			if text is None:
				raise SystemExit(
					"ERROR: tracked file %s does not exist.\n"
					"       If it was renamed or removed, update PROTOCOLS in %s."
					% (rel_path, os.path.relpath(__file__, REPO_ROOT)))

			if version is None:
				version = read_version(rel_path, text)

			normalized = normalize(text)
			file_hashes[rel_path] = sha256(normalized)
			symbols.extend(extract_symbols(protocol, text))
			combined.append(rel_path + "\n" + normalized)

		result[protocol] = {
			"version": version,
			"fingerprint": sha256("\n".join(combined)),
			"files": file_hashes,
			"symbols": sorted(symbols),
		}
	return result


def load_manifest():
	if not os.path.isfile(MANIFEST_PATH):
		return None
	with open(MANIFEST_PATH, "r", encoding="utf-8-sig") as handle:
		return json.load(handle)


def write_manifest(current):
	payload = {
		"_comment": [
			"Generated by tools/protocol_version_check.py -- do not hand-edit.",
			"Records the wire surface each ProtocolVersion corresponds to, so that changing",
			"the surface without bumping the version fails the gate instead of shipping.",
			"After a deliberate bump: python tools/protocol_version_check.py --update",
		],
	}
	for protocol in sorted(current):
		payload[protocol] = current[protocol]

	with open(MANIFEST_PATH, "w", encoding="utf-8", newline="\n") as handle:
		json.dump(payload, handle, indent="\t", sort_keys=False)
		handle.write("\n")


def describe_symbol_changes(old_symbols, new_symbols):
	"""Human-readable delta between two symbol lists, capped so output stays readable."""
	old_set = set(old_symbols or [])
	new_set = set(new_symbols or [])

	# Match on the symbol name so a renumbering reads as a change rather than as an
	# unrelated add plus an unrelated remove.
	def by_name(symbols):
		return dict((s.split(" = ", 1)[0], s.split(" = ", 1)[1]) for s in symbols
			if " = " in s)

	old_by_name = by_name(old_set)
	new_by_name = by_name(new_set)

	lines = []
	for name in sorted(set(new_by_name) - set(old_by_name)):
		lines.append("    + %s = %s" % (name, new_by_name[name]))
	for name in sorted(set(old_by_name) - set(new_by_name)):
		lines.append("    - %s = %s" % (name, old_by_name[name]))
	for name in sorted(set(old_by_name) & set(new_by_name)):
		if old_by_name[name] != new_by_name[name]:
			lines.append("    ~ %s: %s -> %s" % (name, old_by_name[name], new_by_name[name]))

	if len(lines) > 25:
		hidden = len(lines) - 25
		lines = lines[:25] + ["    ... and %d more" % (hidden,)]
	return lines


def describe_file_changes(old_files, new_files):
	old_files = old_files or {}
	lines = []
	for rel_path in sorted(new_files):
		if old_files.get(rel_path) != new_files[rel_path]:
			lines.append("    ~ %s" % (rel_path,))
	for rel_path in sorted(set(old_files) - set(new_files)):
		lines.append("    - %s (no longer tracked)" % (rel_path,))
	return lines


def check(current, manifest):
	"""Returns a list of human-readable problems; empty means everything lines up."""
	problems = []

	for protocol in sorted(current):
		now = current[protocol]
		recorded = (manifest or {}).get(protocol)
		constant = PROTOCOLS[protocol]["constant"]
		version_file = PROTOCOLS[protocol]["files"][0]

		if recorded is None:
			problems.append(
				"%s: no fingerprint recorded yet.\n"
				"  Seed it with: python tools/protocol_version_check.py --update"
				% (protocol,))
			continue

		surface_changed = recorded.get("fingerprint") != now["fingerprint"]
		version_changed = recorded.get("version") != now["version"]

		if not surface_changed and not version_changed:
			continue

		if surface_changed and not version_changed:
			detail = describe_symbol_changes(recorded.get("symbols"), now["symbols"])
			if not detail:
				detail = ["    (no opcode changes -- framing, cipher or layout changed)"]
			detail += describe_file_changes(recorded.get("files"), now["files"])

			problems.append(
				"%s: the wire surface changed but %s is still %d.\n"
				"%s\n"
				"  A peer built against the old surface will misparse these packets rather\n"
				"  than be rejected. Bump the constant in %s, then re-record:\n"
				"    python tools/protocol_version_check.py --update"
				% (protocol, constant, now["version"], "\n".join(detail), version_file))
			continue

		# The version moved, so the change was deliberate -- the manifest just has to catch
		# up. Still a failure: a stale manifest silently widens the window in which the next
		# real change goes unnoticed.
		problems.append(
			"%s: %s went %s -> %d but the fingerprint was not re-recorded.\n"
			"  Run: python tools/protocol_version_check.py --update"
			% (protocol, constant, recorded.get("version"), now["version"]))

	return problems


def main():
	parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
	parser.add_argument("--update", action="store_true",
		help="re-record the fingerprints after a deliberate version bump")
	args = parser.parse_args()

	current = snapshot()
	manifest = load_manifest()

	if args.update:
		if manifest is not None:
			# --update must not become a way to make the check go away. Re-recording a
			# changed surface under an unchanged version is exactly the mistake this tool
			# exists to prevent, so it is the one thing --update refuses to do.
			refused = []
			for protocol in sorted(current):
				recorded = manifest.get(protocol)
				if recorded is None:
					continue
				if (recorded.get("fingerprint") != current[protocol]["fingerprint"]
						and recorded.get("version") == current[protocol]["version"]):
					refused.append("  %s: %s is still %d" % (
						protocol, PROTOCOLS[protocol]["constant"], current[protocol]["version"]))

			if refused:
				print("Refusing to re-record: the wire surface changed with no version bump.")
				print("\n".join(refused))
				print("\nBump the version constant first, then run --update again.")
				return 1

		write_manifest(current)
		print("Recorded protocol fingerprints in %s"
			% (os.path.relpath(MANIFEST_PATH, REPO_ROOT).replace("\\", "/"),))
		for protocol in sorted(current):
			print("  %-6s version %d" % (protocol, current[protocol]["version"]))
		return 0

	problems = check(current, manifest)
	if problems:
		print("PROTOCOL VERSION CHECK FAILED")
		print("")
		for problem in problems:
			print(problem)
			print("")
		return 1

	print("Protocol versions are in sync with the wire surface:")
	for protocol in sorted(current):
		print("  %-6s version %d" % (protocol, current[protocol]["version"]))
	return 0


if __name__ == "__main__":
	sys.exit(main())
