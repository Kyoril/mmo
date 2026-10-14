# Professions

Professions (Tailoring first) are trade skills: a character learns a profession by
learning its unlock spell, then learns recipes as ordinary spells from trainers or items.
Crafting an item is a normal spell cast whose reagents and result come from the recipe
spell. The profession window in the client is a view over this data.

## Data model

- `SkillEntry` (`skills.proto`) describes a profession: id, localized name, the spells it
  owns, and its recipe `categories` (`SkillCategory`: id, localized name, `sortorder`).
- Recipe spells carry the profession skill, category and difficulty thresholds (orange,
  yellow, green, grey) plus their reagents and the item created. The shared rules in
  trade-skill difficulty code turn a character's skill value into a difficulty colour and
  decide whether a craft rolls a skill-up.
- Each character has a per-profession `value`/`max`. The server persists them in the realm
  database, sends them to the client (`TradeSkills` / `TradeSkillUpdate` packets) and
  validates skill and bag space on every craft. Failures surface as the spell cast results
  `SKILL_TOO_LOW` and `INVENTORY_FULL`; trainers refuse recipes above the player's skill
  with `TRAINER_ERROR_SKILL_TOO_LOW`. Recipes are hidden from the spellbook.
- Wire changes are recorded in [protocol_versions.md](protocol_versions.md).

## Lua API (client)

| Function | Result |
|---|---|
| `GetProfessions()` | array of `{ skillId, name, icon, value, max }`, sorted by name; empty without professions |
| `GetProfessionCategories(skillId)` | array of `{ id, name }` sorted by `sortorder` |
| `GetRecipes(skillId)` | array of `{ spellId, name, icon, category, difficulty, resultItemId, resultCount, reagents = { { itemId, count }, ... } }`, ordered by category then name; `difficulty` is `"orange"`, `"yellow"`, `"green"`, `"grey"` or `"impossible"` |
| `HasProfessions()` | true if the player knows at least one profession |
| `GetTradeSkillValue(skillId)` | `value, max` |

Events: `TRADE_SKILLS_CHANGED` (the profession list changed), `TRADE_SKILL_UPDATE`
(`skillId`, a value or cap changed). The window also listens to `SPELL_LEARNED`,
`ITEM_COUNT_CHANGED`, `INVENTORY_CHANGED`, `PLAYER_SPELL_CAST_FINISH` and
`PLAYER_SPELL_CAST_FAILED`.

## Window behaviour

Files: `data/client/Interface/GameUI/Crafting.xml` and `Crafting.lua`.

- The overview shows one card per known profession (up to ten) with icon, name and a
  skill bar; unused cards are hidden. Clicking a card opens its recipe page.
- The recipe list reuses nine slots (virtual scrolling). Category headers collapse and
  expand; recipes are coloured by difficulty (orange, yellow, green, grey, red for
  impossible). The "grants skill points" filter shows everything that is not grey; the
  other filters are materials available and favourites. Selection is keyed by recipe
  spell id.
- Details show the result (icon and tooltip from the item cache, retried until the
  asynchronous item query has answered) and up to three reagents with owned/needed counts.
- Create casts the recipe spell with `CastSpellById`. A local queue repeats it for the
  chosen quantity or "Create all" (limited by the reagents in the bags, capped at 99).
  The queue advances on `PLAYER_SPELL_CAST_FINISH` (which carries only a success flag)
  and stops on `PLAYER_SPELL_CAST_FAILED`, when the window hides or the profession
  changes. While it runs the Create button becomes Cancel (`SpellStopCasting()`). Casting
  progress is shown by the normal cast bar; item creation and reagent use are server-side.
- Favourites and filters are not persisted.

## Lock state and opening

The window is locked until the player knows a profession. The menu bar button
(`Interface/GameUI/Alestia/MenuIcons/Professions.htex`) is disabled and its tooltip reads
`MENUBAR_TOOLTIP_PROFESSIONS_LOCKED`; `Crafting_Toggle()` does nothing, so the key binding
and the slash commands (`/craft`, `/crafting`, `/berufe`, `/professions`) are inert too.
The lock is re-evaluated on `PLAYER_ENTER_WORLD`, `TRADE_SKILLS_CHANGED`,
`TRADE_SKILL_UPDATE` and `SPELL_LEARNED`; an open window closes if the professions vanish.
Default key binding: `K` (`TOGGLEPROFESSIONS`, `Config/DefaultBindings.cfg`).

The menu icon is currently a copy of the Repertoire icon; a bespoke one is pending.

## Localization

Chrome strings are `CRAFT_*` keys; profession, category and recipe names come from game
data. Added keys: `MENUBAR_TOOLTIP_PROFESSIONS(_LOCKED)`, `BINDING_TOGGLEPROFESSIONS`,
`SPELL_CAST_FAILED_SKILL_TOO_LOW`, `SPELL_CAST_FAILED_INVENTORY_FULL`,
`TRAINER_ERROR_SKILL_TOO_LOW`. French and Russian entries for older keys are partly
English placeholders.

## Verification

`python -m unittest tools/tests/test_professions_ui.py` checks statically (standard
library only) that the layout is well-formed with existing frames and textures, that no
`CraftingMock` reference remains, that every `CRAFT_` key used is present in all locales
and none is unused, and that the binding, default key, menu icon and trainer message are
wired. It runs as part of the gate's tool tests. It does not replace a visual check in
the running client (`cmake --build build --config Release -t mmo_client`).
