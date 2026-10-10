-- e2e-own-character: hca
-- e2e-timeout: 420
--
-- Every Hollow Choir boss uses its whole rotation in a straight fight. The phase changes (adds,
-- Mourning Chorus, Choral Resonance, seals) are covered by the encounter scenarios; this one stays
-- in melee with each boss and expects every recurring ability to go off, and to come back, and
-- the ground zones and the Dirge to actually hit (we stand in them; godmode keeps us alive):
--
--   Oswin   Grave Strike (251, creature AI, ~10 s)   Guttering Candle (253, trigger, 14-16 s) -> 254
--   Mereth  Lament (255, creature AI, 18 s)          Silent Place (258, trigger, 15-19 s)     -> 259
--   Veyr    Dirge of the Grave (262, AI, 25 s) -> 263  Dissonance (260, trigger, 12 s)        -> 261
--
-- The AI abilities are the ones that silently went missing when a boss's spell ranges made the
-- creature AI treat it as a caster, or when the melee AI never cast at all. Silent Place aims at a
-- random player other than the tank and falls back to the tank when nobody else is there.

local SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z = 0, 292.267, 5.33, 552.571
local WINDOW_MS = 55000

-- entry, label, landing point (from hollow_choir_boss_chase.lua), abilities as
-- { spell, name, casts expected within the window }.
local BOSSES = {
	{ 86, "Oswin",  { -6.5, 0.5, -30.0 }, {
		{ 251, "Grave Strike", 2 }, { 253, "Guttering Candle", 2 }, { 254, "Guttering Candle detonation", 1 } } },
	{ 87, "Mereth", { -127.0, -2.5, 8.0 }, {
		{ 255, "Lament", 2 }, { 258, "Silent Place", 2 }, { 259, "Silent Place pulse", 1 } } },
	{ 88, "Veyr",   { -91.0, 5.7, -15.0 }, {
		{ 262, "Dirge of the Grave", 2 }, { 263, "Dirge pulse", 1 }, { 260, "Dissonance", 2 }, { 261, "Dissonance wave", 1 } } },
}

local function Counts(boss, abilities)
	local parts = {}
	for _, a in ipairs(abilities) do
		parts[#parts + 1] = a[2] .. "=" .. SpellGoCount(boss, a[1])
	end
	return table.concat(parts, ", ")
end

local function AllSeen(boss, abilities)
	for _, a in ipairs(abilities) do
		if SpellGoCount(boss, a[1]) < a[3] then
			return false
		end
	end
	return true
end

local function Fight(entry, label, landing, abilities)
	GM.Worldport(1, landing[1], landing[2], landing[3], 0)
	Assert(WaitUntil(function() return GetPosX(Me()) < 100 end, 30000, label .. ": arrived"),
		label .. ": the worldport into the Hollow Choir should land")
	Sleep(3000)
	-- Godmode does not survive the map change.
	GM.Godmode(true)

	local boss = nil
	Assert(WaitUntil(function() boss = FindUnitByEntry(entry); return boss ~= nil end, 30000, label .. " visible"),
		label .. " should be spawned")
	Assert(WaitUntil(function() return GetHealth(boss) > 0 end, 30000, label .. " health arrives"),
		label .. ": current health never replicated")
	local maxHealth = GetMaxHealth(boss)

	TargetUnit(boss)
	FaceUnit(boss)
	GM.DamageTarget(50)
	StartAttack(boss)
	Assert(WaitUntil(function() return GetHealth(boss) < maxHealth end, 10000, label .. " pulled"),
		label .. ": the hit should open the encounter")

	-- Stay on the boss; keep facing it so the auto attack keeps the fight going.
	WaitUntil(function() FaceUnit(boss); return AllSeen(boss, abilities) end, WINDOW_MS, label .. " rotation")
	Log(label .. ": " .. Counts(boss, abilities))
	for _, a in ipairs(abilities) do
		Assert(SpellGoCount(boss, a[1]) >= a[3], label .. " should use " .. a[2] .. " (" .. a[1]
			.. ") at least " .. a[3] .. "x in " .. (WINDOW_MS / 1000) .. " s (" .. Counts(boss, abilities) .. ")")
	end
	StopAttack()
end

GM.Godmode(true)
GM.Worldport(SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) > 0 end, 30000, "at the spawn point"),
	"player should start from the default spawn")

for _, b in ipairs(BOSSES) do
	Fight(b[1], b[2], b[3], b[4])
end

-- Leave the instance so the bosses reset behind us.
GM.Worldport(SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) > 0 end, 30000, "back at the spawn point"),
	"player should leave the Hollow Choir")
Assert(IsAlive(Me()), "the character should survive the scenario")
