// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <filesystem>
#include <string>

#include "lua/lua.hpp"

namespace
{
	/// The shipped UI scripts under test, loaded in this order.
	const char* g_scripts[] =
	{
		"PetActionBar.lua",
		"PetFrame.lua",
		"SpellBook.lua",
		"ContextMenu.lua",
		"StaticDialog.lua"
	};

	/// Stand-ins for the frames and the client functions the pet UI uses. Frames are created on
	/// first use from their global name and record what the scripts do to them; client calls are
	/// recorded in `calls`. The pet API mirrors PetInfo::RegisterScriptFunctions and is driven by
	/// the `pet` table.
	const char* g_stubs = R"lua(
		calls = {};

		function record(name, arg)
			calls[#calls + 1] = { name = name, arg = arg };
		end

		function lastCall(name)
			for i = #calls, 1, -1 do
				if calls[i].name == name then
					return calls[i];
				end
			end
			return nil;
		end

		-- The client hands every handler call a new wrapper of the frame: fields a script sets on
		-- `self` are gone by the next call. Only the frame's real state (and its id / userData
		-- properties) reaches the frame.
		local frameState = { visible = true, text = true, props = true, checked = true, enabled = true, progress = true,
			unit = true, onClick = true, onEnter = true, onLeave = true, onDrag = true, id = true, userData = true };
		function Wrap(frame)
			return setmetatable({}, {
				__index = frame,
				__newindex = function(wrapper, key, value)
					if frameState[key] then
						frame[key] = value;
					else
						rawset(wrapper, key, value);
					end
				end });
		end

		local events = {};
		function FireEvent(name, ...)
			for _, registration in ipairs(events[name] or {}) do
				registration.handler(Wrap(registration.frame), ...);
			end
		end

		local Frame = {};
		Frame.__index = Frame;

		function NewFrame(name)
			local frame = setmetatable({ name = name, visible = true, text = "", props = {}, checked = false, enabled = true, progress = 0, children = {} }, Frame);
			local number = name and tonumber(name:match("(%d+)$"));
			if number then
				frame.id = number;
			end
			return frame;
		end

		function Frame:Show() self.visible = true; end
		function Frame:Hide() self.visible = false; end
		function Frame:IsVisible() return self.visible; end
		function Frame:SetText(text) self.text = text; end
		function Frame:GetText() return self.text; end
		function Frame:SetProperty(name, value) self.props[name] = value; end
		function Frame:SetProgress(progress) self.progress = progress; end
		function Frame:SetProgressInstant(progress) self.progress = progress; end
		function Frame:GetProgress() return self.progress or 0; end
		function Frame:SetChecked(checked) self.checked = checked; end
		function Frame:Enable() self.enabled = true; end
		function Frame:Disable() self.enabled = false; end
		function Frame:SetUnit(unit) self.unit = unit; end
		function Frame:GetName() return self.name; end
		function Frame:SetClickedHandler(handler) self.onClick = handler; end
		function Frame:SetOnEnterHandler(handler) self.onEnter = handler; end
		function Frame:SetOnLeaveHandler(handler) self.onLeave = handler; end
		function Frame:SetOnDragHandler(handler) self.onDrag = handler; end
		function Frame:ClearAnchors() end
		function Frame:SetAnchor() end
		function Frame:GetChild(index)
			if not self.children[index] then
				self.children[index] = NewFrame(nil);
			end
			return self.children[index];
		end
		function Frame:RegisterEvent(name, handler)
			events[name] = events[name] or {};
			table.insert(events[name], { frame = self, handler = handler });
		end

		-- Any frame of the pet UI, the Repertoire, the tooltip or the dialogs comes into being on
		-- first access, like the frames the XML layouts create.
		local framePrefixes = { "Pet", "SpellBook", "GameTooltip", "StaticDialog", "ContextMenu", "ErrorFrame" };
		setmetatable(_G, { __index = function(globals, name)
			if type(name) ~= "string" then
				return nil;
			end
			for _, prefix in ipairs(framePrefixes) do
				if name:sub(1, #prefix) == prefix then
					local frame = NewFrame(name);
					rawset(globals, name, frame);
					return frame;
				end
			end
			return nil;
		end });

		AnchorPoint = { TOP = 0, BOTTOM = 1, LEFT = 2, RIGHT = 3, H_CENTER = 4, V_CENTER = 5 };
		TOOLTIP_LINE_LEFT = 0;

		function Localize(key) return "L:" .. key; end
		function UIErrorFrame_AddMessage(message, color) record("UIErrorFrame_AddMessage", message); end
		function GameTooltip_Clear() end
		function GameTooltip_AddLine(line) record("GameTooltip_AddLine", line); end
		function GameTooltip_SetSpell(spell) record("GameTooltip_SetSpell", spell.id); end
		function TargetUnit(unit) record("TargetUnit", unit); end
		function GetCursorPosition() return { x = 10, y = 20 }; end
		function GetSpell(index) return nil; end
		function PickupSpell(id) record("PickupSpell", id); end
		function CastSpellById(id) record("CastSpellById", id); end
		function PlaySound(path) end
		function AddMenuBarButton() end
		function IsPassiveSpell(spell) return spell.passive == true; end

		pet = { persistent = false, temporary = false, spells = {}, strategy = 0, levelMode = 0, xp = 0, nextXp = 0, canRename = false, cooldowns = {}, unit = nil };

		function HasPetUI() return pet.persistent or pet.temporary; end
		function HasPersistentPet() return pet.persistent; end
		function HasTemporaryPets() return pet.temporary; end
		function GetNumPetSpells() return #pet.spells; end
		function GetPetSpell(index) return pet.spells[index + 1]; end
		function PetCastSpell(index) record("PetCastSpell", index); end
		function GetPetSpellCooldown(index) return (pet.cooldowns[index] or { 0, 0 })[1]; end
		function GetPetSpellCooldownDuration(index) return (pet.cooldowns[index] or { 0, 0 })[2]; end
		function PetAttack() record("PetAttack"); end
		function PetFollow() record("PetFollow"); end
		function PetStay() record("PetStay"); end
		function PetSetStrategy(strategy) record("PetSetStrategy", strategy); end
		function GetPetStrategy() return pet.strategy; end
		function GetPetLevelMode() return pet.levelMode; end
		function PetDismiss() record("PetDismiss"); end
		function PetRename(name) record("PetRename", name); end
		function CanRenamePet() return pet.canRename; end
		function GetPetXp() return pet.xp, pet.nextXp; end

		function GetUnit(name)
			if name == "pet" then
				return pet.unit;
			end
			return nil;
		end

		function MakeSpell(id, passive)
			return { id = id, name = "Spell" .. id, icon = "Icon" .. id, rank = 0, passive = passive };
		end

		function MakePetUnit()
			local unit = {};
			function unit:GetName() return "Rex"; end
			function unit:GetLevel() return 7; end
			function unit:GetHealth() return 50; end
			function unit:GetMaxHealth() return 200; end
			function unit:GetPowerType() return 0; end
			function unit:GetPower() return 30; end
			function unit:GetMaxPower() return 60; end
			return unit;
		end
	)lua";

	/// Runs a chunk and returns an empty string, or the Lua error it raised.
	std::string RunChunk(lua_State* state, const char* chunk)
	{
		if (luaL_loadstring(state, chunk) != LUA_OK || lua_pcall(state, 0, 0, 0) != LUA_OK)
		{
			return lua_tostring(state, -1);
		}

		return {};
	}

	/// Loads the stubs and the pet UI scripts, runs the frames' OnLoad handlers and then the test
	/// chunk, whose assert() calls decide the result. Returns "ok" or the first error.
	std::string RunPetUi(const char* test)
	{
		lua_State* state = luaL_newstate();
		REQUIRE(state != nullptr);
		luaL_openlibs(state);

		std::string result = RunChunk(state, g_stubs);
		for (const char* script : g_scripts)
		{
			if (!result.empty())
			{
				break;
			}

			const std::string path = std::string(MMO_UI_SCRIPT_DIR) + "/" + script;
			if (luaL_loadfile(state, path.c_str()) != LUA_OK || lua_pcall(state, 0, 0, 0) != LUA_OK)
			{
				result = lua_tostring(state, -1);
			}
		}

		if (result.empty())
		{
			result = RunChunk(state, "PetActionBar_OnLoad(PetActionBar); PetFrame_OnLoad(PetFrame); SpellBook_OnLoad(SpellBook);");
		}

		if (result.empty())
		{
			result = RunChunk(state, test);
		}

		lua_close(state);
		return result.empty() ? "ok" : result;
	}
}

TEST_CASE("Pet UI scripts are available", "[pets][pet_ui]")
{
	for (const char* script : g_scripts)
	{
		REQUIRE(std::filesystem::exists(std::string(MMO_UI_SCRIPT_DIR) + "/" + script));
	}
}

TEST_CASE("Pet bar is hidden without pets", "[pets][pet_ui]")
{
	CHECK(RunPetUi(R"lua(
		assert(not PetActionBar.visible, "bar visible without a pet");
		pet.persistent = true;
		FireEvent("PET_CHANGED");
		assert(PetActionBar.visible, "bar hidden with a persistent pet");
		pet.persistent = false;
		FireEvent("PET_CHANGED");
		assert(not PetActionBar.visible, "bar still visible after the pet left");
	)lua") == "ok");
}

TEST_CASE("Pet bar shows only the commands for temporary pets", "[pets][pet_ui]")
{
	CHECK(RunPetUi(R"lua(
		pet.temporary = true;
		pet.spells = { MakeSpell(1, false) };
		FireEvent("PET_CHANGED");
		assert(PetActionBar.visible, "bar hidden with temporary pets");
		for i = 1, PET_ACTION_COMMAND_BUTTONS do
			assert(_G["PetActionButton" .. i].visible, "command " .. i .. " hidden");
		end
		for i = PET_ACTION_COMMAND_BUTTONS + 1, PET_ACTION_BUTTONS do
			assert(not _G["PetActionButton" .. i].visible, "ability " .. i .. " visible");
		end
	)lua") == "ok");
}

TEST_CASE("Pet bar fills active abilities and casts them by pet spell index", "[pets][pet_ui]")
{
	CHECK(RunPetUi(R"lua(
		pet.persistent = true;
		pet.spells = { MakeSpell(10, true), MakeSpell(11, false), MakeSpell(12, false) };
		FireEvent("PET_SPELLS_CHANGED");

		local first = _G["PetActionButton" .. (PET_ACTION_COMMAND_BUTTONS + 1)];
		local second = _G["PetActionButton" .. (PET_ACTION_COMMAND_BUTTONS + 2)];
		local third = _G["PetActionButton" .. (PET_ACTION_COMMAND_BUTTONS + 3)];
		assert(first.visible and first.props.Icon == "Icon11", "first ability is not the first active spell");
		assert(second.visible and second.props.Icon == "Icon12", "second ability wrong");
		assert(not third.visible, "unused ability slot visible");

		first.onClick(Wrap(first), "LEFT");
		assert(lastCall("PetCastSpell") and lastCall("PetCastSpell").arg == 1, "cast did not use the pet spell index");

		first.onEnter(first);
		assert(lastCall("GameTooltip_SetSpell").arg == 11, "ability tooltip shows the wrong spell");
	)lua") == "ok");
}

TEST_CASE("Pet bar command buttons send the pet commands", "[pets][pet_ui]")
{
	CHECK(RunPetUi(R"lua(
		pet.persistent = true;
		FireEvent("PET_CHANGED");
		PetActionButton1.onClick(Wrap(PetActionButton1), "LEFT");
		PetActionButton2.onClick(Wrap(PetActionButton2), "LEFT");
		PetActionButton3.onClick(Wrap(PetActionButton3), "LEFT");
		PetActionButton5.onClick(Wrap(PetActionButton5), "LEFT");
		assert(lastCall("PetAttack"), "attack not sent");
		assert(lastCall("PetFollow"), "follow not sent");
		assert(lastCall("PetStay"), "stay not sent");
		assert(lastCall("PetDismiss"), "dismiss not sent");

		PetActionButton1.onEnter(PetActionButton1);
		assert(lastCall("GameTooltip_AddLine").arg == "L:PET_ATTACK", "attack tooltip not localized");
	)lua") == "ok");
}

TEST_CASE("Pet bar strategy toggle reflects and switches the strategy", "[pets][pet_ui]")
{
	CHECK(RunPetUi(R"lua(
		pet.persistent = true;
		pet.strategy = 0;
		FireEvent("PET_CHANGED");
		local toggle = _G["PetActionButton" .. PET_ACTION_STRATEGY];
		assert(toggle.checked, "toggle not checked while assisting");

		toggle.onEnter(toggle);
		assert(lastCall("GameTooltip_AddLine").arg == "L:PET_ASSIST", "assist tooltip missing");

		toggle.onClick(Wrap(toggle), "LEFT");
		assert(lastCall("PetSetStrategy").arg == 1, "toggle did not switch to passive");

		pet.strategy = 1;
		FireEvent("PET_CHANGED");
		assert(not toggle.checked, "toggle still checked while passive");
		toggle.onClick(Wrap(toggle), "LEFT");
		assert(lastCall("PetSetStrategy").arg == 0, "toggle did not switch to assist");
	)lua") == "ok");
}

TEST_CASE("Pet bar ability cooldowns are shown", "[pets][pet_ui]")
{
	CHECK(RunPetUi(R"lua(
		pet.persistent = true;
		pet.spells = { MakeSpell(11, false) };
		pet.cooldowns[0] = { 5, 10 };
		FireEvent("PET_SPELLS_CHANGED");
		local cooldown = _G["PetActionButton" .. (PET_ACTION_COMMAND_BUTTONS + 1)]:GetChild(0);
		assert(cooldown.visible, "cooldown hidden");
		assert(math.abs(cooldown.progress - 0.5) < 0.001, "cooldown progress " .. tostring(cooldown.progress));

		pet.cooldowns[0] = nil;
		FireEvent("PET_SPELLS_CHANGED");
		assert(not cooldown.visible, "cooldown still visible");
	)lua") == "ok");
}

TEST_CASE("Pet results are shown localized in the error frame", "[pets][pet_ui]")
{
	CHECK(RunPetUi(R"lua(
		FireEvent("PET_RESULT", "PET_RESULT_INVALID_NAME");
		assert(lastCall("UIErrorFrame_AddMessage").arg == "L:PET_RESULT_INVALID_NAME", "error not shown");
	)lua") == "ok");
}

TEST_CASE("Pet frame shows the pet and an experience bar only for pets that level on their own", "[pets][pet_ui]")
{
	CHECK(RunPetUi(R"lua(
		assert(not PetFrame.visible, "pet frame visible without a pet");

		pet.persistent = true;
		pet.unit = MakePetUnit();
		pet.levelMode = 0;
		pet.xp = 25;
		pet.nextXp = 100;
		FireEvent("PET_CHANGED");
		assert(PetFrame.visible, "pet frame hidden");
		assert(PetName.text == "Rex", "pet name " .. tostring(PetName.text));
		assert(PetPortraitLevel.text == "7", "pet level");
		assert(math.abs(PetHealthBar.progress - 0.25) < 0.001, "health progress");
		assert(math.abs(PetManaBar.progress - 0.5) < 0.001, "power progress");
		assert(PetExperienceBar.visible, "xp bar hidden for an own-level pet");
		assert(math.abs(PetExperienceBar.progress - 0.25) < 0.001, "xp progress");

		pet.xp = 50;
		FireEvent("PET_XP_UPDATE");
		assert(math.abs(PetExperienceBar.progress - 0.5) < 0.001, "xp update ignored");

		pet.levelMode = 1;
		FireEvent("PET_CHANGED");
		assert(not PetExperienceBar.visible, "xp bar visible for a master-level pet");

		pet.unit = nil;
		FireEvent("PET_CHANGED");
		assert(not PetFrame.visible, "pet frame visible without a pet unit");
	)lua") == "ok");
}

TEST_CASE("Pet frame clicks target the pet and open its menu", "[pets][pet_ui]")
{
	CHECK(RunPetUi(R"lua(
		-- ContextMenu.lua and StaticDialog.lua replaced the recording stubs while loading.
		function ContextMenu_Show(menu, x, y, data) record("ContextMenu_Show", menu); end
		local renameDialog = { editBox = NewFrame("RenameEditBox") };
		function StaticDialog_Show(which) record("StaticDialog_Show", which); return renameDialog; end

		PetFrame_OnClick(Wrap(PetFrame), "LEFT", 0, 0);
		assert(lastCall("TargetUnit").arg == "pet", "left click did not target the pet");
		PetFrame_OnClick(Wrap(PetFrame), "RIGHT", 0, 0);
		assert(lastCall("ContextMenu_Show").arg == "PET_FRAME", "right click did not open the pet menu");

		pet.persistent = true;
		pet.canRename = false;
		local items = ContextMenus["PET_FRAME"].items(nil);
		assert(#items == 1 and items[1].text == "L:PET_DISMISS", "menu without rename is wrong");
		items[1].callback();
		assert(lastCall("PetDismiss"), "dismiss not sent");

		pet.canRename = true;
		pet.unit = MakePetUnit();
		items = ContextMenus["PET_FRAME"].items(nil);
		assert(#items == 2 and items[1].text == "L:PET_RENAME", "rename missing");
		items[1].callback();
		assert(lastCall("StaticDialog_Show").arg == "RENAME_PET", "rename dialog not shown");
		assert(renameDialog.editBox:GetText() == "Rex", "rename dialog not prefilled with the pet's name");
	)lua") == "ok");
}

TEST_CASE("Rename dialog renames the pet from its edit box", "[pets][pet_ui]")
{
	CHECK(RunPetUi(R"lua(
		local dialog = StaticDialogs["RENAME_PET"];
		assert(dialog and dialog.hasEditBox, "rename dialog has no edit box");
		assert(dialog.text == "L:PET_RENAME_PROMPT", "rename prompt not localized");
		StaticDialog.editBox = { GetText = function() return "Fang"; end };
		dialog.OnAccept();
		assert(lastCall("PetRename").arg == "Fang", "rename not sent");

		calls = {};
		StaticDialog.editBox = { GetText = function() return ""; end };
		dialog.OnAccept();
		assert(lastCall("PetRename") == nil, "empty name sent");
	)lua") == "ok");
}

TEST_CASE("Repertoire pet tab lists the pet spells while a persistent pet is out", "[pets][pet_ui]")
{
	CHECK(RunPetUi(R"lua(
		SpellBook_UpdatePage();
		assert(not SpellBookTab4.visible, "pet tab visible without a pet");

		pet.persistent = true;
		pet.spells = { MakeSpell(10, true), MakeSpell(11, false) };
		FireEvent("PET_SPELLS_CHANGED");
		assert(SpellBookTab4.visible, "pet tab hidden with a pet");
		assert(SpellBookTab4.text == "L:PET_TAB", "pet tab text");

		SpellBook_SelectTab(SPELLBOOK_TAB_PET);
		assert(SpellBookButton01.visible and SpellBookButton01:GetChild(0).text == "Spell10", "passive spell not listed");
		assert(SpellBookButton02.visible and SpellBookButton02:GetChild(0).text == "Spell11", "active spell not listed");
		assert(not SpellBookButton03.visible, "empty slot visible");

		SpellButton_OnClick(Wrap(SpellBookButton01), "RIGHT");
		assert(lastCall("PetCastSpell") == nil, "passive pet spell cast");
		SpellButton_OnClick(Wrap(SpellBookButton02), "LEFT");
		assert(lastCall("PetCastSpell").arg == 1, "pet spell not cast by its index");
		assert(lastCall("PickupSpell") == nil and lastCall("CastSpellById") == nil, "pet spell used as a player spell");

		pet.persistent = false;
		pet.spells = {};
		FireEvent("PET_CHANGED");
		assert(not SpellBookTab4.visible, "pet tab still visible");
		assert(SpellBookTab == SPELLBOOK_TAB_GENERAL, "selected tab did not fall back");
	)lua") == "ok");
}
