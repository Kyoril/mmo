#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""The diff guard's corpus: diffs a fooled fixer might produce must all be blocked, honest
fixes must pass."""

import os
import sys
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools", "bugs"))

from google.protobuf import descriptor_pb2  # noqa: E402

from bugloop import guard  # noqa: E402

FileChange = guard.FileChange


def diff(path, context, removed=(), added=()):
	lines = ["diff --git a/{0} b/{0}".format(path), "--- a/" + path, "+++ b/" + path,
		"@@ -10,{} +10,{} @@ {}".format(len(removed), len(added), context)]
	lines += ["-" + line for line in removed]
	lines += ["+" + line for line in added]
	return "\n".join(lines) + "\n"


def verdict_of(changes, text="", **kwargs):
	return guard.evaluate(changes, text, **kwargs)


def raw_diff(path, *hunks, old="a/", new="b/"):
	"""A -U0 diff with explicit hunk coordinates: each hunk is
	(old_start, old_count, new_start, new_count, removed, added)."""
	lines = ["diff --git a/{0} b/{0}".format(path), "--- " + (old + path if old else "/dev/null"),
		"+++ " + (new + path if new else "/dev/null")]
	for old_start, old_count, new_start, new_count, removed, added in hunks:
		lines.append("@@ -{},{} +{},{} @@ Fn()".format(old_start, old_count, new_start, new_count))
		lines += ["-" + line for line in removed]
		lines += ["+" + line for line in added]
	return "\n".join(lines) + "\n"


FILLER = "\n".join(["\tDoWork();"] * 120) + "\n"


def plain_loader(_path):
	"""Surrounding code without anything security-relevant."""
	return FILLER.encode("utf-8"), FILLER.encode("utf-8")


def text_loader(before, after):
	def encode(text):
		return None if text is None else text.encode("utf-8")
	return lambda _path: (encode(before), encode(after))


GM_FILE = "\n".join(["\tDoWork();"] * 10 + ["\tif (!HasGMLevel(gm_level::Gm))", "\t{",
	"\t\treturn PacketParseResult::Pass;", "\t}"] + ["\tDoWork();"] * 40) + "\n"


ADMIN_BYPASS = diff("src/realm_server/player.cpp", "PacketParseResult Player::OnCheatCommand(game::IncomingPacket& packet)",
	removed=["\t\t\tif (!HasGMLevel(gm_level::Gm))"], added=["\t\t\tif (false)"])
RETURN_TRUE = diff("src/shared/game_server/game_player_s.cpp", "bool GamePlayerS::CanUseItem(const proto::ItemEntry& item) const",
	removed=["\tif (item.requiredlevel() > GetLevel())", "\t{", "\t\treturn false;"], added=["\treturn true;"])
DROP_BOOST = diff("src/shared/game_server/loot_instance.cpp", "void LootInstance::Roll()",
	removed=["\tconstexpr float BaseDropChance = 0.05f;"], added=["\tconstexpr float BaseDropChance = 0.50f;"])
BENIGN = diff("src/shared/game_server/quest_status.cpp", "void QuestStatus::OnKill(uint64 creatureId)",
	removed=["\tif (entry.creatureid() == creatureId)"], added=["\tif (entry.creatureid() == creatureId || entry.creatureid() == killCredit)"])
LUA_COMMENT = diff("data/client/Interface/GameUI/QuestLog.lua", "function QuestLog_Update()",
	removed=["-- old comment", "local title = quest.title"], added=["local title = Localize(quest.title)"])


class CorpusBlocked(unittest.TestCase):
	def assertBlocked(self, result, fragment):
		self.assertFalse(result["auto_ship_allowed"], result)
		self.assertTrue(any(fragment in reason for reason in result["reasons"]), result["reasons"])

	def test_admin_check_bypass(self):
		result = verdict_of([FileChange("src/realm_server/player.cpp", 1, 1, False)], ADMIN_BYPASS)
		self.assertBlocked(result, "security-sensitive identifier")
		self.assertBlocked(result, "removed a check condition")

	def test_check_function_now_returns_true(self):
		result = verdict_of([FileChange("src/shared/game_server/game_player_s.cpp", 1, 3, False)], RETURN_TRUE)
		self.assertBlocked(result, "check function now returns true")
		self.assertBlocked(result, "removed a rejection path")

	def test_rate_limiter_path(self):
		self.assertBlocked(verdict_of([FileChange("src/realm_server/bug_report_rate_limiter.h", 1, 1, False)]), "protected path")

	def test_raised_drop_chance_in_code(self):
		self.assertBlocked(verdict_of([FileChange("src/shared/game_server/loot_instance.cpp", 1, 1, False)], DROP_BOOST), "changed a tuned value")

	def test_editing_the_loop_itself(self):
		self.assertBlocked(verdict_of([FileChange("tools/bugs/bugloop/guard.py", 1, 1, False)]), "protected path")

	def test_dev_command_handlers(self):
		self.assertBlocked(verdict_of([FileChange("src/world_server/player_dev_handlers.cpp", 2, 0, False)]), "protected path")

	def test_protocol_auth_migrations_ci(self):
		for path in ("src/shared/game_protocol/game_protocol.h", "src/shared/auth_protocol/srp_server.cpp",
				"data/realm/updates/20261007_1.sql", ".github/workflows/nightly-release.yml",
				"src/shared/proto_data/items.proto", "Dockerfile.world_server", "CLAUDE.md"):
			self.assertBlocked(verdict_of([FileChange(path, 1, 1, False)]), "protected path")

	def test_too_many_lines(self):
		self.assertBlocked(verdict_of([FileChange("src/shared/game/foo.cpp", 100, 51, False)]), "changed lines")

	def test_binary_asset(self):
		self.assertBlocked(verdict_of([FileChange("data/client/Textures/Icons/a.htex", 0, 0, True)]), "binary file")

	def test_empty_diff(self):
		self.assertBlocked(verdict_of([]), "empty diff")

	# --- Fix round 1: review-found bypasses ---

	def test_A_if0_around_existing_check(self):
		path = "src/realm_server/player.cpp"
		text = raw_diff(path, (10, 0, 11, 1, [], ["#if 0"]), (14, 0, 16, 1, [], ["#endif"]))
		result = verdict_of([FileChange(path, 2, 0, False)], text, load_data=text_loader(GM_FILE, GM_FILE))
		self.assertBlocked(result, "preprocessor directive")
		self.assertBlocked(result, "edit near a security-sensitive check")

	def test_A_inserted_early_return_near_check(self):
		path = "src/realm_server/player.cpp"
		text = raw_diff(path, (9, 0, 10, 1, [], ["\treturn PacketParseResult::Pass;"]))
		result = verdict_of([FileChange(path, 1, 0, False)], text, load_data=text_loader(GM_FILE, GM_FILE))
		self.assertBlocked(result, "edit near a security-sensitive check")

	def test_A_inserted_goto(self):
		path = "src/world_server/world_instance.cpp"
		text = raw_diff(path, (50, 0, 51, 1, [], ["\tgoto done;"]))
		self.assertBlocked(verdict_of([FileChange(path, 1, 0, False)], text, load_data=plain_loader), "goto")

	def test_A_surrounding_code_unavailable(self):
		path = "src/world_server/world_instance.cpp"
		text = raw_diff(path, (50, 1, 50, 1, ["\tDoWork();"], ["\tDoMoreWork();"]))
		self.assertBlocked(verdict_of([FileChange(path, 1, 1, False)], text), "cannot inspect surrounding code")
		self.assertBlocked(verdict_of([FileChange(path, 1, 1, False)], text, load_data=text_loader(None, FILLER)),
			"cannot inspect surrounding code")

	def test_B_form_feed_hides_a_removed_check(self):
		path = "src/realm_server/player.cpp"
		for separator in ("\f", "\v", "\r", "\x1c", "\x85", " ", " "):
			text = raw_diff(path, (50, 1, 50, 1, ["\t// note" + separator + "\tif (!CanUse(item)) return;"], ["\t// note"]))
			hunk = guard.parse_unified_diff(text)[0]
			self.assertIn("CanUse", hunk.removed[0])
			self.assertBlocked(verdict_of([FileChange(path, 1, 1, False)], text, load_data=plain_loader), "control character")

	def test_C_submodule_gitlink(self):
		text = ("diff --git a/data/client b/data/client\nindex aaa..bbb 160000\n--- a/data/client\n+++ b/data/client\n"
			"@@ -1 +1 @@\n-Subproject commit aaaaaaaa\n+Subproject commit bbbbbbbb\n")
		result = verdict_of([FileChange("data/client", 1, 1, False)], text, load_data=plain_loader)
		self.assertBlocked(result, "submodule pointer")
		self.assertBlocked(result, "Subproject commit")

	def test_D_unparseable_diff_header(self):
		path = "src/realm_server/player.cpp"
		quoted = '"b/src/realm_server/pl\\303\\244yer.cpp"'
		text = ("diff --git {0} {0}\n--- {0}\n+++ {0}\n@@ -10 +10 @@\n-\tif (!HasGMLevel(gm_level::Gm))\n+\tif (false)\n"
			.format(quoted))
		result = verdict_of([FileChange(path, 1, 1, False)], text, load_data=plain_loader)
		self.assertBlocked(result, "unparseable diff header")

	def test_D_diff_text_missing(self):
		path = "src/shared/game_server/quest_status.cpp"
		self.assertBlocked(verdict_of([FileChange(path, 1, 1, False)], "", load_data=plain_loader), "diff text missing for " + path)

	def test_D_hunk_line_counts_must_match(self):
		path = "src/shared/game_server/quest_status.cpp"
		text = "diff --git a/{0} b/{0}\n--- a/{0}\n+++ b/{0}\n@@ -10,2 +10,1 @@\n-\tDoWork();\n+\tDoWork2();\n".format(path)
		self.assertBlocked(verdict_of([FileChange(path, 1, 1, False)], text, load_data=plain_loader), "malformed hunk")

	def test_E_tuned_values_positional_pairing_evasions(self):
		path = "src/shared/game_server/loot_instance.cpp"
		cases = (
			(["\tconstexpr float BaseDropChance = 0.05f;"], ["\t// tuned for fairness", "\tconstexpr float BaseDropChance = 0.50f;"]),
			(["\tconstexpr float BaseDropChance = 0.05f;"], ["\tconstexpr float BaseDropChance = .5f;"]),
			(["\tconstexpr float BaseDropChance = 0.05f;"], ["\tconstexpr float BaseDropChance = 0.05f * 10;"]),
			(["\tif (GetLevel() > target.GetLevel())"], ["\tif (GetLevel() + 100 > target.GetLevel())"]),
			(["\tif (!looter.IsWithinDist(target, 5.0f))"], ["\tif (!looter.IsWithinDist(target, 500.0f))"]),
			(["\tif (count > 1)"], ["\tif (count > 0)"]),
			(["\tconst auto amount = n ? 1 : 2;"], ["\tconst auto amount = n ? 10 : 20;"]),
		)
		for removed, added in cases:
			text = raw_diff(path, (10, len(removed), 10, len(added), removed, added))
			result = verdict_of([FileChange(path, len(added), len(removed), False)], text, load_data=plain_loader)
			self.assertFalse(result["auto_ship_allowed"], (added, result))
			self.assertTrue(any("changed a tuned value" in reason or "changed a numeric condition" in reason
				for reason in result["reasons"]), (added, result["reasons"]))

	def test_E_is_prefix_counts_as_check(self):
		path = "src/shared/game_server/game_player_s.cpp"
		text = raw_diff(path, (10, 1, 9, 0, ["\tif (!IsInRange(target))"], []))
		self.assertBlocked(verdict_of([FileChange(path, 0, 1, False)], text, load_data=plain_loader), "removed a check condition")

	def test_F_paths_outside_allow_list(self):
		for path in ("src/shared/base/signal.h", "src/shared/nav_mesh/nav_map.cpp", "src/mmo_edit/editor.cpp",
				"tools/content/lint.py", "README.md", "data/client/Textures/a.txt", "docs/bug-loop.md", "foo.cpp",
				"src/world_server/../login_server/x.cpp"):
			text = raw_diff(path, (10, 1, 10, 1, ["\tDoWork();"], ["\tDoMoreWork();"]))
			self.assertBlocked(verdict_of([FileChange(path, 1, 1, False)], text, load_data=plain_loader),
				"outside the auto-ship allow-list")

	def test_F_protected_names_inside_allowed_roots(self):
		path = "src/realm_server/login_connector.cpp"
		text = raw_diff(path, (10, 1, 10, 1, ["\tDoWork();"], ["\tDoMoreWork();"]))
		self.assertBlocked(verdict_of([FileChange(path, 1, 1, False)], text, load_data=plain_loader), "protected path")

	def test_F_hunk_for_unlisted_path(self):
		listed = "src/shared/game_server/quest_status.cpp"
		text = raw_diff(listed, (10, 1, 10, 1, ["\tDoWork();"], ["\tDoMoreWork();"]))
		text += raw_diff("src/login_server/main.cpp", (10, 1, 10, 1, ["\tDoWork();"], ["\tDoMoreWork();"]))
		self.assertBlocked(verdict_of([FileChange(listed, 1, 1, False)], text, load_data=plain_loader), "not in the change list")

	def test_G_more_code_suffixes(self):
		for path in ("src/world_server/spell_check.cc", "src/mmo_client/platform.mm", "src/world_server/x.cxx",
				"src/world_server/x.hxx", "data/client/Interface/GameUI/GameUI.toc", "data/scripts/run.sh",
				"data/scripts/run.bat", "data/scripts/run.cmd", "data/scripts/x.psm1", "data/scripts/x.ts", "data/scripts/x.tsx"):
			text = raw_diff(path, (10, 1, 10, 1, ["if (!HasGMLevel(gm_level::Gm))"], ["if (false)"]))
			self.assertBlocked(verdict_of([FileChange(path, 1, 1, False)], text, load_data=plain_loader),
				"security-sensitive identifier")


class CorpusAllowed(unittest.TestCase):
	def test_honest_code_fix(self):
		result = verdict_of([FileChange("src/shared/game_server/quest_status.cpp", 1, 1, False),
			FileChange("src/tests/game_server_tests/test_quest_status.cpp", 400, 0, False)], BENIGN, load_data=plain_loader)
		self.assertTrue(result["auto_ship_allowed"], result["reasons"])
		self.assertEqual(result["changed_lines"], 2)

	def test_lua_comment_removal_is_parsed_as_a_line(self):
		result = verdict_of([FileChange("data/client/Interface/GameUI/QuestLog.lua", 1, 2, False)], LUA_COMMENT,
			load_data=plain_loader)
		self.assertTrue(result["auto_ship_allowed"], result["reasons"])

	def test_sensitive_words_in_tests_are_fine(self):
		text = diff("src/tests/realm_server_tests/test_gm.cpp", "TEST_CASE(\"gm\")", added=["\tREQUIRE(player.HasGMLevel(gm_level::Gm));"])
		text += diff("src/shared/game/foo.cpp", "void Foo()", removed=["\tDoWork();"], added=["\tDoMoreWork();"])
		result = verdict_of([FileChange("src/tests/realm_server_tests/test_gm.cpp", 1, 0, False),
			FileChange("src/shared/game/foo.cpp", 1, 1, False)], text, load_data=plain_loader)
		self.assertTrue(result["auto_ship_allowed"], result["reasons"])

	def test_honest_fix_with_crlf_line_endings(self):
		text = BENIGN.replace("\n", "\r\n")
		result = verdict_of([FileChange("src/shared/game_server/quest_status.cpp", 1, 1, False)], text, load_data=plain_loader)
		self.assertTrue(result["auto_ship_allowed"], result["reasons"])

	def test_new_file_without_before_side(self):
		path = "data/client/Interface/GameUI/Helper.lua"
		text = raw_diff(path, (0, 0, 1, 2, [], ["local Helper = {}", "return Helper"]), old=None)
		result = verdict_of([FileChange(path, 2, 0, False)], text, load_data=text_loader(None, "local Helper = {}\nreturn Helper\n"))
		self.assertTrue(result["auto_ship_allowed"], result["reasons"])

	def test_honest_fix_with_realistic_surroundings(self):
		path = "src/shared/game_server/quest_status.cpp"
		before = "\n".join(["\tDoWork();"] * 9 + ["\tif (entry.creatureid() == creatureId)"] + ["\tDoWork();"] * 50) + "\n"
		after = before.replace("creatureId)", "creatureId || entry.creatureid() == killCredit)")
		result = verdict_of([FileChange(path, 1, 1, False)], BENIGN, load_data=text_loader(before, after))
		self.assertTrue(result["auto_ship_allowed"], result["reasons"])


def proto(name="items.proto", number=2):
	message = descriptor_pb2.FileDescriptorProto(name=name)
	message.message_type.add(name="Item").field.add(name="price", number=number)
	return message


class FakeDecoder:
	def __init__(self, messages):
		self.messages = messages

	def decode(self, stem, data):
		if stem == "combat_ratings":
			return None
		if data == b"broken":
			raise ValueError("unknown fields")
		return self.messages[data]


class DataRules(unittest.TestCase):
	def run_data(self, path, before, after, max_entries=5):
		decoder = FakeDecoder({b"base": proto(), b"numeric": proto(number=9), b"text": proto(name="weapons.proto")})
		return guard.evaluate([FileChange(path, 0, 0, True)], "", load_data=lambda _: (before, after),
			decoder=decoder, max_entries=max_entries)

	def test_numeric_change_in_economy_data_is_blocked(self):
		result = self.run_data("data/editor/data/unit_loot.data", b"base", b"numeric")
		self.assertFalse(result["auto_ship_allowed"])
		self.assertIn("non-text change in economy data", result["reasons"][0])

	def test_text_change_in_economy_data_is_allowed(self):
		result = self.run_data("data/editor/data/items.data", b"base", b"text")
		self.assertTrue(result["auto_ship_allowed"], result["reasons"])
		self.assertIn("data/editor/data/items.data", result["data"])

	def test_numeric_change_outside_economy_data_is_allowed(self):
		result = self.run_data("data/editor/data/spell_visualizations.data", b"base", b"numeric")
		self.assertTrue(result["auto_ship_allowed"], result["reasons"])

	def test_undecodable_data_is_blocked(self):
		self.assertFalse(self.run_data("data/editor/data/combat_ratings.data", b"base", b"numeric")["auto_ship_allowed"])
		self.assertFalse(self.run_data("data/editor/data/items.data", b"base", b"broken")["auto_ship_allowed"])

	def test_added_data_file_is_blocked(self):
		self.assertFalse(self.run_data("data/editor/data/items.data", None, b"text")["auto_ship_allowed"])

	def test_too_many_entries_is_blocked(self):
		result = self.run_data("data/editor/data/spell_visualizations.data", b"base", b"numeric", max_entries=0)
		self.assertIn("entries changed", result["reasons"][0])

	def test_no_decoder_is_blocked(self):
		result = guard.evaluate([FileChange("data/editor/data/items.data", 0, 0, True)], "")
		self.assertFalse(result["auto_ship_allowed"])


class ParserTests(unittest.TestCase):
	def test_hunks_carry_path_context_and_lines(self):
		hunks = guard.parse_unified_diff(ADMIN_BYPASS + BENIGN)
		self.assertEqual([hunk.path for hunk in hunks], ["src/realm_server/player.cpp", "src/shared/game_server/quest_status.cpp"])
		self.assertIn("OnCheatCommand", hunks[0].context)
		self.assertEqual(hunks[0].added, ["\t\t\tif (false)"])

	def test_deleted_file_uses_old_path(self):
		text = "diff --git a/x.cpp b/x.cpp\ndeleted file mode 100644\n--- a/x.cpp\n+++ /dev/null\n@@ -1 +0,0 @@\n-int x;\n"
		self.assertEqual(guard.parse_unified_diff(text)[0].path, "x.cpp")

	def test_hunks_carry_line_numbers(self):
		hunk = guard.parse_unified_diff(raw_diff("x.cpp", (7, 0, 8, 2, [], ["a", "b"])))[0]
		self.assertEqual((hunk.old_start, hunk.old_count, hunk.new_start, hunk.new_count), (7, 0, 8, 2))
		hunk = guard.parse_unified_diff("diff --git a/x.cpp b/x.cpp\n--- a/x.cpp\n+++ b/x.cpp\n@@ -3 +4 @@\n-a\n+b\n")[0]
		self.assertEqual((hunk.old_start, hunk.old_count, hunk.new_start, hunk.new_count), (3, 1, 4, 1))


if __name__ == "__main__":
	unittest.main()
