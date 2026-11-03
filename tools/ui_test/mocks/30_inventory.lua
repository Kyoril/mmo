-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
--
-- Inventory: equipped bags. UiTest.data.bags[i] is the slot count of bag i (0-based like the client;
-- 0 = no bag). Items are not mocked yet: GetInventorySlotItem & co. still return nil.

UiTest.data.bags = { [0] = 12, [1] = 8, [2] = 0, [3] = 0 }

function GetContainerNumSlots(container)
	return UiTest.data.bags[container] or 0
end
