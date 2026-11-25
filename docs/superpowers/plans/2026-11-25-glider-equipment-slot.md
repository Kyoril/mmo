# Glider Equipment Slot Implementation Plan (Part 1 of 4)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Gliders become equipment: a new item type and equipment slot, the glider spell becomes a hidden base spell that only works with a glider equipped, and the glider's look comes from the equipped item.

**Architecture:** The slot is appended in the unused range after buyback (subslot 86 of bag 255) so no existing slot moves; it is treated as equipment by the shared slot helpers. Its replicated fields (inventory guid + visible item block) are appended at the end of the player fields. Server and client gates for gliding check "knows 1436 and has a glider equipped"; `GliderVisual` reads mesh and bone attachment from the equipped glider's `ItemDisplayEntry`.

**Tech Stack:** C++17 (game, game_server, game_client, world_server, realm_server, mmo_client, mmo_edit), Catch2, protobuf data (editor + ClientDB copies), Lua/XML UI (`ui_test`).

**Spec:** `docs/superpowers/specs/2026-11-25-skywright-glider-questline-design.md` (Part 1)

## Global Constraints

- Branch `feature/skywright-quests` in worktree `D:/mmo-traversal` (stacked on `feature/glider-visuals`); data submodules on branches `feature/skywright-quests`.
- No existing inventory slot, bag, bank or buyback number changes. Realm DB migrations additive only.
- Game protocol bump (0x2B → 0x2C) with `python tools/protocol_version_check.py --update` and a row in `docs/protocol_versions.md`.
- Spell 1436 is hidden in the spellbook via `spell_attributes::HiddenClientSide` (`IsSpellBookVisible` already filters it).
- UI strings localized in every locale (`data/client/Locales/Locale_{deDE,enUS,frFR,ruRU}`); deDE uses Ihr/Euch/Euer.
- Exact strings: error "You have no glider equipped." / deDE "Ihr habt keinen Gleiter angelegt."; slot label "Glider" / deDE "Gleiter"; item "Elowen's Prototype" / deDE "Elowens Prototyp".
- Next free ids: items 181, item displays 240 (re-check on the branch before writing data).
- Code style per CLAUDE.md; one-off data scripts in the scratchpad only.

## Review Focus

1. Unequipping or destroying the glider mid-glide → the server ends the glide (`ForceStopGlide`), the client folds the glider; no violation, no kick.
2. A character saved with a glider by this build, loaded by the previous build (rollback) → the glider item is not deleted (test the old load path's handling of subslot 86, Task 3).
3. Remote player equips a different glider while another client watches → that client's glider mesh updates (visible field mirror triggers a re-resolve).
4. Item swap between bag and glider slot with a non-glider item (drag an axe onto the glider slot) → refused with the normal "item doesn't fit" result.
5. Max Vigor and the Vigor bar → appear only once a glider is equipped or Dash is known; equipping/unequipping updates them live.

---

### Task 1: Item type, slot constants and equip rules

**Files:**
- Modify: `src/shared/game/item.h` (add `inventory_type::Glider` before `Count_`; add `namespace player_glider_slots { enum Enum { Slot = 86 }; }` after `player_buy_back_slots`; helper `bool IsGliderSlot(uint8 subslot)`)
- Modify: `src/shared/game/item.cpp` (`s_inventoryTypeStrings` "GLIDER", `GetComparisonEquipmentSlot` returns the glider slot for Glider at index 0)
- Modify: `src/shared/game_server/inventory_types.cpp` (`InventorySlot::IsEquipment` also true for bag 255 / subslot 86)
- Modify: `src/shared/game_server/equipment_manager.cpp` (`ValidateSlotCompatibility`: glider slot accepts only `inventory_type::Glider`; no other slot accepts Glider), `src/shared/game_server/item_validator.cpp` (same rule in the second copy)
- Modify: `src/world_server/player_inventory_handlers.cpp` (`OnAutoEquipItem`: Glider → glider slot)
- Modify: `src/mmo_edit/editor_windows/item_editor_window.cpp` (string table), `data/client/Interface/GameUI/GameParent.lua` (`INVENTORY_TYPE_GLIDER`)
- Test: `src/tests/game_server_tests/glider_slot_test.cpp`

**Interfaces:**
- Produces: `inventory_type::Glider`, `player_glider_slots::Slot` (= 86), `bool IsGliderSlot(uint8 subslot)` in `game/item.h`; `InventorySlot::IsEquipment()` includes the glider slot.

- [ ] **Step 1: Failing tests** `[glider_slot]`: a Glider item validates in slot 0xFF56; a Glider item is refused in Head (0xFF00) and in a pack slot is allowed (bags hold anything); an axe (`inventory_type::Weapon`) is refused in 0xFF56; `InventorySlot::Absolute(0xFF56).IsEquipment()` is true; bag-bar slot 0xFF13 and buyback 0xFF4A..0xFF55 are unchanged in `IsBag`/`IsBuyBack`; `GetComparisonEquipmentSlot(Glider, 0) == player_glider_slots::Slot`.
- [ ] **Step 2:** Build `game_server_tests`, run `bin/Debug/game_server_tests.exe "[glider_slot]"` → compile failure (missing symbols).
- [ ] **Step 3:** Implement per Files; every `switch` on `inventory_type` that has a `default` keeps compiling, every `static_assert` on `Count_` updated.
- [ ] **Step 4:** Tests pass; full `game_server_tests` green.
- [ ] **Step 5: Commit** `game: glider item type and equipment slot`

### Task 2: Replicated fields and protocol bump

**Files:**
- Modify: `src/shared/game/object_type_id.h` (append after `MaxVigor`, before `PlayerFieldCount`: `GliderSlot` (2 fields, item guid) and `VisibleGlider_CREATOR` (2), `VisibleGlider_0` (12), `VisibleGlider_PROPERTIES` (1), same layout as one `VISIBLE_ITEM_FIELDS` block)
- Create: `src/shared/game_server/inventory_fields.h/.cpp` — `uint32 GetInventorySlotField(uint8 subslot)` and `uint32 GetVisibleItemField(uint8 subslot)` (equipment 0-18 keep the old formulas, glider slot maps to the new fields, invalid → `UINT32_MAX`)
- Modify: `src/shared/game_server/inventory.cpp` (replace the inline `InvSlotHead + slot*2` / `VisibleItem1_0 + slot*stride` writes at ~220, 393, 402-404, 1102-1106, 2059-2061 with the helpers)
- Modify: `src/shared/game_protocol/game_protocol.h` (`ProtocolVersion = 0x0000002C`), `src/shared/protocol_fingerprint.json` (via the tool), `docs/protocol_versions.md`
- Test: `src/tests/game_server_tests/inventory_fields_test.cpp`

**Interfaces:**
- Consumes: Task 1 constants.
- Produces: `object_fields::GliderSlot`, `object_fields::VisibleGlider_0` (and siblings); `GetInventorySlotField`, `GetVisibleItemField`.

- [ ] **Step 1: Failing tests** `[inventory_fields]`: Head → `InvSlotHead`; Tabard → `InvSlotHead + 18*2`; pack slot 23 → `PackSlot_1`; glider slot → `GliderSlot`; visible Head → `VisibleItem1_0`; visible glider → `VisibleGlider_0`; a bank slot has no visible field (`UINT32_MAX`).
- [ ] **Step 2:** Run → fails to compile.
- [ ] **Step 3:** Implement; equipping the glider writes both field groups, unequipping clears them.
- [ ] **Step 4:** Tests pass; `python tools/protocol_version_check.py --update`; `python tools/protocol_version_check.py` → OK.
- [ ] **Step 5: Commit** `game: replicate the glider slot (protocol 44)`

### Task 3: Persistence and rollback safety

**Files:**
- Modify: realm load/save of `character_items` (`src/realm_server/mysql_database.cpp` ~226, 504, 700, 2862-2922) only if the glider slot is filtered there today
- Modify: world server inventory load (`Inventory` initialisation from `ItemData`) so subslot 86 is accepted
- Test: `src/tests/game_server_tests/glider_slot_test.cpp` (add)

- [ ] **Step 1: Failing test** `[glider_slot]`: an inventory built from item data containing a glider at 0xFF56 has it equipped (glider field set) after load, and saving yields slot 0xFF56 again.
- [ ] **Step 2:** Run → FAIL (item dropped or misplaced).
- [ ] **Step 3:** Implement the load acceptance.
- [ ] **Step 4:** Tests pass. Read the previous build's load path (`git show 7a7bc2fb:src/shared/game_server/inventory.cpp`) and record in the ledger what it does with an item at subslot 86 (kept as unknown / dropped from memory only / deleted on save). If it would delete the row on save, add a ruling and stop to ask the user before continuing.
- [ ] **Step 5: Commit** `game: persist the glider slot`

### Task 4: Glider spell as hidden base spell, gated by the equipped glider

**Files:**
- Data (scratchpad script, both copies byte-identical): `data/editor/data/spells.data` + `data/client/ClientDB/spells.data` — spell 1436 gets `spell_attributes::HiddenClientSide`, `classmask 0`; `data/editor/data/races.data` — 1436 in every race's `racialSpells` (the realm's login backfill teaches existing characters)
- Modify: `src/shared/game_server/objects/game_player_s.cpp` (`CanUseTraversal(Glider)` = knows 1436 **and** glider slot holds an item; `ComputeMaxVigor` = glider equipped **or** knows Dash; recompute on equip/unequip of the glider slot)
- Modify: `src/world_server/player.cpp` (unequip/destroy of the glider while gliding → `ForceStopGlide`, no violation)
- Create: `src/shared/game_client/glider_usability.h/.cpp` — `bool CanOpenGlider(bool knowsGlider, bool hasGliderEquipped, float predictedVigor, bool alive, bool rooted, bool serverControlled)`; `GameUnitC::IsGliderUsable` calls it
- Modify: client jump-key path: when falling, the glider is known but none is equipped → UI error `ERR_NO_GLIDER_EQUIPPED` (localized in all locales)
- Test: `src/tests/game_server_tests/glider_gate_test.cpp`, `src/tests/game_client_tests/test_glider_usability.cpp`

**Interfaces:**
- Consumes: Task 1 slot constants, Task 2 fields.
- Produces: `CanOpenGlider(...)`; `GamePlayerS::HasGliderEquipped() const`.

- [ ] **Step 1: Failing tests** `[glider_gate]`: player knowing 1436 without a glider → `CanUseTraversal(Glider)` false and max Vigor 0; equip glider → true and max Vigor 100; unequip → false and 0; knows Dash only → max Vigor 100, `CanUseTraversal(Glider)` false. `[glider]` client: `CanOpenGlider` false without equipped glider, true with all conditions met, false when rooted.
- [ ] **Step 2:** Run → fail.
- [ ] **Step 3:** Implement code, then data via a scratchpad script (verify editor and ClientDB copies byte-identical afterwards).
- [ ] **Step 4:** Tests pass; `game_server_tests`, `game_client_tests`, `realm_server_tests` green.
- [ ] **Step 5: Commit** code + data `traversal: glider spell is a hidden base spell gated by the equipped glider`

### Task 5: Prototype item and glider look from the item display

**Files:**
- Data: item 181 "Elowen's Prototype" (`inventorytype` Glider, quality 2, bind on pickup, `displayid` 240, no glide stats) in `items.data` (item skill); item display 240 in editor + ClientDB `item_displays.data` with one variant: mesh `Models/Gear/Glider/Glider.hmsh`, `attached_bone_default` bone `Back_M`, offset (0.10, 0.05, 0), rotation (w 0.70710678, x 0, y 0.70710678, z 0), scale 1
- Create: `src/shared/game_client/glider_display.h/.cpp` — `struct GliderAttachment { String mesh; String bone; Vector3 offset; Quaternion rotation; Vector3 scale; }` and `std::optional<GliderAttachment> ResolveGliderAttachment(const proto_client::ItemDisplayEntry& display, uint32 modelDisplayId)` (first variant that applies to the model, as `ApplyItemDisplay` selects them)
- Modify: `src/shared/game_client/glider_visual.h/.cpp` (constructor takes `const GliderAttachment&`; the fixed `MeshFile`/`AttachBone`/orientation/offset constants go; wing tips stay in mesh space)
- Modify: `src/shared/game_client/game_player_c.cpp` / `game_unit_c.cpp` (mirror `VisibleGlider_0`; keep the resolved attachment; `UpdateGliderVisual` builds the visual from it, rebuilds when the glider display changes)
- Test: `src/tests/game_client_tests/test_glider_display.cpp`

**Interfaces:**
- Consumes: Task 2 `VisibleGlider_0`.
- Produces: `ResolveGliderAttachment`, `GliderVisual(Scene&, Entity&, uint64 guid, const GliderAttachment&)`.

- [ ] **Step 1: Failing tests** `[glider]`: a display with a default bone attachment resolves mesh, bone, offset and rotation exactly; a variant filtered to another model (`variant.model` = 7 for model 8) is skipped; a display without a mesh variant returns nullopt.
- [ ] **Step 2:** Run → fail.
- [ ] **Step 3:** Implement; then the item and display data.
- [ ] **Step 4:** Tests pass; `mmo_client` builds.
- [ ] **Step 5: Commit** `client: glider look comes from the equipped glider item`

### Task 6: Character window travel row

**Files:**
- Modify: `data/client/Interface/GameUI/CharacterWindow.xml` (new frame `CharacterInvSlotGlider`, `inherits="InventoryItemButtonTemplate"`, `id="65366"` (0xFF56), in a "travel row" below the weapon row with a label frame; window height grown so nothing overlaps), `CharacterWindow.lua` (`CHARACTER_EQUIPMENT_SLOTS` includes it)
- Create: slot icon `Interface/Icons/ItemSlots/icons_01_glider.htex` (wing silhouette, same style/size as the other `icons_01_*` slot icons; generated in the scratchpad, imported with `mmo_edit --import-texture`)
- Modify: Localization keys `CHARACTER_SLOT_GLIDER` ("Glider"/"Gleiter") and `ERR_NO_GLIDER_EQUIPPED` in all locales
- Modify: `tools/ui_test/scenarios/` — scenario with the character window open and the glider slot empty and filled

- [ ] **Step 1:** `cmake --build build --config Release -t ui_test`; run the character window scenario before the change; save `artifacts/ui_test/latest.png` as the baseline.
- [ ] **Step 2:** Implement the row; rerun; `status.json` has no errors or unmocked calls; compare against the baseline.
- [ ] **Step 3:** Render with the deDE locale too; send both screenshots to the user (SendUserFile) for sign-off.
- [ ] **Step 4: Commit** data + scenario `ui: glider slot in the character window`

### Task 7: Verification

- [ ] **Step 1:** Local stack + client: GM `.additem 181`, equip from the bag onto the glider slot, glide (look as before), unequip mid-air (glider folds, no kick), try to glide without it (error text), check a second client sees the glider. Screenshots to the user.
- [ ] **Step 2:** `tools/gate/verify.ps1 -Tier fast` green.
- [ ] **Step 3:** Ledger + memory update; Part 2 plan is written next.
