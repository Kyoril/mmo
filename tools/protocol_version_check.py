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

# Version of the fingerprinting scheme itself -- NOT of any protocol.
#
# BUMP THIS whenever a change to this script makes fingerprints incomparable with previously
# recorded ones: a file added to or removed from PROTOCOLS, a change to normalize() or to the
# order files are combined in. Without it, changing the tool looks exactly like changing the
# wire format, and the only way out would be a flag that bypasses the version check -- which
# would then be the obvious way around it for everyone else too.
#
# When this differs from the manifest, --update re-records without demanding a version bump,
# because the recorded fingerprint was produced by a different algorithm and comparing the two
# means nothing. Bumping it is a visible edit to this file, so it cannot be used quietly.
FINGERPRINT_FORMAT = 2

# The files whose content defines what goes on the wire, per protocol. The version constant
# lives in the first entry of each list.
#
# The framing (.._incoming_packet / .._outgoing_packet) and the cipher are in here for the
# same reason the opcode lists are: a peer that disagrees about either cannot read a single
# packet, and that is precisely what a version handshake exists to catch early.
PROTOCOLS = {
	"auth": {
		"constant": "mmo::auth::ProtocolVersion",
		"version_file": "src/shared/auth_protocol/auth_protocol.h",
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
		"version_file": "src/shared/game_protocol/game_protocol.h",
		"files": [
			# The field layer is as much a wire format as the opcode list, and a more fragile
			# one. FieldMap indices go out in every UpdateObject, object_fields::UnitFields is
			# numbered implicitly from ObjectFieldCount, and adding a unit stat is ordinary
			# gameplay work -- so inserting a field renumbers every field after it and breaks
			# every object update, from a change that looks nothing like a protocol edit.
			"src/shared/game/object_type_id.h",
			"src/shared/game/field_map.h",
			"src/shared/game/movement_info.h",

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


def parse_enumerators(body):
	"""Resolves every enumerator's value, mirroring how the compiler assigns implicit ones.

	Implicit values are the reason this matters. Several enums stop numbering part way
	through -- realm_world_packet does, and object_fields::UnitFields is numbered entirely
	implicitly from ObjectFieldCount -- so inserting an entry in the middle silently
	renumbers everything after it. That is a breaking change that reads as an addition in a
	diff, and it is exactly the kind this check exists to catch.

	Where an enumerator is initialised with an expression rather than a literal
	(`ObjectFieldCount = Owner + 2`), the value cannot be resolved to a number. It is kept
	as text and subsequent implicit values are expressed relative to it -- 'ObjectFieldCount',
	'ObjectFieldCount+1', 'ObjectFieldCount+2'. Still deterministic, still shifts as a block
	when something is inserted, which is all the comparison needs.
	"""
	entries = []
	previous = -1		# last resolved numeric value
	anchor = None		# last unresolvable value, for relative numbering
	offset = 0

	for member in body.split(","):
		member = member.strip()
		if not member:
			continue

		name, sep, raw = member.partition("=")
		name = name.strip()
		if not name:
			continue

		if sep:
			raw = raw.strip()
			try:
				value = int(raw, 0)
				previous = value
				anchor = None
			except ValueError:
				# An expression or a reference to another constant.
				value = raw
				anchor = raw
				offset = 0
		elif anchor is not None:
			offset += 1
			value = "%s+%d" % (anchor, offset)
		else:
			previous += 1
			value = previous

		entries.append((name, value))

	return entries


NAMESPACE_DECL = re.compile(r"namespace\s+(\w+)\s*\{")
ENUM_DECL = re.compile(r"enum\s+(?:class\s+)?(\w+)\s*(?::\s*[\w:]+\s*)?\{")


def extract_symbols(protocol, text):
	"""Lists every wire enumerator in a file as 'protocol::scope::Name = value'.

	Recorded alongside the hash so that a failure can say WHICH opcode moved, and so that
	the manifest diff in a review reads as a description of the protocol change rather than
	as one unreadable hash swapping for another.
	"""
	stripped = strip_comments(text)
	symbols = []

	# Walked rather than pattern-matched. A single regex anchored on `namespace X { enum ...`
	# only ever sees the FIRST enum in a namespace, which silently dropped
	# object_fields::UnitFields -- the most breakage-prone enum in the codebase -- while
	# still reporting a plausible-looking symbol list.
	namespaces = []		# (name, brace depth at which it opened)
	depth = 0
	i = 0
	length = len(stripped)

	while i < length:
		char = stripped[i]

		# Only two keywords can start something interesting; skip the regex otherwise.
		if char == "n":
			match = NAMESPACE_DECL.match(stripped, i)
			if match:
				depth += 1
				namespaces.append((match.group(1), depth))
				i = match.end()
				continue
		elif char == "e":
			match = ENUM_DECL.match(stripped, i)
			if match:
				end = stripped.find("}", match.end())
				if end != -1:
					# `enum Type` is the project's pseudo-namespace idiom and adds nothing to
					# the name; any other enum name is meaningful and must be kept, or the two
					# enums inside object_fields would collide.
					scope = namespaces[-1][0] if namespaces else ""

					# An enum sitting directly in `namespace mmo` or in the protocol's own
					# namespace gets no scope prefix from it -- the protocol key already says
					# that, and 'auth::auth::AuthLocale' helps nobody.
					if scope in ("mmo", protocol):
						scope = ""

					if match.group(1) != "Type":
						scope = "%s::%s" % (scope, match.group(1)) if scope else match.group(1)

					for name, value in parse_enumerators(stripped[match.end():end]):
						symbols.append("%s::%s::%s = %s" % (protocol, scope, name, value))

					# Both of the enum's braces are consumed here, so `depth` is untouched.
					i = end + 1
					continue

		if char == "{":
			depth += 1
		elif char == "}":
			depth -= 1
			while namespaces and namespaces[-1][1] > depth:
				namespaces.pop()

		i += 1

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

		# Sorted so the combined hash does not depend on the order entries happen to be
		# listed in, and so adding a file in the middle of the list is not mistaken for a
		# surface change on its own.
		for rel_path in sorted(spec["files"]):
			text = read_source(rel_path)
			if text is None:
				raise SystemExit(
					"ERROR: tracked file %s does not exist.\n"
					"       If it was renamed or removed, update PROTOCOLS in %s."
					% (rel_path, os.path.relpath(__file__, REPO_ROOT)))

			if rel_path == spec["version_file"]:
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
		try:
			return json.load(handle)
		except ValueError as error:
			# A traceback out of here would be reported by the gate as a wire format
			# violation, which is the wrong thing to go looking for.
			raise SystemExit(
				"ERROR: %s is not valid JSON (%s).\n"
				"       It is generated, so the fix is to restore it from git:\n"
				"         git checkout -- %s"
				% (os.path.relpath(MANIFEST_PATH, REPO_ROOT).replace("\\", "/"), error,
					os.path.relpath(MANIFEST_PATH, REPO_ROOT).replace("\\", "/")))


def write_manifest(current):
	payload = {
		"_comment": [
			"Generated by tools/protocol_version_check.py -- do not hand-edit.",
			"Records the wire surface each ProtocolVersion corresponds to, so that changing",
			"the surface without bumping the version fails the gate instead of shipping.",
			"After a deliberate bump: python tools/protocol_version_check.py --update",
		],
		"_format": FINGERPRINT_FORMAT,
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

	if manifest is not None and manifest.get("_format") != FINGERPRINT_FORMAT:
		# Every per-protocol comparison below would be meaningless, so report only this.
		return ["the manifest was recorded by a different version of this checker "
			"(format %s, this is %d).\n"
			"  The fingerprints are not comparable, so nothing can be concluded about\n"
			"  whether the wire format changed. Re-record:\n"
			"    python tools/protocol_version_check.py --update"
			% (manifest.get("_format"), FINGERPRINT_FORMAT)]

	for protocol in sorted(current):
		now = current[protocol]
		recorded = (manifest or {}).get(protocol)
		constant = PROTOCOLS[protocol]["constant"]
		version_file = PROTOCOLS[protocol]["version_file"]

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
	parser.add_argument("--seed", action="store_true",
		help="with --update, allow writing a manifest that does not exist yet")
	args = parser.parse_args()

	current = snapshot()
	manifest = load_manifest()

	if args.update:
		# Without this, deleting the manifest would be a way around the refusal below --
		# --update would see nothing recorded, have nothing to compare, and happily write a
		# changed surface under an unchanged version.
		if manifest is None and not args.seed:
			print("There is no manifest at %s."
				% (os.path.relpath(MANIFEST_PATH, REPO_ROOT).replace("\\", "/"),))
			print("If it was deleted, restore it (git checkout -- <path>) rather than")
			print("re-recording: a fresh manifest cannot tell a deliberate bump from a")
			print("forgotten one. To create the very first one, pass --seed.")
			return 1

		# A format change means the recorded fingerprints were produced by a different
		# algorithm, so "the fingerprint changed" carries no information about the wire
		# format and there is nothing to refuse on.
		if manifest is not None and manifest.get("_format") == FINGERPRINT_FORMAT:
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
