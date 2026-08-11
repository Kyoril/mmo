#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for tools/protocol_version_check.py.

The point of that tool is that it fails when someone forgets a version bump. A tool like
that fails quietly when it breaks: it keeps exiting 0, and nobody notices until an
incompatible build ships. Its own happy path -- everything in sync -- proves almost
nothing, so the interesting cases are pinned down here instead.

Stdlib unittest, no pytest dependency, so it runs anywhere python does:

    python tools/tests/test_protocol_version_check.py
"""

import importlib.util
import json
import os
import sys
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MODULE_PATH = os.path.join(REPO_ROOT, "tools", "protocol_version_check.py")

_spec = importlib.util.spec_from_file_location("protocol_version_check", MODULE_PATH)
pvc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pvc)


class StripComments(unittest.TestCase):
	def test_removes_line_and_block_comments(self):
		self.assertEqual(pvc.strip_comments("int a; // trailing").strip(), "int a;")
		self.assertEqual(pvc.strip_comments("int /* mid */ a;").split(), ["int", "a;"])

	def test_leaves_string_literals_alone(self):
		# The whole reason strip_comments is a scanner and not a regex.
		self.assertIn('"http://x"', pvc.strip_comments('const char* s = "http://x";'))
		self.assertIn('"/* not a comment */"', pvc.strip_comments('s = "/* not a comment */";'))

	def test_handles_escaped_quote_inside_literal(self):
		source = r'const char* s = "he said \"hi\""; // gone'
		result = pvc.strip_comments(source)
		self.assertIn(r'\"hi\"', result)
		self.assertNotIn("gone", result)


class Normalize(unittest.TestCase):
	def test_comment_and_whitespace_edits_are_invisible(self):
		before = "enum Type\n{\n\tA = 0,\n};\n"
		after = "// a new comment\nenum   Type\n{\n\n\t\tA   =   0,\t// inline\n};\n\n"
		self.assertEqual(pvc.normalize(before), pvc.normalize(after))

	def test_version_line_is_excluded(self):
		with_v4 = "constexpr uint32 ProtocolVersion = 0x00000004;\nenum Type { A };"
		with_v9 = "constexpr uint32 ProtocolVersion = 0x00000009;\nenum Type { A };"
		self.assertEqual(pvc.normalize(with_v4), pvc.normalize(with_v9))

	def test_an_actual_opcode_change_is_visible(self):
		self.assertNotEqual(
			pvc.normalize("enum Type { A = 0, B = 1 };"),
			pvc.normalize("enum Type { A = 0, B = 2 };"))


class ParseEnumerators(unittest.TestCase):
	def test_implicit_values_follow_the_last_explicit_one(self):
		self.assertEqual(
			pvc.parse_enumerators("A = 0x05, B, C"),
			[("A", 5), ("B", 6), ("C", 7)])

	def test_insertion_renumbers_the_tail(self):
		# The silent break this tool exists to catch.
		before = dict(pvc.parse_enumerators("A = 0, B, C"))
		after = dict(pvc.parse_enumerators("A = 0, X, B, C"))
		self.assertEqual(before["C"], 2)
		self.assertEqual(after["C"], 3)

	def test_expression_initialiser_anchors_relative_numbering(self):
		# object_fields::UnitFields is numbered this way, entirely implicitly.
		self.assertEqual(
			pvc.parse_enumerators("Level = ObjectFieldCount, Bytes, Faction"),
			[("Level", "ObjectFieldCount"), ("Bytes", "ObjectFieldCount+1"),
				("Faction", "ObjectFieldCount+2")])

	def test_trailing_comma_does_not_produce_an_empty_entry(self):
		self.assertEqual(pvc.parse_enumerators("A = 1, B = 2,"), [("A", 1), ("B", 2)])


class ExtractSymbols(unittest.TestCase):
	def test_enum_type_scope_omits_the_placeholder_name(self):
		source = "namespace client_login_packet { enum Type { LogonChallenge = 0x00 }; }"
		self.assertEqual(pvc.extract_symbols("auth", source),
			["auth::client_login_packet::LogonChallenge = 0"])

	def test_named_enum_keeps_its_name(self):
		# Two enums share one namespace in object_fields; without the name they collide.
		source = ("namespace object_fields { enum ObjectFields { Guid = 0 }; "
			"enum UnitFields { Level = 8 }; }")
		symbols = pvc.extract_symbols("game", source)
		self.assertIn("game::object_fields::ObjectFields::Guid = 0", symbols)
		self.assertIn("game::object_fields::UnitFields::Level = 8", symbols)

	def test_enum_class_is_captured(self):
		source = "enum class AuthLocale { Default = 0x00, frFR = 0x01 };"
		self.assertIn("auth::AuthLocale::frFR = 1", pvc.extract_symbols("auth", source))


class AgainstTheRealSources(unittest.TestCase):
	"""Guards the parser against the actual headers, not just synthetic input."""

	@classmethod
	def setUpClass(cls):
		cls.snapshot = pvc.snapshot()

	def test_every_tracked_file_exists_and_is_hashed(self):
		for protocol, spec in pvc.PROTOCOLS.items():
			self.assertIn(spec["version_file"], spec["files"],
				"%s: version_file must be one of the tracked files" % (protocol,))
			for rel_path in spec["files"]:
				self.assertTrue(os.path.isfile(os.path.join(REPO_ROOT, rel_path)), rel_path)
				self.assertIn(rel_path, self.snapshot[protocol]["files"])

	def test_versions_were_found(self):
		for protocol in pvc.PROTOCOLS:
			self.assertIsInstance(self.snapshot[protocol]["version"], int)
			self.assertGreater(self.snapshot[protocol]["version"], 0)

	def test_implicit_opcode_resolves_against_the_real_header(self):
		# realm_world_packet stops numbering at LocalChatMessage = 0x05.
		self.assertIn("auth::realm_world_packet::FetchCharacterLocation = 6",
			self.snapshot["auth"]["symbols"])

	def test_unit_fields_are_captured(self):
		# The blind spot this tool grew to cover: FieldMap indices are wire layout.
		unit_fields = [s for s in self.snapshot["game"]["symbols"]
			if "object_fields::UnitFields" in s]
		self.assertTrue(unit_fields, "object_fields::UnitFields must be fingerprinted")

	def test_manifest_matches_the_tree(self):
		# Equivalent to running the tool with no arguments; stated separately so a failure
		# here reads as "someone forgot to re-record" rather than as a parser bug.
		self.assertEqual(pvc.check(self.snapshot, pvc.load_manifest()), [])


class CheckSemantics(unittest.TestCase):
	def setUp(self):
		self.current = {"auth": {"version": 5, "fingerprint": "sha256:aaa",
			"files": {"f.h": "sha256:1"}, "symbols": ["auth::x::A = 1"]}}
		self.protocols = {"auth": {"constant": "mmo::auth::ProtocolVersion",
			"version_file": "f.h", "files": ["f.h"]}}
		self._saved = pvc.PROTOCOLS
		pvc.PROTOCOLS = self.protocols

	def tearDown(self):
		pvc.PROTOCOLS = self._saved

	def manifest_from_current(self):
		manifest = json.loads(json.dumps(self.current))
		manifest["_format"] = pvc.FINGERPRINT_FORMAT
		return manifest

	def test_in_sync_is_silent(self):
		self.assertEqual(pvc.check(self.current, self.manifest_from_current()), [])

	def test_changed_surface_without_bump_is_reported(self):
		manifest = self.manifest_from_current()
		manifest["auth"]["fingerprint"] = "sha256:bbb"
		manifest["auth"]["symbols"] = []
		problems = pvc.check(self.current, manifest)
		self.assertEqual(len(problems), 1)
		self.assertIn("still 5", problems[0])
		self.assertIn("auth::x::A", problems[0])		# names what moved

	def test_bump_without_rerecord_is_reported(self):
		manifest = self.manifest_from_current()
		manifest["auth"]["version"] = 4
		manifest["auth"]["fingerprint"] = "sha256:bbb"
		problems = pvc.check(self.current, manifest)
		self.assertEqual(len(problems), 1)
		self.assertIn("not re-recorded", problems[0])

	def test_missing_manifest_is_reported(self):
		problems = pvc.check(self.current, None)
		self.assertEqual(len(problems), 1)
		self.assertIn("no fingerprint recorded", problems[0])

	def test_stale_format_is_reported_on_its_own(self):
		# A fingerprint from a different algorithm says nothing about the wire format, so
		# the per-protocol verdicts must not be reported alongside it.
		manifest = self.manifest_from_current()
		manifest["_format"] = pvc.FINGERPRINT_FORMAT - 1
		manifest["auth"]["fingerprint"] = "sha256:bbb"
		problems = pvc.check(self.current, manifest)
		self.assertEqual(len(problems), 1)
		self.assertIn("different version of this checker", problems[0])

	def test_recorded_manifest_declares_the_current_format(self):
		# Catches a re-record done with an older checker, which would otherwise only show up
		# as a confusing failure on someone else's machine.
		self.assertEqual(pvc.load_manifest().get("_format"), pvc.FINGERPRINT_FORMAT)


if __name__ == "__main__":
	unittest.main(verbosity=2)
