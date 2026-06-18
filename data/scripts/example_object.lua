-- Example world object script demonstrating OnUse hook.
-- Register for object entry 1 (change to match actual object entry).

local script = {}

function script.OnUse(worldObject, player)
    print("[LuaScript] Player " .. player:GetName() .. " used a world object")
end

RegisterObjectScript(1, script)
