#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Shape checks for the adversarial triage fixtures and the stage prompts. The fixtures run
against the real model only via tools/bugs/adversarial_check.py, never in the gate."""

import glob
import json
import os
import sys
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUGS_DIR = os.path.join(REPO_ROOT, "tools", "bugs")
sys.path.insert(0, BUGS_DIR)

from bugloop import inputs, verdicts  # noqa: E402


def fixtures():
	result = []
	for path in sorted(glob.glob(os.path.join(BUGS_DIR, "adversarial", "*.json"))):
		with open(path, "r", encoding="utf-8") as handle:
			result.append(json.load(handle))
	return result


class FixtureTests(unittest.TestCase):
	def test_fixtures_are_well_formed(self):
		loaded = fixtures()
		self.assertGreaterEqual(len(loaded), 8)
		for fixture in loaded:
			self.assertTrue(set(fixture["expect"]) <= set(verdicts.CATEGORIES), fixture["name"])
			self.assertTrue(fixture["bug"]["comment"], fixture["name"])
			self.assertIn(fixture["bug"]["comment"][:20], inputs.build_triage_input(fixture["bug"], [], nonce="n"))

	def test_corpus_has_attacks_and_controls(self):
		loaded = fixtures()
		self.assertTrue(any(fixture["expect"] == ["abuse_suspected"] for fixture in loaded))
		self.assertTrue(any(set(fixture["expect"]) <= set(verdicts.FIXABLE) for fixture in loaded))
		for fixture in loaded:
			if "abuse_suspected" in fixture["expect"]:
				self.assertFalse(set(fixture["expect"]) & set(verdicts.FIXABLE), fixture["name"])


class PromptTests(unittest.TestCase):
	def read(self, name):
		with open(os.path.join(BUGS_DIR, "bugloop", "prompts", name), "r", encoding="utf-8") as handle:
			return handle.read()

	def test_prompts_carry_their_trust_rules(self):
		self.assertIn("nonce", self.read("triage.md"))
		self.assertIn("abuse_suspected", self.read("triage.md"))
		fix = self.read("fix.md")
		for fragment in ("no_project_basis", "expected_source", "FIX.json", "Co-Authored-By", "Never push"):
			self.assertIn(fragment, fix)
		self.assertIn("reduces_security", self.read("review.md"))
		self.assertIn("MAINTAINER GUIDANCE", fix)
		self.assertIn("design_question", self.read("review.md"))
		self.assertIn("guidance_followed", self.read("review.md"))
		self.assertIn("Ordinary bug fixes that restore documented or evident behaviour need no question; leave it empty.",
			" ".join(self.read("review.md").split()))
		self.assertIn("FEATURE REQUEST", fix)
		self.assertIn("FEATURE REQUEST", self.read("review.md"))

	def test_fixer_never_runs_the_e2e_stack(self):
		"""The fixer runs outside the gate mutex; E2E runs only in the orchestrator's proof."""
		fix = self.read("fix.md")
		self.assertIn("Never run `tools/e2e/e2e_run.ps1`", fix)
		self.assertNotIn("Build and run it: confirm it fails before your fix and passes after.", fix)


if __name__ == "__main__":
	unittest.main()
