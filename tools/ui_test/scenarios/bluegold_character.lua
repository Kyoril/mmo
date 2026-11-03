-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
dofile(UiTest.scenarioDir .. "bluegold_fixture.lua")
ShowUIPanel(CharacterWindow)
CharacterWindowTitle:SetText(GetUnit("player"):GetName())
InventoryFrame:Show()
