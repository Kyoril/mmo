#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Writes player-facing patch notes for a nightly release.

A free model on GitHub Models (authenticated with the workflow's GITHUB_TOKEN, no paid key)
turns the commits, diff stats and game data changes into classic-MMO style notes. Any failure
falls back to notes built from the cleaned commit subjects: a release never fails here.

Output: patch_notes.json ({headline, sections: [{title, bullets}], source, model}) and a
Markdown rendering for the GitHub release body.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ENDPOINT = "https://models.github.ai/inference/chat/completions"
DEFAULT_MODEL = "openai/gpt-4o-mini"
# The free tier accepts about 8000 input tokens per request; ~4 characters per token leaves
# room for the instructions.
CONTEXT_BUDGET = 18000
BODY_LIMIT = 400
MAX_COMMITS = 200
TIMEOUT_S = 120

SECTIONS = (
	"General", "Classes", "Quests", "Items", "Creatures and NPCs", "World", "Dungeons",
	"User Interface", "Audio and Visuals", "Launcher", "Bug Fixes",
)
MAX_BULLETS = 15
BULLET_LIMIT = 300
HEADLINE_LIMIT = 200

_PREFIX = re.compile(r"^(\w+)(\(([^)]*)\))?!?:\s*")
_INTERNAL_TYPES = {"ci", "test", "tests", "build", "chore", "docs", "style", "refactor"}
_INTERNAL_SCOPES = {"gate", "ci", "deployer", "docker", "e2e", "tests", "test", "release", "nightly", "tools", "skills", "worldkit", "bots", "version"}
_INTERNAL_PATHS = (".github/", ".claude/", ".agents/", "tools/", "deploy/", "docs/", "e2e/", "src/tests/", "bruno/", "docker/", "Dockerfile", "compose", "CLAUDE.md", "README")

_AREAS = (
	("src/launcher/", "Launcher"),
	("data/client", "Client art, sound and interface data"),
	("data/editor", "Game data (quests, items, spells, creatures)"),
	("src/mmo_client/", "Game client"),
	("src/shared/frame_ui/", "Game client"),
	("src/shared/game_client/", "Game client"),
	("src/world_server/", "Gameplay (server)"),
	("src/shared/game_server/", "Gameplay (server)"),
	("src/shared/game/", "Gameplay (shared rules)"),
	("src/realm_server/", "Characters and realm"),
	("src/login_server/", "Login"),
	("src/shared/", "Engine"),
	("data/scripts/", "Server scripts"),
)

INSTRUCTIONS = """You write the patch notes for a nightly update of "Alestia Online", a fantasy MMORPG, in the style of classic World of Warcraft patch notes.

Readers are players. Use their language: what they will notice in game. Never mention commit hashes, file names, code identifiers, tests, build tooling, CI, servers' internals or refactoring. Merge related changes into one bullet. Do not invent features, numbers or names that the input does not support; when unsure, stay general. Prefer concrete names from the data changes (quest, item, spell, creature and zone names).

Answer with JSON only:
{"headline": "<one sentence, at most 140 characters, the most exciting change>",
 "sections": [{"title": "<section>", "bullets": ["<one change, one sentence>", ...]}]}

Allowed section titles, in this order, omit empty ones: %s. For class-specific changes you may use "Classes: <Class>" (for example "Classes: Mage"). At most %d bullets per section. Fixes go under "Bug Fixes", phrased as "Fixed an issue where ..." or "<Thing> now ...".
""" % (", ".join(SECTIONS), MAX_BULLETS)


def _git(repo, *args):
	return subprocess.run(["git", "-C", repo] + list(args), check=True, capture_output=True, text=True,
		encoding="utf-8", errors="replace").stdout


def _strip_trailers(body):
	return "\n".join(line for line in body.splitlines()
		if not line.lower().startswith(("co-authored-by:", "signed-off-by:"))).strip()


def collect_commits(repo, previous, commit, limit=MAX_COMMITS):
	"""[{subject, body, files}] of the non-merge commits in previous..commit, newest first."""
	revs = ["{}..{}".format(previous, commit)] if previous else ["-n", "20", commit]
	text = _git(repo, "log", "--no-merges", "--name-only", "--format=%x1e%s%x1f%b%x1f", *revs)
	commits = []
	for record in text.split("\x1e"):
		if not record.strip():
			continue
		subject, _, rest = record.partition("\x1f")
		body, _, files = rest.partition("\x1f")
		commits.append({
			"subject": subject.strip(),
			"body": _strip_trailers(body),
			"files": [line.strip() for line in files.splitlines() if line.strip()],
		})
	return commits[:limit]


def commit_kind(subject):
	"""(type, scope) of a conventional commit subject, lower case; ("", "") otherwise."""
	match = _PREFIX.match(subject)
	if not match:
		return "", ""
	return match.group(1).lower(), (match.group(3) or "").lower()


def clean_subject(subject):
	stripped = _PREFIX.sub("", subject).strip()
	if not stripped:
		return subject.strip()
	return stripped[:1].upper() + stripped[1:]


def is_internal(commit):
	"""True for commits players never notice: tooling, CI, tests, docs, deployment."""
	kind, scope = commit_kind(commit["subject"])
	if kind in _INTERNAL_TYPES or scope in _INTERNAL_SCOPES:
		return True
	files = commit.get("files") or []
	return bool(files) and all(path.startswith(_INTERNAL_PATHS) for path in files)


def area_stats(commits):
	"""{area: changed file count} over the player-facing commits."""
	stats = {}
	for commit in commits:
		for path in commit.get("files") or []:
			for prefix, area in _AREAS:
				if path.startswith(prefix):
					stats[area] = stats.get(area, 0) + 1
					break
	return dict(sorted(stats.items(), key=lambda item: -item[1]))


def _format_commit(commit, with_body):
	text = "- " + clean_subject(commit["subject"])
	if with_body and commit["body"]:
		body = " ".join(commit["body"].split())
		text += "\n  " + (body[:BODY_LIMIT] + "..." if len(body) > BODY_LIMIT else body)
	return text


def build_context(commits, data, budget=CONTEXT_BUDGET):
	"""The model input, condensed to `budget` characters: data changes first, then commits."""
	player = [c for c in commits if not is_internal(c)]
	parts = []
	catalogs = data.get("catalogs") or {}
	if catalogs:
		lines = ["## New and renamed game data"]
		for key, diff in catalogs.items():
			if diff["added"]:
				more = diff["added_total"] - len(diff["added"])
				lines.append("- New {}: {}{}".format(key, ", ".join(diff["added"]), " (+{} more)".format(more) if more > 0 else ""))
			for old, new in diff["renamed"]:
				lines.append("- Renamed {}: {} -> {}".format(key, old, new))
		parts.append("\n".join(lines))
	for key, title in (("editor_commits", "Game data changes"), ("client_commits", "Art, sound and interface changes")):
		if data.get(key):
			parts.append("## {}\n{}".format(title, "\n".join(_format_commit(c, True) for c in data[key])))
	stats = area_stats(player)
	if stats:
		parts.append("## Areas touched (changed files)\n" + "\n".join("- {}: {}".format(area, count) for area, count in stats.items()))
	head = "\n\n".join(parts)

	# Features before fixes before the rest; bodies go first when space runs out.
	order = {"feat": 0, "fix": 1}
	ranked = sorted(player, key=lambda c: order.get(commit_kind(c["subject"])[0], 2))
	for with_body in (True, False):
		lines = []
		for commit in ranked:
			line = _format_commit(commit, with_body)
			if len(head) + sum(len(x) + 1 for x in lines) + len(line) + 40 > budget:
				break
			lines.append(line)
		if len(lines) == len(ranked) or not with_body:
			break
	section = "## Code changes\n" + ("\n".join(lines) if lines else "- None")
	return (head + "\n\n" + section).strip()[:budget]


def call_github_models(context, token, model=DEFAULT_MODEL, urlopen=urllib.request.urlopen):
	"""The model's JSON answer as a dict. Raises on any transport or format error."""
	payload = {
		"model": model,
		"temperature": 0.3,
		"max_tokens": 2000,
		"response_format": {"type": "json_object"},
		"messages": [
			{"role": "system", "content": INSTRUCTIONS},
			{"role": "user", "content": context},
		],
	}
	request = urllib.request.Request(ENDPOINT, data=json.dumps(payload).encode("utf-8"), method="POST", headers={
		"Authorization": "Bearer " + token,
		"Content-Type": "application/json",
		"Accept": "application/json",
		"User-Agent": "mmo-nightly-release",
	})
	with urlopen(request, timeout=TIMEOUT_S) as response:
		answer = json.loads(response.read().decode("utf-8"))
	content = answer["choices"][0]["message"]["content"]
	content = re.sub(r"^```(?:json)?\s*|\s*```$", "", content.strip())
	return json.loads(content)


def _allowed_title(title):
	return title in SECTIONS or re.fullmatch(r"Classes: [A-Z][A-Za-z ]{1,30}", title) is not None


def validate_notes(raw):
	"""Normalised {headline, sections}; raises ValueError if the answer is unusable."""
	if not isinstance(raw, dict):
		raise ValueError("notes are not an object")
	headline = raw.get("headline")
	if not isinstance(headline, str) or not headline.strip():
		raise ValueError("notes have no headline")
	sections = []
	for section in raw.get("sections") or []:
		if not isinstance(section, dict):
			continue
		title = str(section.get("title", "")).strip()
		bullets = [" ".join(str(b).split())[:BULLET_LIMIT] for b in section.get("bullets") or [] if str(b).strip()]
		if _allowed_title(title) and bullets:
			sections.append({"title": title, "bullets": bullets[:MAX_BULLETS]})
	if not sections:
		raise ValueError("notes have no usable section")
	return {"headline": " ".join(headline.split())[:HEADLINE_LIMIT], "sections": sections}


def fallback_notes(commits):
	"""Notes from the cleaned subjects of the player-facing commits."""
	player = [c for c in commits if not is_internal(c)]
	general = [clean_subject(c["subject"]) for c in player if commit_kind(c["subject"])[0] != "fix"]
	fixes = [clean_subject(c["subject"]) for c in player if commit_kind(c["subject"])[0] == "fix"]
	sections = [{"title": t, "bullets": b[:MAX_BULLETS]} for t, b in (("General", general), ("Bug Fixes", fixes)) if b]
	if not sections:
		return {"headline": "Maintenance and stability improvements.",
			"sections": [{"title": "General", "bullets": ["Behind-the-scenes maintenance and stability improvements."]}]}
	count = len(player)
	headline = clean_subject(player[0]["subject"]) if count == 1 else "{} improvements and fixes.".format(count)
	return {"headline": headline, "sections": sections}


def render_markdown(notes):
	lines = []
	for section in notes["sections"]:
		lines.append("## " + section["title"])
		lines.extend("- " + bullet for bullet in section["bullets"])
		lines.append("")
	return "\n".join(lines).strip()


def generate(repo, previous, commit, token, model=DEFAULT_MODEL, data=None, urlopen=urllib.request.urlopen, log=print):
	commits = collect_commits(repo, previous, commit)
	data = data or {}
	if token:
		try:
			notes = validate_notes(call_github_models(build_context(commits, data), token, model, urlopen))
			return dict(notes, source="ai", model=model)
		except (urllib.error.URLError, OSError, ValueError, KeyError, IndexError, TypeError) as error:
			log("patch notes: model call failed, using commit subjects: {}: {}".format(type(error).__name__, error))
	else:
		log("patch notes: no token, using commit subjects")
	return dict(fallback_notes(commits), source="fallback", model=None)


def main(argv=None):
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", default=".")
	parser.add_argument("--commit", required=True)
	parser.add_argument("--previous", default="")
	parser.add_argument("--version", default="")
	parser.add_argument("--model", default=os.environ.get("PATCH_NOTES_MODEL", DEFAULT_MODEL))
	parser.add_argument("--no-data", action="store_true", help="skip the data repo diff")
	parser.add_argument("--out", required=True, help="patch_notes.json")
	parser.add_argument("--markdown", help="also write the release body here")
	args = parser.parse_args(argv)

	data = {}
	if not args.no_data:
		import data_changes
		with tempfile.TemporaryDirectory() as workdir:
			data = data_changes.collect(args.repo, args.previous, args.commit, workdir)
	notes = generate(args.repo, args.previous, args.commit, os.environ.get("GITHUB_TOKEN", ""), args.model, data)
	with open(args.out, "w", encoding="utf-8") as handle:
		json.dump(notes, handle, indent=2, ensure_ascii=False)
	if args.markdown:
		title = "Patch {}".format(args.version) if args.version else "Patch notes"
		with open(args.markdown, "w", encoding="utf-8") as handle:
			handle.write("# {}\n\n*{}*\n\n{}\n".format(title, notes["headline"], render_markdown(notes)))
	print("wrote {} ({} notes, {} sections)".format(args.out, notes["source"], len(notes["sections"])))
	return 0


if __name__ == "__main__":
	sys.exit(main())
