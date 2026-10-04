-- e2e-timeout: 86400
--
-- Long-lived puppet for staging screenshots with headless party members.
--
-- The bot polls a command file (SHOWCASE_CMD) and runs every new block of Lua it finds there,
-- so a capture session can choreograph several bots live: move them into formation, cast at a
-- target, post party chat. A block is "new" when the file's first line, a `-- <sequence>`
-- comment, changes. Between commands the bot optionally follows the leader (SHOWCASE_LEADER)
-- so it stays in frame.
--
-- Run with: e2e_client -c <bot config> -s tools/showcase/director.lua -t 86400

local cmdPath = os.getenv("SHOWCASE_CMD")
Assert(cmdPath ~= nil and cmdPath ~= "", "SHOWCASE_CMD must name the command file")

Following = os.getenv("SHOWCASE_LEADER")
FollowDistance = 3.5
FollowOffset = { x = 0, z = 0 }

--- Moves next to the leader (with this bot's formation offset) when it drifted too far.
local function KeepUp()
	if Following == nil or Following == "" then
		return
	end

	local leader = FindUnitByName(Following)
	if leader == nil then
		return
	end

	local lx, ly, lz = GetPosX(leader), GetPosY(leader), GetPosZ(leader)
	local tx, tz = lx + FollowOffset.x, lz + FollowOffset.z
	local dx, dz = GetPosX(Me()) - tx, GetPosZ(Me()) - tz
	if dx * dx + dz * dz > FollowDistance * FollowDistance then
		MoveTo(tx, ly, tz, 8000)
	end
end

local lastSequence = nil

--- Reads the command file's sequence line without running it.
local function PeekSequence()
	local file = io.open(cmdPath, "r")
	if file == nil then
		return nil
	end
	local first = file:read("*l")
	file:close()
	return first and first:match("^%-%-%s*(%S+)") or nil
end

--- True once a newer command arrived; long-running commands poll this to hand over control.
function Interrupted()
	local sequence = PeekSequence()
	return sequence ~= nil and sequence ~= lastSequence
end

--- Waits for an invite and accepts it. Only accept once one arrived: the realm disconnects a
--- client that accepts with no invitation pending.
function AcceptInvite(timeoutMs)
	if IsInParty() then
		return true
	end
	if not WaitUntil(HasPendingPartyInvitation, timeoutMs or 60000, "party invitation") then
		return false
	end
	AcceptPartyInvitation()
	return WaitUntil(IsInParty, 10000, "joined the party")
end

--- GUID of a player to keep alive during staged fights (the un-godmoded hero), or nil.
Protect = os.getenv("SHOWCASE_PROTECT")

--- Heals (or revives) the protected player. Selection changes briefly; spells are cast with an
--- explicit target, so the fight is unaffected.
function ProtectHero()
	if Protect == nil or Protect == "" then
		return
	end
	TargetUnit(Protect)
	if UnitExists(Protect) and not IsAlive(Protect) then
		GM.Revive()
	end
	GM.Heal()
end

--- Fights the nearest unit with `entry` for `seconds`, cycling through `spells` (spell ids cast
--- at the target; 0 = keep auto-attacking only). Power and cooldowns are refilled so the effects
--- keep coming, and the target is healed whenever it drops below `floorPct` so a staged fight
--- never ends by accident.
function Fight(entry, spells, seconds, floorPct, gapMs)
	local target = FindUnitByEntry(entry)
	if target == nil then
		Log("fight: no unit " .. entry)
		return false
	end

	TargetUnit(target)
	FaceUnit(target)
	StartAttack(target)
	local untilMs = seconds * 1000
	local elapsed = 0
	local i = 0
	while elapsed < untilMs and not Interrupted() do
		if not IsAlive(target) then
			break
		end
		ProtectHero()
		TargetUnit(target)
		GM.RestorePower()
		GM.ResetCooldowns()
		if #spells > 0 then
			i = i % #spells + 1
			if spells[i] ~= 0 then
				CastSpell(spells[i], target)
			end
		end
		local maxHealth = GetMaxHealth(target)
		if floorPct ~= nil and maxHealth > 0 and GetHealth(target) < maxHealth * floorPct / 100 then
			TargetUnit(target)
			GM.Heal()
		end
		Sleep(gapMs or 1600)
		elapsed = elapsed + (gapMs or 1600)
	end
	StopAttack()
	return true
end

--- Staged bots must never stay dead in a shot, and godmode does not survive a map change.
local lastGuard = 0
local function Guard()
	lastGuard = lastGuard + 1
	if lastGuard % 20 ~= 0 then
		return
	end
	if not IsAlive(Me()) then
		TargetUnit(Me())
		GM.Revive()
		Sleep(500)
	end
	GM.Godmode(true)
end

Log("director ready, polling " .. cmdPath)

while true do
	local file = io.open(cmdPath, "r")
	if file ~= nil then
		local text = file:read("*a")
		file:close()

		local sequence = text:match("^%-%-%s*(%S+)")
		if sequence ~= nil and sequence ~= lastSequence then
			lastSequence = sequence
			local chunk, err = load(text, "=" .. cmdPath)
			if chunk == nil then
				Log("command " .. sequence .. " does not compile: " .. err)
			else
				local ok, runErr = pcall(chunk)
				Log("command " .. sequence .. (ok and " done" or (" failed: " .. tostring(runErr))))
			end
		end
	end

	Guard()
	KeepUp()
	Sleep(250)
end
