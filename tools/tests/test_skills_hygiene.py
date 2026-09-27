#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Keeps the tracked skill source (.agents/skills) free of machine-specific paths and
harness-specific instructions, and pins the tooling that must read it.

.claude/skills is gitignored and becomes a set of junctions to .agents/skills
(tools/sync_skills.ps1), so anything checked here is what every agent actually reads.

	python tools/tests/test_skills_hygiene.py
"""

import re
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SKILLS = REPO / ".agents" / "skills"
TEXT_SUFFIXES = {".md", ".py", ".yaml", ".ps1"}


def skill_text_files():
	for path in sorted(SKILLS.rglob("*")):
		if path.is_file() and path.suffix in TEXT_SUFFIXES and "__pycache__" not in path.parts:
			yield path


class SkillHygieneTests(unittest.TestCase):
	def test_no_machine_specific_repo_paths(self):
		offenders = []
		for path in skill_text_files():
			text = path.read_text(encoding="utf-8", errors="replace")
			if re.search(r"F:[\\/]mmo", text):
				offenders.append(str(path.relative_to(REPO)))
		self.assertEqual(offenders, [], "skills must not hardcode F:\\mmo; derive the repo root instead")

	def test_no_codex_only_tool_instructions(self):
		offenders = []
		for path in skill_text_files():
			if path.parent.name.startswith("source-command-") or path.name == "openai.yaml":
				continue
			text = path.read_text(encoding="utf-8", errors="replace")
			for needle in ("codex_app.", ".codex\\", ".codex/", "built-in `image_gen` tool"):
				if needle in text:
					offenders.append(f"{path.relative_to(REPO)}: {needle}")
		self.assertEqual(offenders, [])

	def test_skill_names_match_directories(self):
		for skill_md in sorted(SKILLS.glob("*/SKILL.md")):
			text = skill_md.read_text(encoding="utf-8")
			match = re.search(r"^name:\s*\"?([^\"\n]+)\"?\s*$", text, re.MULTILINE)
			self.assertIsNotNone(match, f"{skill_md} has no name: frontmatter")
			self.assertEqual(match.group(1).strip(), skill_md.parent.name)

	def test_authoring_skills_are_tracked(self):
		for name in ("terrain-author", "particle-author", "mmo-quest-creator", "mmo-npc-designer"):
			self.assertTrue((SKILLS / name / "SKILL.md").is_file(), f"{name} must live in .agents/skills")

	def test_quest_skill_has_no_object_counter_contradiction(self):
		text = (SKILLS / "mmo-quest-creator" / "SKILL.md").read_text(encoding="utf-8")
		self.assertNotIn("lack of native object-use counters", text)

	def test_content_audit_reads_tracked_skills(self):
		sys.path.insert(0, str(REPO / "tools" / "gate"))
		import content_audit
		self.assertEqual(content_audit.SKILLS, REPO / ".agents" / "skills")


if __name__ == "__main__":
	unittest.main()
