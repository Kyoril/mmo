# E2E Test Harness

End-to-end gameplay tests against a real (but isolated) login + realm + world server
stack, driven by a headless Lua-scripted client (`e2e_client`). Designed so that an AI
agent or a developer can verify server-side gameplay changes with one command and
deterministic, machine-readable results.

## One-command run

```powershell
# Prerequisites (once per shell):
$env:MMO_E2E_MYSQL_PASSWORD = "<mysql password>"   # user via MMO_E2E_MYSQL_USER, default root

# Build what the harness needs:
cmake --build build -t e2e_client login_server realm_server world_server --config Debug

# Run the whole suite (brings the stack up, runs every scenario, tears down):
powershell -File tools/e2e/e2e_run.ps1
```

- Exit code `0` = every scenario had its expected result, `1` otherwise.
- Per-scenario JSONL transcripts and a `summary.json` land in `e2e/runtime/logs/`.
- `-Scenario <name>` runs a single scenario, `-KeepStack` leaves the servers running
  (then use `-NoStack` on subsequent runs for a fast inner loop, and
  `tools/e2e/e2e_down.ps1` when done).

The test stack uses its own ports (auth 13724, realm 18129, REST 18090/18092) and
throwaway databases (`mmo_login_e2e`, `mmo_realm_e2e`), so it coexists with a running
dev stack. Servers must be built with `-DMMO_WITH_DEV_COMMANDS=ON` (the documented
Windows configure line already does this).

## Writing a scenario

Scenarios are Lua files in `e2e/scenarios/`. Each runs in a fresh client process with a
fresh character (GM level 3), spawned into map 0. A scenario passes when it reaches the
end of the script; it fails on the first failed `Assert`/`Fail` (exit 1), watchdog
timeout (exit 3), or lost connection (exit 4).

```lua
-- Example: verify a GM-granted item arrives in the inventory.
local BOAR_MEAT = 1

GM.AddItem(BOAR_MEAT, 3)
Assert(WaitUntil(function() return GetItemCount(BOAR_MEAT) >= 3 end, 10000, "items arrive"),
	"item count should reach 3")
```

**Unit guids are strings** (e.g. `"0xf1300000280000d4"`) — they don't fit in a Lua
number. Treat them as opaque handles.

### API reference

Control:
| Function | Notes |
|---|---|
| `Log(msg)` | Into console output and transcript |
| `Sleep(ms)` | Keeps the connection pumping |
| `Assert(cond [, msg])` | Fails the scenario if `cond` is falsy |
| `Fail(msg)` | Fails immediately |
| `WaitUntil(fn [, timeoutMs [, desc]]) -> bool` | Polls `fn` while pumping; default 10s |

Session (for scenarios that deliberately provoke a disconnect):
| Function | Notes |
|---|---|
| `ExpectDisconnect()` | Losing the realm connection stops being a failure. Arm it **before** the disconnect can happen |
| `IsDisconnected() -> bool` | True once the realm connection has been lost |
| `LastKickReason() -> string` | `"none"` \| `"logged_in_elsewhere"` \| `"banned"` |
| `LoginElsewhere([timeoutMs]) -> bool` | Opens a second login-server session on the same account and waits for it to authenticate; default 15s |
| `Reconnect([timeoutMs]) -> bool` | Logs back in and returns to the world over the **same** connectors; default 30s |

Queries (`g` is a guid string; numeric queries return `-1` for unknown units):
`Me()`, `UnitExists(g)`, `GetHealth(g)`, `GetMaxHealth(g)`, `GetLevel(g)`, `GetPower(g)`,
`GetMaxPower(g)`, `IsAlive(g)`, `GetName(g)`, `GetPosX/Y/Z(g)`, `GetDistance(a, b)`,
`HasAura(g, spellId)`, `HasSpell(spellId)`, `GetItemCount(itemId)`, `GetMoney()`,
`GetXp()`, `GetNextLevelXp()` (own character only),
`LastCastResult()` (`"none" | "pending" | "started" | "ok" | "failed:<reason>"`),
`FindUnitByEntry(entry) -> g|nil`, `FindUnitByName(name) -> g|nil`,
`FindObjectByEntry(entry) -> g|nil` (world objects: chests, doors, ...),
`GetObjectState(g)` (the object's State field; doors: 0 = closed, 1 = open),
`GetStandState(g)` (0 = Stand, 1 = Sit, 2 = Sleep, 3 = Dead, 4 = Kneel),
`GetSitPoseEmote(g)`, `GetMoodEmote(g)`

Actions:
`TargetUnit(g)`, `FaceUnit(g)`, `CastSpell(spellId [, g]) -> bool` (faces the target
automatically), `CastSpellOnObject(spellId, g) -> bool` (world-object target, e.g.
the Open spell on a door), `CancelCast()`, `StartAttack(g)`, `StopAttack()`,
`MoveTo(x, y, z [, timeoutMs]) -> bool` (nav-mesh pathing), `SendChat(msg)`,
`DoEmote(emoteId)` (emote 4 = Sit, a pose emote that toggles: performing it again
stands the character up), `CyclePose()`,
`SendAreaTrigger(areaTriggerId)` (reports area-trigger entry — the real client
detects the overlap locally, the headless client reports it explicitly; the
server validates the player's position, so walk inside the area first)

GM commands (server must run with dev commands; the test account has GM level 3):
`GM.AddItem(itemId, count)`, `GM.LearnSpell(spellId)`, `GM.LearnEmote(emoteId)`, `GM.LevelUp(levels)`,
`GM.ClassLevelUp(levels)` (levels the active class, not the character),
`GM.GiveMoney(copper)`, `GM.CreateMonster(entry) -> g` (waits for the spawn),
`GM.DestroyMonster(g)`, `GM.CreateObject(entry [, state]) -> g` (spawns a temporary
world object at the player's position facing the player's direction; waits for the
spawn; doors: state 0 = closed), `GM.DestroyObject(g)`,
`GM.CheckLoS(g) -> bool` (server-side line of sight check from the player to the
object/unit with that guid; waits for the result — check against a unit on the far
side of a door, never the door itself, since its own geometry occludes its center),
`GM.KillTarget()` (tags an untagged creature to the GM
character first, so the kill grants real kill xp and quest kill credit),
`GM.Worldport(map, x, y, z, facing)` (waits for the teleport),
`GM.SetSpeed(multiplier)`, `GM.AcceptQuest(questId)` (no questgiver needed),
`GM.TurnInQuest(questId [, rewardChoice])` (quest must be objective-complete; no
quest ender needed), `GM.ClearInventory()` (destroys all backpack items, keeps
equipment), `GM.DamageTarget(amount)` (deals raw damage to the current target through the
normal damage path, so `OnHealthDroppedBelow` triggers, threat and death handling
all run — use it to walk a boss across its phase thresholds instead of depending
on the test character's damage output, which cannot chew through an elite's
health pool quickly or reliably; also tags the creature like `GM.KillTarget`),
`GM.SetInstanceVariable(key, value)`,
`GM.Godmode(enable)` (toggles damage immunity on the GM character;
school-agnostic, covers auto-attacks, periodic auras and spell effects alike —
use to survive content the level-10 test character otherwise cannot, and disable
it again before asserting anything about the fight's actual outcome)

### Gotchas

- `Reconnect` deliberately reuses the existing connector objects rather than building fresh
  ones, because that is what the game client does — it keeps one connector for the whole
  process. Session state left installed on a connector (the packet cipher, above all) is
  invisible to any test that reconnects with a new object. A stale cipher surfaces as
  "Received a malformed packet" the instant the realm answers, never as a decryption error.
- `Reconnect` clears any `ExpectDisconnect()` the scenario armed: the new session is a fresh
  one, so a drop after it fails the run again unless you arm it a second time.
- `LoginElsewhere` stops at the login server on purpose: the duplicate-login displacement
  fires as soon as `LogonProof` succeeds, so picking a realm and a character would only make
  the scenario slower. It also means the second session never enters the world — don't reach
  for it as a way to get a second character in play.
- `GM.Godmode` does **not survive a cross-map teleport**. Porting to another map spawns the
  character into a new world instance (the world log shows a second "wants to join world")
  and the flag is lost with the old one. Re-arm it after the final `GM.Worldport`, or the
  character dies to whatever it was sent to fight. That death is easy to misread: it drops
  the character from every threat list, and a creature whose threat list empties resets to
  full health — so the visible symptom is "the boss keeps resetting", not "I died".
- `GM.CreateMonster` spawns the monster **at the player's position**. Spells with an
  in-front requirement can never validate at zero distance — step aside first with
  `GM.Worldport` (see `spell_cast_smoke.lua`).
- Character names are 3–12 letters, no digits (realm-side validation).
- The default test character is a Mage (mana), so it can cast most spells after
  `GM.LearnSpell`. Useful ids on the current dev data: Fireball = spell 4,
  Frost Armor (instant self-buff with aura) = spell 6, Training Dummy = creature 40,
  Chunk of Boar Meat = item 1.
- The server never echoes your own movement back, so `GetPosX/Y/Z(Me())` and
  `GetDistance` resolve the self position from the client's simulated movement state
  (kept honest by server-side movement validation).
- Scenarios expected to fail (negative controls) are registered in `$expectedFailures`
  inside `tools/e2e/e2e_run.ps1`.
- Header directives (first comment lines of a scenario): `-- e2e-class: <id>` picks the
  character class (each class gets its own character), `-- e2e-timeout: <seconds>`
  extends the 120s default for long scenarios, and `-- e2e-own-character: <letters>`
  gives the scenario a dedicated character so it does not share progression state with
  other scenarios of the same class (see `quest_path_1_to_10.lua`, which must start
  at level 1).
- The runner retries a scenario **once** on infrastructure failures (exit 2/3/4 —
  connect, timeout, disconnect) because a reconnect within ~1s of the previous
  session can race server-side session cleanup. Assertion failures (exit 1) are
  never retried. Retries show up in the console output and as `attempts` in
  `summary.json`.

## Reading results (for agents)

- `e2e/runtime/logs/summary.json` — suite outcome, per-scenario exit codes and durations.
- `e2e/runtime/logs/<scenario>.jsonl` — one JSON event per line: actions, assertion
  results, `wait_until` outcomes, errors, final `scenario_end` with exit code. Flushed
  per line, so it's complete even after a crash.
- `e2e/runtime/logs/<scenario>.out.log` — full client console output.
- Server logs: `e2e/runtime/{login,realm,world}/logs/*.log` — the world log is where
  movement-validation and spell-validation errors show up.

## Layout

```
tools/e2e/           e2e_up.ps1 / e2e_down.ps1 / e2e_run.ps1 / e2e_common.psm1
src/e2e_client/      headless client (ported mmo_bot core) + Lua scenario engine
e2e/scenarios/       committed test scenarios (*.lua)
e2e/runtime/         generated: configs, pids, logs, transcripts (gitignored)
```
