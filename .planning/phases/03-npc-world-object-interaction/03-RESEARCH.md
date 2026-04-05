# Phase 3: NPC & World Object Interaction - Research

**Researched:** 2026-04-05
**Domain:** C++ MMO client/server — Lua UI frames, packet opcodes, game object interaction
**Confidence:** HIGH

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Vendor UI Feature Scope**
- No buyback system — defer to Phase 7 (Loot & Economy)
- Display stock count visually only — no server-side interval enforcement
- Extended cost items (badge/token currencies) — skip, not in success criteria
- Repair NPC functionality — skip, defer to Phase 7

**Gossip & Dialog UX**
- Show NPC name/portrait header in gossip dialog
- Fire Lua events (GOSSIP_SHOW, GOSSIP_CLOSED) following same pattern as VendorClient
- Multi-page gossip uses prev/next navigation buttons (GossipMenu action type chains)
- Quest givers show available quest icons in gossip using the show_quests proto flag

**World Object Interaction Scope**
- Implement both doors and chests
- Chest opened: play open animation + stub loot window (Phase 7 owns full loot tables)
- Quest-gated objects enforce requirements server-side (block interaction if conditions not met)
- Door/chest state changes broadcast to all nearby players

**Trainer Implementation Depth**
- Skill requirements on trainer spells deferred to Phase 6 — TODO in code stands
- Already-known spells shown greyed-out in trainer UI with "Known" label (server sends isKnown field)
- Trainer type (class/profession/mount) reflected in window title using TrainerType enum + title field
- Spell tooltips on hover — reuse existing spell tooltip system for consistency

### the agent's Discretion
- Exact Lua frame layout and styling
- Specific opcode names for the new UseObject packet
- How to structure the object state machine implementation details

### Deferred Ideas (OUT OF SCOPE)
- Buyback system (sell-back recently sold items) — Phase 7
- Extended cost items (badge/token currencies) — Phase 7
- Repair NPC functionality — Phase 7
- Skill requirement validation on trainer spells — Phase 6
- Profession trainers (SKILL_TRAINER type) — Phase 6
- NPC reputation-based pricing — post-alpha
</user_constraints>

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| NPC-01 | Player can interact with vendor NPCs to buy and sell items | VendorClient fully wired; VendorFrame.lua exists; server handlers complete |
| NPC-02 | Player can interact with trainer NPCs to learn abilities | TrainerClient fully wired; TrainerFrame.lua exists; server handlers complete |
| NPC-03 | Gossip menus support multi-page dialog trees | QuestClient handles GossipMenu packet; QuestFrame.lua renders gossip; GossipAction routing complete |
| NPC-04 | NPC interaction enforces range and line-of-sight checks | `IsInteractable()` on GameCreatureS checks 5.0f radius + alive + faction — all server handlers call this |
| WOBJ-01 | World objects respond to player interaction (doors open, chests give loot) | UseObject opcode pipeline is the missing piece; GameWorldObjectS::Use() exists; loot path complete for chests |
| WOBJ-02 | Quest-required world objects are only usable when the quest is active | IsUsable() + RequiresQuest flag already implemented; dynamic Interactable flag already broadcast |
| WOBJ-03 | World objects can be enabled/disabled by triggers and scripts | SetEnabled() + SetWorldObjectState trigger action from Phase 1 already complete |
</phase_requirements>

---

## Summary

Phase 3 is more complete than the context description suggests. Deep codebase investigation reveals that server-side NPC handlers (gossip, vendor, trainer), client-side system classes, and all Lua script function bindings are **already fully implemented**. The Lua UI frames (`VendorFrame.lua`, `TrainerFrame.lua`) exist with substantially complete implementations. Gossip is handled by the existing `QuestFrame.lua` + `QuestClient` — no separate GossipFrame is needed.

The single major missing piece is the **world object interaction pipeline**: there is no `UseObject` opcode, no client `RealmConnector::UseObject()` method, and no `Player::OnGameObjectUse()` server handler. The client currently attempts a spell-cast approach via `GetOpenSpell()` / `CastSpell()` which is fragile and can fail if the player lacks an "open" spell. Additionally, `GameWorldObjectS::Initialize()` has a hardcoded TODO setting every object's type to Chest instead of reading `m_entry.type()`, which must be fixed before door-specific Use() behavior can work.

**Primary recommendation:** This phase is predominantly wiring work and one new opcode pipeline. Plan into small, focused tasks: (1) audit/fix vendor/trainer/gossip Lua completeness, (2) implement the UseObject opcode end-to-end, (3) fix the GameWorldObjectType TODO and add door Use() behavior.

---

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Luabind (no-boost) | bundled (`deps/luabind_noboost/`) | C++17 ↔ Lua binding | Project standard; all existing Lua registrations use this |
| Lua | bundled (`deps/lua/`) | UI scripting runtime | Project standard |
| Protobuf | v3.21.12 | Game data (vendors, trainers, gossip menus, objects protos) | Project standard |
| ASIO + custom TCP | bundled | Packet transport | Project standard |
| Catch2 | bundled (`deps/catch/`) | Unit testing | Project standard for server-side tests |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| FrameManager | in-project | Fire Lua events from C++ | Any time C++ needs to notify Lua UI |
| io::Reader / io::Writer | in-project | Packet serialization | All packet read/write |
| ForEachSubscriberInSight | in-project | Broadcast to nearby players | Not needed for world object — Set<>() auto-queues via AddObjectUpdate |

**Installation:** No new packages — everything bundled.

---

## Architecture Patterns

### Recommended Project Structure
```
src/world_server/
├── player.h                   # Add OnGameObjectUse() declaration
├── player.cpp                 # Add UseObject case to switch
├── player_npc_handlers.cpp    # Add OnGameObjectUse() implementation
src/shared/
├── game_protocol/game_protocol.h    # Add UseObject to client_realm_packet enum
├── game_server/objects/game_world_object_s.cpp  # Fix type TODO; add door Use() branch
├── game_client/               # No changes needed
src/mmo_client/
├── net/realm_connector.h/.cpp # Add UseObject() method
├── player_controller.cpp      # Replace spell-cast world-object click with UseObject()
data/client/Interface/GameUI/
├── VendorFrame.lua             # EXISTS — audit completeness
├── TrainerFrame.lua            # EXISTS — audit completeness
```

---

### Pattern 1: Lua Event Registration

**What:** C++ fires named events that Lua frames subscribe to via `self:RegisterEvent()`.

**When to use:** Any time C++ wants to tell the UI "something happened".

**C++ side (fire event):**
```cpp
// Source: vendor_client.cpp:81 and trainer_client.cpp:44
FrameManager::Get().TriggerLuaEvent("VENDOR_SHOW");
FrameManager::Get().TriggerLuaEvent("TRAINER_CLOSED");
FrameManager::Get().TriggerLuaEvent("TRAINER_BUY_ERROR", 0);  // with arg
```

**Lua side (subscribe):**
```lua
-- Source: VendorFrame.lua:128-129, TrainerFrame.lua:113-119
function VendorFrame_OnLoad(self)
    self:RegisterEvent("VENDOR_SHOW", VendorFrame_Show);
    self:RegisterEvent("VENDOR_CLOSED", VendorFrame_Close);
end
```

---

### Pattern 2: Lua Script Function Registration

**What:** Two-tier system. Simple direct calls are in the system class `RegisterScriptFunctions()`. Complex operations (multi-return, game-state-aware) go in `game_script.cpp`.

**When to use:**
- Simple getter/closer → `RegisterScriptFunctions()` in the system class  
- Multi-out or needs game context → `game_script.cpp` with `luabind::joined<pure_out_value<N>...>`

```cpp
// Source: vendor_client.cpp:33-37 — simple registration
void VendorClient::RegisterScriptFunctions(lua_State* lua)
{
    LUABIND_MODULE(lua,
        luabind::def_lambda("GetVendorNumItems", [this]() { return GetNumVendorItems(); }),
        luabind::def_lambda("CloseVendor", [this]() { CloseVendor(); })
    );
}

// Source: game_script.cpp:1142-1145 — multi-out via game_script
luabind::def<std::function<void(int32, const ItemInfo*&, String&, int32&, int32&, int32&, bool&)>>(
    "GetVendorItemInfo", [this](int32 slot, const ItemInfo*& outItem, ...) { ... },
    luabind::joined<luabind::pure_out_value<2>, luabind::pure_out_value<3>, ...>()),
luabind::def_lambda("BuyVendorItem", [this](uint32 slot) { this->BuyVendorItem(slot, 1); }),
```

**Trainer equivalents (game_script.cpp:1148-1152):**
```cpp
luabind::def<std::function<uint32()>>("GetNumTrainerSpells", [this]() { return m_trainerClient.GetNumTrainerSpells(); }),
luabind::def<std::function<void(int32, int32&, String&, String&, int32&, bool&)>>(
    "GetTrainerSpellInfo", [this](int32 slot, int32& out_spellId, ...) { ... },
    luabind::joined<luabind::pure_out_value<2>, ...>()),
luabind::def<std::function<void(uint32)>>("BuyTrainerSpell", [this](uint32 slot) { ... }),
```

---

### Pattern 3: Packet Handler Registration

**What:** Server-side handlers declared in `player.h`, implemented in handler `.cpp` files, dispatched via `player.cpp` switch.

```cpp
// Source: player.h:295-338 (declarations)
void OnGossipHello(uint16 opCode, uint32 size, io::Reader& contentReader);
void OnListInventory(uint16 opCode, uint32 size, io::Reader& contentReader);
// NEW:
void OnGameObjectUse(uint16 opCode, uint32 size, io::Reader& contentReader);

// Source: player.cpp:512-554 (dispatch)
case game::client_realm_packet::GossipHello:
    OnGossipHello(opCode, buffer.size(), reader);
    break;
// NEW:
case game::client_realm_packet::UseObject:
    OnGameObjectUse(opCode, buffer.size(), reader);
    break;
```

---

### Pattern 4: Sending a Packet from Server to Client

```cpp
// Source: player_npc_handlers.cpp:808-813
void Player::CloseGossip()
{
    SendPacket([](game::OutgoingPacket& packet)
        {
            packet.Start(game::realm_client_packet::GossipComplete);
            packet.Finish();
        });
}
```

---

### Pattern 5: Adding a New Client→Server Opcode

**Step 1:** Add to `client_realm_packet` enum in `game_protocol.h`:
```cpp
// Source: game_protocol.h:246 — add before Count_
UseObject,
```

**Step 2:** Add sender in `realm_connector.cpp`:
```cpp
// Source: realm_connector.cpp:572-580 — pattern from Loot()
void RealmConnector::UseObject(uint64 objectGuid)
{
    sendSinglePacket([objectGuid](game::OutgoingPacket& packet) {
        packet.Start(game::client_realm_packet::UseObject);
        packet << io::write<uint64>(objectGuid);
        packet.Finish();
    });
}
```

**Step 3:** Declare in `realm_connector.h`.

**Step 4:** Call from `player_controller.cpp` replacing the current spell-cast approach.

---

### Pattern 6: World Object Field Change → Auto-Broadcast

**What:** `GameObjectS::Set<T>()` in `game_object_s.h:200-204` checks if the value actually changed, then calls `m_worldInstance->AddObjectUpdate(*this)`. The WorldInstance tick processes `m_objectUpdates`, calling `UpdateObject()` → `ForEachSubscriberInSight()` → `NotifyObjectsUpdated()` → sends `UpdateObject` packets to all nearby players.

**Implication:** When `GameWorldObjectS::Use()` calls `Set<uint32>(object_fields::ObjectFlags, ...)`, the state change is **automatically broadcast** to all nearby players — no explicit broadcast code needed in the handler.

```cpp
// Source: game_object_s.h:198-205
template<typename T>
void Set(uint16 index, T value)
{
    const bool updated = m_fields.SetFieldValue(index, value);
    if (updated && m_worldInstance)
        m_worldInstance->AddObjectUpdate(*this);  // queues broadcast
}
```

---

### Pattern 7: Server-Side NPC Interaction Validation

All NPC handlers follow the same validation order:
```cpp
// Source: player_npc_handlers.cpp:682-690, 407-410
1. FindByGuid<GameCreatureS>(objectGuid)  // object must exist
2. unit->IsInteractable(*m_character)      // distance (5.0f) + alive + faction
3. Type-specific check (HasVendorEntry, HasTrainerEntry, etc.)
```

For world objects, use `GameWorldObjectS::IsUsable(player)` which checks `Disabled` + `RequiresQuest` flags.

---

### Existing Lua Functions Available to VendorFrame.lua

| Function | Source |
|----------|--------|
| `GetVendorNumItems()` | `vendor_client.cpp:RegisterScriptFunctions` |
| `CloseVendor()` | `vendor_client.cpp:RegisterScriptFunctions` |
| `GetVendorItemInfo(slot)` → item, icon, price, qty, numAvailable, usable | `game_script.cpp:1142` |
| `BuyVendorItem(slot)` | `game_script.cpp:1144` |
| Events: `VENDOR_SHOW`, `VENDOR_CLOSED` | `vendor_client.cpp:81,129` |

### Existing Lua Functions Available to TrainerFrame.lua

| Function | Source |
|----------|--------|
| `GetNumTrainerSpells()` | `game_script.cpp:1148` |
| `GetTrainerSpellInfo(slot)` → spellId, name, icon, price, isKnown | `game_script.cpp:1150` |
| `BuyTrainerSpell(slot)` | `game_script.cpp:1152` |
| Events: `TRAINER_SHOW`, `TRAINER_UPDATE`, `TRAINER_CLOSED` | `trainer_client.cpp:44,75,151,198` |
| Events: `TRAINER_BUY_ERROR`, `TRAINER_BUY_SUCCEEDED` | `trainer_client.cpp:75,198` |
| Responds to: `MONEY_CHANGED`, `SPELL_LEARNED`, `PLAYER_LEVEL_CHANGED` | `trainer_client.cpp` |

### Existing Lua Functions for Gossip (QuestFrame.lua context)

| Function | Source |
|----------|--------|
| `GetNumGossipActions()` | `quest_client.cpp:107` |
| `GetGossipAction(index)` → {id, text, icon} | `quest_client.cpp:108` |
| `GossipAction(index)` | `quest_client.cpp:115` (calls ExecuteGossipAction) |
| `GetGreetingText()` | `quest_client.cpp:99` |
| Event: `QUEST_GREETING` | `quest_client.cpp:545` (also fires when gossip menu received) |

---

### Anti-Patterns to Avoid

- **Using spell-cast for world object interaction:** The current `player_controller.cpp:810-818` code attempts `m_controlledUnit->GetOpenSpell()` and casts it. This fails if the player has no "open" spell and adds unneeded spell validation overhead. Replace with direct `UseObject` packet.
- **Registering Lua functions in VendorClient/TrainerClient for complex multi-return:** The project already settled on `game_script.cpp` for complex bindings. Don't add multi-out Luabind registrations to the system classes.
- **Hardcoding world object type:** `game_world_object_s.cpp:23` has `// TODO` hardcoding type to Chest. Must fix before door-specific behavior works.
- **Creating loot for Door type in Use():** After fixing the type TODO, `Use()` must branch on `GetType()` — creating a loot instance for a door is wrong.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| State change broadcast to nearby players | Custom broadcast loop | `Set<T>()` auto-calls `AddObjectUpdate()` → WorldInstance tick broadcasts | Already wired — one `Set<>()` call does it all |
| Lua multi-out function | Custom wrapper | `luabind::joined<pure_out_value<N>...>()` | Project pattern already in place (see `GetVendorItemInfo`) |
| Interactable cursor display | Custom hover check | `dynamic_world_object_flags::Interactable` field already sets cursor in `player_controller.cpp:508` | Server already computes and sends this per-player |
| Distance check | Custom math | `unit->IsInteractable(*m_character)` — checks 5.0f radius | Every handler uses this |
| Quest-gate for world objects | Server logic | `world_object_flags::RequiresQuest` + `IsUsable()` | Already implemented in `game_world_object_s.cpp` |

**Key insight:** The main danger is building things that already exist. Audit each "gap" against the codebase before implementing.

---

## Missing Lua Script Functions

Based on VendorFrame.lua and TrainerFrame.lua content vs. registered functions, **all required functions are already registered**. Summary:

| Frame | Functions Used in Lua | All Registered? |
|-------|----------------------|-----------------|
| VendorFrame | GetVendorNumItems, GetVendorItemInfo, BuyVendorItem, CloseVendor | ✅ All in vendor_client or game_script |
| TrainerFrame | GetNumTrainerSpells, GetTrainerSpellInfo, BuyTrainerSpell | ✅ All in game_script |
| QuestFrame (gossip) | GetNumGossipActions, GetGossipAction, GossipAction, GetGreetingText | ✅ All in quest_client |

**No new Lua function registrations required for vendor, trainer, or gossip.**

---

## World Object Interaction Pipeline

### What Needs to Be Added

**1. New opcode in `game_protocol.h`:**
```cpp
// In client_realm_packet enum, before Count_:
UseObject,          // client → server: player wants to use a world object
```

**2. `RealmConnector::UseObject()` in `realm_connector.h`/`realm_connector.cpp`:**
```cpp
// .h declaration
void UseObject(uint64 objectGuid);

// .cpp implementation (pattern from Loot())
void RealmConnector::UseObject(uint64 objectGuid)
{
    sendSinglePacket([objectGuid](game::OutgoingPacket& packet) {
        packet.Start(game::client_realm_packet::UseObject);
        packet << io::write<uint64>(objectGuid);
        packet.Finish();
    });
}
```

**3. Update `player_controller.cpp` world object click:**
```cpp
// CURRENT (fragile, requires open spell):
const proto_client::SpellEntry* unlockSpell = m_controlledUnit->GetOpenSpell();
if (!unlockSpell) { ... error ... }
else { m_spellCast.CastSpell(unlockSpell->id(), m_hoveredObject); }

// NEW (direct opcode):
m_connector.UseObject(m_hoveredObject->GetGuid());
```

**4. `player.h` — declare handler:**
```cpp
// In private section, with other NPC handler declarations:
void OnGameObjectUse(uint16 opCode, uint32 size, io::Reader& contentReader);
```

**5. `player.cpp` — register in switch:**
```cpp
case game::client_realm_packet::UseObject:
    OnGameObjectUse(opCode, buffer.size(), reader);
    break;
```

**6. `player_npc_handlers.cpp` — implement handler:**
```cpp
void Player::OnGameObjectUse(uint16 opCode, uint32 size, io::Reader& contentReader)
{
    uint64 objectGuid;
    if (!(contentReader >> io::read<uint64>(objectGuid)))
    {
        ELOG("Failed to read UseObject packet!");
        return;
    }

    ASSERT(m_worldInstance);
    GameWorldObjectS* worldObject = m_worldInstance->FindByGuid<GameWorldObjectS>(objectGuid);
    if (!worldObject)
    {
        WLOG("UseObject: object not found " << log_hex_digit(objectGuid));
        return;
    }

    // Distance check using object's position vs character position
    const float dist = (worldObject->GetPosition() - m_character->GetPosition()).GetLength();
    if (dist > 5.0f)
    {
        WLOG("UseObject: object too far away");
        return;
    }

    worldObject->Use(*m_character);
}
```

**7. `game_world_object_s.cpp` — fix type TODO and add door branch:**
```cpp
// In Initialize() — replace hardcoded Chest:
Set<uint32>(object_fields::ObjectTypeId, m_entry.type());  // was: Chest // TODO

// In Use() — branch on type:
void GameWorldObjectS::Use(GamePlayerS& player)
{
    if (!IsUsable(player))
    {
        WLOG("Player tried to use object that is not usable");
        return;
    }

    switch (GetType())
    {
    case GameWorldObjectType::Chest:
        {
            // Existing loot logic
            if (!m_loot) { ... }
            player.LootObject(shared_from_this());
        }
        break;

    case GameWorldObjectType::Door:
        {
            // Toggle "open" flag — auto-broadcast via Set<>()
            const uint32 flags = Get<uint32>(object_fields::ObjectFlags);
            const bool isOpen = (flags & world_object_flags::Disabled) == 0;
            // Mark as disabled (closed) or re-enable — or use a dedicated open flag
            // For phase 3: set Disabled flag to toggle open/closed state
            SetEnabled(isOpen ? false : true);

            // Fire server-side Lua hook if any script is registered
            // (Phase 1 trigger system handles OnUse scripting)
        }
        break;
    }
}
```

> **Note on door state semantics:** The simplest approach for doors in phase 3 is to use the `Disabled` flag as a toggle (set to "disabled" = open, cleared = closed, or vice versa). A cleaner approach would be adding a dedicated `Open` flag to `world_object_flags`. The planner should decide which approach to use — either works since `Set<>()` auto-broadcasts the change to nearby players.

---

## FrameUI Frame Creation Pattern

Frame definitions live in `data/client/Interface/GameUI/`. The Lua `OnLoad` handler is the entry point. Study the established pattern from QuestFrame.lua (lines 212-227) and VendorFrame.lua (lines 124-137):

```lua
-- From VendorFrame.lua:124-137
function VendorFrame_OnLoad(self)
    -- 1. Initialize side panel (title bar + close button)
    SidePanel_OnLoad(self);

    -- 2. Register events
    self:RegisterEvent("VENDOR_SHOW", VendorFrame_Show);
    self:RegisterEvent("VENDOR_CLOSED", VendorFrame_Close);

    -- 3. Wire button handlers by name (_G["ButtonName" .. i])
    for i = 1, VENDOR_ITEMS_PER_PAGE, 1 do
        local button = _G["VendorButton" .. i];
        button:SetClickedHandler(VendorButton_OnClick);
        button:SetOnEnterHandler(VendorButton_OnEnter);
        button:SetOnLeaveHandler(VendorButton_OnLeave);
    end
end
```

Frames support:
- `ShowUIPanel(frame)` / `HideUIPanel(frame)` — show/hide with UI panel tracking
- `frame:RegisterEvent("EVENT_NAME", handler)` — subscribe to C++ events
- `_G["FrameName"]` — access named frame by global string
- `button:SetClickedHandler(fn)` / `:SetOnEnterHandler(fn)` / `:SetOnLeaveHandler(fn)`
- `frame:GetChild(index)` — zero-based child access
- `GameTooltip:SetAnchor(...)` / `GameTooltip_SetItemTemplate(item)` — tooltip system
- `RefreshMoneyFrame("MoneyFrameName", amount, ...)` — gold/silver/copper display

---

## Common Pitfalls

### Pitfall 1: TrainerList_Update Called as Event Handler — Self Parameter
**What goes wrong:** `TrainerList_Update` is registered for `MONEY_CHANGED` and `SPELL_LEARNED` events (TrainerFrame.lua:117-119). In these contexts `self` is the frame. But if called manually without a frame reference, `self:GetChild(0):SetText(...)` crashes.
**Why it happens:** Event handlers receive the frame as first argument; direct calls don't.
**How to avoid:** Always call `TrainerList_Update(TrainerFrame)` explicitly when updating outside events.
**Warning signs:** Nil error on `self:GetChild(0)` in TrainerList_Update.

### Pitfall 2: GameWorldObjectType TODO — All Objects Are Chests
**What goes wrong:** `game_world_object_s.cpp:23` hardcodes `ObjectTypeId` to `GameWorldObjectType::Chest` with a `// TODO` comment. If not fixed before implementing door behavior, every world object calls the loot path in `Use()`, including doors.
**Why it happens:** Known incomplete implementation.
**How to avoid:** Fix the TODO first: `Set<uint32>(object_fields::ObjectTypeId, m_entry.type())`.
**Warning signs:** Door objects trigger LootResponse packets instead of state changes.

### Pitfall 3: Chest Use() With Zero Loot Entry Crashes
**What goes wrong:** `GameWorldObjectS::Use()` calls `m_project.unitLoot.getById(m_entry.objectlootentry())` and passes the result to `LootInstance`. If `objectlootentry()` is 0, `getById(0)` may return nullptr, and passing nullptr to LootInstance may crash or produce undefined behavior.
**Why it happens:** Stub chests in phase 3 may have no loot configured.
**How to avoid:** Guard `if (m_entry.objectlootentry() == 0)` — either skip loot creation (no loot window) or create an empty instance. For phase 3, skipping and sending a "nothing here" Lua event is acceptable.
**Warning signs:** Server crash when using a chest with no loot entry configured.

### Pitfall 4: World Object Distance Check Must Use Object Position
**What goes wrong:** Unlike NPC interaction (`unit->IsInteractable()` which checks distance internally), the new `OnGameObjectUse` handler must explicitly check distance. The character-to-object distance must be computed from world positions, not tile positions.
**Why it happens:** `IsInteractable()` is only on GameCreatureS (units), not GameWorldObjectS.
**How to avoid:** Use `(worldObject->GetPosition() - m_character->GetPosition()).GetLength() > 5.0f`.
**Warning signs:** Players can use objects from across the map.

### Pitfall 5: Gossip "QUEST_GREETING" Event Also Fires for Pure Gossip
**What goes wrong:** `QuestClient::OnGossipMenu()` fires `QUEST_GREETING` (line 545) even when there are no quests — it's reused for pure gossip dialogs. The frame name "QuestFrame" may confuse newcomers into thinking gossip needs its own frame.
**Why it happens:** Design decision: gossip+quest share the same frame.
**How to avoid:** Do NOT create a separate GossipFrame. The gossip UI is the QuestFrame with empty quest lists and populated gossip action lists. This is already working correctly.
**Warning signs:** Duplicate frame registered for "GOSSIP_SHOW" event that conflicts with QUEST_GREETING.

### Pitfall 6: Vendor LEFT-Click "Grab" Is A Known TODO
**What goes wrong:** `VendorFrame.lua:95-97` prints "TODO: Grab item from vendor" on left-click. This is intentional — left-click is for a drag-to-bag feature not in scope. Right-click calls `BuyVendorItem(self.id)` which works.
**Why it happens:** UI stub for Phase 3.
**How to avoid:** Leave the TODO as-is. Document that buy-on-left-click is a future polish item. The success criterion (player can buy items) is met via right-click.
**Warning signs:** Treating the TODO as a blocker for NPC-01.

---

## Code Examples

### Verified Example: How GossipMenu Packet Is Handled Client-Side
```cpp
// Source: quest_client.cpp:495-547
PacketParseResult QuestClient::OnGossipMenu(game::IncomingPacket& packet)
{
    uint64 npcGuid;
    String menuText;
    uint8 showQuests;
    packet >> io::read<uint64>(npcGuid)
           >> io::read<uint32>(m_gossipMenu)
           >> io::read_container<uint16>(menuText, 512)
           >> io::read<uint8>(showQuests);

    if (showQuests) ReadQuestList(packet);

    uint16 actionCount;
    packet >> io::read<uint16>(actionCount);
    for (...) {
        // reads actionId, actionIcon, actionText
        m_gossipActions.emplace_back(actionId, actionIcon, std::move(actionText));
    }

    FrameManager::Get().TriggerLuaEvent("QUEST_GREETING");  // fires gossip UI
    return PacketParseResult::Pass;
}
```

### Verified Example: Server SendGossipMenu (full server gossip path)
```cpp
// Source: player_npc_handlers.cpp:755-805
void Player::HandleGossipAction(GameCreatureS& unit, uint32 menuId, const proto::GossipMenuOption& action)
{
    switch (action.action_type())
    {
    case gossip_actions::GossipMenu:
        SendGossipMenu(unit, *m_project.gossipMenus.getById(action.action_param()));
        break;
    case gossip_actions::Trainer:
        SendTrainerList(*m_project.trainers.getById(unit.GetEntry().trainerentry()), unit);
        break;
    case gossip_actions::Vendor:
        SendVendorInventory(*m_project.vendors.getById(unit.GetEntry().vendorentry()), unit);
        break;
    case gossip_actions::Trigger:
        unit.RaiseTrigger(trigger_event::OnGossipAction, { menuId, action.id() }, m_character.get());
        CloseGossip();
        break;
    }
}
```

### Verified Example: GameWorldObjectS::Use() — Current Implementation
```cpp
// Source: game_world_object_s.cpp:62-84
void GameWorldObjectS::Use(GamePlayerS& player)
{
    if (!IsUsable(player))
    {
        WLOG("Player tried to use object that is not usable");
        return;
    }

    if (!m_loot)
    {
        m_loot = std::make_shared<LootInstance>(
            m_project.items, m_worldInstance->GetConditionMgr(), GetGuid(),
            m_project.unitLoot.getById(m_entry.objectlootentry()), 0, 0,
            std::vector{ weakPlayer });
        m_lootSignals += m_loot->closed.connect(...);
        m_lootSignals += m_loot->cleared.connect(...);
    }
    player.LootObject(shared_from_this());
}
// THIS IS THE FULL FUNCTION — door type not handled yet
```

---

## State of the Art

| Old Approach | Current Approach | Impact |
|--------------|------------------|--------|
| Spell-cast to interact with world objects | Direct UseObject opcode (to be added) | Removes spell dependency; more explicit server validation |
| World object type hardcoded to Chest | Read from `m_entry.type()` (TODO fix) | Enables door-type behavior |

**Deprecated/outdated:**
- `GetOpenSpell()` / `CastSpell()` path for world object interaction: Replace with `UseObject()` opcode

---

## Open Questions

1. **Door "open" state representation**
   - What we know: There is no dedicated "door open/closed" flag in `world_object_flags`. `Disabled` exists but semantically means "server-scripted off".
   - What's unclear: Should phase 3 add a new `Open` flag to `world_object_flags`, or reuse `Disabled`?
   - Recommendation: Add a new `Open = 0x04` flag to `world_object_flags` (both client and server headers). This is clean and doesn't misuse `Disabled`. The planner can decide; both approaches work for phase 3.

2. **Chest loot with zero loot entry**
   - What we know: `objectlootentry()` may be 0 for stub chests. `m_project.unitLoot.getById(0)` likely returns nullptr.
   - What's unclear: Does `LootInstance` handle nullptr loot entry gracefully?
   - Recommendation: Guard with `if (m_entry.objectlootentry() == 0) { /* no loot */ return; }` before creating LootInstance. The planner should confirm this is the right stub behavior for WOBJ-01.

3. **Door interaction: should doors require player clicking or only trigger-driven?**
   - What we know: `GameWorldObjectC_Type_Door::CanUse() = false` but this is NOT checked by `GameWorldObjectC::IsUsable()` — only server dynamic flags matter. Doors CAN be made player-clickable if the server marks them Interactable.
   - What's unclear: Should phase 3 allow players to click/open doors directly, or only via triggers?
   - Recommendation: Allow player-clickable doors (the WOBJ-01 requirement says "doors open" in response to interaction). The server already has a `Disabled` flag for doors that admins/scripts don't want players to click.

---

## Environment Availability

Step 2.6: SKIPPED (no new external dependencies identified — all libraries are bundled and the server build environment was confirmed functional in Phase 1 and 2).

---

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 (bundled, `deps/catch/`) |
| Config file | `src/unit_tests/CMakeLists.txt` and `src/game_server_unit_tests/CMakeLists.txt` |
| Quick run command | Build and run `unit_tests` or `game_server_unit_tests` executable |
| Full suite command | Run both test executables |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| NPC-01 | Vendor buy/sell gold transaction is atomic | unit | `game_server_unit_tests` (extend `bag_manager_test` or new `vendor_test.cpp`) | ❌ Wave 0 |
| NPC-02 | Trainer spell purchase deducts gold, grants spell | unit | `game_server_unit_tests` (new `trainer_test.cpp`) | ❌ Wave 0 |
| NPC-03 | Gossip action routing (Vendor/Trainer/GossipMenu) | unit | `unit_tests/test_gossip_handler.cpp` | ❌ Wave 0 |
| NPC-04 | Distance check: interaction fails beyond 5.0f | unit | `unit_tests/test_game_unit_s.cpp` (extend) | ✅ (extend) |
| WOBJ-01 | UseObject on chest creates loot, fires LootResponse | unit | `game_server_unit_tests` (new `world_object_test.cpp`) | ❌ Wave 0 |
| WOBJ-02 | Quest-gated object blocks use when quest not active | unit | same `world_object_test.cpp` | ❌ Wave 0 |
| WOBJ-03 | SetEnabled toggles Disabled flag and broadcasts | unit | same `world_object_test.cpp` | ❌ Wave 0 |
| NPC-01/02/03/04 | End-to-end vendor/trainer/gossip UI in game client | manual | Start server+client, click vendor NPC | manual only |
| WOBJ-01 | End-to-end chest click shows loot frame | manual | Start server+client, click chest object | manual only |

### Sampling Rate
- **Per task commit:** Compile successfully (no automated test for UI changes)
- **Per wave merge:** Run `game_server_unit_tests` — all tests green
- **Phase gate:** Full unit test suite green + manual end-to-end smoke test of each success criterion

### Wave 0 Gaps
- [ ] `src/game_server_unit_tests/world_object_test.cpp` — covers WOBJ-01, WOBJ-02, WOBJ-03
- [ ] `src/game_server_unit_tests/vendor_trainer_test.cpp` — covers NPC-01, NPC-02 server-side logic
- [ ] `src/unit_tests/test_gossip_routing.cpp` — covers NPC-03 action routing logic

*(Unit tests for server-side logic are strongly recommended but the core priority is end-to-end manual testing since the Lua UI paths cannot be unit-tested without a full client.)*

---

## Project Constraints (from copilot-instructions.md)

| Constraint | How It Applies to This Phase |
|------------|------------------------------|
| **Allman braces** — opening brace on its own line | All new C++ code in `OnGameObjectUse`, Use() door branch, RealmConnector::UseObject |
| **Always use braces** even for single-line if bodies | Every if/else in new handlers |
| **Tabs for indentation** | All new C++ source files |
| **`m_camelCase` members** | New member variables if any added to world object |
| **PascalCase public methods** | `OnGameObjectUse`, `UseObject` |
| **`#pragma once`** + Doxygen headers in .h files | New declarations in player.h, realm_connector.h |
| **Copyright header** on all source files | Any new .cpp/.h files |
| **`[[nodiscard]]` and `noexcept`** where appropriate | Const getters on GameWorldObjectS |
| **No exceptions** (`SIMPLE_NO_EXCEPTIONS`) | Don't use try/catch; use error codes and ELOG |
| **snake_case filenames** | New test files: `world_object_test.cpp`, `vendor_trainer_test.cpp` |
| **`ASSERT(x)`**, `ELOG()`, `WLOG()`, `DLOG()` macros | Use consistently in new handlers |

---

## Sources

### Primary (HIGH confidence)
- Direct codebase inspection — all files read in this research session:
  - `src/world_server/player_npc_handlers.cpp` — complete NPC handler implementations
  - `src/world_server/player.h` / `player.cpp` — handler declarations and dispatch
  - `src/mmo_client/systems/vendor_client.cpp` / `trainer_client.cpp` — client system implementations
  - `src/mmo_client/systems/quest_client.cpp` — gossip + quest client with all Lua registrations
  - `src/mmo_client/game_script.cpp` — vendor/trainer Lua bindings (GetVendorItemInfo, BuyVendorItem, GetTrainerSpellInfo, BuyTrainerSpell)
  - `src/shared/game_server/objects/game_world_object_s.cpp` / `.h` — Use(), IsUsable(), field flag system
  - `src/shared/game_client/game_world_object_c_base.cpp` / `game_world_object_c_type_base.h`
  - `src/shared/game_server/world/world_instance.cpp` — UpdateObject, AddObjectUpdate broadcast
  - `src/shared/game_object_s.h` — Set<T>() auto-broadcast mechanism
  - `src/shared/game_protocol/game_protocol.h` — all opcode enums (confirmed no UseObject)
  - `src/mmo_client/player_controller.cpp` — current world object spell-cast path
  - `data/client/Interface/GameUI/VendorFrame.lua` — full existing implementation
  - `data/client/Interface/GameUI/TrainerFrame.lua` — full existing implementation
  - `data/client/Interface/GameUI/QuestFrame.lua` — gossip handling via QUEST_GREETING
  - `data/client/Interface/GameUI/LootFrame.lua` — pattern for world object loot window
  - `src/shared/game/gossip.h` — gossip_actions enum
  - `src/shared/proto_data/objects.proto` — ObjectEntry fields including `type = 6`

### Secondary (MEDIUM confidence)
- `data/scripts/example_object.lua`, `example_npc.lua` — confirmed server-side Lua hook patterns

---

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — all libraries verified from codebase
- Architecture: HIGH — verified from actual source files
- Pitfalls: HIGH — identified from actual TODO comments, code inspection, and design gaps
- Missing UseObject pipeline: HIGH — confirmed by searching all opcodes and handler registrations

**Research date:** 2026-04-05
**Valid until:** 2026-05-05 (stable C++ project, 30-day window reasonable)
