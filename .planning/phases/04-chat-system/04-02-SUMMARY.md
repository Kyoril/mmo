---
phase: 04-chat-system
plan: 02
subsystem: chat-ui-lua
tags: [chat, whisper, raid, localization, lua]
dependency_graph:
  requires: []
  provides: [CHAT_MSG_WHISPER_handler, CHAT_MSG_RAID_handler, CHAT_MSG_SYSTEM_handler, whisper_sticky_reply, raid_slash_commands, chat_locale_strings]
  affects: [ChatFrame.lua, SlashCommandStrings.lua, all_locale_files]
tech_stack:
  added: []
  patterns: [lua-event-handler, sticky-reply-state, slash-command-registration]
key_files:
  created: []
  modified:
    - data/client/Interface/GameUI/ChatFrame.lua
    - data/client/Interface/GameUI/SlashCommandStrings.lua
    - data/client/Locales/Locale_enUS/Localization.txt
    - data/client/Locales/Locale_deDE/Localization.txt
    - data/client/Locales/Locale_frFR/Localization.txt
    - data/client/Locales/Locale_ruRU/Localization.txt
decisions:
  - "CHAT_MSG_SYSTEM handler receives only (this, message) — no senderName — matching world_state.cpp TriggerLuaEvent signature"
  - "ChatFrame_ReplyTarget (sticky) vs ChatFrame_WhisperTarget (active command) are kept as separate state variables for clarity"
  - "RAID ChatTypeInfo sticky=1 so chat box stays in RAID mode between messages"
metrics:
  duration: "~25 minutes"
  completed: "2026-04-05"
  tasks_completed: 2
  files_modified: 6
---

# Phase 04 Plan 02: Chat UI Lua Event Handlers & Localization Summary

**One-liner:** Added 4 missing chat event handlers (WHISPER/WHISPER_INFORM/RAID/SYSTEM), whisper sticky-reply state machine, RAID ChatTypeInfo, `/raid` slash commands, and all locale strings across 4 language files.

## What Was Built

### Task 1: ChatFrame.lua — 5 targeted changes

**New event handlers registered in ChatFrame_OnLoad:**

| Event | Handler behavior |
|-------|----------------|
| `CHAT_MSG_WHISPER` | Sets `ChatFrame_ReplyTarget = senderName`; displays "From [Name]: msg" in pink (WHISPER color) |
| `CHAT_MSG_WHISPER_INFORM` | Displays "To [Name]: msg" in pink (WHISPER_INFORM color) — local echo for sent whispers |
| `CHAT_MSG_RAID` | Displays "\|Hchannel:RAID\|h[Raid]\|h Name: msg" in light-blue (RAID color) |
| `CHAT_MSG_SYSTEM` | Displays raw message string in yellow (SYSTEM color) — no format string, server sends complete text |

**Whisper sticky-reply state machine:**

```
/w PlayerName <body>  →  ChatFrame_ParseText extracts target → ChatFrame_WhisperTarget = "PlayerName"
                          ChatType = "WHISPER"; body placed in ChatInput

Incoming whisper      →  CHAT_MSG_WHISPER handler: ChatFrame_ReplyTarget = senderName

Press Enter (OpenChat) →  ChatFrame_OpenChat: if ReplyTarget set and no input:
                          ChatType = "WHISPER"; ChatFrame_WhisperTarget = ChatFrame_ReplyTarget

Send message          →  ChatFrame_SendMessage: if ChatType == "WHISPER":
                          target = ChatFrame_WhisperTarget or ChatFrame_ReplyTarget
                          SendChatMessage(text, "WHISPER", target)
```

**Additional changes:**
- Added `ChatTypeInfo["RAID"] = { sticky = 1, r = 0.41, g = 0.80, b = 0.94 }` (light-blue)
- Removed commented-out stub `--ChatEdit_ExtractTellTarget(editBox, msg)` — replaced with real target extraction
- `ChatFrame_WhisperTarget` and `ChatFrame_ReplyTarget` declared as module-level state variables

### Task 2: SlashCommandStrings.lua + 4 Localization.txt files

**Slash commands added to SlashCommandStrings.lua:**
```lua
SLASH_RAID1 = "/raid"; -- Raid chat
SLASH_RAID2 = "/ra";   -- Raid chat
```

**Locale keys added to all 4 locale files:**

| Key | enUS | deDE | frFR | ruRU |
|-----|------|------|------|------|
| `CHAT_FORMAT_WHISPER_FROM` | `From [%s]: %s` | `Von [%s]: %s` | `De [%s]: %s` | `От [%s]: %s` |
| `CHAT_FORMAT_WHISPER_TO` | `To [%s]: %s` | `An [%s]: %s` | `À [%s]: %s` | `Кому [%s]: %s` |
| `CHAT_FORMAT_RAID` | `\|Hchannel:RAID\|h[Raid]\|h %s: %s` | `...[Schlachtzug]...` | `...[Raid]...` | `...[Рейд]...` |
| `CHAT_TYPE_WHISPER` | `Whisper` | `Flüstern` | `Chuchoter` | `Шёпот` |
| `CHAT_TYPE_RAID` | `Raid` | `Schlachtzug` | `Raid` | `Рейд` |

## Commits

| Task | Description | Commit |
|------|-------------|--------|
| 1 | ChatFrame.lua changes (TypeInfo, state, handlers, ParseText, SendMessage, OpenChat) | `22c5a44` (data/client submodule) |
| 2 | SlashCommandStrings.lua + all 4 Localization.txt files | `40db866` (data/client submodule) |

## Deviations from Plan

None — plan executed exactly as written. The ruRU locale did not have `CHAT_FORMAT_CREATURE_SAY/YELL` entries (unlike the other three locales), so new CHAT_FORMAT_* keys were inserted after `CHAT_FORMAT_GUILD` instead — same logical insertion point, different anchor line.

## Known Stubs

None. All event handlers are fully wired to real format strings and state variables.

## Self-Check: PASSED

- `data/client/Interface/GameUI/ChatFrame.lua` — modified ✓
- `data/client/Interface/GameUI/SlashCommandStrings.lua` — modified ✓
- `data/client/Locales/Locale_enUS/Localization.txt` — modified ✓
- `data/client/Locales/Locale_deDE/Localization.txt` — modified ✓
- `data/client/Locales/Locale_frFR/Localization.txt` — modified ✓
- `data/client/Locales/Locale_ruRU/Localization.txt` — modified ✓
- Commit `22c5a44` exists in data/client submodule ✓
- Commit `40db866` exists in data/client submodule ✓
