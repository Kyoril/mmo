# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Resolves the conflicts a merge of a fix with develop leaves behind, so a fix no longer parks
with "needs a manual merge" whenever develop touched the same file.

Two steps, in this order:

1. Game data (*.data): a structured three-way merge by entry and field (data_merge). Exact; no
   model involved. A value both sides changed differently stays a conflict.
2. Every other text file: a Claude agent edits the conflicted files in place. Its result is then
   checked without trusting it: no conflict markers, no file touched besides the conflicted ones,
   HEAD untouched. A second, read-only Claude stage reviews the resolution against both sides and
   must confirm that it keeps both and adds nothing else. The caller gates the merge result before
   anything is pushed.

Binary files other than game data cannot be merged and stay conflicts."""

import difflib
import os
import re
import tempfile

from . import data_merge
from .claude import REVIEW_TOOLS, ClaudeError

_MARKER = re.compile(r"^(<{7}|={7}|>{7}|\|{7})(\s|$)", re.MULTILINE)
DATA_SUFFIX = ".data"
# The review must see every resolution in full; a larger one is left to the maintainer.
REVIEW_LIMIT = 60000
MANUAL = "needs a manual merge"
# Where a protocol version lives, and what changes with it (tools/protocol_version_check.py).
VERSION_HEADERS = frozenset(("src/shared/game_protocol/game_protocol.h", "src/shared/auth_protocol/auth_protocol.h"))
PROTOCOL_FILES = VERSION_HEADERS | frozenset(("src/shared/protocol_fingerprint.json", "docs/protocol_versions.md"))
_VERSION_LINE = re.compile(r"^\s*constexpr\s+uint32\s+ProtocolVersion\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*;\s*$")


def _diff(before, after, before_name, after_name):
	return "".join(difflib.unified_diff(before.splitlines(True), after.splitlines(True), before_name, after_name, n=3))


def _text(data):
	return (data or b"").decode("utf-8", "replace")


def _only_version_changed(before, after):
	"""True when `after` differs from `before` only in a higher ProtocolVersion constant."""
	old, new = before.splitlines(), after.splitlines()
	if len(old) != len(new):
		return False
	changed = [(a, b) for a, b in zip(old, new) if a != b]
	if len(changed) != 1:
		return False
	matches = [_VERSION_LINE.match(line) for line in changed[0]]
	return all(matches) and int(matches[1].group(1), 0) > int(matches[0].group(1), 0)


def _split_marked(marked):
	"""The clean segments and conflict blocks of `git merge-file --diff3` output; None when the
	markers do not nest as git writes them."""
	clean, blocks = [[]], []
	state = block = None
	for line in marked.splitlines(True):
		bare = line.rstrip("\r\n")
		if state is None and line.startswith("<<<<<<< "):
			state, block = "ours", {"ours": [], "base": [], "theirs": []}
		elif state == "ours" and line.startswith("||||||| "):
			state = "base"
		elif state in ("ours", "base") and bare == "=======":
			state = "theirs"
		elif state == "theirs" and line.startswith(">>>>>>> "):
			blocks.append({side: "".join(lines) for side, lines in block.items()})
			clean.append([])
			state = None
		elif state is None:
			clean[-1].append(line)
		else:
			block[state].append(line)
	if state is not None or not blocks:
		return None
	return ["".join(lines) for lines in clean], blocks


def conflict_regions(marked, result, name, context=6):
	"""The review view of one resolved file: per conflict the base, OURS and THEIRS text, a few
	lines around it and what the resolution put there. None when the resolution changed anything
	outside the conflicts (the text git merged cleanly must come back unchanged, in order); the
	caller then shows full diffs instead."""
	split = _split_marked(marked)
	if split is None:
		return None
	clean, blocks = split
	if not result.startswith(clean[0]):
		return None
	position = len(clean[0])
	resolutions = []
	for index, segment in enumerate(clean[1:], 1):
		if index == len(clean) - 1:
			end = len(result) - len(segment)
			if end < position or not result.endswith(segment):
				return None
		else:
			end = result.find(segment, position) if segment else -1
			if end < 0:
				return None
		resolutions.append(result[position:end])
		position = end + len(segment)
	parts = ["=== {} ({} conflict(s); everything else is git's own merge, unchanged) ===".format(name, len(blocks))]
	for index, (block, resolution) in enumerate(zip(blocks, resolutions)):
		before = "".join(clean[index].splitlines(True)[-context:])
		after = "".join(clean[index + 1].splitlines(True)[:context])
		parts.append("--- conflict {} of {}\n--- context before\n{}--- BASE\n{}--- OURS\n{}--- THEIRS\n{}--- RESOLUTION\n{}"
			"--- context after\n{}".format(index + 1, len(blocks), before, block["base"], block["ours"], block["theirs"], resolution, after))
	return "\n".join(parts)


class ConflictResolver:
	"""`git(repo, *args, check=True, binary=False)` runs git (gitops.run_git); `runner` is the
	ClaudeRunner, None to resolve game data only; `budget_ok()` says whether two more Claude
	invocations fit today's budget."""

	def __init__(self, git, runner=None, prompts=None, schemas=None, budget_ok=None, timeout=3600, max_usd=5.0, log=None,
			decoder=None):
		self.run_git = git
		self.runner = runner
		self.prompts = prompts or {}
		self.schemas = schemas or {}
		self.budget_ok = budget_ok
		self.timeout = timeout
		self.max_usd = max_usd
		self.log = log or (lambda message: None)
		# data_diff.DataDecoder: gives the game-data merge field names and types; it works without one.
		self.decoder = decoder

	def _git(self, repo, *args):
		return self.run_git(repo, *args).stdout

	def _data_schema(self, path):
		if self.decoder is None:
			return None
		try:
			return self.decoder.schema(path)
		except Exception:  # the merge works without a schema, only less cleverly
			return None

	def _stages(self, repo, path):
		"""{stage: blob bytes} of an unmerged path (1 base, 2 ours, 3 theirs; missing = absent on that side)."""
		stages = {}
		for line in self._git(repo, "ls-files", "-u", "-z", "--", path).split("\0"):
			if not line:
				continue
			meta, _ = line.split("\t", 1)
			_, sha, stage = meta.split()
			stages[int(stage)] = self.run_git(repo, "cat-file", "blob", sha, binary=True).stdout
		return stages

	def _marked(self, repo, stages):
		"""git's own merge of the three stages, with diff3 conflict markers."""
		with tempfile.TemporaryDirectory(prefix="mmo_bugloop_merge_") as folder:
			files = []
			for stage in (2, 1, 3):
				path = os.path.join(folder, str(stage))
				with open(path, "wb") as handle:
					handle.write(stages[stage])
				files.append(path)
			merged = self.run_git(repo, "merge-file", "-p", "--diff3", "-L", "ours", "-L", "base", "-L", "theirs", *files,
				check=False, binary=True)
		return _text(merged.stdout)

	def _index(self, repo):
		entries = {}
		for line in self._git(repo, "ls-files", "-s", "-z").split("\0"):
			if not line:
				continue
			meta, path = line.split("\t", 1)
			entries.setdefault(path, []).append(meta)
		return entries

	def resolve(self, root, repo, prefix, paths, sides):
		"""Resolves and stages the unmerged `paths` (relative to `repo`, the repository with the merge
		in progress; `prefix` is its path below `root`, the worktree). `sides` names the two sides
		for the model: {"ours": ..., "theirs": ...}. Returns (reason, report): reason is "" when every
		path is resolved and staged, else why not (the caller aborts the merge)."""
		report = {"repo": prefix.rstrip("/") or ".", "data": [], "text": [], "sides": sides}
		text_paths = []
		stages_of = {}
		for path in paths:
			stages = self._stages(repo, path)
			stages_of[path] = stages
			shown = prefix + path
			if path.endswith(DATA_SUFFIX) and 2 in stages and 3 in stages:
				try:
					merged = data_merge.merge(stages.get(1), stages[2], stages[3], self._data_schema(path))
				except data_merge.DataConflict as conflict:
					return "{}: {} changed differently on both sides ({})".format(shown, conflict, MANUAL), report
				except ValueError:
					return "{}: not a protobuf data file ({})".format(shown, MANUAL), report
				with open(os.path.join(repo, *path.split("/")), "wb") as handle:
					handle.write(merged)
				self._git(repo, "add", "--", path)
				report["data"].append(shown)
				continue
			if any(b"\0" in blob[:8000] for blob in stages.values()):
				return "{}: binary file changed on both sides ({})".format(shown, MANUAL), report
			text_paths.append(path)
		if not text_paths:
			return "", report
		listed = ", ".join(prefix + path for path in text_paths[:5])
		if self.runner is None or "merge" not in self.prompts:
			return "merge conflict in {} ({})".format(listed, MANUAL), report
		if self.budget_ok is not None and not self.budget_ok():
			return "merge conflict in {} (no Claude budget left today to resolve it)".format(listed), report
		reason = self._resolve_text(root, repo, prefix, text_paths, stages_of, sides, report)
		return reason, report

	def _resolve_text(self, root, repo, prefix, paths, stages_of, sides, report):
		listed = ", ".join(prefix + path for path in paths[:5])
		head = self._git(repo, "rev-parse", "HEAD").strip()
		before = {path: meta for path, meta in self._index(repo).items() if path not in paths}
		lines = ["REPOSITORY: {}".format(prefix.rstrip("/") or "(the worktree root)"),
			"OURS (stage 2, HEAD): {}".format(sides.get("ours", "")),
			"THEIRS (stage 3, MERGE_HEAD): {}".format(sides.get("theirs", "")),
			"CONFLICTED FILES (paths relative to the worktree root):"]
		lines += ["- " + prefix + path for path in paths]
		try:
			self.runner.agent(self.prompts["merge"], "\n".join(lines), root, self.timeout, self.max_usd)
		except ClaudeError as error:
			return "merge conflict in {}: the resolver failed ({}; {})".format(listed, str(error)[:200], MANUAL)
		if self._git(repo, "rev-parse", "HEAD").strip() != head:
			return "merge conflict in {}: the resolver moved HEAD ({})".format(listed, MANUAL)
		for path in paths:
			self._git(repo, "add", "-A", "--", path)
		if self._git(repo, "ls-files", "-u", "-z").strip("\0"):
			return "merge conflict in {}: still unmerged after the resolver ({})".format(listed, MANUAL)
		sections = []
		companions = []
		if not prefix and any(path in PROTOCOL_FILES for path in paths):
			# Both sides bumped the protocol: the merged version must go above both, which touches the
			# version header even when git merged it cleanly, and the fingerprint and docs with it.
			for path in sorted(PROTOCOL_FILES - set(paths)):
				if self.run_git(repo, "diff", "--quiet", "--", path, check=False).returncode == 0:
					continue
				staged = _text(self.run_git(repo, "show", ":" + path, check=False, binary=True).stdout)
				with open(os.path.join(repo, *path.split("/")), "rb") as handle:
					changed = _text(handle.read())
				if path in VERSION_HEADERS and not _only_version_changed(staged, changed):
					return "the merge resolver changed {} beyond its ProtocolVersion constant ({})".format(path, MANUAL)
				self._git(repo, "add", "--", path)
				companions.append(path)
				report["text"].append({"path": path, "protocol_companion": True})
				sections.append("=== {} (not a conflict: changed to give the merged protocol its own version) ===\n{}".format(
					path, _diff(staged, changed, "merged/" + path, "resolved/" + path)))
		after = {path: meta for path, meta in self._index(repo).items() if path not in paths}
		touched = sorted(path for path in set(before) | set(after) if before.get(path) != after.get(path) and path not in companions)
		status = self._git(repo, "status", "--porcelain", "-z", "--ignore-submodules=all", "--untracked-files=all").split("\0")
		touched += sorted(entry[3:] for entry in status if len(entry) > 3 and (entry[1] != " " or entry.startswith("??")))
		if touched:
			return "the merge resolver changed files outside the conflict: {} ({})".format(", ".join(prefix + path for path in touched[:5]), MANUAL)
		for path in paths:
			stages = stages_of[path]
			full = os.path.join(repo, *path.split("/"))
			resolved = None
			if os.path.exists(full):
				with open(full, "rb") as handle:
					resolved = handle.read()
			if resolved is not None and _MARKER.search(_text(resolved)):
				return "{}{}: conflict markers left after the resolver ({})".format(prefix, path, MANUAL)
			base, ours, theirs, result = (_text(stages.get(1)), _text(stages.get(2)), _text(stages.get(3)), _text(resolved))
			kept = set(ours.splitlines()) | set(theirs.splitlines())
			novel = [line for line in result.splitlines() if line.strip() and line not in kept]
			report["text"].append({"path": prefix + path, "deleted": resolved is None, "novel_lines": len(novel)})
			name = prefix + path
			regions = None
			if resolved is not None and all(stage in stages for stage in (1, 2, 3)):
				regions = conflict_regions(self._marked(repo, stages), result, name)
			report["text"][-1]["review"] = "conflicts" if regions is not None else "full diffs"
			if regions is not None:
				sections.append(regions)
				continue
			sections.append("=== {} ===\n--- OURS changed (base -> ours)\n{}\n--- THEIRS changed (base -> theirs)\n{}\n"
				"--- RESOLUTION against ours\n{}\n--- RESOLUTION against theirs\n{}".format(name,
				_diff(base, ours, "base/" + name, "ours/" + name), _diff(base, theirs, "base/" + name, "theirs/" + name),
				_diff(ours, result, "ours/" + name, "resolved/" + name), _diff(theirs, result, "theirs/" + name, "resolved/" + name)))
		review_input = "OURS: {}\nTHEIRS: {}\n\n{}".format(sides.get("ours", ""), sides.get("theirs", ""), "\n\n".join(sections))
		if len(review_input) > REVIEW_LIMIT:
			return "merge conflict in {}: the resolution is too large to review ({} characters; {})".format(listed, len(review_input), MANUAL)
		try:
			review = self.runner.structured(self.prompts["merge_review"], review_input, self.schemas["merge_review"],
				REVIEW_TOOLS, root, self.timeout)
		except (ClaudeError, KeyError) as error:
			return "merge conflict in {}: the resolution review failed ({}; {})".format(listed, str(error)[:200], MANUAL)
		report["review"] = review
		problems = [str(problem)[:300] for problem in review.get("problems") or []]
		if not review.get("keeps_ours") or not review.get("keeps_theirs") or review.get("adds_unrelated") or problems:
			found = "; ".join(problems[:3]) or "it does not keep both sides"
			return "merge conflict in {}: the resolution review refused it: {} ({})".format(listed, found[:400], MANUAL)
		self.log("merge conflicts resolved in {}".format(listed))
		return ""
