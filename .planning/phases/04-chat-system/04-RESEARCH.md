# Phase 4: Chat System — Research

**Researched:** 2026-04-05
**Domain:** C++ MMO server chat routing + Lua UI event handling
**Confidence:** HIGH — all findings verified directly from source files

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- Whisper works cross-world-node — realm server routes directly without world node involvement (same pattern as guild/group)
- "Player not found / offline" feedback delivered as a system message in the chat window
- Full reply-to-sender sticky: pressing Enter after receiving a whisper pre-fills `/w SenderName` as the current chat type
- Whisper stored in the chat database log — consistent with all other channels
- Raid chat routes through the existing group/party system for now (Phase 5 owns proper raid structure)
- Keep `ChatType::Raid` as a separate opcode/display type for forward-compatibility — client shows raid color, server upgrades routing in Phase 5 without client changes
- Raid Officer (`/ro`) deferred — not in success criteria
- ChatFrame shows "To [PlayerName]:" prefix for sent whispers and "From [PlayerName]:" for received — ChatTypeInfo already has WHISPER style (pink)
- Chat bubbles (balloon above player head) deferred — not in success criteria
- Custom channels (/join ChannelName) deferred — Phase 8+ scope
- Verify Say/Yell/Group/Guild end-to-end — confirm client fires events, server dispatches, no silent failures

### the agent's Discretion
- Exact player-name lookup mechanism for cross-instance whisper (likely realm server's existing player registry)
- Sticky reply state storage location (ChatFrame Lua variable vs. C++ side)
- Packet structure for whisper target name

### Deferred Ideas (OUT OF SCOPE)
- Chat bubbles / speech balloons above player heads — Phase 8+ or post-alpha
- Custom channel subscriptions (/join ChannelName) — full subscription system, Phase 8
- Raid Officer channel (/ro) — Phase 5 with proper raid leadership
- Offline whisper message queuing — post-alpha
- Reply shortcut `/r` alias (without sticky) — deferred since full sticky is chosen
</user_constraints>

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| CHAT-01 | Player can send and receive Say messages (nearby players) | Server-side already implemented at `player.cpp:709` forwarding to world node via `LocalChatMessage`; client fires `CHAT_MSG_SAY`. Verify only. |
| CHAT-02 | Player can send and receive Yell messages (wider radius) | Same pattern as Say — already implemented. Verify only. |
| CHAT-03 | Player can send and receive Group/Raid chat messages | Group: fully implemented. Raid: stub at `player.cpp:783` — add `case ChatType::Raid:` using `m_group->BroadcastPacket()`. |
| CHAT-04 | Player can send and receive Guild chat messages | Already implemented at `player.cpp:737`. Verify only. |
| CHAT-05 | Player can send whisper messages to other players by name | Not implemented. Full breakdown below — 6 coordinated changes across C++ and Lua. |
</phase_requirements>

---

## Summary

Phase 4 is predominantly a gap-filling phase. Say/Yell/Group/Guild are already working at the server level. The primary new work is: (1) the Whisper server handler (cross-instance routing via the realm server's existing player registry), (2) the Raid server handler (reuse group broadcast), and (3) the Lua UI layer (whisper event display, sticky reply, raid display, system message display).

The realm server's `PlayerManager::GetPlayerByCharacterName()` already provides the name-to-Player lookup needed for whisper routing — no new registry infrastructure is required. Routing a whisper packet to a specific player uses `target->SendPacket(...)`, the same pattern used throughout `player_group.cpp` and `friend_mgr.cpp`.

The most important discovery: the client's `OnChatMessage` handler already has `case ChatType::Whisper` and `case ChatType::Raid` — the client side events are already wired. But the ChatFrame.lua has no handlers registered for `CHAT_MSG_WHISPER`, `CHAT_MSG_RAID`, or `CHAT_MSG_SYSTEM`, and the Localization.txt is missing all corresponding format strings.

**Primary recommendation:** Add `ChatType::System` to the shared enum, implement the server-side Whisper and Raid cases, fire `CHAT_MSG_WHISPER_INFORM` locally from `game_script.cpp` on send (avoiding a new protocol opcode), and add all missing Lua event handlers + locale strings.

---

## Standard Stack

### Core (No New Dependencies)
| Component | Location | Purpose |
|-----------|----------|---------|
| `PlayerManager::GetPlayerByCharacterName()` | `src/realm_server/player_manager.cpp:132` | Name→Player* lookup for whisper target resolution |
| `Player::SendPacket()` | `src/realm_server/player.h:357` | Template: encrypts and sends a packet to a specific player's TCP connection |
| `PlayerGroup::BroadcastPacket()` | `src/realm_server/player_group.cpp` | Send to all group members — reuse for Raid |
| `game::realm_client_packet::ChatMessage` | `src/shared/game_protocol/game_protocol.h:306` | Opcode for server→client chat packet |
| `FrameManager::Get().TriggerLuaEvent()` | `src/mmo_client/` | Fire Lua events from C++ |
| `io::read_container<uint8>()` | `src/shared/binary_io/` | Read length-prefixed string (for whisper target name) |

---

## Architecture Patterns

### 1. Whisper Packet Wire Format (Client → Server)

**Verified in** `src/mmo_client/net/realm_connector.cpp:856`:

```cpp
// RealmConnector::SendChatMessage() — client side
packet.Start(game::client_realm_packet::ChatMessage);
packet
    << io::write<uint8>(chatType)
    << io::write_range(message) << io::write<uint8>(0);  // null-terminated message
if (chatType == ChatType::Whisper || chatType == ChatType::Channel)
{
    packet << io::write_dynamic_range<uint8>(target);  // uint8 length prefix + target name bytes
}
packet.Finish();
```

**The server's current `OnChatMessage` parser only reads `chatType` + `message`. For Whisper, the target name bytes remain unread in the buffer — this is the root-cause bug for whisper.**

**Fixed server read:**
```cpp
// src/realm_server/player.cpp — OnChatMessage()
uint8 chatType;
std::string message;
std::string targetName;

if (!(packet >> io::read<uint8>(chatType) >> io::read_limited_string<512>(message)))
{
    return PacketParseResult::Disconnect;
}

// Read target name only for types that send one
if (static_cast<ChatType>(chatType) == ChatType::Whisper
    || static_cast<ChatType>(chatType) == ChatType::Channel)
{
    if (!(packet >> io::read_container<uint8>(targetName)))
    {
        return PacketParseResult::Disconnect;
    }
}
```

### 2. Player Registry Lookup (Name → Player*)

**Verified in** `src/realm_server/player_manager.h:56` and `player_manager.cpp:132`:

```cpp
// PlayerManager::GetPlayerByCharacterName() — already exists, ready to use
Player* target = m_manager.GetPlayerByCharacterName(targetName);
if (target == nullptr)
{
    // Player not found — send system error to sender
}
```

`m_manager` is the `PlayerManager&` member already available inside `Player`. Covers cross-world-instance lookup because all players on the realm are registered, regardless of which world node they're on.

### 3. Sending Packet to Specific Player

**Pattern from** `src/realm_server/friend_mgr.cpp:62`:

```cpp
// Route whisper to target
target->SendPacket([this, &message, chatType](game::OutgoingPacket& outPacket)
{
    outPacket.Start(game::realm_client_packet::ChatMessage);
    outPacket
        << io::write_packed_guid(m_characterData->characterId)  // sender's GUID
        << io::write<uint8>(chatType)
        << io::write_range(message) << io::write<uint8>(0)
        << io::write<uint8>(0)   // flags
        << io::write<uint8>(0);  // tag
    outPacket.Finish();
});
```

**`SendPacket()` handles encryption via `GameCrypt` and flushes the TCP buffer.** This is the single correct API — do not use `GetConnection().sendSinglePacket()` directly (that bypasses the encryption wrapper).

### 4. "Player Not Found" Error — System Message Pattern

**Problem:** `ChatType` enum (verified in `src/shared/game/chat_type.h`) has no `System` value. `ChatTypeInfo["SYSTEM"]` exists in ChatFrame.lua (yellow, line 29), but there is no corresponding enum value or client switch case.

**Solution: Add `System` to the shared enum:**

```cpp
// src/shared/game/chat_type.h — add after UnitEmote
/// @brief A system-generated message displayed in the chat window.
System,
```

**Server sends error to whisper sender:**
```cpp
// In the "not found" branch of the Whisper case:
const std::string errorMsg = "No player named '" + targetName + "' is currently online.";
SendPacket([this, &errorMsg](game::OutgoingPacket& outPacket)
{
    outPacket.Start(game::realm_client_packet::ChatMessage);
    outPacket
        << io::write_packed_guid(0)               // GUID 0 = system
        << io::write<uint8>(ChatType::System)
        << io::write_range(errorMsg) << io::write<uint8>(0)
        << io::write<uint8>(0)   // flags
        << io::write<uint8>(0);  // tag
    outPacket.Finish();
});
```

**Client handles System type** (in `world_state.cpp` OnChatMessage — must handle before the `GetNameCache().Get()` call since GUID 0 would fail the lookup):
```cpp
// Add before the existing else branch in OnChatMessage:
if (type == ChatType::System)
{
    FrameManager::Get().TriggerLuaEvent("CHAT_MSG_SYSTEM", message);
    return PacketParseResult::Pass;
}
```

**ChatFrame.lua registers handler:**
```lua
this:RegisterEvent("CHAT_MSG_SYSTEM", function(this, message)
    local info = ChatTypeInfo["SYSTEM"];
    ChatFrame:AddMessage(message, info.r, info.g, info.b);
end);
```

### 5. Raid Chat Routing — Confirmed Correct

**`m_group->BroadcastPacket()` is the right call.** Verified against the Group case at `player.cpp:725`. Raid case is identical, only `chatType` differs:

```cpp
case ChatType::Raid:
    if (!m_group || !m_group->IsMember(m_characterData->characterId))
    {
        WLOG("Player tried to send raid chat message without being in a group!");
        break;
    }
    m_group->BroadcastPacket([this, &message, chatType](game::OutgoingPacket& outPacket)
    {
        outPacket.Start(game::realm_client_packet::ChatMessage);
        outPacket
            << io::write_packed_guid(m_characterData->characterId)
            << io::write<uint8>(chatType)
            << io::write_range(message)
            << io::write<uint8>(0)
            << io::write<uint8>(0)
            << io::write<uint8>(0);
        outPacket.Finish();
    });
    break;
```

Phase 5 will upgrade this to a proper raid roster. The `chatType` byte transmitted is `ChatType::Raid`, so the client will display it with raid color — forward-compatible.

### 6. Whisper Sent Confirmation (WHISPER_INFORM) — Client-Side Only

`ChatTypeInfo["WHISPER_INFORM"]` already defined in Lua (line 25, pink). The decision to show "To [PlayerName]: msg" when you send a whisper can be done entirely client-side in `game_script.cpp`, avoiding a new protocol opcode:

```cpp
// In GameScript::SendChatMessage(), after routing to m_realmConnector.SendChatMessage():
if (chatType == ChatType::Whisper)
{
    FrameManager::Get().TriggerLuaEvent("CHAT_MSG_WHISPER_INFORM", target, message);
}
```

**ChatFrame.lua registers a handler for CHAT_MSG_WHISPER_INFORM:**
```lua
this:RegisterEvent("CHAT_MSG_WHISPER_INFORM", function(this, targetName, message)
    local info = ChatTypeInfo["WHISPER_INFORM"];
    ChatFrame:AddMessage(string.format(Localize("CHAT_FORMAT_WHISPER_TO"), targetName, message), info.r, info.g, info.b);
end);
```

### 7. Sticky Whisper Reply — Lua State

Two distinct whisper scenarios need separate tracking:

| Scenario | Variable | Set When |
|----------|----------|---------|
| User types `/w Name msg` | `ChatFrame_WhisperTarget` | `ChatFrame_ParseText` extracts first word of WHISPER slash cmd |
| User receives a whisper | `ChatFrame_ReplyTarget` | `CHAT_MSG_WHISPER` event handler fires |

**Module-level declarations** (add near top of ChatFrame.lua, after `ChatType = "SAY"`):
```lua
ChatFrame_WhisperTarget = nil;   -- target for active /w whisper
ChatFrame_ReplyTarget = nil;     -- sticky: last player who whispered you
```

**Fix `ChatFrame_ParseText` for WHISPER case** (currently commented out):
```lua
if ( index == "WHISPER" ) then
    -- Extract target from first word of msg
    local target = string.match(msg, "^(%S+)");
    local body = string.match(msg, "^%S+%s+(.*)") or "";
    if target and string.len(target) > 0 then
        ChatFrame_WhisperTarget = target;
        ChatType = "WHISPER";
        ChatInput:SetText(body);
        ChatEdit_UpdateHeader();
    end
    return;
```

**Fix `ChatFrame_SendMessage`** to pass whisper target as 3rd arg:
```lua
function ChatFrame_SendMessage(this)
    ChatFrame_ParseText(true);
    local text = ChatInput:GetText();
    if ( string.len(string.gsub(text, "%s*(.*)", "%1")) > 0 ) then
        if ( ChatType == "WHISPER" ) then
            local target = ChatFrame_WhisperTarget or ChatFrame_ReplyTarget;
            if target then
                SendChatMessage(text, ChatType, target);
            end
        else
            SendChatMessage(text, ChatType);
        end
    end
    ChatInput_OnEscapePressed();
end
```

**CHAT_MSG_WHISPER handler sets sticky reply target:**
```lua
this:RegisterEvent("CHAT_MSG_WHISPER", function(this, senderName, message)
    ChatFrame_ReplyTarget = senderName;
    local info = ChatTypeInfo["WHISPER"];
    ChatFrame:AddMessage(string.format(Localize("CHAT_FORMAT_WHISPER_FROM"), senderName, message), info.r, info.g, info.b);
end);
```

**Sticky pre-fill on chat open:** In `ChatFrame_OpenChat()`, if `ChatFrame_ReplyTarget` is set, set the chat type to WHISPER for the session (player can override by typing a different slash command):
```lua
function ChatFrame_OpenChat(input)
    -- Sticky whisper reply
    if ( ChatFrame_ReplyTarget and not input ) then
        ChatType = "WHISPER";
        ChatFrame_WhisperTarget = ChatFrame_ReplyTarget;
        ChatEdit_UpdateHeader();
    end
    if (input) then
        ChatInput:SetText(input);
    end
    ChatInputFrame:Show();
    ChatInput:CaptureInput();
end
```

### 8. game_script.cpp Missing RAID Type Mapping

**Verified gap in** `src/mmo_client/game_script.cpp:1288`. Add to `s_typeStrings[]`:
```cpp
{"RAID", ChatType::Raid},
```
Without this, the Lua `SendChatMessage(msg, "RAID")` call silently fails with "Unknown chat type 'RAID'!" — raid messages are never sent.

### 9. Complete List of Lua/Data Gaps

**SlashCommandStrings.lua** — add:
```lua
SLASH_RAID1 = "/raid";
SLASH_RAID2 = "/ra";
```
(Without these, no slash command sets `ChatType = "RAID"` — there's no way to type raid chat.)

**ChatFrame.lua ChatTypeInfo** — add:
```lua
ChatTypeInfo["RAID"] = { sticky = 1, r = 0.41, g = 0.80, b = 0.94 };  -- light blue
```

**ChatFrame_OnLoad** — add missing event registrations:
```lua
this:RegisterEvent("CHAT_MSG_WHISPER", ...);       -- incoming whisper
this:RegisterEvent("CHAT_MSG_WHISPER_INFORM", ...); -- sent whisper echo
this:RegisterEvent("CHAT_MSG_RAID", ...);           -- incoming raid message
this:RegisterEvent("CHAT_MSG_SYSTEM", ...);         -- system/error messages
```

**Localization.txt** — add (enUS and other locales):
```
(key = "CHAT_FORMAT_WHISPER_FROM", string = "From [%s]: %s")
(key = "CHAT_FORMAT_WHISPER_TO", string = "To [%s]: %s")
(key = "CHAT_FORMAT_RAID", string = "|Hchannel:RAID|h[Raid]|h %s: %s")
(key = "CHAT_TYPE_WHISPER", string = "Whisper")
(key = "CHAT_TYPE_RAID", string = "Raid")
```

### 10. Recommended Project Structure for Changes

```
src/shared/game/chat_type.h          ← Add ChatType::System
src/realm_server/player.cpp          ← Fix OnChatMessage: read target, add Whisper + Raid cases
src/mmo_client/game_states/world_state.cpp  ← Handle ChatType::System before name cache lookup
src/mmo_client/game_script.cpp       ← Add "RAID" type string; fire WHISPER_INFORM event
data/client/Interface/GameUI/ChatFrame.lua   ← Handlers, sticky reply, RAID type info
data/client/Interface/GameUI/SlashCommandStrings.lua  ← SLASH_RAID1/2
data/client/Locales/Locale_enUS/Localization.txt ← Format strings
data/client/Locales/Locale_deDE/Localization.txt ← Same strings (deDE locale also present)
data/client/Locales/Locale_frFR/Localization.txt ← Same strings
data/client/Locales/Locale_ruRU/Localization.txt ← Same strings
```

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Player name → connection lookup | Custom map | `m_manager.GetPlayerByCharacterName()` | Already exists, thread-safe with `std::scoped_lock` |
| Sending encrypted packet to player | Direct socket write | `Player::SendPacket()` template | Handles `GameCrypt` encryption and buffer flush |
| Group broadcast for Raid | New broadcast system | `m_group->BroadcastPacket()` | Identical to Group path, forward-compatible by chatType byte |
| Whisper send confirmation | New protocol opcode | `FrameManager::Get().TriggerLuaEvent("CHAT_MSG_WHISPER_INFORM", ...)` | Client-side only; avoids protocol change |

---

## Common Pitfalls

### Pitfall 1: Server Doesn't Read Whisper Target Name from Packet
**What goes wrong:** `OnChatMessage` reads `chatType` and `message`, stops. The `targetName` field (sent by client for Whisper/Channel) sits in the TCP buffer unread. The next packet read corrupts or disconnects the client.
**Root cause:** Client `SendChatMessage()` conditionally appends target via `io::write_dynamic_range<uint8>(target)` for Whisper/Channel only.
**Fix:** Add conditional `io::read_container<uint8>(targetName)` after reading message, guarded by `chatType == ChatType::Whisper`.

### Pitfall 2: System Message GUID 0 Crashes Name Cache Lookup
**What goes wrong:** Client `OnChatMessage` calls `m_cache.GetNameCache().Get(characterGuid, ...)` for all non-NPC types. With `characterGuid = 0` (system message), the name cache either fires with an empty name or hangs waiting for a server name query that never resolves.
**Fix:** In `world_state.cpp OnChatMessage`, add an early return for `ChatType::System` before the `GetNameCache().Get()` call.

### Pitfall 3: `SendPacket()` vs `GetConnection().sendSinglePacket()`
**What goes wrong:** Using `m_connection->sendSinglePacket()` directly (as seen in some older code paths) bypasses the `GameCrypt` encryption wrapper in `Player::SendPacket()`. The client receives garbled data.
**Fix:** Always use `Player::SendPacket()` for packets sent to the player's game connection from realm server context.

### Pitfall 4: `m_group` is null for non-grouped players
**What goes wrong:** Raid case runs `m_group->BroadcastPacket()` without null check → crash.
**Fix:** Add `if (!m_group || !m_group->IsMember(...))` guard (same as Group case at `player.cpp:720`).

### Pitfall 5: `ChatType::System` Enum Value Ordering Breaks Binary Protocol
**What goes wrong:** Adding `System` between existing values (e.g., between `Whisper` and `Emote`) changes all subsequent enum integer values — existing Emote/Channel/UnitSay packets between client and server become mismatched.
**Fix:** Always append new enum values **at the end** (after `UnitEmote`). `System` goes after `UnitEmote`.

### Pitfall 6: No RAID Slash Command → Silent Failure
**What goes wrong:** `ChatFrame_ParseText` iterates `ChatTypeInfo` looking for `SLASH_RAID1` global. If undefined, RAID type is never selected. Players have no way to send raid messages.
**Fix:** Add `SLASH_RAID1 = "/raid"; SLASH_RAID2 = "/ra";` to `SlashCommandStrings.lua`.

### Pitfall 7: Whisper Sticky Reply Pre-fills Before Every Chat Open
**What goes wrong:** `ChatFrame_ReplyTarget` persists across sessions. If last person to whisper you logged out, typing a message sends to a phantom target.
**Fix:** Clear `ChatFrame_ReplyTarget = nil` after a successful whisper (after `ChatFrame_SendMessage` sends). Or document that "player not found" error handles this gracefully.

---

## Code Examples

### Complete Whisper Case (Server)

```cpp
// src/realm_server/player.cpp — inside OnChatMessage() switch
case ChatType::Whisper:
{
    if (targetName.empty())
    {
        WLOG("Whisper received with empty target name, ignoring.");
        break;
    }

    // Log to database before routing (matches existing pattern for all channels)
    // Note: DB log at end of function already handles this for non-early-returns

    Player* target = m_manager.GetPlayerByCharacterName(targetName);
    if (target == nullptr)
    {
        // Player not found — send system error back to sender
        const std::string errorMsg = "No player named '" + targetName + "' is currently online.";
        SendPacket([this, &errorMsg](game::OutgoingPacket& outPacket)
        {
            outPacket.Start(game::realm_client_packet::ChatMessage);
            outPacket
                << io::write_packed_guid(0)
                << io::write<uint8>(ChatType::System)
                << io::write_range(errorMsg) << io::write<uint8>(0)
                << io::write<uint8>(0)
                << io::write<uint8>(0);
            outPacket.Finish();
        });
        break;
    }

    // Deliver whisper to target
    target->SendPacket([this, &message, chatType](game::OutgoingPacket& outPacket)
    {
        outPacket.Start(game::realm_client_packet::ChatMessage);
        outPacket
            << io::write_packed_guid(m_characterData->characterId)
            << io::write<uint8>(chatType)
            << io::write_range(message) << io::write<uint8>(0)
            << io::write<uint8>(0)
            << io::write<uint8>(0);
        outPacket.Finish();
    });
}
break;
```

### Client OnChatMessage System-Type Guard

```cpp
// src/mmo_client/game_states/world_state.cpp — OnChatMessage()
// Add BEFORE the existing if/else NPC-type block

// Handle system messages (GUID 0, no name lookup needed)
if (type == ChatType::System)
{
    FrameManager::Get().TriggerLuaEvent("CHAT_MSG_SYSTEM", message);
    return PacketParseResult::Pass;
}
```

---

## Runtime State Inventory

This phase adds new functionality rather than renaming/refactoring existing identifiers. **No runtime state migration required.**

| Category | Items Found | Action Required |
|----------|-------------|-----------------|
| Stored data | Existing `chat_log` table columns use `chat_type` uint16 — new `ChatType::System` value (14) will be logged if server sends it to the DB async request | None — new value extends the schema naturally |
| Live service config | None | None |
| OS-registered state | None | None |
| Secrets/env vars | None | None |
| Build artifacts | None | None |

---

## Environment Availability

Step 2.6: SKIPPED — This phase consists entirely of C++ and Lua code changes. No external tools, services, or databases beyond the existing project build chain are required.

---

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | Catch2 (bundled in `deps/catch/`) |
| Config file | none — uses `catch_with_main.hpp` in `src/unit_tests/main.cpp` |
| Quick run command | `ctest -R test_chat` (after cmake build) or run `unit_tests.exe "[chat]"` directly |
| Full suite command | `unit_tests.exe` |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| CHAT-01 | Say packet from world node fires `CHAT_MSG_SAY` event | grep | `grep -r "CHAT_MSG_SAY" data/client` | ✅ |
| CHAT-02 | Yell packet fires `CHAT_MSG_YELL` event | grep | `grep -r "CHAT_MSG_YELL" data/client` | ✅ |
| CHAT-03 | Raid case in `OnChatMessage` calls `m_group->BroadcastPacket` | grep | `grep -n "ChatType::Raid" src/realm_server/player.cpp` | ❌ Wave 0 |
| CHAT-03 | `CHAT_MSG_RAID` event handler registered in ChatFrame.lua | grep | `grep -n "CHAT_MSG_RAID" data/client` | ❌ Wave 0 |
| CHAT-04 | Guild case present — verify no regression | grep | `grep -n "ChatType::Guild" src/realm_server/player.cpp` | ✅ |
| CHAT-05 | Whisper case reads target name from packet | unit | `unit_tests.exe "[chat_whisper]"` | ❌ Wave 0 |
| CHAT-05 | `GetPlayerByCharacterName` called in whisper case | grep | `grep -n "GetPlayerByCharacterName" src/realm_server/player.cpp` | ❌ Wave 0 |
| CHAT-05 | `CHAT_MSG_WHISPER` handler in ChatFrame.lua | grep | `grep -n "CHAT_MSG_WHISPER" data/client` | ❌ Wave 0 |
| CHAT-05 | System error message on unknown whisper target | unit | `unit_tests.exe "[chat_whisper]"` | ❌ Wave 0 |

### Sampling Rate
- **Per task commit:** `grep -n "ChatType::Whisper\|ChatType::Raid\|ChatType::System" src/realm_server/player.cpp` (fast existence check)
- **Per wave merge:** `unit_tests.exe` full suite
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps

- [ ] `src/unit_tests/test_chat_protocol.cpp` — Catch2 unit test covering: whisper packet serialization round-trip (write target name, read it back), System ChatType enum value == 13 (compile-time), and `GetPlayerByCharacterName` return semantics via manual mock
- [ ] Framework install: none needed — Catch2 already bundled

---

## State of the Art

| Old Approach | Current Approach | Status | Impact |
|---|---|---|---|
| `case ChatType::Raid: WLOG(...)` | Add group broadcast | **To implement** | Enables raid chat routing |
| Silent whisper drop (no case) | Full cross-instance routing | **To implement** | Core CHAT-05 deliverable |
| No `CHAT_MSG_WHISPER` Lua handler | Register in `ChatFrame_OnLoad` | **To implement** | Client can display whispers |

**Deprecated/outdated:**
- The `//ChatEdit_ExtractTellTarget(editBox, msg);` comment at `ChatFrame.lua:444` is the whisper target extraction stub — implement it inline rather than a separate function

---

## Open Questions

1. **DB logging for "player not found" whispers**
   - What we know: The DB log at `player.cpp:797` runs `m_database.asyncRequest(..., &IDatabase::ChatMessage, ...)` unconditionally after the switch.
   - What's unclear: Should "failed" whispers (target not found) be logged to the database? Current code logs all messages including the Raid and Channel stubs.
   - Recommendation: Skip DB log for failed whispers — add `return PacketParseResult::Pass;` in the "not found" branch before reaching the log call, or add a flag. Alternatively, keep the logging for audit purposes (it logs the intent, not delivery).

2. **Whisper to self**
   - What we know: `m_manager.GetPlayerByCharacterName(targetName)` would find the player themselves if they whisper their own name.
   - What's unclear: Should self-whisper be blocked?
   - Recommendation: Allow it (consistent with guild/group showing your own messages) — the UX is harmless.

3. **`ChatFrame_ReplyTarget` persistence across zones/transfers**
   - What we know: The Lua variable persists for the session.
   - What's unclear: Should it clear on zone transfer?
   - Recommendation: Leave as-is for this phase. The "player not found" error handles the offline case gracefully.

---

## Sources

### Primary (HIGH confidence)
- `src/realm_server/player.cpp` — `OnChatMessage()` implementation, all chat cases (lines 695–800)
- `src/realm_server/player_manager.cpp` — `GetPlayerByCharacterName()` (lines 132–150)
- `src/realm_server/player.h` — `SendPacket()` template (lines 356–375)
- `src/shared/game/chat_type.h` — Complete `ChatType` enum (all values verified)
- `src/shared/game_protocol/game_protocol.h` — All opcode enums including `ChatMessage`
- `src/mmo_client/net/realm_connector.cpp:856` — Client send format for whisper with target
- `src/mmo_client/game_states/world_state.cpp:1576` — Client `OnChatMessage` handler, all type cases
- `src/mmo_client/game_script.cpp:1266` — `SendChatMessage` binding, type string map
- `data/client/Interface/GameUI/ChatFrame.lua` — All existing event handlers, ChatTypeInfo
- `data/client/Interface/GameUI/SlashCommandStrings.lua` — All registered slash commands
- `data/client/Locales/Locale_enUS/Localization.txt` — All existing CHAT_FORMAT_* strings

---

## Metadata

**Confidence breakdown:**
- Player lookup mechanism: HIGH — `GetPlayerByCharacterName()` code read directly
- Packet routing via `SendPacket()`: HIGH — pattern confirmed from `friend_mgr.cpp` and `player_group.cpp`
- Wire format for whisper target: HIGH — `realm_connector.cpp:864` read directly
- Lua event handlers: HIGH — `ChatFrame_OnLoad` read fully; gaps confirmed absent
- `ChatType::System` addition: HIGH — enum file read, no System value present
- RAID type string gap in `game_script.cpp`: HIGH — `s_typeStrings[]` read fully

**Research date:** 2026-04-05
**Valid until:** 2026-05-05 (stable C++ codebase, no external dependencies)
