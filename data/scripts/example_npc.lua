-- Example NPC script demonstrating event hooks.
-- Register for creature entry 1 (change to match actual NPC entry).

local script = {}

function script.OnGossipHello(creature, player)
    print("[LuaScript] Player " .. player:GetName() .. " says hello to " .. creature:GetName())
end

function script.OnGossipSelect(creature, player, optionId)
    print("[LuaScript] Player " .. player:GetName() .. " selected option " .. optionId)
end

function script.OnQuestAccept(creature, player, questId)
    print("[LuaScript] Player " .. player:GetName() .. " accepted quest " .. questId)
end

function script.OnQuestComplete(creature, player, questId)
    print("[LuaScript] Player " .. player:GetName() .. " completed quest " .. questId)
end

RegisterCreatureScript(1, script)
