# Character Creation Outfits

Date: 2026-09-09

## Problem

The character creation preview renders the selected race/gender avatar naked. A player
picking "Human Warrior" sees an underwear-clad body instead of the fantasy the class is
selling. The character *select* screen already dresses characters from their equipment's
item display ids (`CharSelect::SelectCharacter`), but creation has no character yet and so
no equipment.

Item ids cannot be used here: item data is owned entirely by the world server and the
client fetches it per item entry on demand, which is not possible in the character creation
state (the client is only connected to the realm server). Item *display* ids, on the other
hand, are pure client data — they ship in `ClientDB/item_displays.data` and are already
resolvable via `proto_client::Project::itemDisplays`.

## Goals

- Show a class-appropriate, race/gender-aware cosmetic outfit on the creation preview.
- Show the matching combat-ready stance so a drawn weapon reads correctly.
- Let the player toggle the outfit off to inspect the body (skin tone, hair, face).
- Author the outfit data for every class from the art that already exists.

## Non-goals

- Changing what the server actually grants a new character. Starting gear stays
  `races.initialItems` (item ids, server-side) and is deliberately not linked to outfits.
- Dressing the character select screen differently — it already shows real equipment.
- New art. Where a race has no armor variants, its outfit degrades to what does exist.

## Data model

Outfits live on `ClassEntry`, scoped by race and gender. Added to
`src/shared/proto_data/classes.proto` and mirrored **at identical field numbers** in
`src/shared/client_data/classes.proto` (the client subset schema).

```proto
// A cosmetic outfit shown on the character creation preview so a new character is not
// displayed naked. Purely client-side eye candy: it grants nothing and is unrelated to the
// actual starting equipment (see races.initialItems).
message CharacterOutfit {
	// Race id this outfit applies to, or -1 for any race.
	optional int32 race = 1 [default = -1];

	// Gender this outfit applies to (0 = male, 1 = female), or -1 for any gender.
	optional int32 gender = 2 [default = -1];

	// Item display ids applied to the preview model, in order. Armor and weapons alike.
	repeated uint32 item_displays = 3;

	// Skeleton animation state played while the outfit is shown, e.g. "1HReady" or
	// "2HLReady". Empty keeps the model frame's configured idle animation.
	optional string animation = 4;
}

message ClassEntry {
	...
	repeated CharacterOutfit outfits = 28;   // 27 is classlevels
}
```

`-1` rather than `0` is the "any" sentinel because both race ids and class ids are 0-based
in the shipped data (Human = 0, Mage = 0), so `0` is a legitimate race.

### Selection rule

`SelectCharacterOutfit(const ClassEntry&, int32 race, int32 gender)`:

1. Skip any entry whose `race` is set (not -1) and does not equal the selected race.
2. Skip any entry whose `gender` is set (not -1) and does not equal the selected gender.
3. Score the survivors: +2 for an explicit race match, +1 for an explicit gender match.
4. Return the highest-scoring entry; the first entry in list order wins ties.
5. Return `nullptr` when nothing survives — the preview then stays naked, as today.

This lets a class define one race-agnostic outfit and override individual races (or a
single race+gender combination) without repeating the shared entry.

### Transport

No new plumbing. `MainWindow::ExportToClient` copies the server `.data` files verbatim into
the client data directory, and the client's narrower schema silently ignores fields it does
not know. A field added at the same number in both schemas therefore reaches the client as
soon as the editor exports. Nothing goes on the wire, so no `ProtocolVersion` bump and no
`tools/protocol_version_check.py` update are required.

## Client runtime

`CharCreateInfo` (`src/mmo_client/char_creation/char_create_info.h` / `.cpp`) gains the same
item-attachment machinery `CharSelect` already uses (`ApplyItemDisplay`,
`ClearItemDisplayAttachments` from `shared/game_client/item_display_applier.h`).

New state:

- `ItemDisplayAttachmentMap m_itemAttachments` — attached weapon/armor meshes.
- `bool m_outfitVisible = true` — the show/hide toggle, session-scoped.

Changes:

- `SetCharacterCreationFrame` clears `m_itemAttachments` before adopting the new frame. The
  entity pointers in it belong to the previous frame's scene and are dangling — the same
  guard `CharSelect::SetModelFrame` carries.
- `RefreshModel` is restructured. It always forces entity recreation
  (`SetModelFile("")` before setting the real mesh) so sub-entity visibility and material
  overrides are reset to defaults, but it only resets `m_configuration` and re-seeds the
  customization defaults when the *model* changed (race or gender). This is what lets
  `SetSelectedClass` re-dress the preview without discarding the player's customization
  choices. `m_propertyNameCache` is repopulated on every call.
- `SetSelectedClass` now triggers `RefreshModel()` (without setting `m_modelChanged`).
- `ApplyCustomizations()` ends by calling `ApplyOutfit()`. Without this, cycling skin tone
  or hair re-shows the body parts the armor hid through `hidden_by_name` / `hidden_by_tag`,
  because `VisibilitySetPropertyGroup::Apply` rewrites visibility for every tagged
  sub-entity.
- `ApplyOutfit()`:
  - Returns immediately when the outfit is hidden, when there is no frame/entity, or when
    the class has no matching outfit.
  - Resolves the model display id as `race->malemodel()` / `race->femalemodel()` — the same
    id `ApplyItemDisplay` filters display variants by.
  - Calls `ApplyItemDisplay(scene, entity, modelDisplayId, displayId, display, true,
    m_itemAttachments)` for each id in the outfit, with weapons drawn.
  - Sets the model frame's animation to the outfit's `animation` when non-empty.
- `SetOutfitVisible(bool)`: when turning off, clears the attachments and rebuilds the entity
  (the only reliable way to undo `hidden_by_name` / `hidden_by_tag`, which are not
  necessarily covered by any customization visibility group), reapplies customizations, and
  restores the frame's default idle animation. When turning on, re-runs `ApplyOutfit()`.

The default idle animation is captured from the frame's `Animation` property the first time
a frame is adopted, so the XML stays the single source of truth for the idle pose.

### Combat-ready stance

In-world, the combat-ready clip comes from the equipped weapon's item *subclass*
(`item_subclasses.proto: readyanimation`, e.g. `1HReady`, `2HReady`, `2HLReady`), resolved
through item info the creation screen cannot reach. The outfit therefore names the clip
explicitly. `ModelFrame::SetAnimation` already exists and `CharCreate.xml` already sets
`Animation = "Idle"`, so this is a property change, not new rendering work.

### Lua API

Registered in `CharCreateInfo::RegisterScriptFunctions`:

- `SetCharCreateOutfitVisible(visible)` — bool.
- `IsCharCreateOutfitVisible()` — bool.

## UI

`data/client/Interface/GlueUI/CharCreate.xml` and `CharCreate.lua` (the `data/client`
submodule, committed separately).

A `ListCheckButtonBase` checkbox is added inside `CharCreatePropertiesFrame` on
`CharCreatePage2`, anchored below `CharCreatePropertyList`. Page 2 is the customization
page — where a player adjusts skin tone and face — so that is where stripping the gear is
useful. The frame's height grows to fit it.

The checkbox is checked by default, and its `OnClick` calls
`SetCharCreateOutfitVisible(this:IsChecked())`. `CharCreate_ShowCustomizationPage` syncs the
checkbox from `IsCharCreateOutfitVisible()` so the two never disagree.

Localization: a new key `SHOW_OUTFIT` is added to `Localization.txt` in all four locales
(`Locale_enUS`, `Locale_deDE`, `Locale_frFR`, `Locale_ruRU`). Non-English locales get a
translated string where confident and the English text as a placeholder otherwise. The XML
uses the key as its `Text` property value — text properties are localized automatically.

## Editor

`src/mmo_edit/editor_windows/class_editor_window.cpp` gains a "Character Creation Outfits"
collapsing section:

- A list of outfit entries with add/remove.
- Per entry: a race combo (`Any` plus the project's race names), a gender combo
  (`Any` / `Male` / `Female`), an animation-name text field, and an item display list with
  a name-searchable picker and per-row remove.
- A hint line stating the entries are cosmetic and only affect character creation.

## Content

Outfits are authored for all five classes (Mage, Warrior, Cleric, Acolyte, Scout) against
the three shipped races (Human 0, Orc 1, Undead Human 2) using existing item display
entries — the Battlemage white set, the Copper Chain set, the Harmony set, the Acolyte
robe, peasant/recruit pieces, and the existing weapon displays — and written to both
`data/editor/data/classes.data` and `data/client/ClientDB/classes.data`.

**Known coverage limit.** Item display variants are filtered by character model id, and
much of the existing armor art only defines variants for models 7 and 8 (Human female and
male). Orc (12/13) and Undead Human (17/18) may have little or no matching armor, in which
case those races fall back to a weapon-only outfit. The actual coverage is audited against
`item_displays.data` during authoring and reported; no art is invented to fill gaps.

## Testing

- `SelectCharacterOutfit` lives in `src/shared/client_data/character_outfit.h` / `.cpp` —
  pure protobuf, no graphics — so it builds and tests on the headless Linux server build. A
  new `src/tests/client_data_tests/` suite (one `mmo_add_test(client_data_tests client_data)`
  line plus one `add_subdirectory` in `src/tests/CMakeLists.txt`) covers: no outfits, a
  race-agnostic fallback, an exact race+gender match beating a race-only match beating the
  fallback, mismatched entries being skipped, and first-wins tie-breaking.
- Visual verification in the real client (see the `client-visual-verification` memory):
  cycle race/class/gender and confirm the outfit swaps, the ready stance plays, the toggle
  strips and restores gear, and customization cycling does not un-hide covered body parts.
- No E2E scenario: character creation preview is glue-UI-only and the E2E harness drives a
  headless client past that screen.

## Risks

- **Entity rebuild cost on class change.** Switching class now recreates the preview entity.
  This is the same work a race switch already does on every click, so it is acceptable, but
  the customization configuration must survive it — covered by the `m_modelChanged` split
  in `RefreshModel`.
- **Ordering of material overrides.** Outfit material overrides are applied after
  customization material overrides, so armor wins over skin. This is intentional; a skin
  material that must show through armor would need art changes, not code changes.
- **Race art gaps** as described under Content.
