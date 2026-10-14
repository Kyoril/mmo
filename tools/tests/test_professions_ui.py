# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Static checks for the profession window (Crafting.xml / Crafting.lua) and its wiring."""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
CLIENT = ROOT / "data/client"
UI = CLIENT / "Interface/GameUI"
NS = {"u": "http://www.w3schools.com/Ui"}
LOCALES = sorted((CLIENT / "Locales").glob("*/Localization.txt"))

NEW_KEYS = [
    "MENUBAR_TOOLTIP_PROFESSIONS",
    "MENUBAR_TOOLTIP_PROFESSIONS_LOCKED",
    "BINDING_TOGGLEPROFESSIONS",
    "SPELL_CAST_FAILED_SKILL_TOO_LOW",
    "SPELL_CAST_FAILED_INVENTORY_FULL",
    "TRAINER_ERROR_SKILL_TOO_LOW",
]


def read(path):
    return path.read_text(encoding="utf-8-sig")


def locale_keys(path):
    return re.findall(r'key = "([^"]+)"', read(path))


def used_craft_keys():
    lua = read(UI / "Crafting.lua")
    keys = {"CRAFT_" + k for k in re.findall(r'caption\("([A-Z_]+)"\)', lua)}
    # caption(cond and "A" or "B") style calls
    for call in re.findall(r"caption\(([^)]*)\)", lua):
        keys.update("CRAFT_" + k for k in re.findall(r'"([A-Z_]+)"', call))
    keys.update(re.findall(r'value="(CRAFT_[A-Z_]+)"', read(UI / "Crafting.xml")))
    return keys


class ProfessionLayoutTests(unittest.TestCase):
    def test_layout_names_property_order_and_textures(self):
        layout = ET.parse(UI / "Crafting.xml")
        names = []
        for frame in layout.findall(".//u:Frame", NS):
            names.append(frame.get("name"))
            seen_content = False
            for child in frame:
                if child.tag.endswith("}Property"):
                    self.assertFalse(seen_content, frame.get("name"))
                else:
                    seen_content = True
        self.assertEqual(len(names), len(set(names)))
        for element in layout.iter():
            if element.get("texture"):
                self.assertTrue((CLIENT / element.get("texture")).is_file(), element.get("texture"))
        self.assertIn("Crafting.xml", read(UI / "GameUI.toc"))

    def test_lua_references_existing_frames(self):
        names = {f.get("name") for f in ET.parse(UI / "Crafting.xml").findall(".//u:Frame", NS)}
        lua = read(UI / "Crafting.lua")
        # Series frames are addressed as _G["CraftingRow" .. i]; the series must exist.
        for prefix in set(re.findall(r'_G\["(Crafting[A-Za-z]*)" \.\. ', lua)):
            self.assertIn(prefix + "1", names)
        # Plain identifiers (outside string literals) must be frames or Lua globals.
        code = re.sub(r'"[^"\n]*"', '""', lua)
        for name in set(re.findall(r"\b(Crafting[A-Z][A-Za-z]*)\b", code)):
            self.assertIn(name, names)

    def test_native_templates(self):
        frames = {}
        for file in ["GameTemplates.xml", "Crafting.xml"]:
            frames.update({f.get("name"): f for f in ET.parse(UI / file).findall(".//u:Frame", NS)})
        for name in ["CraftingCreate", "CraftingAll", "CraftingBack"]:
            self.assertEqual(frames[name].get("inherits"), "GlueButton")
        self.assertEqual(frames["CraftingScrollBar"].get("inherits"), "VerticalScrollBar")
        for i in range(1, 10):
            self.assertEqual(frames[f"CraftingCategory{i}"].get("inherits"), "CraftingCategoryButton")
            self.assertEqual(frames[f"CraftingRow{i}"].get("inherits"), "CraftingRecipeButton")
        for i in range(1, 11):
            for prefix in ("CraftingProfession", "CraftingProfessionName", "CraftingProfessionSkill", "CraftingProfessionIcon"):
                self.assertIn(f"{prefix}{i}", frames)


class MockRemovedTests(unittest.TestCase):
    def test_no_mock_left(self):
        self.assertFalse((UI / "CraftingMockData.lua").exists())
        for path in (CLIENT / "Interface").rglob("*"):
            if path.suffix in (".lua", ".xml", ".toc"):
                text = read(path)
                self.assertNotIn("CraftingMock", text, str(path))
        xml = read(UI / "Crafting.xml")
        for name in ("CraftingTarget", "CraftingReset", "CraftingWork"):
            self.assertNotIn(f'name="{name}"', xml)
        # Profession names come from game data, never from XML text.
        self.assertNotRegex(xml, r'value="CRAFT_(MINING|TAILOR|BLACKSMITH|ALCHEMY)')


class ProfessionLocaleTests(unittest.TestCase):
    def test_used_craft_keys_exist_in_every_locale_and_nothing_else_remains(self):
        used = used_craft_keys()
        self.assertTrue(used)
        for locale in LOCALES:
            keys = locale_keys(locale)
            craft_list = [k for k in keys if k.startswith("CRAFT_")]
            craft = set(craft_list)
            self.assertEqual(len(craft_list), len(craft), str(locale))
            self.assertEqual(craft, used, str(locale))

    def test_new_keys_in_every_locale(self):
        for locale in LOCALES:
            keys = set(locale_keys(locale))
            for key in NEW_KEYS:
                self.assertIn(key, keys, f"{locale}: {key}")

    def test_localize_calls_resolve(self):
        keys_per_locale = [set(locale_keys(l)) for l in LOCALES]
        for key in re.findall(r'Localize\("([A-Z_]+)"\)', read(UI / "Crafting.lua")):
            for keys in keys_per_locale:
                self.assertIn(key, keys)


class ProfessionWiringTests(unittest.TestCase):
    def test_binding_and_default_key(self):
        self.assertIn('name="TOGGLEPROFESSIONS"', read(CLIENT / "Interface/Bindings.xml"))
        self.assertRegex(read(CLIENT / "Config/DefaultBindings.cfg"), r"(?m)^bind K TOGGLEPROFESSIONS\s*$")

    def test_menu_icon_and_trainer_error(self):
        self.assertTrue((UI / "Alestia/MenuIcons/Professions.htex").is_file())
        self.assertIn('"TRAINER_ERROR_SKILL_TOO_LOW"', read(UI / "TrainerFrame.lua"))

    def test_lock_and_events(self):
        lua = read(UI / "Crafting.lua")
        self.assertIn("HasProfessions()", lua)
        for event in ("TRADE_SKILLS_CHANGED", "TRADE_SKILL_UPDATE", "SPELL_LEARNED",
                      "PLAYER_SPELL_CAST_FINISH", "PLAYER_SPELL_CAST_FAILED"):
            self.assertIn(f'RegisterEvent("{event}"', lua)


if __name__ == "__main__":
    unittest.main()
