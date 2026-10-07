# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Mechanical diff guard: decides, without any model, whether a bug-loop diff may ship without
human review. A triage or fixer that was talked into something cannot talk its way past this
file. Every rule errs towards parking: a parked fix costs a review, a bad ship costs players."""

import collections
import os
import re

from . import data_diff

FileChange = collections.namedtuple("FileChange", "path added removed binary")
Hunk = collections.namedtuple("Hunk", "path context removed added")

TEST_PREFIXES = ("src/tests/", "e2e/scenarios/", "tools/tests/")
DATA_PREFIX = "data/editor/data/"

PROTECTED_PREFIXES = (
	"src/login_server/", "src/shared/auth_protocol/", "src/shared/network/", "src/shared/game_protocol/",
	"src/shared/http/", "src/shared/web_services/", "src/shared/mysql_wrapper/", "src/shared/proto_data/",
	"data/login/", "data/realm/", "data/world/",
	"tools/gate/", "tools/bugs/", "tools/e2e/", "tools/protocol_version_check.py", "tools/shutdown_check.py",
	".github/", ".claude/", ".agents/", "deploy/", "cmake/", "deps/",
	"CLAUDE.md", "compose.yml", ".gitmodules", ".gitignore",
)
PROTECTED_NAMES = re.compile(
	r"(^|/)Dockerfile|(^|/)CMakeLists\.txt$|\.sql$|cheat|dev_(handlers|commands?)|gm_level|rate_limit"
	r"|(^|[/_])(auth|srp|session|connector|connection)[_./]|(^|/)login_",
	re.IGNORECASE)

CODE_SUFFIXES = (".cpp", ".h", ".hpp", ".inl", ".c", ".lua", ".py", ".ps1", ".js", ".xml")
SENSITIVE = re.compile(
	r"gm_?level|game_?master|mmo_with_dev_commands|cheat|rate_?limit|protocol_?version"
	r"|packetparseresult::(disconnect|block)|is_?admin",
	re.IGNORECASE)
CHECK_FUNCTION = re.compile(r"\b\w*(Check|Can|Allow|Valid|Verify|Permi)\w*\s*\(")
REMOVED_CHECK = re.compile(r"\bif\s*\(.*\b\w*(Check|Can|Allow|Valid|Verify|Permi|Has)\w*\s*\(")
RETURN_TRUE = re.compile(r"\breturn\s+true\s*;")
REJECTION = re.compile(r"\breturn\s+(false|PacketParseResult::\w+)\s*;")
TUNED = re.compile(r"max|limit|\bcap|cooldown|cost|price|chance|\brate|reward|\bxp|money|gold|drop", re.IGNORECASE)
NUMBER = re.compile(r"\d+(\.\d+)?")

# Data whose numbers are game economy or player power. Only text may change there unreviewed.
ECONOMY_DATA = frozenset((
	"items", "item_sets", "item_loot", "unit_loot", "object_loot", "skinning_loot", "fishing_loot",
	"pickpocketing_loot", "quests", "units", "unit_classes", "spells", "talents", "talent_tabs",
	"classes", "races", "levels", "skills", "proficiencies", "vendors", "trainers", "factions",
	"faction_templates", "triggers", "conditions", "variables", "objects", "gtvalues",
	"stat_calculations", "stat_constants", "combat_settings",
))


def parse_unified_diff(text):
	hunks = []
	path = minus_path = None
	current = None
	for line in text.splitlines():
		if line.startswith("diff --git "):
			path = minus_path = None
			current = None
			continue
		if current is None:
			if line.startswith("--- "):
				minus_path = line[6:] if line.startswith("--- a/") else None
			elif line.startswith("+++ "):
				path = line[6:] if line.startswith("+++ b/") else minus_path
			elif line.startswith("@@"):
				current = _new_hunk(hunks, path, line)
			continue
		if line.startswith("@@"):
			current = _new_hunk(hunks, path, line)
		elif line.startswith("-"):
			current.removed.append(line[1:])
		elif line.startswith("+"):
			current.added.append(line[1:])
	return hunks


def _new_hunk(hunks, path, header):
	end = header.find("@@", 2)
	hunk = Hunk(path, header[end + 2:].strip() if end >= 0 else "", [], [])
	hunks.append(hunk)
	return hunk


def _protected(path):
	return path.startswith(PROTECTED_PREFIXES) or bool(PROTECTED_NAMES.search(path))


def _is_test(path):
	return path.startswith(TEST_PREFIXES)


def _content_reasons(hunks):
	reasons = []
	for hunk in hunks:
		if not hunk.path or _is_test(hunk.path) or not hunk.path.lower().endswith(CODE_SUFFIXES):
			continue
		for line in hunk.removed + hunk.added:
			if SENSITIVE.search(line):
				reasons.append("{}: touches a security-sensitive identifier: {}".format(hunk.path, line.strip()[:120]))
				break
		if CHECK_FUNCTION.search(hunk.context) and any(RETURN_TRUE.search(line) for line in hunk.added):
			reasons.append("{}: a check function now returns true ({})".format(hunk.path, hunk.context[:80]))
		kept = {line.strip() for line in hunk.added}
		for line in hunk.removed:
			stripped = line.strip()
			if stripped in kept:
				continue
			if REMOVED_CHECK.search(line):
				reasons.append("{}: removed a check condition: {}".format(hunk.path, stripped[:120]))
			elif REJECTION.search(line):
				reasons.append("{}: removed a rejection path: {}".format(hunk.path, stripped[:120]))
		for old, new in zip(hunk.removed, hunk.added):
			if old != new and NUMBER.sub("#", old) == NUMBER.sub("#", new) and TUNED.search(old):
				reasons.append("{}: changed a tuned value: {} -> {}".format(hunk.path, old.strip()[:80], new.strip()[:80]))
	return reasons


def _jsonable(value):
	return value if isinstance(value, (str, int, float, bool)) or value is None else repr(value)


def _data_reasons(path, load_data, decoder, max_entries, report):
	if decoder is None or load_data is None:
		return ["{}: no decoder for game data".format(path)]
	stem = os.path.splitext(os.path.basename(path))[0]
	before, after = load_data(path)
	if before is None or after is None:
		return ["{}: data file added or removed".format(path)]
	try:
		old = decoder.decode(stem, before)
		new = decoder.decode(stem, after)
	except Exception as error:  # any decode failure means the change cannot be judged
		return ["{}: cannot decode ({})".format(path, error)]
	if old is None or new is None:
		return ["{}: no schema to decode it".format(path)]
	changes = data_diff.diff_leaves(data_diff.leaf_items(old), data_diff.leaf_items(new))
	entries = data_diff.changed_entries(changes)
	report[path] = {
		"entries": sorted(entries),
		"changes": [dict(change, before=_jsonable(change["before"]), after=_jsonable(change["after"])) for change in changes[:50]],
	}
	reasons = []
	if len(entries) > max_entries:
		reasons.append("{}: {} entries changed (limit {})".format(path, len(entries), max_entries))
	if stem in ECONOMY_DATA:
		numeric = [change["path"] for change in changes if not change["text"]]
		if numeric:
			reasons.append("{}: non-text change in economy data: {}".format(path, ", ".join(numeric[:5])))
	return reasons


def evaluate(changes, diff_text, load_data=None, decoder=None, max_lines=150, max_entries=5):
	reasons = []
	data = {}
	changed_lines = 0
	for change in changes:
		path = change.path
		if _protected(path):
			reasons.append("{}: protected path".format(path))
		if path.startswith(DATA_PREFIX) and path.endswith(".data"):
			reasons.extend(_data_reasons(path, load_data, decoder, max_entries, data))
			continue
		if change.binary:
			reasons.append("{}: binary file changed".format(path))
			continue
		if not _is_test(path):
			changed_lines += change.added + change.removed
	if not changes:
		reasons.append("empty diff")
	if changed_lines > max_lines:
		reasons.append("{} changed lines outside tests (limit {})".format(changed_lines, max_lines))
	reasons.extend(_content_reasons(parse_unified_diff(diff_text)))
	return {"auto_ship_allowed": not reasons, "reasons": reasons, "changed_lines": changed_lines, "data": data}
