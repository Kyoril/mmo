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
# Words that mean the model leaked workshop talk into player notes; such bullets are dropped.
_TECHNICAL = re.compile(r"\b(navmesh|nav mesh|navigation mesh|collision|gizmo|submodule|editor|commit|hotfix|refactor|"
	r"cmake|json|proto|packet|thread|cache|api|database|sql|server|docker|pipeline|workflow|nightly|"
	r"launcher\.json|script|shader|hpak|asset|import(?:ed|s)?|night watch|record keepers?|apprentice)\b"
	r"|--\w|\b[0-9a-f]{8,}\b|\.(?:cpp|h|lua|proto|hmat|hpar|json|py)\b", re.IGNORECASE)
_HIDDEN_SECTIONS = {"behind the scenes", "behind the scene", "development", "technical"}
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

# Shown to the model as a sample of tone and structure, never as content. Every name in it is made up.
STYLE_SAMPLE = """Title: The Hollow Bell
Headline: A new dungeon opens beneath the old chapel, wolves learn to cooperate, and the Warrior finally gets a proper reason to be angry.

Adventurers, the bell beneath Emberfall Chapel has been ringing for three nights. Nobody has volunteered to find out why. Yet.

## Dungeons
- The Hollow Bell is now open to adventurers of level 12 to 15. Three bosses, one very unhappy choir and a staircase that is longer than it looks.
- The first boss no longer reaches through walls to hit you. We understand it was unpopular. It was also unfair.

## Classes: Warrior
- Rend now deals 15% more damage. Bleeding is back in fashion.
- Charge no longer sends you spinning in circles when the target is standing on a slope.

## Classes: Mage
- Frost Nova now lasts 6 seconds, up from 4. Wolves report feeling "unusually cold".
- Fireball's casting time increased from 2.5 to 3 seconds. This one is not a mistake, only a lesson.

## Creatures and NPCs
- Wolves in Greywood Hollow now howl for help when attacked. Expect company. Possibly a lot of it.
- Old Marta the innkeeper has been given a new apron. She says it changes nothing.

## Items
- Ashen Cloak of the Watch now grants +4 Spirit instead of +2 Stamina.
- Wolfhide Gloves can now be sold to vendors. Previously they could only be admired.

## Quests
- "The Missing Lantern" can be completed again if you abandoned it. The lantern was never really missing.
- Fixed an issue where "A Wolf at the Door" did not credit the final kill if the wolf died to a damage-over-time effect.

## User Interface
- Action bar buttons now visibly sink when you press them. Your keyboard was always doing something.

## Bug Fixes
- Fixed an issue where talking to a vendor while mounted could leave your character floating politely in mid-air."""

INSTRUCTIONS = """You write the patch notes of "Alestia Online", a fantasy MMORPG, in the spirit of the classic Blizzard patch notes of vanilla World of Warcraft. They appear in the game launcher and are read by PLAYERS. Players want to know what changed in the game: content, mechanics, classes, balance, items, quests, creatures, dungeons, interface. They do not care how it was made.

VOICE
- Classic patch-note energy: short, concrete, confident, with real numbers where the input has them ("Rend now deals 15%% more damage"). Exciting where something is exciting (a new dungeon, a new class ability, a new zone), matter-of-fact everywhere else.
- Humour is dry and well placed: a short wry aside in roughly every second or third bullet, the best ones in the lighter sections (creatures, NPCs, items, quests, interface). Never in a bullet about a serious bug, never at the player's expense, never more than one aside per bullet. Big news (a new dungeon, a new boss, a new class ability) gets a line of genuine excitement, like a classic Blizzard announcement. A joke must never suggest something that did not happen.
- Speak to "Adventurers" in the intro only. British-neutral English, no emojis, no exclamation-mark salesmanship, no marketing talk, no "we're excited".

WHAT PLAYERS CARE ABOUT (this is the whole list)
- New and changed dungeons, bosses, zones, creatures, NPCs, quests, items, spells, talents, classes, races, professions, vendors, loot, sounds, music, animations, visual effects, the interface and options (graphics, sound, controls), and bug fixes players would have noticed.
- Name the things: boss names, quest names, item names, spell names, zone names from the input. Say what is different for the player: who is stronger or weaker, what drops where, what is new to do.

WHAT PLAYERS NEVER SEE (leave it out completely, do not translate it into metaphors)
- Editor and tooling changes, level-building and authoring work (rooms, portals, collision, navigation meshes, imported art kits, terrain copy/paste tools), build, test, deployment, backup and server operations, the bug-reporting helpers, the launcher's internals, data migrations, anything about "the night watch", "record keepers" or other stand-ins for infrastructure. No "Behind the Scenes" section. If a change has no effect a player could notice, it does not appear. If the dungeon's rooms were only rebuilt, say at most that the dungeon is now more complete, and only if the input shows it is playable.
- Never mention commit hashes, file names, ids, command-line flags, code identifiers, or programming and tooling terms (thread, packet, cache, API, CI, tests, refactor, branch, merge, Docker, database, server, editor, navmesh, collision shape, gizmo, submodule, import, script). Spell and item ids in the input are for your understanding only; never print them.

TRUTH
- Only describe what the input supports. Do not invent features, numbers, names, zones, classes or lore. If the input does not give a number, give no number.
- When a fix is described from the developer's side, rewrite it from the player's side: what used to go wrong, what happens now.
- Merge related changes into one bullet. Twenty commits about one dungeon become a few bullets about what players will experience in it.
- If very little player-facing change is in the input, write a short, honest update. Never pad it. Never invent content to fill sections.

STRUCTURE
- "title": a short evocative name for this update, 2 to 5 words, from its biggest theme.
- "headline": one or two sentences, at most 160 characters, the most exciting change, in voice.
- "intro": one or two short paragraphs, at most 500 characters in total, scene-setting and teasing the biggest change. Separate paragraphs with a blank line. May be empty for a tiny update.
- "sections": use the headings of classic patch notes where they fit: Dungeons, World, Classes: <Class>, Combat, Creatures and NPCs, Quests, Items, Spells and Talents, User Interface, Audio and Visuals, Bug Fixes. Biggest news first, "Bug Fixes" last. Bullets in "Bug Fixes" begin "Fixed an issue where ..." or "<Thing> now ...".
- At most %d bullets per section, at most %d sections, at most 3500 characters in total.

Answer with one JSON object and nothing else:
{"title": "...", "headline": "...", "intro": "...", "sections": [{"title": "...", "bullets": ["...", "..."]}]}

STYLE SAMPLE (every name and number is made up; copy its tone and structure, never its content or jokes):
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
	(tooling, testing and deployment commits are left out: players never see them)."""
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

	# Features before fixes before the rest.
	order = {"feat": 0, "fix": 1}
	ranked = sorted(player, key=lambda c: order.get(commit_kind(c["subject"])[0], 2))
	head = (head + "\n\n" + _fit(head, "Player-facing changes", ranked, True, budget)).strip()
	return head[:budget]


def find_claude():
	"""The claude executable: on PATH, else in the usual npm and installer locations."""
	found = shutil.which("claude")
	if found:
		return found
	candidates = [os.path.expanduser("~/.local/bin"), os.path.expanduser("~/.claude/local"), "/usr/local/bin", "/usr/bin"]
	for command in (["npm", "prefix", "-g"],):
		try:
			prefix = subprocess.run(command, capture_output=True, text=True, timeout=30).stdout.strip()
		except (OSError, subprocess.SubprocessError):
			prefix = ""
		if prefix:
			candidates += [os.path.join(prefix, "bin"), prefix]
	return shutil.which("claude", path=os.pathsep.join(candidates))


def _claude_command(model):
	"""argv for a one-shot, tool-less `claude -p` run with our instructions as the system prompt."""
	executable = find_claude()
	if not executable:
		raise OSError("claude CLI not found on PATH or in the npm prefix")
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
		raise OSError("claude exited with {}: {}".format(result.returncode, failure_reason(result)))
	envelope = json.loads(result.stdout)
	if envelope.get("is_error"):
		raise ValueError("claude reported an error: {}".format(str(envelope.get("result"))[:300]))
	return parse_answer(envelope["result"])


def failure_reason(result):
	"""Why a claude run failed. With --output-format json the cause is in the envelope's
	`result`, far behind the usage block, so a plain prefix of stdout never shows it."""
	stdout = (result.stdout or "").strip()
	try:
		envelope = json.loads(stdout)
	except ValueError:
		envelope = None
	if isinstance(envelope, dict):
		parts = [str(envelope.get(key)) for key in ("subtype", "api_error_status", "result") if envelope.get(key) not in (None, "")]
		if parts:
			return " | ".join(parts)[:500]
	return ((result.stderr or "").strip() or stdout)[:500]


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
		bullets = [_one_line(b, BULLET_LIMIT) for b in section.get("bullets") or []
			if str(b).strip() and not _TECHNICAL.search(str(b))]
		if title.lower() in _HIDDEN_SECTIONS:
			continue
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
	"""Notes for when the model is unavailable. Commit subjects are written for developers and
	must never reach players, so these only say that an update happened."""
	return {"title": "", "headline": "Assorted fixes and improvements.", "intro": "",
		"sections": [{"title": "General", "bullets": ["Assorted fixes and improvements to the game world and the game client."]}]}



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
			# A GitHub annotation, so a silent fallback shows up on the run summary.
			log("::warning::patch notes: model call failed, using commit subjects: {}: {}".format(type(error).__name__, error))
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
