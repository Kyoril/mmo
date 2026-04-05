---
phase: 06-character-progression
created: 2026-04-05
status: planning
---

# Phase 6 — Context & Decisions

## Phase Goal
Players gain XP, level up, and unlock abilities — creating a progression loop.

## Scout Findings — Already Working

The progression system is ~95% complete. Most of it is already wired:

| Component | Status | Evidence |
|-----------|--------|---------|
| `RewardExperience()` XP grant | ✅ Full | game_player_s.cpp:1556 |
| Level-up handler (server) | ✅ Full | game_player_s.cpp:1573, world player.cpp:2323 |
| `XpLog` floating text | ✅ Full | world_state.cpp:2557, OnXpLog fires world text "+N XP" |
| `PLAYER_XP_CHANGED` event | ✅ Full | world_state.cpp:452, field mirror on object_fields::Xp |
| `PLAYER_LEVEL_UP` event | ✅ Full | world_state.cpp:2899, fires with all stat diffs |
| `PLAYER_LEVEL_CHANGED` event | ✅ Full | world_state.cpp:458, field mirror on object_fields::Level |
| XP bar UI (XML + Lua) | ✅ Full | GameMenuBar.xml `PlayerExperienceBar`, GameMenuBar.lua registers PLAYER_XP_CHANGED |
| `PlayerXp()`/`PlayerNextLevelXp()` Lua bindings | ✅ Full | game_script.cpp:1057-1058 |
| Level display in PlayerFrame | ✅ Full | PlayerFrame.lua:10, registers PLAYER_LEVEL_CHANGED |
| Level-up chat messages | ✅ Full | ChatFrame.lua:203 handles PLAYER_LEVEL_UP with stat breakdown |
| LEVEL_UP localization (all 4 locales) | ✅ Full | enUS/deDE strings confirmed |
| Quest XP calculation | ✅ Full | game_player_s.cpp:842-872, scaled by level difference |
| `questDetails.rewardedXp` Lua field | ✅ Full | quest_client.cpp:94, reads from packet |
| Trainer level requirements | ✅ Full | TrainerFrame.lua:50-61 greys out by level; server enforces |
| `PLAYER_LEVEL_CHANGED` → trainer refresh | ✅ Full | TrainerFrame.lua:124 |

## Real Gap — Quest XP Display

`questDetails.rewardedXp` is populated from the server packet (quest_client.cpp:702) and exposed to Lua (quest_client.cpp:94), but **QuestFrame.lua never reads or displays it**. Neither the quest detail panel nor the quest rewards panel shows the XP reward.

- `QuestFrame.xml` has money reward frames (`QuestDetailRewardMoney`, `QuestDetailRewardMoneyLabel`) but no XP frame
- QuestFrame.lua shows money via `RefreshMoneyFrame("QuestDetailRewardMoney", ...)` but skips XP entirely
- No `QUEST_REWARD_XP` or similar localization string in any locale

## Implementation Scope

### Only gap to fix: Quest Reward XP Display

1. **QuestFrame.xml** — Add `QuestDetailRewardXpLabel` text frame after the money reward block in `QuestDetailRewards`
2. **QuestFrame.lua** — In `QuestFrame_OnQuestDetail()`, display `questDetails.rewardedXp` when > 0; hide when 0
3. **QuestFrame.xml (rewards panel)** — Also show XP in `QuestFrameRewardsPanel` if separate panel needs it
4. **Localization.txt (all 4 locales)** — Add `QUEST_REWARD_XP = "Experience: %d"` string

## Grey Area Decisions

No user questions needed — all design decisions are self-evident:
- Show XP in the quest detail panel (when accepting) so players know what they'll earn
- Use same layout pattern as money reward (label + value)
- Show only when rewardedXp > 0 (hide for quests with no XP)

## Out of Scope
- Talent/ability auto-learn on level-up (design choice: trainer-only)
- "New trainer spell" notification (not in Phase 6 success criteria)
- Character stat window polish (CharacterWindow.xml already exists)
