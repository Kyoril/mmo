# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Mechanical diff guard: decides, without any model, whether a bug-loop diff may ship without
human review. A triage or fixer that was talked into something cannot talk its way past this
file. Every rule errs towards parking: a parked fix costs a review, a bad ship costs players."""

import collections
import os
import re

from . import data_diff

FileChange = collections.namedtuple("FileChange", "path added removed binary")
# old_start/old_count/new_start/new_count come from the "@@ -a,b +c,d @@" header; new_file and
# deleted_file mark a side that legitimately does not exist; unchanged holds context lines and
# problems anything the parser could not account for (each one parks the diff).
Hunk = collections.namedtuple("Hunk",
	"path context removed added old_start old_count new_start new_count new_file deleted_file unchanged problems",
	defaults=(0, 0, 0, 0, False, False, (), ()))

# Test roots the regression proof runs from. Their files skip the production allow-list and the
# changed-line limit, but not the rules that could smuggle code in (see _test_line_reasons).
TEST_PREFIXES = ("src/tests/", "e2e/scenarios/")
# Python tool tests run on CI and in every gate run with the operator's credentials; a fix never
# ships them unreviewed.
TOOL_TESTS_PREFIX = "tools/tests/"
MAX_TEST_LINES = 600
DATA_PREFIX = "data/editor/data/"
SUBMODULE_ROOTS = ("data/client", "data/editor")

# Only these roots may ship unreviewed (besides tests). Everything else parks.
ALLOWED_ROOTS = (
	"src/shared/game/", "src/shared/game_server/", "src/shared/game_client/", "src/world_server/",
	"src/realm_server/", "src/mmo_client/", "src/shared/frame_ui/", "src/shared/scene_graph/",
	"src/shared/graphics/", "src/shared/graphics_d3d11/", "src/shared/graphics_metal/", "src/shared/terrain/",
	"src/shared/deferred_shading/", "src/shared/audio/", "src/shared/math/", "src/shared/client_data/",
	"data/client/Interface/", "data/client/Locales/", "data/editor/data/", "data/scripts/",
)

# An additional deny layer inside (and outside) the allow-list.
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

CODE_SUFFIXES = (
	".cpp", ".cc", ".cxx", ".h", ".hpp", ".hxx", ".inl", ".c", ".mm", ".lua", ".py", ".ps1", ".psm1",
	".sh", ".bat", ".cmd", ".js", ".ts", ".tsx", ".xml", ".toc",
)
C_FAMILY_SUFFIXES = (".cpp", ".cc", ".cxx", ".h", ".hpp", ".hxx", ".inl", ".c", ".mm")

# File types each allowed root may ship (tests excepted). Anything else could be pulled into the
# build or the client by an #include, a require or a .toc line while no content rule looks at it.
ROOT_SUFFIXES = (
	("src/", C_FAMILY_SUFFIXES),
	("data/client/Interface/", (".lua", ".xml", ".toc")),
	("data/client/Locales/", (".txt",)),
	("data/editor/data/", (".data",)),
	("data/scripts/", (".lua",)),
)
# File types each test root may ship. A C-family test may only #include headers (TEST_INCLUDE).
TEST_SUFFIXES = (
	("src/tests/", C_FAMILY_SUFFIXES),
	("e2e/scenarios/", (".lua",)),
)
HEADER_SUFFIXES = (".h", ".hpp", ".hxx", ".inl")
# Server-authoritative code: here an edit to reward, loot or economy logic needs a human, literals or not.
SERVER_ROOTS = ("src/shared/game/", "src/shared/game_server/", "src/world_server/", "src/realm_server/", "data/scripts/")
# Client UI Lua cannot cheat the server, so a numeric condition alone does not park there.
CLIENT_UI_ROOT = "data/client/Interface/"


def _word_pattern(words):
	"""Matches any of the words at the start of an identifier part: after a non-letter (m_xp, xp) or as
	a capitalised camelCase part (kXp, RewardXp), but not inside a word (expr, Backdrop, Generate)."""
	return r"(?:(?<![A-Za-z])|(?=[A-Z]))(?i:" + "|".join(words) + r")"


ECONOMY_WORDS = ("xp(?!(?-i:[a-z]))", "experience", "reward", "money", "gold", "loot", "drop", "credit", "cutoff",
	"price", "cost", "honor", "reputation", "stack", "charges")
# A camel-anchored economy word, or one buried in a lowercase protobuf accessor (minlevelxp(), rewardmoney()).
ECONOMY = re.compile(_word_pattern(ECONOMY_WORDS) + r"|(?<!\w)[a-z0-9_]*(?:xp|" + "|".join(ECONOMY_WORDS[1:]) + r")\s*\(")
SENSITIVE = re.compile(
	r"gm_?level|game_?master|mmo_with_dev_commands|cheat|rate_?limit|protocol_?version"
	r"|packetparseresult::(disconnect|block)|is_?admin",
	re.IGNORECASE)
CHECK_STEMS = r"(Check|Can|Allow|Valid|Verify|Permi|Is)"
CHECK_FUNCTION = re.compile(r"\b\w*" + CHECK_STEMS + r"\w*\s*\(")
CHECK_CALL = re.compile(r"\b\w*(Check|Can|Allow|Valid|Verify|Permi|Has|Is)\w*\s*\(")
REMOVED_CHECK = re.compile(r"\bif\s*\(.*\b\w*(Check|Can|Allow|Valid|Verify|Permi|Has|Is)\w*\s*\(")
# Lua conditions have no parentheses: "if not player:HasItem(KEY) then return end".
LUA_CONDITION = re.compile(r"\b(if|elseif)\b")
LUA_NOT = re.compile(r"\bnot\s")
RETURN_TRUE = re.compile(r"\breturn\s+true\s*;")
# An added early success return in server code skips whatever check follows it, however far away.
EARLY_RETURN = re.compile(r"\breturn\s+(true\b|PacketParseResult::\w+)")
FAILED_RESULT = r"(\w+::)+Failed\w*|[\w:]*Error\b"
REJECTION = re.compile(r"\breturn\s+(false\b|PacketParseResult::\w+|" + FAILED_RESULT + r")")
# What makes surrounding code worth a human look: security identifiers and explicit rejections. Plain
# "return false" and ordinary IsAlive()/HasAura() calls are everywhere and would park most server code.
NEAR_CHECK = re.compile(r"PacketParseResult|\breturn\s+(" + FAILED_RESULT + r")")
TUNED = re.compile(
	r"(?i:max|limit|cooldown|cost|price|chance|reward|money|gold|drop"
	r"|range|dist|radius|level|duration|delay|speed)|" + _word_pattern(("cap", "rate", "xp(?!(?-i:[a-z]))")))
# A numeric literal is a digit (or ".digit") that does not continue an identifier: 5, 0.05f, .5f, 0x10.
NUMERIC_LITERAL = re.compile(r"(?<![\w.])(\d|\.\d)")
CONDITION = re.compile(r"\b(if|elseif|while|until)\b|\?.*:|<=|>=|==|!=|~=|<|>")
CONDITION_NOISE = re.compile(r"->|<<|>>")
PREPROCESSOR = re.compile(
	r"^\s*(#|%:)\s*(if|ifdef|ifndef|else|elif|elifdef|elifndef|endif|define|undef|include|include_next|pragma"
	r"|line|import)\b|\b_Pragma\b")
# Lua that pulls in or compiles other code.
LUA_LOADER = re.compile(r"\b(require|dofile|loadstring|loadfile)\b|\bload\s*\(")
GOTO = re.compile(r"\bgoto\b")
# The one preprocessor directive a test may change: an include of a header by a plain path.
TEST_INCLUDE = re.compile(r'^\s*#\s*include\s*(?:<([^<>"]+)>|"([^<>"]+)")\s*(?://.*)?$')
# Line separators str.splitlines() honours, other control characters, and bidi overrides.
CONTROL = re.compile("[\x00-\x08\x0a-\x1f\x7f\x85\u2028\u2029\u202a-\u202e\u2066-\u2069]")
HUNK_HEADER = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@ ?(.*)$")
LUA_BLOCK_OPEN = re.compile(r"--\[=*\[")
LUA_BLOCK_CLOSE = re.compile(r"\]=*\]")
C_COMMENT_LINE = re.compile(r"^\s*//")
LUA_COMMENT_LINE = re.compile(r"^\s*--(?!\[=*\[)")

# Lines of surrounding code inspected on each side of a hunk for a security-sensitive check.
NEAR_WINDOW = 20

# Data whose numbers are game economy or player power. Only text may change there unreviewed.
ECONOMY_DATA = frozenset((
	"items", "item_sets", "item_loot", "unit_loot", "object_loot", "skinning_loot", "fishing_loot",
	"pickpocketing_loot", "quests", "units", "unit_classes", "spells", "talents", "talent_tabs",
	"classes", "races", "levels", "skills", "proficiencies", "vendors", "trainers", "factions",
	"faction_templates", "triggers", "conditions", "variables", "objects", "gtvalues",
	"stat_calculations", "stat_constants", "combat_settings", "maps", "gossip_menus",
))


def _split_lines(text):
	"""Splits on "\\n" only: str.splitlines() also breaks on form feeds, lone carriage returns and
	Unicode separators, which would hide the rest of a changed line from every rule."""
	lines = text.split("\n")
	if lines and lines[-1] == "":
		lines.pop()
	return [line[:-1] if line.endswith("\r") else line for line in lines]


class _HunkBuilder:
	def __init__(self, path, header, new_file, deleted_file):
		self.path = path
		self.new_file = new_file
		self.deleted_file = deleted_file
		self.removed = []
		self.added = []
		self.unchanged = []
		self.problems = []
		match = HUNK_HEADER.match(header)
		if match:
			self.old_start = int(match.group(1))
			self.old_count = int(match.group(2)) if match.group(2) is not None else 1
			self.new_start = int(match.group(3))
			self.new_count = int(match.group(4)) if match.group(4) is not None else 1
			self.context = match.group(5).strip()
		else:
			self.old_start = self.old_count = self.new_start = self.new_count = 0
			self.context = ""
			self.problems.append("malformed hunk header: " + header[:80])
		self.old_left = self.old_count
		self.new_left = self.new_count

	def feed(self, line):
		marker, body = line[:1], line[1:]
		if marker == "-":
			self.removed.append(body)
			self.old_left -= 1
		elif marker == "+":
			self.added.append(body)
			self.new_left -= 1
		elif marker == " ":
			self.unchanged.append(body)
			self.old_left -= 1
			self.new_left -= 1
		elif marker == "\\":
			return
		else:
			self.problems.append("malformed hunk: unexpected line: " + repr(line[:80]))

	def build(self):
		if self.old_left != 0 or self.new_left != 0:
			self.problems.append("malformed hunk: line counts do not match its header")
		return Hunk(self.path, self.context, self.removed, self.added, self.old_start, self.old_count,
			self.new_start, self.new_count, self.new_file, self.deleted_file, self.unchanged, self.problems)


def parse_unified_diff(text):
	"""Parses `git diff` output into hunks. A hunk whose path cannot be determined (quoted or
	mismatching headers, missing headers) gets path None; evaluate() parks it."""
	hunks = []
	plus_path = minus_path = None
	saw_plus = False
	new_file = deleted_file = False
	current = None

	def finish():
		if current is not None:
			hunks.append(current.build())

	def hunk_path():
		if not saw_plus:
			return None
		if deleted_file:
			return minus_path
		if new_file:
			return plus_path
		return plus_path if plus_path is not None and plus_path == minus_path else None

	for line in _split_lines(text):
		if line.startswith("diff --git "):
			finish()
			current = None
			plus_path = minus_path = None
			saw_plus = new_file = deleted_file = False
			continue
		if line.startswith("@@"):
			finish()
			current = _HunkBuilder(hunk_path(), line, new_file, deleted_file)
			continue
		if current is not None:
			current.feed(line)
			continue
		if line.startswith("--- "):
			new_file = line == "--- /dev/null"
			minus_path = line[6:] if line.startswith("--- a/") else None
		elif line.startswith("+++ "):
			saw_plus = True
			deleted_file = line == "+++ /dev/null"
			plus_path = line[6:] if line.startswith("+++ b/") else None
	finish()
	return hunks


def _protected(path):
	return path.startswith(PROTECTED_PREFIXES) or bool(PROTECTED_NAMES.search(path))


def _is_test(path):
	return path.startswith(TEST_PREFIXES)


def _normal_path(path):
	parts = path.split("/")
	return bool(path) and "\\" not in path and not path.startswith("/") and ".." not in parts and "." not in parts \
		and "" not in parts


def _allowed(path):
	return _normal_path(path) and (_is_test(path) or path.startswith(ALLOWED_ROOTS))


def _suffix_allowed(path):
	lower = path.lower()
	roots = TEST_SUFFIXES if _is_test(path) else ROOT_SUFFIXES
	return any(path.startswith(root) and lower.endswith(suffixes) for root, suffixes in roots)


def _plain_header(match):
	angle, quoted = match.group(1), match.group(2)
	target = angle if angle is not None else quoted
	parts = target.split("/")
	if not target or ".." in parts or target.startswith("/") or "\\" in target or ":" in target:
		return False
	# <vector> has no suffix; anything with one must be a header.
	return target.lower().endswith(HEADER_SUFFIXES) or (angle is not None and "." not in parts[-1])


def _test_line_reasons(hunk):
	"""The rules a test hunk keeps: tests never ship to players, but they are compiled and run by the
	gate and CI, so they must not pull in other files or hide text from the reviewer."""
	path = hunk.path
	reasons = []
	lua = path.lower().endswith(".lua")
	for line in hunk.removed + hunk.added:
		if CONTROL.search(line):
			reasons.append("{}: control character in a changed line: {}".format(path, ascii(line)[:120]))
			break
	for line in _unmatched(hunk.removed, hunk.added) + _unmatched(hunk.added, hunk.removed):
		if PREPROCESSOR.search(line):
			include = TEST_INCLUDE.match(line)
			if include is None or not _plain_header(include):
				reasons.append("{}: preprocessor directive changed: {}".format(path, _short(line)))
		if lua and LUA_LOADER.search(line) and not _comment_only(line, lua):
			reasons.append("{}: loads code (require/dofile/load): {}".format(path, _short(line)))
		if not lua and line.rstrip().endswith("\\"):
			reasons.append("{}: line continuation: {}".format(path, _short(line)))
	return reasons


def _is_code(path):
	return path.lower().endswith(CODE_SUFFIXES)


def _short(line):
	return line.strip()[:120]


def _comment_only(line, lua):
	"""A whole-line // (C family) or -- (Lua; in C++ that is a decrement) comment cannot change
	behaviour, unless a trailing backslash splices the next line into it."""
	pattern = LUA_COMMENT_LINE if lua else C_COMMENT_LINE
	return bool(pattern.match(line)) and not line.rstrip().endswith("\\")


def _unmatched(lines, others):
	"""The lines without a counterpart on the other side (a re-indented or moved line is kept). Counts
	multiplicity, so duplicating a line (one removed, two added) leaves one added line to judge."""
	available = collections.Counter(line.strip() for line in others)
	result = []
	for line in lines:
		if available[line.strip()] > 0:
			available[line.strip()] -= 1
		else:
			result.append(line)
	return result


def _line_reasons(hunk):
	"""Rules that judge the changed lines of one non-test code hunk."""
	path = hunk.path
	reasons = []
	removed_new = _unmatched(hunk.removed, hunk.added)
	added_new = _unmatched(hunk.added, hunk.removed)
	for line in hunk.removed + hunk.added:
		if CONTROL.search(line):
			reasons.append("{}: control character in a changed line: {}".format(path, ascii(line)[:120]))
			break
	for line in hunk.removed + hunk.added:
		if SENSITIVE.search(line):
			reasons.append("{}: touches a security-sensitive identifier: {}".format(path, _short(line)))
			break
	if CHECK_FUNCTION.search(hunk.context) and any(RETURN_TRUE.search(line) for line in hunk.added):
		reasons.append("{}: a check function now returns true ({})".format(path, hunk.context[:80]))
	lua = path.lower().endswith(".lua")
	server = path.startswith(SERVER_ROOTS)
	for line in removed_new:
		if REMOVED_CHECK.search(line) or (lua and LUA_CONDITION.search(line)
				and (CHECK_CALL.search(line) or LUA_NOT.search(line))):
			reasons.append("{}: removed a check condition: {}".format(path, _short(line)))
		elif REJECTION.search(line):
			reasons.append("{}: removed a rejection path: {}".format(path, _short(line)))
	for line in removed_new + added_new:
		if PREPROCESSOR.search(line):
			reasons.append("{}: preprocessor directive changed: {}".format(path, _short(line)))
		if lua and LUA_LOADER.search(line):
			reasons.append("{}: loads code (require/dofile/load): {}".format(path, _short(line)))
		if server and ECONOMY.search(line) and not _comment_only(line, lua):
			reasons.append("{}: touches reward or economy logic: {}".format(path, _short(line)))
	for line in added_new:
		if GOTO.search(line):
			reasons.append("{}: added a goto: {}".format(path, _short(line)))
		if server and EARLY_RETURN.search(line):
			reasons.append("{}: added an early return: {}".format(path, _short(line)))
		if path.lower().endswith(C_FAMILY_SUFFIXES):
			if line.count("/*") != line.count("*/"):
				reasons.append("{}: unbalanced block comment: {}".format(path, _short(line)))
			if line.rstrip().endswith("\\"):
				reasons.append("{}: line continuation: {}".format(path, _short(line)))
		elif lua:
			opened = LUA_BLOCK_OPEN.search(line)
			if (opened and not LUA_BLOCK_CLOSE.search(line, opened.end())) or line.strip().startswith(("--]", "]]")):
				reasons.append("{}: unbalanced block comment: {}".format(path, _short(line)))
	for line in removed_new:
		if path.lower().endswith(C_FAMILY_SUFFIXES) and line.rstrip().endswith("\\"):
			reasons.append("{}: removed a line continuation: {}".format(path, _short(line)))
	for line in removed_new + added_new:
		if not NUMERIC_LITERAL.search(line):
			continue
		if TUNED.search(line):
			reasons.append("{}: changed a tuned value: {}".format(path, _short(line)))
		elif not path.startswith(CLIENT_UI_ROOT) and CONDITION.search(CONDITION_NOISE.sub(" ", line)):
			reasons.append("{}: changed a numeric condition: {}".format(path, _short(line)))
	return reasons


def _window(lines, start, count):
	"""The lines of a hunk span (1-based start; count 0 means "after line start") plus NEAR_WINDOW
	lines on each side."""
	begin = max(0, start - 1 - NEAR_WINDOW)
	end = max(begin, start - 1 + count + NEAR_WINDOW)
	return lines[begin:end]


def _surrounding_reasons(hunk, load_data, cache):
	"""An insertion (#if 0, an early return, a goto) can disable a check without touching it, so any
	code edit close to a security-sensitive check is reviewed by a human."""
	path = hunk.path
	if load_data is None:
		return ["{}: cannot inspect surrounding code (no loader)".format(path)]
	if path not in cache:
		try:
			before, after = load_data(path)
		except Exception as error:  # an unreadable side cannot be judged
			cache[path] = error
		else:
			decode = lambda data: None if data is None else data.decode("utf-8", errors="replace").split("\n")
			cache[path] = (decode(before), decode(after))
	loaded = cache[path]
	if isinstance(loaded, Exception):
		return ["{}: cannot inspect surrounding code ({})".format(path, loaded)]
	before, after = loaded
	sides = []
	if not hunk.new_file:
		sides.append((before, hunk.old_start, hunk.old_count, "before"))
	if not hunk.deleted_file:
		sides.append((after, hunk.new_start, hunk.new_count, "after"))
	for lines, start, count, side in sides:
		if lines is None:
			return ["{}: cannot inspect surrounding code ({} side missing)".format(path, side)]
		for line in _window(lines, start, count):
			if SENSITIVE.search(line) or NEAR_CHECK.search(line):
				return ["{}: edit near a security-sensitive check: {}".format(path, _short(line))]
	return []


def _hunk_reasons(hunks, listed, load_data):
	reasons = []
	cache = {}
	for hunk in hunks:
		if not hunk.path:
			reasons.append("unparseable diff header (hunk {})".format(hunk.context[:80] or "without context"))
			continue
		path = hunk.path
		if path not in listed:
			reasons.append("{}: hunk for a path not in the change list".format(path))
		for problem in hunk.problems:
			reasons.append("{}: {}".format(path, problem))
		if any("Subproject commit" in line for line in hunk.removed + hunk.added + list(hunk.unchanged)):
			reasons.append("{}: Subproject commit line in diff (submodule pointer)".format(path))
		if _is_test(path):
			reasons.extend(_test_line_reasons(hunk))
			continue
		if not _is_code(path):
			continue
		reasons.extend(_line_reasons(hunk))
		reasons.extend(_surrounding_reasons(hunk, load_data, cache))
	return reasons


def _jsonable(value):
	return value if isinstance(value, (str, int, float, bool)) or value is None else repr(value)


def _data_reasons(path, load_data, decoder, max_entries, report):
	if decoder is None or load_data is None:
		return ["{}: no decoder for game data".format(path)]
	stem = os.path.splitext(os.path.basename(path))[0]
	try:
		before, after = load_data(path)
	except Exception as error:  # an unreadable file cannot be judged
		return ["{}: cannot load ({})".format(path, error)]
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


def is_production(path):
	"""Anything that is not a regression test counts as a production change (tool tests too)."""
	return not _is_test(path)


def evaluate(changes, diff_text, load_data=None, decoder=None, max_lines=150, max_entries=5, max_test_lines=MAX_TEST_LINES):
	reasons = []
	data = {}
	changed_lines = 0
	test_lines = 0
	hunks = parse_unified_diff(diff_text)
	hunk_paths = {hunk.path for hunk in hunks if hunk.path}
	for change in changes:
		path = change.path
		if path in SUBMODULE_ROOTS:
			reasons.append("{}: submodule pointer changed".format(path))
		if path.startswith(TOOL_TESTS_PREFIX):
			reasons.append("{}: tools/tests changes run on CI and gates; never auto-shipped".format(path))
		if _protected(path):
			reasons.append("{}: protected path".format(path))
		if not _allowed(path):
			reasons.append("{}: outside the auto-ship allow-list".format(path))
		elif not _suffix_allowed(path):
			reasons.append("{}: file type not allowed to auto-ship here".format(path))
		if path.startswith(DATA_PREFIX) and path.endswith(".data"):
			reasons.extend(_data_reasons(path, load_data, decoder, max_entries, data))
			continue
		if change.binary:
			reasons.append("{}: binary file changed".format(path))
			continue
		if change.added + change.removed > 0 and path not in hunk_paths:
			reasons.append("diff text missing for {}".format(path))
		if _is_test(path):
			test_lines += change.added + change.removed
		else:
			changed_lines += change.added + change.removed
	if not changes:
		reasons.append("empty diff")
	elif not any(is_production(change.path) for change in changes):
		reasons.append("no production change: a diff of tests alone fixes nothing")
	if changed_lines > max_lines:
		reasons.append("{} changed lines outside tests (limit {})".format(changed_lines, max_lines))
	if test_lines > max_test_lines:
		reasons.append("{} changed test lines (limit {})".format(test_lines, max_test_lines))
	reasons.extend(_hunk_reasons(hunks, {change.path for change in changes}, load_data))
	return {"auto_ship_allowed": not reasons, "reasons": reasons, "changed_lines": changed_lines, "data": data}
