#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for tools/release/patch_notes.py and tools/release/data_changes.py."""

import importlib.util
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
import urllib.error

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools", "release"))


def _load(name):
	path = os.path.join(REPO_ROOT, "tools", "release", name + ".py")
	spec = importlib.util.spec_from_file_location(name, path)
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


patch_notes = _load("patch_notes")
data_changes = _load("data_changes")


def _git(repo, *args):
	return subprocess.run(
		["git", "-C", repo, "-c", "user.name=Test", "-c", "user.email=test@example.com"] + list(args),
		check=True, capture_output=True, text=True).stdout.strip()


def _commit(repo, path, message):
	full = os.path.join(repo, path)
	os.makedirs(os.path.dirname(full), exist_ok=True)
	with open(full, "a", encoding="utf-8") as handle:
		handle.write("x\n")
	_git(repo, "add", path)
	_git(repo, "commit", "-q", "-m", message)


def _commit_dict(subject, files=("src/world_server/x.cpp",), body=""):
	return {"subject": subject, "body": body, "files": list(files)}


class FakeResponse(io.BytesIO):
	def __enter__(self):
		return self

	def __exit__(self, *args):
		self.close()


def _answer(content):
	payload = {"choices": [{"message": {"content": content}}]}
	return lambda request, timeout: FakeResponse(json.dumps(payload).encode("utf-8"))


GOOD = {"headline": "The Hollow Choir awaits.", "sections": [
	{"title": "Dungeons", "bullets": ["A new five-player dungeon, the Hollow Choir, is now open."]},
	{"title": "Classes: Mage", "bullets": ["Frostbolt now has a new impact effect."]},
	{"title": "Bug Fixes", "bullets": ["Fixed an issue where doors could block line of sight while open."]},
]}


class Classification(unittest.TestCase):
	def test_internal_by_type_scope_and_paths(self):
		self.assertTrue(patch_notes.is_internal(_commit_dict("ci: speed up")))
		self.assertTrue(patch_notes.is_internal(_commit_dict("fix(deployer): send a User-Agent")))
		self.assertTrue(patch_notes.is_internal(_commit_dict("Tweak things", files=["tools/gate/verify.ps1", "docs/x.md"])))
		self.assertFalse(patch_notes.is_internal(_commit_dict("feat(quests): new chain")))
		self.assertFalse(patch_notes.is_internal(_commit_dict("Tweak things", files=["tools/x.py", "src/mmo_client/y.cpp"])))

	def test_clean_subject(self):
		self.assertEqual(patch_notes.clean_subject("fix(combat): swing timer resets"), "Swing timer resets")
		self.assertEqual(patch_notes.clean_subject("Plain subject"), "Plain subject")


class Context(unittest.TestCase):
	def test_data_changes_come_first_and_internal_commits_are_dropped(self):
		commits = [_commit_dict("feat: new mount"), _commit_dict("ci: cache vcpkg", files=[".github/x.yml"])]
		data = {"catalogs": {"quests": {"added": ["The Lost Hymn"], "renamed": [], "added_total": 1}},
			"editor_commits": [{"subject": "Sentinel item displays", "body": "- Item displays 184-215"}]}
		context = patch_notes.build_context(commits, data)
		self.assertLess(context.index("The Lost Hymn"), context.index("New mount"))
		self.assertIn("Sentinel item displays", context)
		self.assertNotIn("vcpkg", context)
		self.assertIn("Gameplay (server): 1", context)

	def test_budget_drops_bodies_then_commits(self):
		commits = [_commit_dict("feat: thing {}".format(i), body="b" * 300) for i in range(200)]
		context = patch_notes.build_context(commits, {}, budget=3000)
		self.assertLessEqual(len(context), 3000)
		self.assertNotIn("bbbb", context)
		self.assertIn("Thing 0", context)


class Validation(unittest.TestCase):
	def test_keeps_allowed_sections(self):
		notes = patch_notes.validate_notes(dict(GOOD, sections=GOOD["sections"] + [{"title": "Refactoring", "bullets": ["x"]}]))
		self.assertEqual([s["title"] for s in notes["sections"]], ["Dungeons", "Classes: Mage", "Bug Fixes"])

	def test_rejects_unusable_answers(self):
		for raw in (None, [], {"headline": "x"}, {"headline": "", "sections": GOOD["sections"]},
				{"headline": "x", "sections": [{"title": "General", "bullets": []}]}):
			with self.assertRaises(ValueError):
				patch_notes.validate_notes(raw)


class Fallback(unittest.TestCase):
	def test_groups_features_and_fixes(self):
		notes = patch_notes.fallback_notes([_commit_dict("feat: new zone"), _commit_dict("fix(ui): tooltip overlaps"),
			_commit_dict("test: more tests", files=["src/tests/a.cpp"])])
		self.assertEqual(notes["headline"], "2 improvements and fixes.")
		self.assertEqual(notes["sections"], [{"title": "General", "bullets": ["New zone"]},
			{"title": "Bug Fixes", "bullets": ["Tooltip overlaps"]}])

	def test_only_internal_commits_give_maintenance_note(self):
		notes = patch_notes.fallback_notes([_commit_dict("ci: x", files=[".github/a"])])
		self.assertEqual(notes["sections"][0]["title"], "General")
		self.assertIn("maintenance", notes["headline"].lower())

	def test_render_markdown(self):
		self.assertEqual(patch_notes.render_markdown({"headline": "h", "sections": [{"title": "General", "bullets": ["a", "b"]}]}),
			"## General\n- a\n- b")


class Generate(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.repo = self.tmp.name
		_git(self.repo, "init", "-q")
		_commit(self.repo, "src/a.cpp", "first")
		self.first = _git(self.repo, "rev-parse", "HEAD")
		_commit(self.repo, "src/world_server/b.cpp", "feat(combat): add parry\n\nMelee classes can parry frontal attacks.\n\nCo-Authored-By: Bot <b@x>")
		_commit(self.repo, ".github/workflows/x.yml", "ci: tweak")
		self.head = _git(self.repo, "rev-parse", "HEAD")
		self.logs = []

	def tearDown(self):
		self.tmp.cleanup()

	def test_collect_commits_reads_bodies_and_files(self):
		commits = patch_notes.collect_commits(self.repo, self.first, self.head)
		self.assertEqual([c["subject"] for c in commits], ["ci: tweak", "feat(combat): add parry"])
		self.assertEqual(commits[1]["body"], "Melee classes can parry frontal attacks.")
		self.assertEqual(commits[1]["files"], ["src/world_server/b.cpp"])

	def test_uses_the_model_answer(self):
		notes = patch_notes.generate(self.repo, self.first, self.head, "token", urlopen=_answer(json.dumps(GOOD)), log=self.logs.append)
		self.assertEqual(notes["source"], "ai")
		self.assertEqual(notes["headline"], "The Hollow Choir awaits.")

	def test_accepts_fenced_json(self):
		notes = patch_notes.generate(self.repo, self.first, self.head, "token",
			urlopen=_answer("```json\n" + json.dumps(GOOD) + "\n```"), log=self.logs.append)
		self.assertEqual(notes["source"], "ai")

	def test_falls_back_on_http_error_garbage_or_no_token(self):
		def failing(request, timeout):
			raise urllib.error.HTTPError(request.full_url, 429, "Too Many Requests", {}, None)

		for urlopen, token in ((failing, "t"), (_answer("not json"), "t"), (_answer(json.dumps({"headline": "x"})), "t"), (failing, "")):
			notes = patch_notes.generate(self.repo, self.first, self.head, token, urlopen=urlopen, log=self.logs.append)
			self.assertEqual(notes["source"], "fallback")
			self.assertEqual(notes["sections"], [{"title": "General", "bullets": ["Add parry"]}])

	def test_request_carries_token_and_model(self):
		seen = {}

		def capture(request, timeout):
			seen["auth"] = request.get_header("Authorization")
			seen["body"] = json.loads(request.data.decode("utf-8"))
			return _answer(json.dumps(GOOD))(request, timeout)

		patch_notes.generate(self.repo, self.first, self.head, "secret", model="openai/gpt-4o-mini", urlopen=capture, log=self.logs.append)
		self.assertEqual(seen["auth"], "Bearer secret")
		self.assertEqual(seen["body"]["model"], "openai/gpt-4o-mini")
		self.assertIn("Add parry", seen["body"]["messages"][1]["content"])


class DataChanges(unittest.TestCase):
	def test_diff_names(self):
		diff = data_changes.diff_names({1: "Old Name", 2: "Same"}, {1: "New Name", 2: "Same", 3: "Brand New"})
		self.assertEqual(diff, {"added": ["Brand New"], "renamed": [["Old Name", "New Name"]], "added_total": 1})

	def test_diff_names_merges_ranks_sharing_a_name(self):
		diff = data_changes.diff_names({1: "Rend", 2: "Rend"}, {1: "Jagged Edge", 2: "Jagged Edge", 3: "Fire", 4: "Fire"})
		self.assertEqual(diff["added"], ["Fire"])
		self.assertEqual(diff["renamed"], [["Rend", "Jagged Edge"]])

	def test_commit_messages_strip_trailers(self):
		with tempfile.TemporaryDirectory() as repo:
			_git(repo, "init", "-q")
			_commit(repo, "data/a.data", "base")
			base = _git(repo, "rev-parse", "HEAD")
			_commit(repo, "data/a.data", "Sentinel displays\n\n- Item displays 184-215\n\nCo-Authored-By: Bot <b@x>")
			self.assertEqual(data_changes.commit_messages(repo, base, "HEAD"),
				[{"subject": "Sentinel displays", "body": "- Item displays 184-215"}])

	def test_collect_never_raises(self):
		with tempfile.TemporaryDirectory() as repo, tempfile.TemporaryDirectory() as work:
			_git(repo, "init", "-q")
			_commit(repo, "a.txt", "only")
			logs = []
			result = data_changes.collect(repo, "", "HEAD", work, log=logs.append)
			self.assertEqual(result, {"editor_commits": [], "client_commits": [], "catalogs": {}})
			self.assertTrue(logs)

	@unittest.skipUnless(importlib.util.find_spec("grpc_tools"), "grpcio-tools not installed")
	def test_real_schemas_compile_and_decode(self):
		with tempfile.TemporaryDirectory() as out:
			modules = data_changes.compile_schemas(os.path.join(REPO_ROOT, "src", "shared", "proto_data"), out)
			quests = modules["quests"].Quests()
			entry = quests.entry.add()
			entry.id = 7
			entry.name = "The Lost Hymn"
			self.assertEqual(data_changes._names(modules, "quests", quests.SerializeToString()), {7: "The Lost Hymn"})


if __name__ == "__main__":
	unittest.main()
