# GM Player Observation — Design

Date: 2026-10-10 · Branch: `feature/gm-player-observation`

## Goal

A game master can observe a player or a group — in the open world and inside their dungeon
instance — without being seen and without influencing the run. Needed for the dungeon
observation session on the weekend of 2026-10-17.

Success criteria:

1. `port <name>` puts the GM into the **same instance** as the target player, even when the
   GM is not in the target's group and even when the GM is already in another copy of that
   dungeon.
2. `invisible on|off` hides the GM from players and creatures; other GMs still see them.
3. Invisibility and godmode survive teleports, map changes and relogs.
4. An observing GM does not change the instance: no binding written, no instance trigger,
   no effect on player count.

## Agreed decisions

| Question | Decision |
|---|---|
| Observation style | GM walks along invisibly with their own character and camera. No spectator camera, no auto-follow. |
| Invisibility scope | World only. Friends list, guild, whisper and chat channels still show the GM online. |
| Persistence | Across map changes **and** relog (stored in the realm DB). |
| Permission | `invisible`: gm level 1 (Gm), same as `port`. `godmode` stays at SeniorGm (2). |

Out of scope (documented as known limits): hiding online presence, hiding the GM's own
active actions (spell casts, `/say`, emotes, loot rolls are broadcast unfiltered — an
observer should only watch), a spectator camera, auto-follow, making `summon` instance-aware.

## 1. Joining the target's instance

Today `Player::OnCheatTeleportToPlayer` (`realm_server/player.cpp`) fetches the target's map
and position and calls `SendTeleportRequest(map, pos, facing)`. The instance the GM ends up in
is resolved later from the **GM's own** bindings (`ResolveDungeonInstanceId` in
`OnMoveWorldPortAck`), and the world node treats a same-map teleport as an in-instance
relocation.

Changes:

- **Realm, `OnCheatTeleportToPlayer`:** when the target is online, read its instance id
  (`Player::GetWorldInstanceId()`) together with the location. Store on the GM's realm
  `Player` a pending *instance override* `{ mapId, instanceId }` and pass the instance id
  in the teleport request. Offline targets keep today's behaviour (DB location, no override).
- **Realm → world `TeleportRequest`:** gains an `InstanceId` field (nil = no preference).
  This is a wire change: bump `mmo::auth::ProtocolVersion`, run
  `python tools/protocol_version_check.py --update`, record it in
  `docs/protocol_versions.md`.
- **World, `RealmConnector::OnTeleportRequest`:** if the instance id is set and differs from
  the character's current instance, the teleport must take the cross-map transfer path even
  when the map id is equal (a new "force transfer" variant of `GameUnitS::Teleport`, or an
  explicit flag). Otherwise behaviour is unchanged.
- **Realm, `OnMoveWorldPortAck`:** if a pending override exists and its map equals the
  transfer map, use its instance id instead of `ResolveDungeonInstanceId`. The override is
  cleared at the end of every `OnMoveWorldPortAck`, used or not, so a failed teleport cannot
  leave a stale override behind. `GetIdealWorldNode(map, instanceId)` already routes to the
  node hosting that instance. If the instance no longer exists the world node creates a new
  one (existing `PlayerCharacterJoin` behaviour) — acceptable.
- **No binding while visiting:** `OnWorldJoined` / `OnWorldChanged` store a group or
  personal binding after every join. A join that used an override writes **no** binding, so
  the GM never redirects their own group's binding or picks up a personal lock on the
  players' instance. Implemented as a flag on the realm `Player` set when the override is
  consumed and checked by the binding code.

## 2. GM invisibility

New command `invisible on|off`, mirroring `godmode`:

- **Client:** console command `invisible` in `world_state.cpp` (inside
  `MMO_WITH_DEV_COMMANDS`, `ConsoleCommandCategory::Gm`), handler in
  `world_state_commands.cpp`, packet writer `RealmConnector::CheatInvisible(bool)`.
- **Opcode:** new `client_realm_packet::CheatInvisible` appended to the client opcode enum.
  Wire change: bump `mmo::game::ProtocolVersion`, run the protocol check, record it.
- **Realm:** added to the proxy-handler registration and to the `OnProxyPacket` gm-level
  switch at `gm_level::Gm`. Before forwarding, the realm persists the flag (section 3).
- **World:** `OnCheatInvisible` in `player_dev_handlers.cpp` sets
  `GameUnitS::SetGmInvisible(bool)`.

Server semantics (`shared/game_server/objects/game_unit_s.*`):

- New member `m_gmInvisible` with `SetGmInvisible` / `IsGmInvisible`.
- `NotifyVisibilityChanged` checks it first: while set, visibility is `unit_visibility::Off`
  regardless of `ModVisibility` / `ModStealth` auras. Turning it off falls through to the
  normal aura-derived computation. Both directions go through `SetVisibility`, so existing
  despawn / respawn per subscriber (`UpdateVisibilityAndView`) is reused.
- `Off` is already visible only to `IsGameMaster()` observers and already ignored by creature
  idle/alert/combat target selection (`CanBeSeenBy`) — no AI change needed.
- AoE effects do not check visibility; godmode covers damage, which is why the GM is expected
  to observe with both flags on.

Instance participation (`shared/game_server/world/world_instance.cpp`):

- A player who is GM-invisible when added to the instance is not counted: no
  `++m_playerCount`, no `StartInstanceTimers`, no `OnPlayerEnterInstance` trigger. The
  instance remembers which players it counted so removal stays symmetric (a GM who toggles
  invisibility inside the instance does not desync the count; toggling does not retroactively
  count or uncount).
- Instances are not destroyed by player count (no caller of `DestroyInstance` depends on it),
  so an uncounted GM inside an otherwise empty instance is safe. Player count drives timers,
  the `PlayerCount` trigger condition and `OnAllPlayersDead`, all of which should ignore the
  observer.

Feedback: when the flag changes, and on every world join while a GM flag is active, the realm
sends the GM a `ChatType::System` chat message ("GM invisibility is on." / "... off.",
"Godmode is on."), so a GM who relogs knows they are still hidden. Plain English like the
realm's existing system messages (e.g. the whisper "No player named ..." reply); GM-only
diagnostics, not player-facing UI, so no Localization.txt keys.

## 3. Persistence

- **Migration** `data/realm/updates/20261010_1_character_gm_flags.sql`:
  `ALTER TABLE characters ADD COLUMN gm_flags TINYINT UNSIGNED NOT NULL DEFAULT 0;`
  Additive, so a rollback to the previous build keeps working. Mirror it in
  `realm_db_schema_full.sql` including the `history` INSERT.
- Bits: `gm_flags::Invisible = 1`, `gm_flags::Godmode = 2` (new header in
  `shared/game/`, enum pseudo-namespace style).
- **Realm DB:** the character load query reads `gm_flags` into `CharacterData`; a new
  `IDatabase::SetCharacterGmFlags(characterId, flags)` writes it (MySQL implementation plus
  any test/null database implementations).
- **`CharacterData`** (`shared/game_server/character_data.h`) gains `uint8 gmFlags`,
  serialized next to `isGameMaster` — part of the same auth protocol bump as section 1.
- **Realm:** when the account has no gm level (`m_gmLevel == 0`), `gmFlags` is forced to 0
  before `CharacterData` is sent to a world node, so a demoted account is never hidden.
- **Realm command flow:** `CheatGodmode` and `CheatInvisible` update
  `m_characterData->gmFlags`, write it to the DB, send the system message, then forward to the
  world node as today.
- **World, `RealmConnector` character join:** applies `SetGodmode` and `SetGmInvisible` from
  `gmFlags` after `SetIsGameMaster`, before the character is added to the instance (so the
  instance-count rule sees the flag). This also fixes godmode being lost on map change.

## 4. Error handling

- Unknown or offline target: unchanged (`port` falls back to the DB location; missing
  character logs a warning).
- Target's instance vanished between fetch and join: world node creates a fresh instance; the
  GM is not bound to it (override join).
- Insufficient gm level: the realm drops the packet with a warning, as for other cheats.
- DB write failure for gm flags: log an error; the in-memory flag still applies for the
  session.

## 5. Testing

- **Unit, `game_server_tests`:**
  - GM-invisible unit: not visible to a player observer, visible to a GM observer.
  - GM invisibility survives applying and removing a stealth aura (`NotifyVisibilityChanged`
    keeps `Off`); turning it off restores aura-derived visibility.
  - Creature idle aggro ignores a GM-invisible player.
  - World instance: adding a GM-invisible player does not change `GetPlayerCount()` and does
    not fire `OnPlayerEnterInstance`; removal after toggling keeps the count correct.
  - `CharacterData` round-trips `gmFlags`.
- **Realm:** if the realm `Player` can be exercised in `realm_server_tests` without heavy
  scaffolding, cover override consumption and the no-binding rule; otherwise covered by E2E
  and the manual plan.
- **E2E:** scenario `gm_invisibility_persistence.lua` — enable `GM.Invisible(true)` and
  `GM.Godmode(true)`, worldport across maps, assert both still apply (creature does not aggro,
  damage is ignored), reconnect, assert again. Needs `GM.Invisible` in the scenario engine and
  an e2e accessor for the flags if not observable otherwise. Cross-player instance joining
  needs two characters in the world, which the harness cannot do (`LoginElsewhere` never
  enters the world), so that part is a manual test.
- **Manual test plan (two clients):** player A enters a dungeon; GM B (not grouped, also
  grouped with someone else) runs `invisible on`, `godmode on`, `port A` — lands next to A in
  A's instance; A does not see B; mobs ignore B; B's group binding unchanged; B repeats `port A`
  from a different copy of the same dungeon.
- Gate: `/gate` fast tier (includes the Linux build in WSL); E2E via `/gate full` since this
  touches transfers.
