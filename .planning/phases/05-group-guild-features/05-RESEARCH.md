# Phase 5: Group & Guild Features — Research

**Researched:** 2025-07-14
**Domain:** C++ MMO server (realm_server / world_server), Lua UI scripting, binary TCP protocol
**Confidence:** HIGH — all findings from direct codebase inspection, no training-data guesses

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
| # | Question | Decision |
|---|----------|----------|
| 1 | How should the group leader change the loot method? | **Both**: slash command (`/lootmethod`) AND right-click party frame dropdown menu |
| 2 | Include raid group conversion? | **No** — deferred |
| 3 | Complete Guild MOTD TODO? | **Yes** — implement storage + broadcast on roster; handler stub at player.cpp:3819 |

### Agent's Discretion
- Architecture for syncing loot method to world server (per-player sync vs per-group packet)
- Whether to include MOTD in GuildRoster packet or rely solely on GuildEvent broadcast

### Deferred Ideas (OUT OF SCOPE)
- Raid group conversion (`ConvertToRaidGroup`)
- Need/Greed rolling UI
- Master loot give-item dialog (MasterLoot = master clicks only, no distribution dialog)
- Guild chat / Officer chat channel routing
</user_constraints>

---

## Summary

Phase 5 has four work streams: loot method enforcement, loot method client UI, guild MOTD implementation, and group/guild UI event verification. Direct codebase inspection reveals that the infrastructure is approximately 60–80% in place, but the critical enforcement and wiring layers are all missing.

**Loot method** has enum + storage in the realm server's `PlayerGroup`, GroupList already carries loot method to the client, and `PartyInfo` already reads and stores it client-side — but there is no client→server `SetLootMethod` opcode, no enforcement in `LootInstance::TakeItem()`, no loot method on the world server's `PlayerGroup`, and the `PARTY_LOOT_METHOD_CHANGED` Lua event is never fired. There is also a latent bug in `party_info.cpp::OnGroupList()` that silently drops loot method for 2-person groups.

**Guild MOTD** has a complete permission-checked handler stub at `realm_server/player.cpp:3783`, `GuildEvent::Motd` exists, the client `guild_client.cpp` already fires the correct Lua event — but the `Guild` class has no `m_motd` field, the `guilds` DB table has no `motd` column, and `GetGuildMOTD()` always returns the hardcoded string `"TODO"`.

**Group/Guild UI** works end-to-end for invites (PARTY_INVITE_REQUEST → GameParent → StaticDialog) and guild events (BroadcastEvent → GuildEvent → GuildFrame_OnEvent). Promotion/demotion/join events route correctly. The main gap is the loot method event chain.

**Primary recommendation:** Implement enforcement bottom-up: (1) add loot method to world-server PlayerGroup + auth sync protocol, (2) store + enforce in LootInstance, (3) add SetLootMethod opcode + realm handler + client Lua bindings, (4) complete Guild MOTD database + broadcast, (5) verify/fix event chains.

---

## Standard Stack

### Core
| Library/Component | Location | Purpose | Notes |
|-------------------|----------|---------|-------|
| `game_protocol.h` | `src/shared/game_protocol/` | Client↔realm opcode definitions | Add `SetLootMethod` to `client_realm_packet` |
| `auth_protocol.h` | `src/shared/auth_protocol/` | Realm↔world opcode definitions | Add `PlayerGroupLootMethodChanged` to `realm_world_packet` |
| `LootInstance` | `src/shared/game_server/loot_instance.{h,cpp}` | Loot container + enforcement | Add method/master fields, enforce in `TakeItem()` |
| `PlayerGroup` (shared) | `src/shared/game_server/player_group.h` | World-server group state | Add `m_lootMethod`, `m_lootMasterGuid` |
| `PlayerGroup` (realm) | `src/realm_server/player_group.{h,cpp}` | Realm-server group state | Already has `m_lootMethod`; add `GetLootMasterGuid()`; fix: call `SendUpdate()` after `SetLootMethod()` |
| `GuildMgr` / `Guild` | `src/realm_server/guild_mgr.h` | Guild logic | Add `m_motd`, `SetMotd()`, `GetMotd()` |
| `GuildClient` | `src/mmo_client/systems/guild_client.{h,cpp}` | Client guild state | Fix `m_guildMotd` initialization |
| `PartyInfo` | `src/mmo_client/systems/party_info.cpp` | Client group state | Fix 2-person group bug; fire `PARTY_LOOT_METHOD_CHANGED` |
| Lua bindings | `src/mmo_client/game_script.cpp` | Expose C++ to Lua | Add `GetLootMethod()`, `SetLootMethod()` |
| Luabind | `deps/luabind_noboost/` | Lua–C++ bridge | Existing pattern; use `luabind::def_lambda` |

---

## Architecture Patterns

### Recommended Project Structure (no new directories needed)

All changes fit within existing files and directories. One new SQL migration file:
```
data/realm/updates/YYYYMMDD_1_guild_motd.sql
```

### Pattern 1: Opcode → Handler Registration
**What:** Client sends opcode → realm server registers handler via `RegisterPacketHandler`
**When to use:** All new client→server opcodes
**Example (existing, follow this pattern):**
```cpp
// game_protocol.h — add to client_realm_packet enum:
SetLootMethod,   // after GroupDisband

// realm_server/player.h — declare handler:
PacketParseResult OnSetLootMethod(game::IncomingPacket& packet);

// realm_server/player.cpp — register in setup:
RegisterPacketHandler(game::client_realm_packet::SetLootMethod, *this, &Player::OnSetLootMethod);

// realm_server/player.cpp — implement:
PacketParseResult Player::OnSetLootMethod(game::IncomingPacket& packet)
{
    uint8 method;
    uint64 lootMasterGuid = 0;
    uint8 lootThreshold = 2;
    if (!(packet >> io::read<uint8>(method) >> io::read<uint64>(lootMasterGuid) >> io::read<uint8>(lootThreshold)))
    {
        return PacketParseResult::Disconnect;
    }
    // ... validate, call group->SetLootMethod(), group->SendUpdate(), world sync
}
```

### Pattern 2: Realm→World Auth Protocol Notification
**What:** Realm sends auth packet to world node(s) to sync game state changes
**When to use:** Any state change that affects world-server logic (group membership, guild, loot method)
**Example (existing `NotifyPlayerGroupChanged`, follow this pattern):**
```cpp
// auth_protocol.h — add to realm_world_packet enum (after PlayerGuildChanged):
PlayerGroupLootMethodChanged,

// realm_server/world.h — declare:
void NotifyPlayerGroupLootMethodChanged(uint64 characterId, uint8 lootMethod, uint64 lootMasterGuid);

// realm_server/world.cpp — implement (identical pattern to NotifyPlayerGroupChanged):
void World::NotifyPlayerGroupLootMethodChanged(uint64 characterId, uint8 lootMethod, uint64 lootMasterGuid)
{
    GetConnection().sendSinglePacket([characterId, lootMethod, lootMasterGuid](auth::OutgoingPacket& outPacket)
    {
        outPacket.Start(auth::realm_world_packet::PlayerGroupLootMethodChanged);
        outPacket
            << io::write<uint64>(characterId)
            << io::write<uint8>(lootMethod)
            << io::write<uint64>(lootMasterGuid);
        outPacket.Finish();
    });
}

// world_server/realm_connector.cpp — register + implement:
RegisterPacketHandler(auth::realm_world_packet::PlayerGroupLootMethodChanged, *this, &RealmConnector::OnPlayerGroupLootMethodChanged);

PacketParseResult RealmConnector::OnPlayerGroupLootMethodChanged(auth::IncomingPacket& packet)
{
    uint64 characterId; uint8 lootMethod; uint64 lootMasterGuid;
    if (!(packet >> io::read<uint64>(characterId) >> io::read<uint8>(lootMethod) >> io::read<uint64>(lootMasterGuid)))
        return PacketParseResult::Disconnect;
    const auto player = m_playerManager.GetPlayerByCharacterGuid(characterId);
    if (player)
        player->UpdateCharacterGroupLootMethod(static_cast<LootMethod>(lootMethod), lootMasterGuid);
    return PacketParseResult::Pass;
}
```

### Pattern 3: GamePlayerS Per-Player State for Group Data
**What:** Loot method lives on `GamePlayerS` (alongside `m_groupId`), set by world server when realm sends sync packet
**When to use:** Any group-level state that world-server game logic needs
**Implementation:**
```cpp
// src/shared/game_server/objects/game_player_s.h — add alongside m_groupId (line ~283):
LootMethod m_lootMethod = loot_method::FreeForAll;
uint64 m_lootMasterGuid = 0;

// Add getters/setters:
LootMethod GetLootMethod() const { return m_lootMethod; }
uint64 GetLootMasterGuid() const { return m_lootMasterGuid; }
void SetLootMethod(LootMethod method) { m_lootMethod = method; }
void SetLootMasterGuid(uint64 guid) { m_lootMasterGuid = guid; }
```

### Pattern 4: LootInstance Enforcement in TakeItem()
**What:** Store method at LootInstance creation, enforce per-item permission in TakeItem()
**When to use:** The single change point for all loot method policies

**Add to `LootItem` struct:**
```cpp
struct LootItem final {
    bool isLooted;
    uint32 count;
    const proto::LootDefinition& definition;
    uint64 assignedRecipientGuid = 0;   // NEW — for RoundRobin pre-assignment

    explicit LootItem(uint32 count, const proto::LootDefinition& def)
        : isLooted(false), count(count), definition(def), assignedRecipientGuid(0) {}
};
```

**Add to `LootInstance`:**
```cpp
// In loot_instance.h — private members:
LootMethod m_lootMethod = loot_method::FreeForAll;
uint64 m_lootMasterGuid = 0;

// Updated full constructor signature:
explicit LootInstance(const proto::ItemManager& items,
                      const ConditionMgr& conditionMgr,
                      uint64 lootGuid,
                      const proto::LootEntry* entry,
                      uint32 minGold, uint32 maxGold,
                      const std::vector<std::weak_ptr<GamePlayerS>>& lootRecipients,
                      LootMethod lootMethod = loot_method::FreeForAll,  // NEW
                      uint64 lootMasterGuid = 0);                        // NEW
```

**TakeItem() enforcement (loot_instance.cpp):**
```cpp
bool LootInstance::TakeItem(uint8 slot, uint64 receiver)
{
    if (slot >= m_items.size()) return false;
    if (m_items[slot].isLooted)  return false;

    // NEW: Loot method enforcement
    switch (m_lootMethod)
    {
    case loot_method::MasterLoot:
        if (receiver != m_lootMasterGuid)
            return false;   // Only loot master can take
        break;
    case loot_method::RoundRobin:
        if (m_items[slot].assignedRecipientGuid != 0 &&
            receiver != m_items[slot].assignedRecipientGuid)
            return false;   // Only assigned player can take
        break;
    default:
        break;  // FreeForAll, GroupLoot — no restriction
    }
    // ... rest of existing TakeItem() logic
}
```

**RoundRobin pre-assignment in constructor (after all items are generated):**
```cpp
// After AddLootItem() calls are complete, in the full LootInstance constructor:
if (m_lootMethod == loot_method::RoundRobin && !m_recipients.empty())
{
    uint32 recipientIndex = 0;
    for (auto& item : m_items)
    {
        item.assignedRecipientGuid = m_recipients[recipientIndex % m_recipients.size()];
        recipientIndex++;
    }
}
```

**creature_ai_death_state.cpp — pass loot method:**
```cpp
// Get loot method from first loot recipient's group data (added to GamePlayerS):
LootMethod groupLootMethod = loot_method::FreeForAll;
uint64 lootMasterGuid = 0;
if (!lootRecipients.empty())
{
    const auto& firstPlayer = lootRecipients.begin()->second;
    groupLootMethod = firstPlayer->GetLootMethod();
    lootMasterGuid = firstPlayer->GetLootMasterGuid();
}

auto loot = std::make_unique<LootInstance>(
    controlled.GetProject().items,
    controlled.GetWorldInstance()->GetConditionMgr(),
    controlled.GetGuid(),
    lootEntry,
    lootEntry->minmoney(), lootEntry->maxmoney(),
    weakRecipients,
    groupLootMethod,   // NEW
    lootMasterGuid);   // NEW
```

### Pattern 5: Guild MOTD — Storage + Broadcast
**What:** Add `m_motd` to Guild, persist to DB, broadcast via existing `BroadcastEvent()`
**Existing infrastructure confirmed working:**
- `Guild::BroadcastEvent(guild_event::Motd, 0, motd.c_str())` will fire `GUILD_EVENT` Lua event with "MOTD" string
- `GuildFrame_OnEvent` already handles event == "MOTD" (displays in green)
- `GuildSetMOTD()` Lua function already sends `GuildMotd` opcode to server
- `/gmotd <text>` slash command already wired to `GuildSetMOTD()`

**Complete `OnGuildMotd()` implementation:**
```cpp
// realm_server/player.cpp — replace TODO at line 3819:
guild->SetMotd(motd);   // sets m_motd

guild->BroadcastEvent(guild_event::Motd, 0, motd.c_str());
// BroadcastEvent sends realm_client_packet::GuildEvent with:
//   event = guild_event::Motd (2)
//   stringCount = 1
//   arg1 = motd text
// → client OnGuildEvent fires "GUILD_EVENT" Lua event → GuildFrame_OnEvent handles it

// Persist to database:
auto handler = [](bool success) { if (!success) ELOG("Failed to persist guild MOTD"); };
m_database.asyncRequest(std::move(handler), &IDatabase::SetGuildMotd, m_characterData->guildId, motd);
```

**Guild::SetMotd() implementation:**
```cpp
// realm_server/guild_mgr.h:
void SetMotd(const String& motd) { m_motd = motd; }
const String& GetMotd() const { return m_motd; }

// realm_server/guild_mgr.h — private member (after m_ranks):
String m_motd;
```

**Fix client GuildClient::m_guildMotd "TODO" bug:**
```cpp
// guild_client.cpp — in OnGuildEvent(), add MOTD case:
if (event == guild_event::Motd && !args.empty())
{
    m_guildMotd = args[0];
    // GuildMOTDLabel will be refreshed on next GuildFrame_OnShow or GUILD_EVENT handler
}
```

**Include MOTD in GuildRoster packet for initial display:**
```cpp
// realm_server/guild_mgr.cpp — Guild::WriteRoster():
writer << io::write_dynamic_range<uint16>(m_motd);   // append after existing member data

// src/mmo_client/systems/guild_client.cpp — OnGuildRoster(), after reading members:
packet >> io::read_container<uint16>(m_guildMotd);
```

### Pattern 6: Client Lua Loot Method API
**What:** Expose loot method read/write to Lua scripts
**Add to game_script.cpp (alongside existing party bindings near line 1221):**
```cpp
luabind::def<std::function<int32()>>("GetLootMethod", [this]()
{
    return static_cast<int32>(m_partyInfo.GetLootMethod());
}),
luabind::def<std::function<void(int32, const String&)>>("SetLootMethod", [this](int32 method, const String& masterName)
{
    uint64 masterGuid = 0;
    // Resolve master name to GUID from ObjectMgr if MasterLoot
    if (method == static_cast<int32>(loot_method::MasterLoot) && !masterName.empty())
    {
        // Look up GUID via nameCache or ObjectMgr
        if (auto* unit = ObjectMgr::GetByName<GamePlayerC>(masterName))
            masterGuid = unit->GetGuid();
    }
    m_realmConnector.SetGroupLootMethod(static_cast<uint8>(method), masterGuid, 2);
}),
```

**SlashCommandStrings.lua — add:**
```lua
SLASH_LOOTMETHOD1 = "/lootmethod";
SLASH_LOOTMETHOD2 = "/lm";
```

**ChatFrame.lua — add handler (alongside GUILD_MOTD handler at ~line 119):**
```lua
SlashCmdList["LOOTMETHOD"] = function(msg)
    local method = string.lower(msg);
    local methodId = 0;
    if (method == "freeforall" or method == "ffa") then methodId = 0;
    elseif (method == "roundrobin" or method == "rr") then methodId = 1;
    elseif (method == "masterloot" or method == "ml") then methodId = 2;
    elseif (method == "grouploot" or method == "gl") then methodId = 3;
    else
        ChatFrame:AddMessage("Usage: /lootmethod [freeforall|roundrobin|masterloot|grouploot]", 1.0, 0.5, 0.5);
        return;
    end
    SetLootMethod(methodId, "");
end
```

**ContextMenu.lua — add loot method submenu to PARTY_MEMBER (after promote-to-leader):**
```lua
if isLeader then
    -- Existing kick/promote items...
    -- New: Loot Method
    table.insert(items, {
        text = Localize("SET_LOOT_METHOD"),
        callback = function()
            local pos = GetCursorPosition();
            ContextMenu_Show("LOOT_METHOD", pos.x, pos.y, nil);
        end,
        enabled = true
    });
end

-- Register new LOOT_METHOD context menu:
RegisterContextMenu("LOOT_METHOD", {
    items = function(data)
        return {
            { text = Localize("LOOT_FREE_FOR_ALL"),  callback = function() SetLootMethod(0, ""); end },
            { text = Localize("LOOT_ROUND_ROBIN"),   callback = function() SetLootMethod(1, ""); end },
            { text = Localize("LOOT_MASTER_LOOT"),   callback = function() SetLootMethod(2, UnitName("player")); end },
            { text = Localize("LOOT_GROUP_LOOT"),    callback = function() SetLootMethod(3, ""); end },
        };
    end
})
```

### Anti-Patterns to Avoid
- **Checking loot method in `OnAutoStoreLootItem()` (world player):** Never do this — it bypasses the single enforcement point in `LootInstance::TakeItem()`. All enforcement MUST live in `TakeItem()`.
- **Hardcoded loot method enum values in Lua:** Always use numeric constants consistently matching `loot_method::Type`. Document them at the Lua call site.
- **Calling `guild->BroadcastEvent()` without `SetMotd()` first:** The MOTD event fires with arg1=motd text; but `GetGuildMOTD()` on the client reads `m_guildMotd` which is updated only from the event. For players who missed the event (logged in later), the MOTD must be in the roster packet.
- **Forgetting `SendUpdate()` after `SetLootMethod()`:** The realm server's `SetLootMethod()` currently does NOT call `SendUpdate()`. Without it, clients won't receive the updated GroupList with the new loot method.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Guild event broadcast | Custom packet loop | `Guild::BroadcastEvent()` | Already handles online-member filtering, packet construction, and error logging |
| Group packet broadcast | Custom member loop | `PlayerGroup::BroadcastPacket<F>()` | Template, handles world-node routing |
| Lua binding | Manual C function registration | `luabind::def_lambda` | Project standard; see game_script.cpp line 1025+ |
| Context menu building | Custom frame code | `RegisterContextMenu()` + `ContextMenu_Show()` | Existing system in ContextMenu.lua |
| Auth packet send | Raw socket write | `GetConnection().sendSinglePacket()` | Standard ASIO pattern; see world.cpp:523 |
| DB async request | Synchronous call | `m_database.asyncRequest()` | Only async DB calls allowed on the main thread |

---

## Runtime State Inventory

> This section applies because Phase 5 adds a database schema change (guild MOTD column).

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| Stored data | `guilds` table: no `motd` column | SQL migration: `ALTER TABLE guilds ADD COLUMN motd VARCHAR(255) DEFAULT ''` |
| Live service config | None identified | None |
| OS-registered state | None identified | None |
| Secrets/env vars | None identified | None |
| Build artifacts | None — C++ rebuild is automatic | None |

**Nothing found in other categories** — verified by reading realm DB schema and update scripts.

---

## Common Pitfalls

### Pitfall 1: 2-Person Group Loot Method Bug
**What goes wrong:** `OnGroupList()` in `party_info.cpp` has the condition `if (members.size() > 1)` before reading loot method. For a 2-person group the server sends memberCount=1, so `members.size() = 1`, and `1 > 1 = false`. Loot method is never read → client always shows default GroupLoot.
**Root cause:** Off-by-one in the conditional.
**How to avoid:** Change line ~248 in `party_info.cpp` from:
```cpp
if (members.size() > 1)
```
to:
```cpp
if (memberCount > 0)
```
**Warning signs:** Loot method UI always shows GroupLoot even after changing it. Particularly affects most common case (2-player groups).

### Pitfall 2: SetLootMethod Without SendUpdate
**What goes wrong:** `PlayerGroup::SetLootMethod()` (realm server) stores the new loot method but does NOT call `SendUpdate()`. If the new `OnSetLootMethod()` handler calls `group->SetLootMethod()` and forgets `group->SendUpdate()`, clients will never get the updated `GroupList` packet and won't know the loot method changed.
**Root cause:** Oversight — the `SetLootMethod()` method silently stores but doesn't broadcast.
**How to avoid:** Always call `m_group->SendUpdate()` after `m_group->SetLootMethod()` in the handler.

### Pitfall 3: GuildMotd Always Returns "TODO"
**What goes wrong:** `GuildClient::m_guildMotd` is set to the string `"TODO"` at line 324 of `guild_client.cpp` when guild name is resolved. It's never updated from the server. `GetGuildMOTD()` always returns "TODO". `GuildFrame_OnLoad` and `GuildFrame_OnShow` call `GetGuildMOTD()` and will display "TODO".
**Root cause:** GuildQuery/cache logic sets the guild name but m_guildMotd has no source of truth.
**How to avoid:** 
1. Update `m_guildMotd` inside `OnGuildEvent()` when event == `guild_event::Motd`.
2. Additionally include MOTD in the GuildRoster packet and update `m_guildMotd` in `OnGuildRoster()`.

### Pitfall 4: Guild MOTD Persistence Missing
**What goes wrong:** Even after implementing `OnGuildMotd()`, the MOTD is only stored in `Guild::m_motd` (RAM). On server restart, guilds are re-loaded from the database but the `guilds` table has no `motd` column, so MOTD is lost.
**Root cause:** `GuildData` struct doesn't include `motd`; `LoadGuilds()` SQL doesn't select it; DB table doesn't have the column.
**How to avoid:** Three-step fix: (1) SQL migration adds column, (2) `GuildData` struct adds `String motd`, (3) `LoadGuilds()` reads and passes it, (4) `IDatabase::SetGuildMotd()` virtual method + `MySQLDatabase` implementation.

### Pitfall 5: LootInstance Has No Auth Context
**What goes wrong:** `LootInstance` is in `src/shared/game_server/` with no access to world-server infrastructure. It cannot look up players or groups. Passing loot method MUST happen at construction time (in `creature_ai_death_state.cpp`).
**Root cause:** Layering — shared library cannot depend on world-server specifics.
**How to avoid:** Retrieve loot method from `GamePlayerS::GetLootMethod()` before constructing `LootInstance`. Store method in `LootInstance` constructor params. All enforcement happens in `TakeItem()` using pre-stored data.

### Pitfall 6: PARTY_LOOT_METHOD_CHANGED Never Fires
**What goes wrong:** `PartyFrame.lua` registers `PartyMemberFrame_OnLootMethodChanged` for `PARTY_LOOT_METHOD_CHANGED`, but this event is never fired anywhere in the codebase. The handler body is also empty. Even if the loot method changes, the UI will never know.
**Root cause:** Event was added to Lua but never wired to C++.
**How to avoid:** In `party_info.cpp::OnGroupList()`, after reading the new loot method, compare with the previous value and fire:
```cpp
if (m_lootMethod != newLootMethod || m_lootMaster != newLootMaster)
{
    m_lootMethod = newLootMethod;
    m_lootMaster = newLootMaster;
    FrameManager::Get().TriggerLuaEvent("PARTY_LOOT_METHOD_CHANGED");
}
```

### Pitfall 7: GetLootMasterGuid Missing from Realm PlayerGroup
**What goes wrong:** `realm_server/player_group.h` stores `m_lootMaster` as a private field with no public getter. Any code (including the new `OnSetLootMethod` handler and auth sync) that needs the current loot master GUID will have a compile error.
**Root cause:** The accessor was never added.
**How to avoid:** Add `uint64 GetLootMasterGuid() const { return m_lootMaster; }` to `realm_server/player_group.h`.

### Pitfall 8: RoundRobin Assignment Timing
**What goes wrong:** RoundRobin assigns items to recipients in rotation at LootInstance creation. If the assignment uses the wrong recipient list ordering (e.g., map iteration order from `lootRecipients`, which is a `std::map<uint64, ...>` keyed by GUID), the "rotation" will be GUID-sorted rather than aggro-order or join-order.
**Root cause:** `std::map` iterates in GUID order, not group-join order.
**How to avoid:** Use the `weakRecipients` vector (which preserves iteration order from `lootRecipients` insertion — the initial tagger is first, then nearby members in `FindUnits` scan order). Pass this same vector order as `m_recipients` in `LootInstance`. This is fine for gameplay purposes.

---

## Code Examples

### RealmConnector::SetGroupLootMethod (client→server send)
```cpp
// src/mmo_client/net/realm_connector.cpp — follow pattern of AutoStoreLootItem():
void RealmConnector::SetGroupLootMethod(uint8 method, uint64 masterGuid, uint8 threshold)
{
    sendSinglePacket([method, masterGuid, threshold](game::OutgoingPacket& packet) {
        packet.Start(game::client_realm_packet::SetLootMethod);
        packet
            << io::write<uint8>(method)
            << io::write<uint64>(masterGuid)
            << io::write<uint8>(threshold);
        packet.Finish();
    });
}
```

### Guild::AddGuild — Initialize m_motd from GuildData
```cpp
// realm_server/guild_mgr.cpp — in AddGuild():
auto guild = std::make_shared<Guild>(
    *this, m_playerManager, m_asyncDatabase,
    info.id, info.name, info.leaderGuid);
guild->GetMembersRef() = info.members;
guild->GetRanksRef() = info.ranks;
guild->SetMotd(info.motd);   // NEW — initialize MOTD from loaded data
```

### IDatabase::SetGuildMotd (new virtual method)
```cpp
// src/realm_server/database.h — add to IDatabase interface:
/// Sets the MOTD for a guild.
/// @param guildId The guild's unique identifier.
/// @param motd The new message of the day.
virtual void SetGuildMotd(uint64 guildId, const String& motd) = 0;

// src/realm_server/mysql_database.cpp — implement:
void MySQLDatabase::SetGuildMotd(uint64 guildId, const String& motd)
{
    const String escaped = m_connection.EscapeString(motd);
    if (!m_connection.Execute(
        "UPDATE `guilds` SET `motd` = '" + escaped + "' WHERE `id` = '" + std::to_string(guildId) + "'"))
    {
        ELOG("Failed to update guild MOTD for guild " << guildId);
    }
}
```

### LoadGuilds() — Read motd column
```cpp
// src/realm_server/mysql_database.cpp — update SELECT query:
mysql::Select select(m_connection,
    "SELECT `id`, `name`, `leader`, COALESCE(`motd`, '') FROM `guilds`");
// In the row parsing loop, add after reading leaderGuid:
row.GetField(3, info.motd);
```

### SQL Migration
```sql
-- data/realm/updates/YYYYMMDD_1_guild_motd.sql
ALTER TABLE `guilds`
    ADD COLUMN `motd` VARCHAR(255) NOT NULL DEFAULT ''
    AFTER `leader`;
```

### GuildData struct update
```cpp
// src/realm_server/database.h — GuildData:
struct GuildData
{
    uint64 id;
    String name;
    uint64 leaderGuid;
    String motd;    // NEW
    std::vector<GuildRank> ranks;
    std::vector<GuildMember> members;
};
```

### WriteRoster with MOTD
```cpp
// realm_server/guild_mgr.cpp — Guild::WriteRoster():
void Guild::WriteRoster(io::Writer& writer)
{
    writer
        << io::write<uint32>(m_members.size())
        << io::write<uint32>(m_ranks.size())
        << io::write_dynamic_range<uint16>(m_motd);   // NEW — before ranks

    for (const auto& rank : m_ranks) { ... }
    for (auto& member : m_members) { ... }
}
```
> **Important:** Client `OnGuildRoster()` in `guild_client.cpp` must read this field before ranks, in the same order. Update the reading loop accordingly.

---

## Group/Guild UI Event Verification

### What Works End-to-End (confirmed)
| Flow | Server Event | Client Handler | Lua Event | UI Handler |
|------|-------------|----------------|-----------|------------|
| Party invite popup | `realm_client_packet::GroupInvite` | `world_state.cpp::OnGroupInvite` | `PARTY_INVITE_REQUEST` | `GameParent.lua → StaticDialog "PARTY_INVITE"` ✓ |
| Party invite declined | `realm_client_packet::GroupUninvite` | `world_state.cpp::OnGroupInvite` (second path) | `PARTY_INVITE_DECLINED` | `PartyFrame_OnInviteDeclined` ✓ |
| Invite sent confirmation | `realm_client_packet::PartyCommandResult` | `world_state.cpp` | `PARTY_INVITE_SENT` | `PartyFrame_OnInviteSent` ✓ |
| Member joins | `realm_client_packet::GroupList` | `party_info.cpp::OnGroupList` | `PARTY_MEMBER_JOINED` | `PartyFrame_OnMemberJoined` ✓ |
| Member leaves | `realm_client_packet::GroupList` | `party_info.cpp::OnGroupList` | `PARTY_MEMBER_LEFT` | `PartyFrame_OnMemberLeft` ✓ |
| Group disbanded | `realm_client_packet::GroupDestroyed` | `party_info.cpp::OnGroupDestroyed` | `PARTY_DISBANDED` | `PartyFrame_OnDisbanded` ✓ |
| Leader changed | `realm_client_packet::GroupSetLeader` | `party_info.cpp::OnGroupSetLeader` | `PARTY_LEADER_CHANGED` | `PartyMemberFrame_OnLeaderChanged` ✓ |
| Guild invite popup | `realm_client_packet::GuildInvite` | `guild_client.cpp::OnGuildInvite` | `GUILD_INVITE_REQUEST` | `GameParent.lua → StaticDialog "GUILD_INVITE"` ✓ |
| Guild joined | Server: `BroadcastEvent(Joined, ...)` | `guild_client.cpp::OnGuildEvent` | `GUILD_EVENT, "JOINED"` | `GuildFrame_OnEvent` → chat ✓ |
| Guild promoted | Server: `BroadcastEvent(Promotion, ...)` | `guild_client.cpp::OnGuildEvent` | `GUILD_EVENT, "PROMOTION"` | `GuildFrame_OnEvent` → chat ✓ |
| Guild left/removed | `BroadcastEvent(Left/Removed, ...)` | `guild_client.cpp::OnGuildEvent` | `GUILD_LEFT`, `GUILD_REMOVED` | `GuildFrame_OnLeft/OnRemoved` ✓ |

### What's Broken / Missing
| Flow | Problem | Fix Location |
|------|---------|-------------|
| Loot method change UI | `PARTY_LOOT_METHOD_CHANGED` never fires | `party_info.cpp::OnGroupList()` |
| Loot method update (2-person groups) | `members.size() > 1` bug drops loot info | `party_info.cpp::OnGroupList()` line ~248 |
| Guild MOTD display | `GetGuildMOTD()` returns "TODO" hardcoded | `guild_client.cpp::NotifyGuildChanged()` line 324 |
| Guild MOTD on frame open | No MOTD in GuildRoster packet | `guild_mgr.cpp::WriteRoster()` + `guild_client.cpp::OnGuildRoster()` |

---

## Files That Need Changes

### New Files
| File | Purpose |
|------|---------|
| `data/realm/updates/YYYYMMDD_guild_motd.sql` | Add `motd` column to `guilds` table |

### Modified Files

**Protocol (shared — affects compilation of all server + client targets):**
| File | Change |
|------|--------|
| `src/shared/game_protocol/game_protocol.h` | Add `SetLootMethod` to `client_realm_packet` enum (after `GroupDisband` ~line 243) |
| `src/shared/auth_protocol/auth_protocol.h` | Add `PlayerGroupLootMethodChanged` to `realm_world_packet` enum (after `PlayerGuildChanged` ~line 133) |

**Shared game server (compiled into realm_server + world_server):**
| File | Change |
|------|--------|
| `src/shared/game_server/player_group.h` | Add `m_lootMethod`, `m_lootMasterGuid` fields + setters/getters |
| `src/shared/game_server/objects/game_player_s.h` | Add `m_lootMethod`, `m_lootMasterGuid` fields + setters/getters (alongside `m_groupId`) |
| `src/shared/game_server/loot_instance.h` | Add `m_lootMethod`, `m_lootMasterGuid` fields; add `assignedRecipientGuid` to `LootItem`; update constructor signature |
| `src/shared/game_server/loot_instance.cpp` | Implement RoundRobin assignment at construction; add enforcement logic in `TakeItem()` |
| `src/shared/game_server/ai/creature_ai_death_state.cpp` | Pass loot method + master GUID from first recipient's GamePlayerS to LootInstance constructor |

**Realm server:**
| File | Change |
|------|--------|
| `src/realm_server/player.h` | Declare `OnSetLootMethod()` |
| `src/realm_server/player.cpp` | Add `OnSetLootMethod()` handler; register opcode; complete `OnGuildMotd()` TODO (line 3819) |
| `src/realm_server/player_group.h` | Add `GetLootMasterGuid()` accessor |
| `src/realm_server/player_group.cpp` | `SetLootMethod()` must call world sync (add after existing lines 113-115) |
| `src/realm_server/guild_mgr.h` | Add `String m_motd`, `SetMotd()`, `GetMotd()` to `Guild` class |
| `src/realm_server/guild_mgr.cpp` | Initialize `m_motd` from GuildData in AddGuild(); add MOTD to `WriteRoster()`; implement `SetMotd()` |
| `src/realm_server/database.h` | Add `String motd` to `GuildData` struct; add `virtual void SetGuildMotd(uint64, const String&) = 0` |
| `src/realm_server/mysql_database.h` | Declare `SetGuildMotd()` |
| `src/realm_server/mysql_database.cpp` | Implement `SetGuildMotd()`; update `LoadGuilds()` SELECT + field parsing |
| `src/realm_server/world.h` | Declare `NotifyPlayerGroupLootMethodChanged()` |
| `src/realm_server/world.cpp` | Implement `NotifyPlayerGroupLootMethodChanged()` (auth packet send) |

**World server:**
| File | Change |
|------|--------|
| `src/world_server/realm_connector.h` | Declare `OnPlayerGroupLootMethodChanged()` |
| `src/world_server/realm_connector.cpp` | Register + implement handler; call `player->UpdateCharacterGroupLootMethod()` |
| `src/world_server/player.h` | Declare `UpdateCharacterGroupLootMethod()` |
| `src/world_server/player.cpp` | Implement `UpdateCharacterGroupLootMethod()`: update `m_character->SetLootMethod()` + `m_character->SetLootMasterGuid()` |

**Game client (C++):**
| File | Change |
|------|--------|
| `src/mmo_client/net/realm_connector.h` | Declare `SetGroupLootMethod()` |
| `src/mmo_client/net/realm_connector.cpp` | Implement `SetGroupLootMethod()` (sends `SetLootMethod` opcode) |
| `src/mmo_client/game_script.cpp` | Add `GetLootMethod()` and `SetLootMethod()` Lua bindings |
| `src/mmo_client/systems/party_info.cpp` | Fix 2-person group bug; fire `PARTY_LOOT_METHOD_CHANGED` on change |
| `src/mmo_client/systems/guild_client.cpp` | Fix `m_guildMotd` "TODO"; update on MOTD event + roster packet |

**Game client (Lua):**
| File | Change |
|------|--------|
| `data/client/Interface/GameUI/SlashCommandStrings.lua` | Add `SLASH_LOOTMETHOD1`, `SLASH_LOOTMETHOD2` |
| `data/client/Interface/GameUI/ChatFrame.lua` | Add `SlashCmdList["LOOTMETHOD"]` handler |
| `data/client/Interface/GameUI/ContextMenu.lua` | Add loot method submenu to `PARTY_MEMBER` context menu; register `LOOT_METHOD` context menu |
| `data/client/Interface/GameUI/PartyFrame.lua` | Implement `PartyMemberFrame_OnLootMethodChanged()` body (currently empty) |

---

## State of the Art

| Old Approach | Current Approach | Notes |
|--------------|------------------|-------|
| Enforce loot in game layer | Enforce in `LootInstance::TakeItem()` | Only single enforcement point; no bypass possible |
| Loot method client-only display | Loot method round-trips through server | Server is authoritative; client cannot spoof |

**Deprecated/outdated:**
- The `members.size() > 1` guard in `party_info.cpp::OnGroupList()` — intended to handle empty groups but has an off-by-one that breaks 2-person groups. Replace with `memberCount > 0`.

---

## Open Questions

1. **Localization strings for loot method UI**
   - What we know: `Localize("SET_LOOT_METHOD")`, `Localize("LOOT_FREE_FOR_ALL")`, etc. are needed by ContextMenu.lua additions
   - What's unclear: Whether these strings exist in the localization system or need to be added
   - Recommendation: Check `data/client/Interface/` for a Localization.lua or string table file. If missing, add them there. Plan should include a "verify/add localization strings" step.

2. **GuildData motd column migration order**
   - What we know: `LoadGuilds()` always runs at server start from `GuildMgr::LoadGuilds()`
   - What's unclear: Whether deployments without the migration would crash or silently ignore the missing column
   - Recommendation: Use `COALESCE(\`motd\`, '')` in the SELECT query as a defensive measure. Add the migration as the first step in the wave.

3. **MasterLoot GUID resolution on client**
   - What we know: `SetLootMethod(method, masterName)` Lua function needs to resolve masterName→GUID
   - What's unclear: Whether `ObjectMgr` can look up players by name efficiently or if we need to pass the name to the server and let the server resolve it
   - Recommendation: Send the GUID 0 and the name string in the opcode payload; let the realm server resolve the name via `PlayerManager::GetPlayerByCharacterName()`. Simplest and most correct.

4. **SetGroupLootMethod permission enforcement (realm server)**
   - What we know: `IsLeaderOrAssistant()` exists on `PlayerGroup`
   - What's unclear: Should assistants be able to change loot method, or only the leader?
   - Recommendation: Restrict to leader-only (`m_leaderGUID == m_characterData->characterId`) for Phase 5. Assistants can be addressed later.

---

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 v2 (header-only, `deps/catch/`) |
| Config file | None — linked directly in CMakeLists |
| Quick run command | `.\bin\Debug\game_server_unit_tests.exe` |
| Full suite command | `.\bin\Debug\unit_tests.exe && .\bin\Debug\game_server_unit_tests.exe` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| LOOT-01 | `TakeItem()` rejects non-master in MasterLoot mode | unit | `game_server_unit_tests` `[loot_instance]` | ❌ Wave 0 |
| LOOT-02 | `TakeItem()` accepts master in MasterLoot mode | unit | `game_server_unit_tests` `[loot_instance]` | ❌ Wave 0 |
| LOOT-03 | `TakeItem()` rejects wrong recipient in RoundRobin mode | unit | `game_server_unit_tests` `[loot_instance]` | ❌ Wave 0 |
| LOOT-04 | `TakeItem()` accepts assigned recipient in RoundRobin mode | unit | `game_server_unit_tests` `[loot_instance]` | ❌ Wave 0 |
| LOOT-05 | RoundRobin assigns items to recipients in rotation | unit | `game_server_unit_tests` `[loot_instance]` | ❌ Wave 0 |
| LOOT-06 | FreeForAll allows any recipient | unit | `game_server_unit_tests` `[loot_instance]` | ❌ Wave 0 |
| MOTD-01 | Guild MOTD persists after server restart (DB round-trip) | manual | manual — restart server, verify | N/A |
| UI-01 | Loot method change sends correct opcode | manual | manual — packet capture | N/A |
| UI-02 | `/lootmethod freeforall` sends correct value | manual | manual | N/A |

### Sampling Rate
- **Per task commit:** `.\bin\Debug\game_server_unit_tests.exe "[loot_instance]"` (once test file exists)
- **Per wave merge:** `.\bin\Debug\unit_tests.exe && .\bin\Debug\game_server_unit_tests.exe`
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `src/game_server_unit_tests/loot_instance_test.cpp` — covers LOOT-01 through LOOT-06
- [ ] Add mock `proto::ItemManager` or reuse existing test helpers from `item_factory_test.cpp` pattern

---

## Environment Availability

> Phase 5 is server + client code changes. No new external tools required.

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| MySQL/MariaDB | Guild MOTD SQL migration | ✓ (system) | runtime | — |
| Lua 5.x | Client Lua changes | ✓ (bundled `deps/lua/`) | bundled | — |
| Catch2 | New loot_instance tests | ✓ (bundled `deps/catch/`) | v2 | — |

**Missing dependencies:** None identified.

---

## Project Constraints (from copilot-instructions.md)

- **Braces:** Allman style — every `if`, loop, and function body on its own line, even single-line bodies
- **Indentation:** Tabs only
- **Naming:** `m_camelCase` for member variables; `PascalCase` for public methods; `snake_case` for files
- **No exceptions:** `SIMPLE_NO_EXCEPTIONS` is defined — no `throw/catch`; use `ELOG()` + return codes
- **Async DB:** All database operations MUST use `m_database.asyncRequest()` on the server
- **Doxygen headers:** All new public methods in headers need Doxygen `///` comments
- **Copyright header:** `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` on new files
- **`#pragma once`** on all new headers
- **Signals:** Use `scoped_connection` for signal lifetime management
- **`[[nodiscard]]` + `noexcept`** on getters where appropriate
- **Root namespace:** All code in `namespace mmo { }`
- **GSD workflow:** No direct edits outside a GSD workflow command

---

## Sources

### Primary (HIGH confidence — direct codebase inspection)
- `src/shared/game_server/loot_instance.{h,cpp}` — TakeItem, Serialize, constructor
- `src/shared/game_server/ai/creature_ai_death_state.cpp` — LootInstance creation point
- `src/realm_server/player_group.{h,cpp}` — SetLootMethod, SendUpdate, GroupList packet
- `src/realm_server/guild_mgr.{h,cpp}` — BroadcastEvent, WriteRoster, Guild class
- `src/realm_server/player.cpp` — OnGuildMotd stub, OnGroupInvite, opcode registration
- `src/realm_server/mysql_database.cpp` — LoadGuilds, SetMessageOfTheDay patterns
- `src/mmo_client/systems/party_info.cpp` — OnGroupList, loot method reading
- `src/mmo_client/systems/guild_client.cpp` — GuildSetMOTD, GetGuildMOTD, OnGuildEvent
- `src/mmo_client/game_script.cpp` — existing Lua binding patterns
- `src/shared/auth_protocol/auth_protocol.h` — realm_world_packet enum
- `src/realm_server/world.{h,cpp}` — NotifyPlayerGroupChanged pattern
- `src/world_server/realm_connector.cpp` — OnPlayerGroupChanged handler pattern
- `data/client/Interface/GameUI/*.lua` — all Lua UI files (full inspection)
- `data/realm/updates/*.sql` — guild table schema

### Secondary (MEDIUM confidence)
- None needed — all findings from direct code.

---

## Metadata

**Confidence breakdown:**
- Loot enforcement architecture: HIGH — full call chain traced from opcode to TakeItem
- GroupList loot method bug: HIGH — verified line-by-line in party_info.cpp
- Guild MOTD path: HIGH — every layer inspected end-to-end
- Lua event verification: HIGH — every registration and firing traced
- Protocol change pattern: HIGH — NotifyPlayerGroupChanged exact template available

**Research date:** 2025-07-14
**Valid until:** 2025-08-14 (stable codebase)
