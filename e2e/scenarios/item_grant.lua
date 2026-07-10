-- Verifies the inventory pipeline: GM-granted items show up in the
-- client-visible inventory with the right stack count.
--
-- Uses: Chunk of Boar Meat (item 1, stackable).

local BOAR_MEAT = 1

local before = GetItemCount(BOAR_MEAT)
Log("Starting with " .. before .. " boar meat")

GM.AddItem(BOAR_MEAT, 3)

Assert(WaitUntil(function() return GetItemCount(BOAR_MEAT) >= before + 3 end, 10000, "items arrive in inventory"),
	"item count should increase by 3 after GM.AddItem (before " .. before .. ", now " .. GetItemCount(BOAR_MEAT) .. ")")

Log("Item grant verified: " .. GetItemCount(BOAR_MEAT) .. " boar meat in inventory")
