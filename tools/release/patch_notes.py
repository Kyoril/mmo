#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Writes player-facing patch notes for a nightly release.

Claude Code (`claude -p`, authenticated with the CLAUDE_CODE_OAUTH_TOKEN of the maintainer's
subscription, no paid API key) turns the commits and game data changes into in-world patch
notes in the voice of "The Night Watch" (docs/versioning.md). Any failure falls back to notes
built from the cleaned commit subjects: a release never fails here.

Output: patch_notes.json ({title, headline, intro, sections: [{title, bullets}], source, model})
and a Markdown rendering for the GitHub release body.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

DEFAULT_MODEL = "opus"
# Far below the model's context; keeps the call quick and the input reviewable.
CONTEXT_BUDGET = 60000
BODY_LIMIT = 400
MAX_COMMITS = 300
TIMEOUT_S = 600

MAX_SECTIONS = 10
MAX_BULLETS = 12
BULLET_LIMIT = 400
TITLE_LIMIT = 60
HEADLINE_LIMIT = 200
INTRO_LIMIT = 1200

_PREFIX = re.compile(r"^(\w+)(\(([^)]*)\))?!?:\s*")
_SECTION_TITLE = re.compile(r"[A-Z][A-Za-z0-9 ,&':/-]{1,47}")
_INTERNAL_TYPES = {"ci", "test", "tests", "build", "chore", "docs", "style", "refactor"}
_INTERNAL_SCOPES = {"gate", "ci", "deployer", "docker", "e2e", "tests", "test", "release", "nightly", "tools", "skills", "worldkit", "bots", "version", "bug-loop", "bugloop", "bugs"}
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

# The approved voice: the hand-written notes of 2026-10-07. Shown to the model as a sample of
# tone, never as content.
STYLE_SAMPLE = """Title: The Night Watch
Headline: A test update to make sure the realm's new night shift is awake. Few changes you will notice, many you hopefully never will.

Adventurers, today's update was a test run. Somewhere behind the walls of the realm, a brand new night watch has taken up its post: it gathers each day's work, checks it twice and delivers it to the realm while most of you are asleep. This update was mostly about making sure the watchman knows where the doors are.

If you noticed a brief maintenance today, that was him practising.

## General
- When the realm needs to restart for an update, a 15-minute countdown warns everyone in game first. Finish your fight, sell your grey items, find an inn.
- Character data is backed up and the backup is now actually checked every night. A backup nobody has ever opened is just a hopeful box.

## Spell Effects
- The package containing spell and particle effects is now included with the game download. Fireballs that arrive without their fire were deemed "underwhelming" by the Mage community.

## Realm Stability
- The realm now confirms the world itself is actually running before declaring an update a success. "The gates are open" no longer counts if there is nothing behind them.
- Old update archives are cleaned up regularly. The storage cellar had begun to resemble a dragon's hoard, minus the gold.

## Bug Fixes
- Test adventurers who were knocked off the realm can now find their way back in, instead of standing confused outside the gates.

## Behind the Scenes
- New rule for the realm's record keepers: changes to the archives may only ever add, never tear pages out. This keeps your characters safe even if an update has to be rolled back."""

INSTRUCTIONS = """You are the chronicler of "Alestia Online", a fantasy MMORPG, and write the notes for tonight's update. They appear in the game launcher and are read by players.

VOICE
- Warm, wry, in-world. Speak to "Adventurers". The realm, its Game Masters, record keepers, smiths and night watch are the people who do the work; servers, databases and pipelines are described as what they mean in the world (gates, archives, cellars, messengers, lanterns), never by their technical names.
- Every bullet first says plainly what changed for the player. About every second or third bullet may add one short, dry joke or in-world aside. Never more than one per bullet, never at the player's expense.
- Short sentences, British-neutral English, no emojis, no exclamation-mark salesmanship, no "we're excited".

TRUTH
- Only describe what the input supports. Do not invent features, numbers, names, zones, classes or lore. Jokes must not suggest something that did not happen.
- Prefer concrete names from the game data changes (quests, items, spells, creatures, zones).
- Never mention commit hashes, file names, code identifiers, programming terms (thread, packet, cache, API, CI, tests, refactor, branch, merge, webhook, Docker, database, server), AI models or tools.

STRUCTURE
- "title": a short evocative name for this update, 2 to 5 words, drawn from its biggest theme (for example "The Night Watch", "Of Wolves and Allies").
- "headline": one or two sentences, at most 160 characters, the most noticeable change, in voice.
- "intro": one or two short paragraphs (at most 600 characters together) that set the scene for this update. Separate paragraphs with a blank line.
- "sections": player-facing sections first, using titles such as General, Classes: <Class>, Combat, Creatures and NPCs, Quests, Items, World, Dungeons, User Interface, Spell Effects, Audio and Visuals, Launcher, Realm Stability. Then "Bug Fixes" (each bullet "Fixed an issue where ..." or "<Thing> now ..."), then "Behind the Scenes" last.
- Merge related commits into one bullet. Many commits about the same machinery become one or two bullets, not twenty.
- Work the players never see (tooling, testing, deployment, the bug-reporting helpers) goes into "Behind the Scenes" as at most four bullets, translated into in-world terms. Leave it out entirely if it is trivial.
- At most %d bullets per section, at most %d sections, at most 4500 characters in total.

Answer with one JSON object and nothing else:
{"title": "...", "headline": "...", "intro": "...", "sections": [{"title": "...", "bullets": ["...", "..."]}]}

STYLE SAMPLE (an earlier update; copy its tone, never its jokes or content):
%s
""" % (MAX_BULLETS, MAX_SECTIONS, STYLE_SAMPLE)


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


def _fit(head, title, commits, with_bodies, budget):
	"""`title` and as many commits as fit after `head`; bodies are dropped before commits."""
	for with_body in ((True, False) if with_bodies else (False,)):
		lines = []
		for commit in commits:
			line = _format_commit(commit, with_body)
			if len(head) + sum(len(x) + 1 for x in lines) + len(line) + len(title) + 40 > budget:
				break
			lines.append(line)
		if len(lines) == len(commits) or not with_body:
			break
	return "## {}\n{}".format(title, "\n".join(lines) if lines else "- None")


def build_context(commits, data, budget=CONTEXT_BUDGET):
	"""The model input, condensed to `budget` characters: data changes, player-facing commits,
	then the internal ones (subjects only, for "Behind the Scenes")."""
	player = [c for c in commits if not is_internal(c)]
	internal = [c for c in commits if is_internal(c)]
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

	# Features before fixes before the rest.
	order = {"feat": 0, "fix": 1}
	ranked = sorted(player, key=lambda c: order.get(commit_kind(c["subject"])[0], 2))
	head = (head + "\n\n" + _fit(head, "Player-facing changes", ranked, True, budget)).strip()
	if internal:
		head += "\n\n" + _fit(head, "Behind the scenes (tooling, testing, deployment; players never see these)", internal, False, budget)
	return head[:budget]


def _claude_command(model):
	"""argv for a one-shot, tool-less `claude -p` run with our instructions as the system prompt."""
	executable = shutil.which("claude")
	if not executable:
		raise OSError("claude CLI not found on PATH")
	return [executable, "-p", "--model", model, "--output-format", "json", "--tools", "",
		"--strict-mcp-config", "--setting-sources", "", "--no-session-persistence",
		"--system-prompt", INSTRUCTIONS]


def call_claude(context, model=DEFAULT_MODEL, run=subprocess.run):
	"""The model's JSON answer as a dict. Raises on any process or format error."""
	# A neutral working directory keeps any CLAUDE.md out of the prompt.
	with tempfile.TemporaryDirectory() as cwd:
		result = run(_claude_command(model), input=context, capture_output=True, text=True,
			encoding="utf-8", errors="replace", timeout=TIMEOUT_S, cwd=cwd)
	if result.returncode != 0:
		raise OSError("claude exited with {}: {}".format(result.returncode, (result.stderr or result.stdout or "").strip()[:300]))
	envelope = json.loads(result.stdout)
	if envelope.get("is_error"):
		raise ValueError("claude reported an error: {}".format(str(envelope.get("result"))[:300]))
	return parse_answer(envelope["result"])


def parse_answer(text):
	"""The JSON object in the model's reply, tolerating code fences and surrounding prose."""
	text = re.sub(r"^```(?:json)?\s*|\s*```$", "", str(text).strip())
	start, end = text.find("{"), text.rfind("}")
	if start < 0 or end < start:
		raise ValueError("no JSON object in the answer")
	return json.loads(text[start:end + 1])


def _one_line(value, limit):
	return " ".join(str(value).split())[:limit]


def validate_notes(raw):
	"""Normalised {title, headline, intro, sections}; raises ValueError if the answer is unusable."""
	if not isinstance(raw, dict):
		raise ValueError("notes are not an object")
	headline = raw.get("headline")
	if not isinstance(headline, str) or not headline.strip():
		raise ValueError("notes have no headline")
	sections = []
	for section in raw.get("sections") or []:
		if not isinstance(section, dict):
			continue
		title = _one_line(section.get("title", ""), 48)
		bullets = [_one_line(b, BULLET_LIMIT) for b in section.get("bullets") or [] if str(b).strip()]
		if _SECTION_TITLE.fullmatch(title) and bullets:
			sections.append({"title": title, "bullets": bullets[:MAX_BULLETS]})
	if not sections:
		raise ValueError("notes have no usable section")
	paragraphs = [_one_line(p, INTRO_LIMIT) for p in re.split(r"\n\s*\n", str(raw.get("intro") or "")) if p.strip()]
	return {
		"title": _one_line(raw.get("title") or "", TITLE_LIMIT),
		"headline": _one_line(headline, HEADLINE_LIMIT),
		"intro": "\n\n".join(paragraphs)[:INTRO_LIMIT],
		"sections": sections[:MAX_SECTIONS],
	}


def fallback_notes(commits):
	"""Notes from the cleaned subjects of the player-facing commits."""
	player = [c for c in commits if not is_internal(c)]
	general = [clean_subject(c["subject"]) for c in player if commit_kind(c["subject"])[0] != "fix"]
	fixes = [clean_subject(c["subject"]) for c in player if commit_kind(c["subject"])[0] == "fix"]
	sections = [{"title": t, "bullets": b[:MAX_BULLETS]} for t, b in (("General", general), ("Bug Fixes", fixes)) if b]
	if not sections:
		return {"title": "", "headline": "Maintenance and stability improvements.", "intro": "",
			"sections": [{"title": "General", "bullets": ["Behind-the-scenes maintenance and stability improvements."]}]}
	count = len(player)
	headline = clean_subject(player[0]["subject"]) if count == 1 else "{} improvements and fixes.".format(count)
	return {"title": "", "headline": headline, "intro": "", "sections": sections}


def render_markdown(notes):
	"""The intro and the sections; the caller adds the title and headline."""
	lines = [notes["intro"], ""] if notes.get("intro") else []
	for section in notes["sections"]:
		lines.append("## " + section["title"])
		lines.extend("- " + bullet for bullet in section["bullets"])
		lines.append("")
	return "\n".join(lines).strip()


def generate(repo, previous, commit, use_model=True, model=DEFAULT_MODEL, data=None, run=subprocess.run, log=print):
	commits = collect_commits(repo, previous, commit)
	data = data or {}
	if use_model:
		try:
			notes = validate_notes(call_claude(build_context(commits, data), model, run))
			return dict(notes, source="ai", model=model)
		except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
			log("patch notes: model call failed, using commit subjects: {}: {}".format(type(error).__name__, error))
	else:
		log("patch notes: model disabled, using commit subjects")
	return dict(fallback_notes(commits), source="fallback", model=None)


def main(argv=None):
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", default=".")
	parser.add_argument("--commit", required=True)
	parser.add_argument("--previous", default="")
	parser.add_argument("--version", default="")
	parser.add_argument("--model", default=os.environ.get("PATCH_NOTES_MODEL", DEFAULT_MODEL))
	parser.add_argument("--no-model", action="store_true", help="skip Claude and write the fallback notes")
	parser.add_argument("--no-data", action="store_true", help="skip the data repo diff")
	parser.add_argument("--context-out", help="also write the model input here, for review")
	parser.add_argument("--out", required=True, help="patch_notes.json")
	parser.add_argument("--markdown", help="also write the release body here")
	args = parser.parse_args(argv)

	data = {}
	if not args.no_data:
		import data_changes
		with tempfile.TemporaryDirectory() as workdir:
			data = data_changes.collect(args.repo, args.previous, args.commit, workdir)
	if args.context_out:
		with open(args.context_out, "w", encoding="utf-8") as handle:
			handle.write(build_context(collect_commits(args.repo, args.previous, args.commit), data))
	notes = generate(args.repo, args.previous, args.commit, not args.no_model, args.model, data)
	with open(args.out, "w", encoding="utf-8") as handle:
		json.dump(notes, handle, indent=2, ensure_ascii=False)
	if args.markdown:
		title = "Patch {}".format(args.version) if args.version else "Patch notes"
		if notes.get("title"):
			title += ": " + notes["title"]
		with open(args.markdown, "w", encoding="utf-8") as handle:
			handle.write("# {}\n\n*{}*\n\n{}\n".format(title, notes["headline"], render_markdown(notes)))
	print("wrote {} ({} notes, {} sections)".format(args.out, notes["source"], len(notes["sections"])))
	return 0


if __name__ == "__main__":
	sys.exit(main())
