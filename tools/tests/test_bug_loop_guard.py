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


class CorpusAllowed(unittest.TestCase):
	def test_honest_code_fix(self):
		result = verdict_of([FileChange("src/shared/game_server/quest_status.cpp", 1, 1, False),
			FileChange("src/tests/game_server_tests/test_quest_status.cpp", 400, 0, False)], BENIGN)
		self.assertTrue(result["auto_ship_allowed"], result["reasons"])
		self.assertEqual(result["changed_lines"], 2)

	def test_lua_comment_removal_is_parsed_as_a_line(self):
		result = verdict_of([FileChange("data/client/Interface/GameUI/QuestLog.lua", 1, 2, False)], LUA_COMMENT)
		self.assertTrue(result["auto_ship_allowed"], result["reasons"])

	def test_sensitive_words_in_tests_are_fine(self):
		text = diff("src/tests/realm_server_tests/test_gm.cpp", "TEST_CASE(\"gm\")", added=["\tREQUIRE(player.HasGMLevel(gm_level::Gm));"])
		result = verdict_of([FileChange("src/tests/realm_server_tests/test_gm.cpp", 1, 0, False),
			FileChange("src/shared/game/foo.cpp", 1, 1, False)], text)
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


if __name__ == "__main__":
	unittest.main()
