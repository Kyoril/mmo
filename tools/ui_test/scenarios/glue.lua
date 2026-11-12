-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
-- Offline fixtures for reviewing all Glue screens (run with --glue). Never connects to a realm.
-- Switch screens with: lua RealmList_Show()  /  lua CharList_Show()  /  lua CharCreate_Show()
GlueSceneFade:Hide()
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
	{ guid=3, name="Maelis", level=14, classId=0, raceId=0, gender=1, zone="Haven" },
	{ guid=4, name="Thorgar", level=8, classId=2, raceId=1, gender=0, zone="Oakenshire" },
}
local selected = 0
function SelectCharacter(i) selected = i end
function GetSelectedCharacterIndex() return selected end
function MoveCharacterInOrder(from, to)
	local c = table.remove(characters, from + 1)
	table.insert(characters, to + 1, c)
	if selected == from then selected = to end
end
function SaveCharacterOrder() end
function RotateCharacter() end
function SetLastCharacter() end
function GetNumCharacters() return #characters end
function GetInitialCharacterIndex() return 0 end
function IsCharacterCreationAvailable() return true end
function GetCharacterInfo(i) return characters[i+1] end
function GetCharacterZoneName(i) return characters[i+1] and characters[i+1].zone or "" end
function SetCharacterPreview(frame, i)
	UiTest.previewIndex = i
	frame:SetProperty("Visible", i >= 0 and "true" or "false")
end
local classNames = { [0]="Mage", [1]="Warrior", [2]="Cleric", [3]="Acolyte", [4]="Scout" }
function GetClassNameById(i) return classNames[i] end
function IsCharacterDisabled() return false end
function GetNumRaces() return 2 end
function GetRaceId(i) return i end
function GetRaceName(i) return i==0 and "Human" or "Dwarf" end
function GetNumClasses() return 5 end
function GetClassId(i) return i end
function GetClassName(i) return classNames[i] end
function IsRaceAvailable() return true end
function IsClassAvailable() return true end
local race, class, gender = 0, 1, 0
function GetCharacterRace() return race end
function GetCharacterClass() return class end
function GetCharacterGender() return gender end
function SetCharacterRace(value) race=value end
function SetCharacterClass(value) class=value end
function SetCharacterGender(value) gender=value end
local properties = { "Skin color", "Face", "Hair style", "Hair color" }
function GetNumCustomizationProperties() return #properties end
function GetCustomizationProperty(i) return properties[i+1] end
function GetCustomizationValue() return "1 / 8" end
function IsCharCreateOutfitVisible() return true end
