-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
-- Offline fixtures for reviewing all Glue screens. Never connects to a realm.
GlueSceneFade:Hide()
AccountLogin:SetOpacity(1)
local realms = {
	{ id=1, name="Kingsroad", population=0, playerCount=42 },
	{ id=2, name="Briarwatch March", population=1, playerCount=217 },
	{ id=3, name="Development Realm", population=2, playerCount=845 },
}
function loginConnector:GetRealms()
	local i=0
	return function() i=i+1; return realms[i] end
end
function GetLastRealmId() return 1 end
function realmConnector:GetRealmName() return "Kingsroad" end
local characters = {
	{ guid=1, name="Kyoril", level=10, classId=1, raceId=0, gender=0, zone="Kingsroad Waypost" },
	{ guid=2, name="Arwyn", level=5, classId=4, raceId=0, gender=1, zone="Briarwatch March" },
}
function GetNumCharacters() return #characters end
function GetInitialCharacterIndex() return 0 end
function GetSelectedCharacterIndex() return 0 end
function IsCharacterCreationAvailable() return true end
function GetCharacterInfo(i) return characters[i+1] end
function GetClassNameById(i) return i==1 and "Warrior" or "Scout" end
function IsCharacterDisabled() return false end
function GetNumRaces() return 2 end
function GetRaceId(i) return i end
function GetRaceName(i) return i==0 and "Human" or "Dwarf" end
function GetNumClasses() return 2 end
function GetClassId(i) return i end
function GetClassName(i) return i==0 and "Mage" or "Warrior" end
function IsRaceAvailable() return true end
function IsClassAvailable() return true end
local race, class, gender = 0, 1, 0
function GetCharacterRace() return race end
function GetCharacterClass() return class end
function GetCharacterGender() return gender end
function SetCharacterRace(value) race=value end
function SetCharacterClass(value) class=value end
function SetCharacterGender(value) gender=value end
function GetNumCustomizationProperties() return 2 end
function GetCustomizationProperty(i) return i==0 and "Hair style" or "Hair color" end
function GetCustomizationValue() return "1 / 8" end
function IsCharCreateOutfitVisible() return true end
