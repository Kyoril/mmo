---
phase: 05-group-guild-features
plan: "02"
subsystem: guild
tags: [guild, motd, persistence, broadcast, client-ui]
dependency_graph:
  requires: []
  provides: [guild-motd-persistence, guild-motd-broadcast, guild-motd-client-display]
  affects: [realm_server, mmo_client, data/client]
tech_stack:
  added: []
  patterns: [asyncRequest-void-handler, write_dynamic_range-uint16, io-read_container]
key_files:
  created:
    - data/realm/updates/20260405_1_guild_motd.sql
  modified:
    - src/realm_server/database.h
    - src/realm_server/mysql_database.h
    - src/realm_server/mysql_database.cpp
    - src/realm_server/guild_mgr.h
    - src/realm_server/guild_mgr.cpp
    - src/realm_server/player.cpp
    - src/mmo_client/systems/guild_client.cpp
    - data/client/Interface/GameUI/GuildFrame.lua
decisions:
  - "MOTD written as uint16-prefixed field in WriteRoster() after member/rank counts but before rank-permissions data, matching exact client read order"
  - "asyncRequest with bool handler used for SetGuildMotd — consistent with other void DB operations (detail::RequestProcessor<void> provides succeeded flag)"
  - "m_guildMotd cleared of 'TODO' hardcode; MOTD now sourced exclusively from roster packet (login path) and Motd broadcast event (live path)"
metrics:
  duration: "~25 minutes"
  completed: "2026-04-05"
  tasks_completed: 3
  tasks_total: 3
  files_modified: 8
  files_created: 1
---

# Phase 5 Plan 02: Guild MOTD End-to-End Summary

**One-liner:** Guild MOTD persistence via MySQL `motd` column, broadcast on `/gmotd` via `guild_event::Motd`, read back from roster packet, with "TODO" hardcode removed from client.

## Tasks Completed

| # | Task | Commit | Key Files |
|---|------|--------|-----------|
| 1 | SQL migration + database layer | `5202d51c` | `20260405_1_guild_motd.sql`, `database.h`, `mysql_database.h/.cpp` |
| 2 | Guild class m_motd + WriteRoster | `c66ffa12` | `guild_mgr.h`, `guild_mgr.cpp` |
| 3 | OnGuildMotd handler + client fix + GuildFrame.lua | `195c9f87` (data/client: `674b304`) | `player.cpp`, `guild_client.cpp`, `GuildFrame.lua` |

## What Was Built

### Task 1 — SQL Migration + Database Layer
- Created `data/realm/updates/20260405_1_guild_motd.sql`: `ALTER TABLE guilds ADD COLUMN motd VARCHAR(255) NOT NULL DEFAULT '' AFTER leader`
- Added `String motd` field to `GuildData` struct in `database.h`
- Added `IDatabase::SetGuildMotd(uint64 guildId, const String& motd) = 0` pure virtual method
- Added `MySQLDatabase::SetGuildMotd()` override declaration in `mysql_database.h`
- Implemented `MySQLDatabase::SetGuildMotd()` using `EscapeString` + `UPDATE guilds SET motd = ...`
- Updated `LoadGuilds()` SELECT: `COALESCE(motd, '')` at field index 3, read into `info.motd`

### Task 2 — Guild Class m_motd + WriteRoster
- Added `String m_motd` private member to `Guild` class
- Added `[[nodiscard]] const String& GetMotd() const` and `void SetMotd(const String&)` accessors
- Updated `WriteRoster()`: writes `io::write_dynamic_range<uint16>(m_motd)` after counts, before rank permissions loop
- Updated `AddGuild()`: calls `guild->SetMotd(info.motd)` after loading members/ranks from DB

### Task 3 — Server Handler + Client Fix + GuildFrame.lua
- `player.cpp::OnGuildMotd()`: replaced TODO/ELOG stub with `guild->SetMotd(motd)` + `guild->BroadcastEvent(guild_event::Motd, 0, motd.c_str())` + `m_database.asyncRequest(handler, &IDatabase::SetGuildMotd, ...)`
- `guild_client.cpp::NotifyGuildChanged()`: removed `m_guildMotd = "TODO"` — MOTD now only comes from roster packet and Motd event
- `guild_client.cpp::OnGuildRoster()`: reads `io::read_container<uint16>(m_guildMotd)` after counts, before ranks loop (mirrors WriteRoster write order)
- `guild_client.cpp::OnGuildEvent()`: sets `m_guildMotd = args[0]` when `event == guild_event::Motd`
- `GuildFrame.lua::GuildFrame_OnEvent()`: added `GuildMOTDLabel:SetText(GetGuildMOTD())` for MOTD events
- `GuildFrame.lua::GuildRoster_Update()`: added `GuildMOTDLabel:SetText(GetGuildMOTD())` to refresh after roster packet arrives

## Decisions Made

1. **MOTD packet position**: Written between the `(memberCount, rankCount)` header and the rank-permissions data in `WriteRoster()`. Client reads in identical order. This avoids changing existing member serialization which would break compatibility.

2. **asyncRequest handler pattern**: Used `void` return signature for `SetGuildMotd`, so `detail::RequestProcessor<void>` automatically wraps it to produce a `bool succeeded` for the lambda handler — consistent with all other fire-and-forget DB operations in the codebase.

3. **"TODO" removal**: The `m_guildMotd = "TODO"` in `NotifyGuildChanged()` was the bug described in Pitfall 3. Removing it means `GetGuildMOTD()` returns `""` until a roster packet or Motd event arrives — correct behavior, not "TODO".

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing functionality] Added GuildMOTDLabel refresh in GuildRoster_Update()**
- **Found during:** Task 3 (reviewing GuildFrame.lua)
- **Issue:** After `OnGuildRoster()` parses and stores the MOTD, the Lua `GUILD_ROSTER_UPDATE` event fires. `GuildFrame_OnLoad` and `GuildFrame_OnShow` both call `GuildMOTDLabel:SetText(GetGuildMOTD())`, but the `GuildRoster_Update` callback (which handles `GUILD_ROSTER_UPDATE`) did not. This meant a player who logged in after MOTD was set would only see the updated MOTD if they closed and reopened the GuildFrame.
- **Fix:** Added `GuildMOTDLabel:SetText(GetGuildMOTD())` at the top of `GuildRoster_Update()`.
- **Files modified:** `data/client/Interface/GameUI/GuildFrame.lua`

## Known Stubs

None — all MOTD paths fully wired from DB → Guild object → packet → client → UI label.

## Self-Check: PASSED

All created/modified files exist on disk. All three task commits verified in git log:
- `5202d51c` — Task 1
- `c66ffa12` — Task 2  
- `195c9f87` — Task 3 (+ `674b304` in data/client submodule)
