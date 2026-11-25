-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
-- Wayfinder unit frames in use: a casting target, a pet, a party of three and the player's cast.
dofile(UiTest.scenarioDir .. "bluegold_fixture.lua")

UiTest.data.units.target = UiTest.MakeUnit({
	name = "Kingsroad Hexer", level = 11, health = 640, maxHealth = 900,
	power = { [0] = 310 }, maxPower = { [0] = 500 },
	hostile = true, friendly = false,
	castSpellName = "Shadow Bolt", castProgress = 0.62,
});
UiTest.FireEvent("PLAYER_TARGET_CHANGED");

UiTest.data.units.pet = UiTest.MakeUnit({
	name = "Ashfang", level = 9, health = 310, maxHealth = 420,
	powerType = 2, power = { [2] = 70 }, maxPower = { [2] = 100 },
});
UiTest.data.pet = {
	persistent = true, strategy = 0, levelMode = 0, xp = 120, nextLevelXp = 400,
	spells = {},
};
UiTest.FireEvent("PET_CHANGED");

UiTest.data.units.party1 = UiTest.MakeUnit({ name = "Brynja", level = 14, health = 880, maxHealth = 1100, power = { [0] = 420 }, maxPower = { [0] = 600 } });
UiTest.data.units.party2 = UiTest.MakeUnit({ name = "Corwin", level = 9, health = 210, maxHealth = 640, powerType = 2, power = { [2] = 85 }, maxPower = { [2] = 100 } });
UiTest.data.units.party3 = UiTest.MakeUnit({ name = "Thorgar", level = 12, health = 1310, maxHealth = 1310, powerType = 1, power = { [1] = 30 }, maxPower = { [1] = 100 } });
UiTest.data.party = { size = 3, leader = 1 };
function GetPartySize() return UiTest.data.party.size; end
function HasPartyMember(index) return index >= 1 and index <= UiTest.data.party.size; end
function GetPartyLeaderIndex() return UiTest.data.party.leader; end
UiTest.FireEvent("PARTY_MEMBERS_CHANGED");

UiTest.FireEvent("PLAYER_SPELL_CAST_START", { name = "Fireball", casttime = 2500 }, 2500);
CastBarProgress = 1.1;
