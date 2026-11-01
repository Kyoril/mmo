# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Static checks for the pet UI (pet frame, pet bar, Repertoire pet tab, rename dialog)."""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
CLIENT = ROOT / "data/client"
UI = CLIENT / "Interface/GameUI"
NS = {"u": "http://www.w3schools.com/Ui"}
LOCALES = sorted((CLIENT / "Locales").glob("*/Localization.txt"))

PET_LAYOUTS = ["PetActionBar.xml", "PetFrame.xml"]
PET_SCRIPTS = ["PetActionBar.lua", "PetFrame.lua", "SpellBook.lua", "ContextMenu.lua", "StaticDialog.lua"]
NEW_KEYS = ["PET", "PET_ATTACK", "PET_FOLLOW", "PET_STAY", "PET_ASSIST", "PET_PASSIVE", "PET_DISMISS",
            "PET_RENAME", "PET_RENAME_PROMPT", "PET_TAB"]


def read(path):
    return path.read_text(encoding="utf-8-sig")


def locale_keys(path):
    return re.findall(r'key = "([^"]+)"', read(path))


def frames(file):
    return {f.get("name"): f for f in ET.parse(UI / file).findall(".//u:Frame", NS)}


class PetLayoutTests(unittest.TestCase):
    def test_toc_loads_pet_layouts_after_the_player_frame_in_order(self):
        toc = [line.strip() for line in read(UI / "GameUI.toc").splitlines()]
        player = toc.index("PlayerFrame.xml")
        bar = toc.index("PetActionBar.xml")
        frame = toc.index("PetFrame.xml")
        # The pet frame anchors to the pet bar, which anchors to the player frame.
        self.assertLess(player, bar)
        self.assertLess(bar, frame)

    def test_layouts_parse_with_properties_first_and_existing_files(self):
        for file in PET_LAYOUTS:
            layout = ET.parse(UI / file)
            for frame in layout.findall(".//u:Frame", NS):
                seen_content = False
                for child in frame:
                    if child.tag.endswith("}Property"):
                        self.assertFalse(seen_content, f"{file}: {frame.get('name')}")
                    else:
                        seen_content = True
            for element in layout.iter():
                if element.get("texture"):
                    self.assertTrue((CLIENT / element.get("texture")).is_file(), element.get("texture"))
                if element.tag.endswith("}Script"):
                    self.assertTrue((UI / element.get("file")).is_file(), element.get("file"))

    def test_pet_bar_buttons(self):
        bar = frames("PetActionBar.xml")
        for i in range(1, 12):
            button = bar[f"PetActionButton{i}"]
            self.assertEqual(button.get("inherits"), "PetActionButtonTemplate")
            self.assertEqual(button.get("id"), str(i))
        self.assertNotIn(f"PetActionButton12", bar)
        # Pet buttons are not character action slots: no drag and drop handlers.
        self.assertNotIn("OnDrag", read(UI / "PetActionBar.xml"))

    def test_command_icons_exist(self):
        for icon in re.findall(r'"(Interface/[^"]+\.htex)"', read(UI / "PetActionBar.lua")):
            self.assertTrue((CLIENT / icon).is_file(), icon)

    def test_lua_frame_names_exist(self):
        names = {}
        for file in PET_LAYOUTS:
            names.update(frames(file))
        for file in ["PetActionBar.lua", "PetFrame.lua"]:
            code = re.sub(r'"[^"\n]*"', '""', read(UI / file))
            for name in set(re.findall(r"\b(Pet[A-Z][A-Za-z]*)\b(?=[:.])", code)):
                self.assertIn(name, names, f"{file}: {name}")

    def test_spellbook_pet_tab(self):
        book = frames("SpellBook.xml")
        tab = book["SpellBookTab4"]
        self.assertEqual(tab.get("id"), "4")
        lua = read(UI / "SpellBook.lua")
        self.assertIn("SPELLBOOK_TAB_PET = 4", lua)
        self.assertIn("SPELLBOOK_MAX_TABS = 4", lua)
        self.assertIn("PetCastSpell(", lua)


class PetLocaleTests(unittest.TestCase):
    def test_new_keys_in_every_locale_exactly_once(self):
        self.assertEqual(len(LOCALES), 4)
        for locale in LOCALES:
            keys = locale_keys(locale)
            for key in NEW_KEYS:
                self.assertEqual(keys.count(key), 1, f"{locale}: {key}")

    def test_localize_calls_and_xml_texts_resolve(self):
        used = set()
        for file in PET_SCRIPTS:
            keys = re.findall(r'Localize\("([A-Z0-9_]+)"\)', read(UI / file))
            # The shared scripts are only checked for the pet strings added to them.
            if not file.startswith("Pet"):
                keys = [key for key in keys if key.startswith("PET")]
            used.update(keys)
        for file in PET_LAYOUTS + ["SpellBook.xml"]:
            used.update(re.findall(r'name="Text" value="([A-Z0-9_]+)"', read(UI / file)))
        # The pet result keys PetInfo sends with PET_RESULT (src/mmo_client/systems/pet_info.cpp).
        used.update(re.findall(r'"((?:PET_RESULT|PET_ERROR|SPELL_CAST_FAILED)_[A-Z_]+)"',
                               read(ROOT / "src/mmo_client/systems/pet_info.cpp")))
        self.assertTrue(set(NEW_KEYS) - {"PET"} <= used)
        for locale in LOCALES:
            missing = sorted(used - set(locale_keys(locale)))
            self.assertEqual(missing, [], str(locale))


if __name__ == "__main__":
    unittest.main()
