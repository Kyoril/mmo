# Phase 4: Chat System - Context

**Gathered:** 2026-04-05
**Status:** Ready for planning

<domain>
## Phase Boundary

Players can communicate through Say, Yell, Group, Raid, Guild, and Whisper channels. The primary new work is implementing Whisper (cross-instance, with sticky reply and "player not found" feedback) and Raid chat (routing through the existing group system with a separate ChatType for forward-compatibility). Say, Yell, Group, and Guild are already implemented — verify them end-to-end.

Out of scope: chat bubbles (visual feature), custom channel subscriptions (/join ChannelName), Raid Officer channel, offline message queuing.

</domain>

<decisions>
## Implementation Decisions

### Whisper Implementation Details
- Whisper works cross-world-node — realm server routes directly without world node involvement (same pattern as guild/group)
- "Player not found / offline" feedback delivered as a system message in the chat window
- Full reply-to-sender sticky: pressing Enter after receiving a whisper pre-fills `/w SenderName` as the current chat type
- Whisper stored in the chat database log — consistent with all other channels

### Raid Chat Scope
- Raid chat routes through the existing group/party system for now (Phase 5 owns proper raid structure)
- Keep `ChatType::Raid` as a separate opcode/display type for forward-compatibility — client shows raid color, server upgrades routing in Phase 5 without client changes
- Raid Officer (`/ro`) deferred — not in success criteria

### UI & Verification Scope
- ChatFrame shows "To [PlayerName]:" prefix for sent whispers and "From [PlayerName]:" for received — ChatTypeInfo already has WHISPER style (pink)
- Chat bubbles (balloon above player head) deferred — not in success criteria
- Custom channels (/join ChannelName) deferred — Phase 8+ scope
- Verify Say/Yell/Group/Guild end-to-end — confirm client fires events, server dispatches, no silent failures

### the agent's Discretion
- Exact player-name lookup mechanism for cross-instance whisper (likely realm server's existing player registry)
- Sticky reply state storage location (ChatFrame Lua variable vs. C++ side)
- Packet structure for whisper target name

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- `ChatType` enum in `src/shared/game/chat_type.h` — Say, Yell, Group, Raid, Guild, GuildOfficer, Whisper, Emote all defined
- `Player::OnChatMessage()` in `src/realm_server/player.cpp:695` — entry point, routes by ChatType; add Whisper and Raid cases here
- `PlayerGroup::BroadcastPacket()` — group broadcast, reuse for Raid
- `Guild::BroadcastPacketWithPermission()` — guild broadcast pattern
- `ChatFrame.lua` + `ChatTypeInfo` — WHISPER style (pink) already defined; `ChatFrame_ParseText()` handles slash commands; `SendChatMessage(msg, type, target)` 3-arg form already exists
- `GameScript::SendChatMessage()` — already maps "WHISPER" string to ChatType, already parses target param

### Established Patterns
- Group/Guild chat broadcasts directly from realm server without world node involvement — whisper follows this exact pattern
- All channels log to database via `m_database.asyncRequest(..., &IDatabase::ChatMessage, ...)`
- Chat message packet format: sender GUID + chatType + message + flags + tag
- Range-based chat handled by `GameUnitS::DoLocalChatMessage()` — not needed for whisper

### Integration Points
- `src/realm_server/player.cpp:787` — Whisper/Channel stubs, add case here
- `src/realm_server/player.cpp:783` — Raid stub, add case here
- `data/client/Interface/GameUI/ChatFrame.lua` — add sticky whisper reply logic
- Realm server player registry — lookup player GUID by name for cross-instance whisper routing

</code_context>

<specifics>
## Specific Ideas

- Whisper reply sticky: store last whisper sender in a Lua variable `ChatFrame_ReplyTarget`; `ChatFrame_ParseText()` on Enter fills it into the message type
- "Player not found" → send a `realm_client_packet::ChatMessage` with type `System` or a dedicated error packet back to the sender
- For raid routing: in `OnChatMessage()`, `case ChatType::Raid:` → call `m_group->BroadcastPacket()` same as Group, since group IS the raid for now

</specifics>

<deferred>
## Deferred Ideas

- Chat bubbles / speech balloons above player heads — Phase 8+ or post-alpha
- Custom channel subscriptions (/join ChannelName) — full subscription system, Phase 8
- Raid Officer channel (/ro) — Phase 5 with proper raid leadership
- Offline whisper message queuing — post-alpha
- Reply shortcut `/r` alias (without sticky) — deferred since full sticky is chosen

</deferred>
