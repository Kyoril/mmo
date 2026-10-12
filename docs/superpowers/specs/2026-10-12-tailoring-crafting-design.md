# Professions & Tailoring — Design

Date: 2026-10-12 · Branch: `feature/tailoring-crafting`

## Goal

Turn the crafting UI proof of concept (`data/client/Interface/GameUI/Crafting.*`) into a real
profession system and ship Tailoring as the first profession. A player can learn Tailoring
through a short quest chain, sees it in the profession window, finds the recipes there, and
crafts first items. Recipe spells never appear in the spellbook.

Success criteria:

1. Completing the three-quest chain teaches "Tailoring (Apprentice)"; the profession appears in
   the profession window with skill 1/75 and three starter recipes.
2. Crafting a recipe consumes reagents, creates the item, and may raise the skill according to
   the recipe's difficulty colour; grey recipes never raise it, the cap is never exceeded.
3. Recipe spells are not in the spellbook and are not auto-placed on the action bar.
4. Without any profession, the menu bar button is disabled (with an explanatory tooltip) and the
   window cannot be opened (button, `K`, slash commands).
5. Skill values survive logout, world-node transfer, and server restart.
6. Humanoids level 1–12 drop Linen Cloth from one shared base loot table.

Out of scope: unlearning professions, a profession count limit, other professions, dyes from
other professions, recipe items (scrolls), bespoke icons, profession balancing beyond first pass.

## Decisions

| Topic | Decision |
|---|---|
| Profession identity | `SkillEntry` record (approach A) — shared by server and client |
| Skill cap | Carried by the unlock spell (`basepoints`); higher ranks are further spells on the same skill |
| Skill-up | WoW colours: orange (100 %) from `skillrequired`, yellow, green, grey (0 %) |
| Recipe acquisition | Starter recipes on learn + profession trainer with enforced skill requirement |
| Profession limit | None for now |
| Craft N / all | Client-side recast queue, no server change |
| Transport of skill values | Dedicated packets (like `KnownClasses`), not object fields |

## 1. Data model

### `SkillEntry` (`src/shared/proto_data/skills.proto`, mirrored into `src/shared/client_data/`)

```proto
message SkillCategory {
	required uint32 id = 1;
	required string name = 2;        // localized like other proto names
	optional uint32 sortorder = 3;
}

message SkillEntry {
	required uint32 id = 1;
	required string name = 2;        // localized
	required int32 category = 3;     // 1 = profession
	required uint32 cost = 4;
	repeated uint32 spells = 5;      // starter recipes, learned when the skill is first learned
	repeated SkillCategory categories = 6;
	// + localization fields following the project's LocalizedString convention
}
```

Skills are exported to the ClientDB from mmo_edit like spells and classes. mmo_edit gets a
skill editor (or the existing one is extended) for name, category, starter spells and
categories.

### Recipe spells

- `attributes[0]` has `TradeSpell` (0x20, already defined, previously unused).
- Effect `CreateItem` (21), reagents in `reagents` (60) — unchanged.
- `skill` (55) = profession skill id.
- New `skillrequired` = minimum skill to cast; orange from here.
- `trivialskilllow` (56) = yellow from here.
- New `skillgreen` = green from here.
- `trivialskillhigh` (57) = grey from here.
- New `craftcategory` = `SkillCategory.id` within the skill.

New fields are appended (≥ 75) in both proto_data and client_data `spells.proto`.

### Unlock spell

Passive spell with the effect `TradeSkill` (39, defined but never handled until now):
`miscvaluea` = skill id, `basepoints` = skill cap. It stays visible in the spellbook. Later ranks
(Journeyman 150, …) are separate spells with the same effect and skill id.

### Shared rules (`src/shared/game/trade_skill.h`)

Pure functions used by server and client:

- `TradeSkillDifficulty(value, required, yellow, green, grey)` → `orange|yellow|green|grey`
  (`impossible` below `required`).
- `TradeSkillUpChance(value, required, yellow, grey)` → 1.0 below yellow, linear down to 0.0 at
  grey, 0.0 at or above grey.

## 2. Server

### State

`GamePlayerS` holds `std::map<uint32, TradeSkillValue{uint16 value, uint16 max}>` with
accessors `HasTradeSkill`, `GetTradeSkill`, `LearnTradeSkill(id, max)`, `SetTradeSkillValue`.
Changes notify the net watcher.

### `TradeSkill` effect handler (`spell_effects.cpp`)

- Unknown skill: add with value 1 and max `basepoints`; learn all `SkillEntry.spells`.
- Known skill: raise max if `basepoints` is higher; never lower it, never touch value.
- Idempotent, because passive spells re-apply on every login.

### Cast validation (`SingleCastState::ValidatePlayerRequirements`)

- Spell with `skill` ≠ 0: player must have the skill and `value ≥ skillrequired`, else new cast
  result `FailedSkillTooLow`.
- Spell with a `CreateItem` effect on the caster: inventory must be able to take the created
  items, else new cast result `FailedInventoryFull`. This also fixes the existing bug where
  reagents are consumed and the item is lost when bags are full.

### Skill-up

After a successful cast of a spell with `skill` ≠ 0 and `value < max`: roll
`TradeSkillUpChance`; on success `value += 1`, send `TradeSkillUpdate`.

### Trainer

`Player::OnTrainerBuySpell` enforces `reqskill`/`reqskillval` (today only sent to the client).
The Master Tailor uses a `SKILL_TRAINER` trainer entry.

### Persistence

- Realm migration `data/realm/updates/20261012_1_character_skills.sql`:
  `character_skills (character bigint, skill int unsigned, value smallint unsigned, max smallint
  unsigned, PK(character, skill), FK → characters ON DELETE CASCADE)`. Additive. Also added to
  `realm_db_schema_full.sql` with its history row.
- `CharacterData` gets `tradeSkills`; serialized at the end of the stream.
- `GamePlayerS` stream operators append the map.
- `World::OnCharacterData` → `IDatabase::UpdateCharacter` (new parameter), load in
  `mysql_database.cpp` next to classes/emotes.

### Protocol (game `ProtocolVersion` 20 → 21)

- `realm_client_packet::TradeSkills`: `uint8 count, {uint32 skill, uint16 value, uint16 max}[]`.
  Sent on spawn after `InitialSpells`, and whenever a skill is learned or its cap changes.
- `realm_client_packet::TradeSkillUpdate`: `uint32 skill, uint16 value, uint16 max`.
- `spell_cast_result`: append `FailedSkillTooLow`, `FailedInventoryFull`.
- Bot realm connector ignores both packets.
- `python tools/protocol_version_check.py --update`; row in `docs/protocol_versions.md`.

## 3. Client

### C++

- `GameUnitC::SetInitialSpells`/`LearnSpell`: spells with `TradeSpell` are kept out of
  `m_spellBookSpells` (like `HiddenClientSide`).
- `world_state.cpp`: no action bar auto-placement for `TradeSpell` spells; handlers for the two
  packets store values in `GamePlayerC` and fire `TRADE_SKILLS_CHANGED` / `TRADE_SKILL_UPDATE`.
- Cast-error strings for the two new cast results.
- Lua API (`game_script.cpp`):
  - `GetNumProfessions()`, `GetProfessionInfo(i)` → `skillId, name, icon, value, max`. The list
    is built from known spells with a `TradeSkill` effect; per skill id the spell with the
    highest `basepoints` provides name and icon; value/max come from the packet state.
  - `GetProfessionCategories(skillId)` → array of `{id, name}` sorted by `sortorder`.
  - `GetNumRecipes(skillId)`, `GetRecipeInfo(skillId, i)` → `spellId, name, icon, category,
    difficulty, resultItemId, resultCount, reagents = {{itemId, count}, …}`.

### UI (`data/client/Interface/GameUI/Crafting.*`)

- Delete `CraftingMockData.lua` and the hard-coded profession cards; cards are filled from
  `GetProfessionInfo`, unused slots hidden.
- Material counts via `GetItemCount`; result and reagent icons/tooltips from real items.
- Recipe rows coloured by difficulty; "skill-up possible" filter = difficulty not grey.
- Create / Create N / Create All: `CastSpellById`, recast on `PLAYER_SPELL_CAST_FINISH` while
  the queue is non-empty, clear the queue on `PLAYER_SPELL_CAST_FAILED` or cancel; progress via
  the normal CastBar (mock work bar removed).
- Removed: enchant target selection, reset button, hard-coded max 75.
- Menu bar button (new icon) + binding `TOGGLEPROFESSIONS` on `K`. With zero professions the
  button is disabled with tooltip `MENUBAR_TOOLTIP_PROFESSIONS_LOCKED`; toggle, binding and slash
  commands do nothing. Re-evaluated on `TRADE_SKILLS_CHANGED`.
- All strings in all four locales; obsolete mock `CRAFT_*` keys removed.

## 4. Content (`data/editor`, `data/client`)

- Skill 1 "Tailoring", category 1, categories: Materials, Bags, Chest, Legs, Feet, Wrists.
- Items: Coarse Thread (vendor 1 Milly Goldwyn + vendor 5 General Goods), Bolt of Linen Cloth,
  Linen Bag (6 slots), Linen Bracers / Boots / Pants / Robe (white/green, levels 2–10), quest
  items Practice Sewing Kit (use spell) and Patched Linen Pouch. Existing displays/icons reused.
- Recipes: starter — Bolt of Linen Cloth, Linen Bracers, Linen Bag; trainer (gold + skill
  requirement) — Linen Boots, Linen Pants, Linen Robe.
- Spells: "Tailoring (Apprentice)" unlock spell, Practice Sewing Kit use spell (2 Linen Cloth +
  1 Coarse Thread → Patched Linen Pouch; not a recipe, no skill).
- NPC: Master Tailor in Oakenshire near Milly Goldwyn — quest giver and skill trainer.
- Quests (min level 5):
  1. "Good Cloth" — bring 6 Linen Cloth.
  2. "Needle and Thread" — buy Coarse Thread from Milly, sew a Patched Linen Pouch with the
     provided Practice Sewing Kit, deliver it.
  3. "The Test Piece" — the Master Tailor hands back the pouch as the quest's start item; take it
     to Milly for appraisal and return; reward spell "Tailoring (Apprentice)".
  Quest 2 consumes the crafted pouch on turn-in; quest 3 provides its own copy, so the chain cannot
  stall on a lost item.
- Loot: new base table "Humanoid Cloth - Level 1-12" (Linen Cloth). Linen groups removed from
  tables 11 and 16; the new table linked to every humanoid unit of level 1–12 (bandits, poachers,
  cultists, Choirbound Acolyte, dungeon trash 92–95).

## 5. Testing

- Unit (`game_tests` / `game_server_tests`): difficulty and skill-up chance boundaries;
  `TradeSkill` effect (learn, cap raise, idempotence, starter recipes); cast validation
  (`FailedSkillTooLow`, `FailedInventoryFull`, reagents not consumed on failure); skill-up never
  exceeds cap; `CharacterData` round-trip with trade skills.
- E2E scenario `e2e/scenarios/tailoring_profession.lua`: complete the chain, check the profession,
  buy thread, craft a Linen Bag, verify item, reagents consumed, skill > 1, recipe spell not in the
  spellbook list.
- Manual/visual: profession window in the real client, locked menu button without profession.
- Gate: `/gate` fast tier including the WSL Linux step; protocol check passes after the bump.
