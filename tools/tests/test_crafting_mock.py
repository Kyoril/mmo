# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Crafting UI checks. Run with Python; install lupa for the Lua behavior checks."""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "data/client/Interface/GameUI"
NS = {"u": "http://www.w3schools.com/Ui"}
try:
    from lupa import LuaRuntime
except ImportError:
    LuaRuntime = None


class CraftingLayoutTests(unittest.TestCase):
    def test_layout_assets_names_and_property_order(self):
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
                self.assertTrue((ROOT / "data/client" / element.get("texture")).is_file())
        for icon in re.findall(r'icon = "([^"]+)"', (UI / "CraftingMockData.lua").read_text(encoding="utf-8")):
            self.assertTrue((ROOT / "data/client" / icon).is_file(), icon)
        self.assertIn("Crafting.xml", (UI / "GameUI.toc").read_text(encoding="utf-8"))

    def test_locale_coverage(self):
        expected = None
        for locale in (ROOT / "data/client/Locales").glob("*/Localization.txt"):
            keys = re.findall(r'key = "(CRAFT_[^"]+)"', locale.read_text(encoding="utf-8-sig"))
            self.assertEqual(len(keys), len(set(keys)), str(locale))
            if expected is None:
                expected = set(keys)
            self.assertEqual(set(keys), expected, str(locale))
        xml_keys = set(re.findall(r'value="(CRAFT_[^"]+)"', (UI / "Crafting.xml").read_text(encoding="utf-8")))
        self.assertTrue(xml_keys <= expected)

    def test_native_bindings_and_distinct_control_templates(self):
        frames = {}
        for file in ["GameTemplates.xml", "Crafting.xml"]:
            frames.update({f.get("name"): f for f in ET.parse(UI / file).findall(".//u:Frame", NS)})

        def declared_properties(frame):
            names = {p.get("name") for p in frame.findall("u:Property", NS)}
            if frame.get("inherits"):
                names.update(declared_properties(frames[frame.get("inherits")]))
            return names

        for frame in ET.parse(UI / "Crafting.xml").findall(".//u:Frame", NS):
            for binding in frame.findall("./u:Visual//u:PropertyValue", NS):
                self.assertIn(binding.get("property"), declared_properties(frame), frame.get("name"))
        for name in ["CraftingCreate", "CraftingAll", "CraftingBack", "CraftingReset"]:
            self.assertEqual(frames[name].get("inherits"), "GlueButton")
        self.assertNotIn("CraftingNext", frames)
        self.assertNotIn("CraftingCancel", frames)
        self.assertEqual(frames["CraftingScrollBar"].get("inherits"), "VerticalScrollBar")
        self.assertEqual(frames["CraftingCategoryButton"].get("renderer"), "ButtonRenderer")
        for i in range(1, 10):
            self.assertEqual(frames[f"CraftingCategory{i}"].get("inherits"), "CraftingCategoryButton")
            self.assertEqual(frames[f"CraftingRow{i}"].get("inherits"), "CraftingRecipeButton")


@unittest.skipIf(LuaRuntime is None, "Install lupa to run Lua behavior checks")
class CraftingBehaviorTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime()
        strings = dict(re.findall(r'key = "([^"]+)", string = "([^"]*)"',
                                 (ROOT / "data/client/Locales/Locale_enUS/Localization.txt").read_text(encoding="utf-8")))
        self.lua.globals().translations = self.lua.table_from(strings)
        self.lua.execute('''
            function Localize(key) assert(translations[key], key); return translations[key]; end
            SlashCmdList = {}; UIPanelWindows = {};
            local methods = {}
            function methods:SetText(text)
                if self.text == text then return; end
                self.text = text; if self.changed then self.changed(self); end
            end
            function methods:GetText() return self.text or ""; end
            function methods:SetProperty(key, value)
                assert(self.properties[key] ~= nil, self.name .. ": undeclared property " .. key);
                if key == "Visible" then self.visible = value == "true"; end
                self[key] = value;
            end
            function methods:SetProgress(value) self.progress = value; end
            function methods:SetEnabled(value) assert(type(value) == "boolean"); self.enabled = value; end
            function methods:Show() self.visible = true; end
            function methods:Hide() self.visible = false; end
            function methods:IsVisible() return self.visible or false; end
            function methods:SetChecked(value) self.checked = value; end
            function methods:SetCheckable(value) self.checkable = value; end
            function methods:IsChecked() return self.checked; end
            function methods:SetClickedHandler(handler) self.click = handler; end
            function methods:SetOnTextChangedHandler(handler) self.changed = handler; end
            function methods:SetOnMouseWheelHandler(handler) self.wheel = handler; end
            function methods:SetOnEnterHandler(handler) self.enter = handler; end
            function methods:SetOnLeaveHandler(handler) self.leave = handler; end
            function methods:ClearAnchors() end
            function methods:SetAnchor(...) end
            function methods:SetMinimum(value) self.minimum = value; end
            function methods:SetMaximum(value)
                self.maximum = value;
                if (self.value or 0) > value then self:SetValue(value); end
            end
            function methods:SetStep(value) self.step = value; end
            function methods:GetValue() return self.value or 0; end
            function methods:SetValue(value)
                value = math.max(self.minimum or 0, math.min(value, self.maximum or 0));
                if self.value == value then return; end
                self.value = value; if self.valueChanged then self.valueChanged(self, value); end
            end
            function methods:SetOnValueChangedHandler(handler) self.valueChanged = handler; end
            function methods:GetChild(index) return self; end
            function methods:Click()
                if self.checkable then self.checked = not self.checked; end
                self.click(self);
            end
            function ShowUIPanel(frame) frame:Show(); end
            function HideUIPanel(frame) frame:Hide(); Crafting_Stop(); end
            function MakeFrame(name, properties)
                _G[name] = setmetatable({name = name, properties = properties,
                    checkable = properties.Checkable == "true"}, {__index = methods});
            end
            AnchorPoint = { LEFT = 1, RIGHT = 2, TOP = 3 };
            GetItemDisplayIcon = function() return "Interface/Icons/Items/Tex_meat_11_b.htex"; end
            GetCachedItemInfo = function(id) if itemReady then return {id = id}; end end
            GameTooltip_SetItemTemplate = function(item) tooltipItem = item.id; end
            MakeFrame("GameTooltip", {});
        ''')
        frames = {}
        for file in ["GameTemplates.xml", "Crafting.xml"]:
            for frame in ET.parse(UI / file).findall(".//u:Frame", NS):
                frames[frame.get("name")] = frame

        def properties(frame):
            result = {key: "" for key in ["Text", "Font", "Visible", "Enabled", "Clickable", "Color", "ProgressColor"]}
            if frame.get("inherits"):
                result.update(properties(frames[frame.get("inherits")]))
            result.update({p.get("name"): p.get("value", "") for p in frame.findall("u:Property", NS)})
            return result

        for frame in ET.parse(UI / "Crafting.xml").findall(".//u:Frame", NS):
            self.lua.globals().MakeFrame(frame.get("name"), self.lua.table_from(properties(frame)))
        for file in ["Crafting.lua", "CraftingMockData.lua"]:
            self.lua.execute((UI / file).read_text(encoding="utf-8"))
        self.lua.execute("Crafting_OnLoad(CraftingFrame)")

    def check(self, code):
        self.lua.execute(code)

    def test_all_professions_and_recipe_localizations(self):
        self.check('''
            assert(#CraftingMock.professions == 10);
            for i, p in ipairs(CraftingMock.professions) do
                Crafting_SelectProfession(i);
                for _, recipe in ipairs(p.recipes) do
                    CraftingMock.selected = recipe.id; Crafting_Refresh();
                    assert(CraftingResult:GetText() == Localize("CRAFT_" .. recipe.key));
                    local seen = {};
                    for _, r in ipairs(recipe.reagents) do assert(not seen[r.key]); seen[r.key] = true; end
                end
            end
        ''')

    def test_batch_consumes_samples_and_cancels_without_extra_consumption(self):
        self.check('''
            Crafting_SelectProfession(1); CraftingMock.quantity = 2; Crafting_Start(false);
            Crafting_Update(CraftingFrame, 1.3);
            assert(CraftingMock.materials.COPPER == 22); assert(CraftingMock.job.remaining == 1);
            Crafting_Stop(); Crafting_Update(CraftingFrame, 10);
            assert(CraftingMock.materials.COPPER == 22);
            Crafting_Reset(); Crafting_Start(true);
            for i = 1, 12 do Crafting_Update(CraftingFrame, 1.3); end
            assert(CraftingMock.materials.COPPER == 0); assert(CraftingMock.job == nil);
            assert(not CraftingCreate.enabled); assert(CraftingMock.profession.skill == 41);
            Crafting_Back(); assert(string.find(CraftingProfessionSkill1.text, "41", 1, true));
        ''')

    def test_filters_selection_and_collapsed_categories(self):
        self.check('''
            Crafting_SelectProfession(3); CraftingSearch:SetText("wool");
            assert(string.find(CraftingRow2.text, "Green wool vest", 1, true)); CraftingRow2:Click();
            assert(CraftingMock.selected == "TAILOR_WOOL_VEST"); CraftingFavorite:Click();
            CraftingSearch:SetText(""); CraftingFavorites:Click();
            assert(string.find(CraftingRow2.text, "Green wool vest", 1, true)); assert(not CraftingRow3.visible);
            local selected = CraftingMock.selected;
            CraftingCategory1:Click(); assert(not CraftingRow2.visible);
            assert(not CraftingCategory1.checked); assert(CraftingMock.selected == selected);
            CraftingCategory1:Click(); assert(CraftingRow2.visible);
            assert(not CraftingCategory1.checked); assert(CraftingRow2.checked);
            CraftingSearch:SetText("no match"); assert(CraftingEmpty.visible);
        ''')

    def test_enchant_target_and_shortage(self):
        self.check('''
            Crafting_SelectProfession(6); Crafting_Start(false); assert(CraftingMock.job == nil);
            CraftingTarget:Click(); Crafting_Start(false); assert(CraftingMock.job ~= nil);
            Crafting_Update(CraftingFrame, 1.3); assert(CraftingMock.materials.DUST == 1);
            Crafting_Start(false); assert(CraftingMock.job == nil);
            Crafting_SelectProfession(8); assert(CraftingCreate.enabled);
            CraftingMock.selected = "COOKING_STEW"; Crafting_Refresh(); assert(not CraftingCreate.enabled);
            CraftingAvailable:Click();
            for i = 1, 9 do
                local row = _G["CraftingRow" .. i];
                assert(not row.visible or not string.find(row.text, "Hearty stew", 1, true));
            end
        ''')

    def test_hide_and_profession_switch_stop_batches(self):
        self.check('''
            Crafting_Toggle(); assert(CraftingFrame.visible);
            Crafting_SelectProfession(1); Crafting_Start(true);
            Crafting_SelectProfession(3); assert(CraftingMock.job == nil);
            Crafting_Start(true); Crafting_Toggle(); assert(CraftingMock.job == nil);
            assert(not CraftingFrame.visible); assert(CraftingMock.materials.CLOTH == 18);
        ''')

    def test_virtual_scroll_reuses_rows_and_preserves_recipe_selection(self):
        self.check('''
            Crafting_SelectProfession(1);
            local p = CraftingMock.profession;
            for i = 1, 20 do
                table.insert(p.recipes, { id = "extra_" .. i, key = "COPPER_BAR", category = "METALS",
                    description = "DESC", icon = p.icon, skillUp = false, reagents = {{ key = "COPPER", count = 1 }} });
            end
            Crafting_Refresh(); assert(CraftingScrollBar.enabled);
            local row = CraftingRow3;
            CraftingScrollBar:SetValue(9); CraftingRow3:Click();
            local id = CraftingMock.selected; assert(id == "extra_9");
            assert(CraftingRow3 == row); assert(CraftingRow3.checked);
            CraftingList.wheel(CraftingList, -1); assert(CraftingMock.offset == 10);
            assert(CraftingRow2.checked); assert(not CraftingRow3.checked);
            CraftingFavorite:Click(); CraftingFavorites:Click();
            assert(CraftingMock.offset == 0); assert(CraftingMock.selected == id);
            assert(CraftingRow2.checked); assert(not CraftingScrollBar.enabled);
        ''')

    def test_only_one_recipe_is_highlighted_and_categories_never_select(self):
        self.check('''
            Crafting_SelectProfession(2);
            CraftingRow3:Click(); CraftingRow3:Click();
            assert(CraftingMock.selected == "BLACKSMITH_COPPER_VEST");
            local count = 0;
            for i = 1, 9 do
                if _G["CraftingRow" .. i].checked then count = count + 1; end
                assert(not _G["CraftingCategory" .. i].checked);
            end
            assert(count == 1);
            CraftingCategory1:Click(); assert(CraftingMock.collapsed.ARMOR);
            assert(CraftingCategory2.visible); assert(not CraftingRow3.checked);
            assert(string.find(CraftingRow3.text, "Rough grinding stone", 1, true));
            CraftingCategory1:Click(); assert(not CraftingMock.collapsed.ARMOR);
            assert(CraftingRow3.checked); assert(not CraftingCategory1.checked);
        ''')

    def test_reagent_tooltip_waits_for_item_cache_and_hides_on_leave(self):
        self.check('''
            Crafting_SelectProfession(1);
            assert(CraftingReagentIcon1.visible); assert(not CraftingReagentIcon3.visible);
            CraftingReagentIcon1.enter(); assert(not GameTooltip.visible);
            itemReady = true; Crafting_Update(CraftingFrame, 0.3);
            assert(GameTooltip.visible); assert(tooltipItem == 1);
            CraftingReagentIcon1.leave(); assert(not GameTooltip.visible);
            CraftingReagentIcon2.enter(); assert(GameTooltip.visible);
            Crafting_Back(); assert(not GameTooltip.visible);
        ''')

    def test_create_button_switches_to_cancel_and_back(self):
        self.check('''
            Crafting_SelectProfession(1);
            CraftingCreate:Click();
            assert(CraftingMock.job); assert(CraftingCreate.enabled);
            assert(CraftingCreate.text == Localize("CRAFT_CANCEL"));
            CraftingCreate:Click();
            assert(not CraftingMock.job);
            assert(CraftingCreate.text == Localize("CRAFT_CREATE"));
            CraftingCreate:Click(); Crafting_Update(CraftingFrame, 1.3);
            assert(not CraftingMock.job);
            assert(CraftingCreate.text == Localize("CRAFT_CREATE"));
            Crafting_SelectProfession(6); CraftingTarget:Click();
            CraftingCreate:Click(); assert(CraftingCreate.text == Localize("CRAFT_CANCEL"));
            CraftingCreate:Click(); assert(CraftingCreate.text == Localize("CRAFT_ENCHANT"));
            Crafting_SelectProfession(1); Crafting_Start(true);
            assert(CraftingCreate.enabled); CraftingCreate:Click(); assert(not CraftingMock.job);
        ''')


if __name__ == "__main__":
    unittest.main()
