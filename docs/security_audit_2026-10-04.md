# Gameplay / Network Security Audit — 2026-10-04

Read-only review of the server tiers against a fully attacker-controlled client.
Five areas: movement, spells/combat, items/economy, network/DoS, interactions/leaks.
Items marked ✅ were re-checked by hand against the code; the rest come from the review
agents' code reading (file:line given) and should be confirmed while fixing.

Severity: **P0** = one client takes down a node or mints items/gold; **P1** = common
cheat or exploit with real gameplay impact; **P2** = narrower abuse / hardening;
**P3** = low impact or design question.

---

## P0 — fix first

| # | Area | Issue | Where | Fix |
|---|---|---|---|---|
| 1 ✅ | Crash | Movement packet with huge finite coords (e.g. x=1e7) → `GetTilePosition` result ignored, `GetTile()` returns nullptr guarded only by `ASSERT` (no-op in release) → null deref / OOB heap write in `TiledUnitFinder::GetTile`, `SolidVisibilityGrid::RequireTile`. Also reachable via ack packets (`MoveTeleportAck`). NaN/Inf are already rejected by the reader. | `world_instance.cpp:38, 895-907`; `tiled_unit_finder.cpp:118`; `solid_visibility_grid.cpp:55`; `player.cpp:2641, 3328` | Reject positions outside the map AABB / unmappable tile in `OnMovement` **and** `OnClientAck` before applying; make grid lookups fail safely instead of ASSERT. |
| 2 ✅ | Crash | `OnLootMoney` divides gold by `recipients.size()` with only `ASSERT(!empty())`. Recipients that left the instance are skipped, and loot opening never checks `IsLootRecipient`, so a non-recipient opening a corpse after the killer left → divide by zero. | `player_inventory_handlers.cpp:555-558` | Early-out on empty; require `IsLootRecipient` on open (see #8). |
| 3 | Dupe | **Mail commits to DB immediately, sender inventory/money stay in memory** until the 5-min save. Mail items+gold to alt, crash node (#1/#2) → sender rolls back, mail still exists. Trades save inventory but not money (`trade_session.cpp:277`) → same trick for gold. | `player_mail_handlers.cpp:156-178`; `mysql_database.cpp:2229`; `player.cpp:204-221` | Save inventory + character data right before `SendMailDraft` / after mail take / after trade; ideally apply inventory diff in the same SQL transaction as `CreateMail`. |
| 4 ✅ | Dupe | **PartyLoot items lootable infinitely**: `GetSlotType` returns `AllowLoot` for any PartyLoot item; `TakeItem` only writes `m_playerLootData` which nothing reads. Spam AutoStoreLootItem. | `loot_instance.cpp:276, 426-431, 639-643` | Reject when `m_playerLootData[receiver][id] >= count`; require receiver ∈ `m_recipients`. |
| 5 ✅ | Gold | Vendor buy `uint8 totalCount = count * buyCount` wraps to 0 → price 0, `CreateItems` turns 0 into 1 → free items. `buyprice * totalCount` can also overflow. | `player_npc_handlers.cpp:903, 939`; `inventory.cpp:255` | Compute in uint32/uint64, reject 0 / > maxstack / overflow. |
| 6 | Crash (pre-auth) | Use-after-free: login DB requests capture `std::cref(m_accountName)`, `std::cref(m_address)` and raw `this`; disconnecting before the DB thread runs frees them. Same pattern on inter-server listeners. | `login_server/player.cpp:396, 533`; `login_server/realm.cpp:437`; `realm_server/world.cpp:246` | Pass by value; never capture `this` in DB-thread lambdas. |

## P1 — movement anti-cheat (the "big one")

Today movement is essentially client-authoritative: one speed check that is easy to
bypass, violations logged but still applied, no collision/terrain validation.

| # | Issue | Where | Fix |
|---|---|---|---|
| 7a ✅ | Speed check only runs if `info.timestamp > last` → constant/decreasing timestamps disable it; forward-warped timestamps allow any distance. `TimeSyncResponse` index is replayable. | `player.cpp:2586, 4415-4441` | Movement budget on **server receive time** (allowed distance accrues with server time × speed, capped burst); clamp client timestamp to server time ± lag; consume time-sync index. |
| 7b ✅ | Only 7 opcodes are speed-checked; `MoveSplineDone`, facing/turn, etc. still apply and relay the client position → free teleport. | `player.cpp:2511, 2574-2581, 2641` | One `ValidateMovement(prev, info, serverNow)` used by **every** movement opcode and every ack. |
| 7c | Falling flag skips the speed check; fall damage computed from client-reported heights → fly/teleport while "falling", no fall damage. | `player.cpp:2583, 2272-2288` | Bound falling motion by gravity/jump velocity over server time; fall damage from server-tracked apex + duration. |
| 7d | Violations are only counted (kick at 5 in 10 s), not rejected; none counted while an ack is pending. | `player.cpp:2527-2540, 2600-2619`; `anti_cheat_tracker.h` | On violation: drop packet + teleport player back to last valid server position. |
| 7e | Acks apply an unvalidated position (`// TODO: Validate movement speed`); `MoveTeleportAck` not compared with the teleport target. | `player.cpp:3104, 3295-3328` | Ack position must be within epsilon of server position / teleport destination. |
| 7f | Stun/root/fear can be escaped by acking with the *wrong* ack opcode (validation only runs when opcode type matches); root enforcement reads client flags. | `player.cpp:3168-3268, 2183-2197` | Map opcode→expected change type, kick on mismatch; enforce roots from server `unit_state`. |
| 7g | `CanFly`, `Flying`, `Levitating`, `WaterWalking`, `SlowFall`, `Ascending`/`Descending` accepted from client. | `OnMovement` | Server-owned allowed-flags set, granted only by auras; reject unauthorised bits. |
| 7h | **No terrain / wall / under-map validation** (walls, wall-climb, hover). Area triggers use the (client-controlled) server position, so teleport reaches portals/quest triggers. | — | Staged: (1) raycast last→new segment against `ServerCollisionMap`; (2) ground-height tolerance when not falling/swimming/levitating; (3) periodic navmesh proximity. |
| 7i | Moving while dead / position change without move flags doesn't interrupt casts (flag-based checks). | `player.cpp:2199`; `game_unit_s.cpp:446` | Use position delta, not flags. |
| 7j | No per-client movement packet rate limit; each one is relayed to every watcher. | `player.cpp:2643-2680` | Token bucket (~50/s), coalesce excess heartbeats. |

## P1 — other gameplay rules

| # | Issue | Where | Fix |
|---|---|---|---|
| 8 | **Anyone can loot any corpse** — `IsLootRecipient` is never called on open. | `player.cpp:4160-4210` | Require `IsLootRecipient` for creature loot. |
| 9 ✅ | **Object-target spells (OpenLock) have no range/LoS** — open doors, get quest-object credit, fire `OnInteraction` boss triggers, lock chests from anywhere in the instance. (Found by two reviewers independently.) | `spell_target_resolver.cpp:92-107`; `single_cast_state.cpp:612`; `spell_effects.cpp:565-607` | Range+LoS for GO targets in `ValidateTargetRequirements`; distance guard in `GameWorldObjectS::Use`. |
| 10 | Trade bait-and-switch: sell/use the offered item after partner accepted; `Execute` re-reads whatever now sits in the slot. Selling/using ignores trade locks. | `trade_session.cpp:180-200`; `player_npc_handlers.cpp:805`; `player_inventory_handlers.cpp:625` | Snapshot item GUID + count on offer, verify at execute; lock check in sell/use. |
| 11 | `NotAttackable` flag never enforced → damage bosses in immune/transition phases. | `object_type_id.h:50`; `game_unit_s.cpp:2905, ~3848` | `IsAttackableBy()` used by attack start/swing, harmful spell validation, AoE target add. |
| 12 | Players auto-attack while stunned/asleep/feared. | `player.cpp:2832`; `ExecuteAutoAttackSwing` | CC check in swing; reject `AttackSwing` while CC'd/dead. |
| 13 | **Private player fields broadcast to everyone in view** (gold, XP, quest log, inventory/bank GUIDs). | `game_object_s.cpp:112-144`; `world_instance.cpp:865` | Owner-only field mask in `FieldMap`, `Serialize*(writer, forOwner)`. Protocol change → bump `game::ProtocolVersion`. |
| 14 ✅ | Guild officer with Remove permission can kick the **guild leader** / higher ranks (orphaned guild). | `realm_server/player.cpp:4282, 4425-4491`; `guild_mgr.cpp:295` | Reject leader / rank ≤ own; compare by id, not name. |
| 15 ✅ | **Live builds ship dev commands** (`-DMMO_WITH_DEV_COMMANDS=ON` in all Dockerfiles + `win-release.yml`); GM gating only on realm; `CheatCheckLineOfSight` and `GuildCreate` not gated; world server re-checks nothing. | `Dockerfile.*:12`; `realm_server/player.cpp:779-825, 2547`; `world_server/player.cpp:790-870` | Build live with dev commands OFF; add missing opcodes to the realm switch; gate all `Cheat*` on `IsGameMaster()` in the world server too. |
| 16 | No connection limits / handshake timeout pre-auth on any listener; inter-server ports (6279) published in `compose.yml`. | `configuration.cpp` (maxPlayers unlimited) | Auth deadline timer (~10 s), per-IP cap, finite defaults; firewall 6279/6280 to server IPs. |
| 17 | No rate limiting besides bug reports; chat = DB insert, unknown NameQuery = DB select → one client stalls the single realm DB thread for everyone. | `realm_server/player.cpp:1035, 1086` | Per-session token buckets by opcode class; negative name-query cache. |

## P2 — hardening

- **Cooldowns:** projectile spells start their cooldown on impact, so you can recast before it lands (`single_cast_state.cpp:952, 1317`). Category cooldowns aren't saved, so a relog resets them (`game_unit_s.cpp:1531`).
- **Spell targeting:** doesn't check stealth/visibility (no `CanBeSeenBy`), and the range check is skipped when a spell has no `rangetype`. Add a default max range and a data lint for harmful spells with no range type.
- **Talent rank skipping:** skipping ranks keeps the lower-rank spells, so their bonuses stack (`game_player_s.cpp:2147`).
- **Item use:**
  - Level, class and race requirements aren't checked (`player_inventory_handlers.cpp:631`).
  - Items with positive charge counts never use them up (`single_cast_state.cpp:715`).
- **Trainers:** `reqskill` and the previous rank aren't enforced (`player_npc_handlers.cpp:513-580`).
- **Quests:**
  - `AcceptQuest` has no interaction or range check (`player_npc_handlers.cpp:127`).
  - Abandon and re-accept farms the quest's source items (`game_player_s.cpp:727, 806`).
- **Area triggers:**
  - The exit trigger fires with no position check.
  - The enter trigger can be replayed by spamming the packet.

  Track inside/outside state per player and fire only on transitions (`player.cpp:1381-1472`).
- **Trade:**
  - The capacity check is per item, not for all items together, and `CreateItems` failures are ignored, so excess items are destroyed (`trade_session.cpp:207-258`).
  - `AddMoney` has no overflow check (trade, mail, quest reward).
- **Selling:** items can be sold to any friendly NPC, not just vendors, and bank items can be sold without a banker.
- **`SplitStack`:** drops `ItemFlags`, so a bound item becomes unbound after a split (`inventory.cpp:1800`).
- **Stack counts:** truncated to uint8 in saving and in mail.
- **Character create:**
  - Appearance values aren't checked (`// TODO` at `realm_server/player.cpp:503`).
  - There is no per-account character limit.
  - There is no reserved-name list.
- **`DeleteChar`/`CreateChar` while in world:** they still work, and you can delete the character you are playing. `OnPartyPing` reads an empty optional after logout.
- **Chat:** markup (`|c`, `|H`), control bytes and invalid UTF-8 are relayed raw to other players. Strip or validate them on the realm.
- **Broadcast spam:** random roll, emote, text emote and party ping have no cooldown.
- **Login:** no lockout or throttle on failed proofs.
- **Stealth leaks:** some broadcasts ignore stealth: `AttackStart`/`AttackStop` and periodic aura logs.

## P3 — low / design

- Facing and turn rate aren't validated (instant 180° turns).
- Releasing spirit revives you at the bind point with no corpse run, so it works as a free hearthstone. Probably a design call.
- The self-aura cancel guard relies only on the `Negative` flag.
- The interrupt lockout is wiped for StartOnCastStart spells.
- Account enumeration: the login server answers unknown accounts differently from real ones. SRP uses a 256-bit N.
- `read_container` calls `resize()` before validating the length. That's safe today with uint8/uint16 lengths, but a future uint32 read would allow a huge allocation.
- The bug report limiter is keyed per character, not per account. A maximum-size report exceeds the realm's 64 KiB receive cap, which is a functional bug.
- Data-dependent undefined behaviour:
  - `attributes(1)` is read with only an `ASSERT` guarding it.
  - `1 << (classId - 1)` with Mage = class 0 is a shift by −1.
- Group loot settings aren't range-checked. Friend adds need no consent and there is no ignore list.

## Already solid (don't re-fix)

- Packet framing caps (16 MiB declared size, 64 KiB client receive buffers), NaN/Inf floats rejected by the reader.
- **Spells:**
  - Only known spells can be cast.
  - Spell, category and GCD cooldowns are checked server-side.
  - Power, reagents and caster state are checked.
  - Unit-target range, LoS and facing are checked at cast start and again at finish.
  - CancelAura is restricted to the player's own positive auras.
  - Talent points, prerequisites and per-row point requirements are enforced.
- **Gossip:**
  - The option index is checked against the menu the server built.
  - Lua gossip hooks aren't reachable from packets yet. Keep it that way, or validate before calling them.
- `IsInteractable`: visibility, alive, not in combat, not hostile, within 6.5 m.
- **Quests:** status, prerequisites, reward index, repeatable gating.
- **Inventory:** bag-into-bag and slot validity are checked, and trade locks cover destroy, swap and split.
- **Mail:** every operation is range-checked, money has an overflow check, and take is transactional (`FOR UPDATE`).
- Chat sender identity comes from the server; group/guild membership and ranks are checked (except #14).
- Charge handshake, speed acks (id + value + opcode type), swim/water checks, movement guid ownership.

---

## Suggested fix batches (one `feature/` branch each)

1. **`feature/net-crash-hardening`**: #1, #2, #6, plus making the grid lookups safe.
2. **`feature/economy-dupes`**: #3, #4, #5, #8, #10, plus `AddMoney` overflow, trade capacity, the `SplitStack` flag fix and quest source items.
3. **`feature/movement-validation`**: the 7a–7f validator: a movement budget on the server clock, rejecting bad packets and teleporting the player back. 7g and 7h (collision raycasts) as a follow-up.
4. **`feature/combat-rule-checks`**: #9, #11, #12, plus projectile cooldowns, the stealth check on spell targets, item use requirements and talent rank skipping.
5. **`feature/ops-hardening`**: #15, #16, #17: dev commands off in live builds, timeouts, connection caps and rate limits.
6. **`feature/private-fields`**: #13. This is a protocol bump, so `tools/protocol_version_check.py --update` and `docs/protocol_versions.md`.
7. **`feature/realm-social-fixes`**: #14, character create validation, delete-while-in-world, chat sanitising.
