# Acolyte Kit Overhaul Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the acolyte six new trainer spells, a Summoner talent tree and a fully real Blight and Dread tree, with the engine support they need and full presentation (visuals, sounds, animations, particles, unique icons).

**Architecture:** Engine work is small, testable additions in `src/shared/game_server` (one spell effect, four aura types). The game data is authored the way the repo already does it: talents in `tools/talent_trees/author_talent_trees.py` (the source of truth, guarded by a drift test), presentation in `tools/acolyte_visuals/author_acolyte_visuals.py` plus the particle and sfx recipe files. Trainer spells, creatures, trainer and stacking entries are authored by a **scratchpad** script (user rule: no new one-off Python in `tools/`); only the resulting data is committed.

**Tech Stack:** C++17, Catch2 (`game_server_tests`), proto2 data in `data/editor/data/` mirrored byte-for-byte into `data/client/ClientDB/`, Python 3.14 (`py -3.14`) for the existing authoring scripts, ElevenLabs connector (sfx + image generation), Lua E2E scenarios.

**Spec:** `docs/superpowers/specs/2026-11-02-acolyte-kit-overhaul-design.md`

**Worktree:** `H:/mmo/.claude/worktrees/acolyte-kit-overhaul`, branch `feature/acolyte-kit-overhaul`, and the same branch name inside `data/client` and `data/editor` (git worktrees of H:/mmo's submodule repos, so commits land in H:/mmo's `.git/modules`). Always use absolute paths into the worktree.

## Global Constraints

- Code style: Allman braces, braces on every `if`, tabs, `m_camelCase`, PascalCase methods, `#pragma once`, Doxygen on public members, copyright header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- No exceptions; `ASSERT`/`VERIFY`/`WLOG`.
- Editor and ClientDB copies of every touched `.data` stay byte-identical.
- Every acolyte German string uses Ihr/Euch/Euer, never du/dich/dein (convert touched acolyte texts too).
- Localized strings in all 4 locales (deDE, enUS, frFR, ruRU).
- Presentation rules: no `duration_ms` on kits, `sound_ids` only, no sound on AURA_APPLIED/IDLE/TICK, aura kits TARGET scope, only CASTING kits loop, sounds are layered mixes of separately generated layers, prompts ≤ 450 chars.
- Icons: new icons from the image-generation connector, normalised to 128×128, imported with `import_spell_icon.py`; no two player-visible abilities share an icon (scope as in the spec).
- Slow aura basepoints must be ≤ 0 (`ASSERT(slow <= 0)`, game_unit_s.cpp:3456).
- A wire change (new packet or field) needs the protocol bump; new aura/effect enum values alone are not wire changes.
- One-off Python lives in the session scratchpad, never in `tools/`.

## Review Focus

1. **Relog / resummon with pet talents:** Dark Bond and Shadow Sympathy must still apply after dismiss+summon, relog, and talent reset (and stop applying after reset). Test in Task 4.
2. **Curse replacement visible to clients:** CategoryExclusive erases the old aura through the shared_ptr drop, not `RemoveAura`; the client must lose the old curse icon. Test in Task 5.
3. **Slowed creature casts:** Tangled Tongue on a creature must not make the creature AI time out its own cast (creature_ai_combat_state.cpp:1204 uses raw `casttime()`). Test in Task 2.
4. **Plaguebearer chain on mass death:** spread copies carry the Death proc again; Rot Field + Miasma on a pack must not recurse unboundedly in one tick (the dead owner is rejected, but verify no stack growth). Test in Task 5.
5. **Retired talent refund:** a character who had Umbra talents logs in and gets the points back, no dead spells on the action bar acting. Covered in Task 6 (E2E/unit via existing reset logic).

---

## Spell ids, family bits and catalog ids (decided here)

New spell ids **1330-1399** (free). Family bits (shared spell family 0, acolyte-only meaning):

| bit | spells |
|---|---|
| 0x2 | Corrupting Touch (existing) |
| 0x4 | Fear (existing) |
| 0x8 | Veil (existing) |
| 0x10 | Shadowbolt (existing) |
| 0x40 | Curse of Frailty |
| 0x80 | Drain Essence (channel + tick) |
| 0x100 | Curse of the Mire |
| 0x200 | Wasting Plague |
| 0x400 | Call Gloomlings |
| 0x800 | Rot Field (+ tick) |
| 0x1000 | Tangled Tongue |
| 0x2000 | Wail of the Lost |

"Acolyte damage" mask = `0x2 | 0x10 | 0x80 | 0x200 | 0x800` = `0xA92`. Embrace of Dread uses it.

Proposed spell ids (implementer may shift within 1330-1399, keep this table updated in the commit message):

| id | spell |
|---|---|
| 1330 | Curse of Frailty |
| 1331 | Drain Essence (channel) / 1332 Drain Essence tick (HealthLeech) |
| 1333 | Curse of the Mire |
| 1334 | Wasting Plague |
| 1335 | Call Gloomlings |
| 1336 | Rot Field / 1337 Rot Field tick |
| 1340-1341 | Dark Bond r1/r2 |
| 1342 | Swarm Lore |
| 1343 | Shadow Sympathy / 1344 its pet aura |
| 1345 | Empower Duskling |
| 1346 | Gloom Tether / 1347 single-Gloomling summon |
| 1348 | Shared Night / 1349 pet heal |
| 1350 | Dark Tutelage |
| 1351 | Soul Siphon |
| 1352 | Brood Horror |
| 1355 | Miasma |
| 1356 | Creeping Rot / 1357 slow debuff |
| 1358 | Venomous Backlash |
| 1360 | Tangled Tongue |
| 1361 | Grasping Dread / 1362 root |
| 1363 | Wail of the Lost |
| 1364 | Lingering Dread / 1365 healing debuff |

Talent ids: reuse the Umbra path's talent ids (160-167, 2) for the Summoner nodes and the placeholder ids (169, 171, 172, 176, 178, 179, 180) for the filled-in nodes, so no talent id disappears.

Visualization ids **141-160**, sound entry ids: next free above the current max (check `sounds.data`), stacking category **3 "Acolyte Curses"**, unit entries: next free ids for "Gloomling" and "Brood Horror".

## Summoner tree (final, replaces spec's draft where noted)

| Slot | Talent | Mechanic |
|---|---|---|
| A1 | Dark Bond (2 ranks) | owner aura ModPetStatsPct: damage +15/30 (misc 0), health +10/20 (misc 1) |
| A2 | Swarm Lore | AddFlat AllEffects +1 and Duration +5000 on mask 0x400 |
| A3 | Shadow Sympathy *(replaces "Pack Instinct": needs no conditional-aura engine)* | owner aura PetPassive → pet carries 1344 (ModDamageTakenPct −20) |
| B1 | Gloom Tether | CT periodic proc 10% → 1347 (Summon 1 Gloomling, 8 s) |
| M | Empower Duskling (active, 60 s cd) | targeta Pet: ModDamageDonePct +30, ModAttackSpeed +30, 12 s |
| B2 | Shared Night | proc on Drain tick (procfamily 0x80) → 1349 Heal targeta Pet |
| C1 | Dark Tutelage | Cooldown −30000 on mask 0x400 |
| C2 | Soul Siphon | CastTime +1000 on mask 0x80 (channel lasts 1 s longer, one more tick) |
| CAP | Brood Horror (active, 180 s cd) | Summon 1 "Brood Horror", 20 s |

---

### Task 1: HealthLeech spell effect

**Files:**
- Modify: `src/shared/game_server/spells/spell_effects.h`, `spell_effects.cpp` (new `HandleHealthLeech`), `single_cast_state.cpp:1165-1217` (register in `kHandlers`)
- Test: `src/tests/game_server_tests/spell_health_leech_test.cpp`

**Interfaces:**
- Produces: effect type `spell_effects::HealthLeech` (9) works. Damage computed exactly like `HandleSchoolDamage`; caster healed by `dealt * ratio` where ratio = `effect.multiplevalue()`, treated as 1.0 when 0. Heal goes through `GameUnitS::Heal` and sends `SpellHealLog`.

- [ ] **Step 1: Write failing tests** `[health_leech]`: (a) caster at 50/100 hp casts a leech spell (basepoints 10, multiplevalue 1.0) on a creature → target lost 10 (or modified amount `dealt`), caster gained `dealt`; (b) multiplevalue 0.5 → caster gains `dealt/2`; (c) immune target → no heal. Follow the setup style of `creature_cast_facing_test.cpp` / existing spell tests in that folder.
- [ ] **Step 2:** Build `game_server_tests`, run `bin/Debug/game_server_tests.exe "[health_leech]"` → FAIL.
- [ ] **Step 3:** Implement `void SpellEffects::HandleHealthLeech(SpellEffectContext& ctx)` by factoring the damage computation out of `HandleSchoolDamage` into a private helper that returns the dealt amount (no copy-paste of the 90-line body); register it.
- [ ] **Step 4:** Tests pass; run the whole `game_server_tests` suite.
- [ ] **Step 5:** Commit `feat(spells): HealthLeech effect heals the caster for a share of damage dealt`.

### Task 2: ModCastingSpeedPct aura (target cast time)

**Files:**
- Modify: `src/shared/game/aura.h` (append `ModCastingSpeedPct = 42` before `Count_`), `aura_effect.cpp:78-116` handler, `game_unit_s.h/.cpp` cached scalar, `single_cast_state.cpp:52-58`, `ai/creature_ai_combat_state.cpp:1204`
- Also: any aura-name tables (editor `spell_editor_window`, `spell_catalog_lib.py AURA_TYPE_NAMES`, talent script constants) get the new name.
- Test: `src/tests/game_server_tests/aura_casting_speed_test.cpp`

**Interfaces:**
- Produces: aura type 42, basepoints = percent (+30 → cast time ×1.30, −20 → ×0.80, floor 0). `float GameUnitS::GetCastingSpeedMultiplier() const` (1.0 default). Applies to every spell the owner casts that has a cast time (incl. channel length, since `m_castTime` feeds ChannelStart), not to procs.

- [ ] **Step 1: Failing tests** `[casting_speed]`: unit with aura +30 casting a 2000 ms spell → `SingleCastState` cast time 2600; two auras +30 and +20 → multiplicative or additive (choose additive: ×1.50, document it); removed aura → 2000; creature with the aura: AI casting timeout ≥ modified cast time (assert the AI does not abort a 2600 ms cast).
- [ ] **Step 2:** Run → FAIL.
- [ ] **Step 3:** Implement: handler accumulates into a cached scalar like `ModCastingManaRegenPercent` does; multiply in the `SingleCastState` ctor after the CastTime spell mod; creature AI timeout uses the modified time.
- [ ] **Step 4:** Pass; full suite green.
- [ ] **Step 5:** Commit `feat(auras): ModCastingSpeedPct slows a unit's casts`.

### Task 3: ModHealingTakenPct aura

**Files:** `aura.h` (`ModHealingTakenPct = 43`), `aura_effect.cpp`, `game_unit_s.*`, `spell_effects.cpp` heal path (`HandleHeal` and periodic heal in `aura_effect.cpp:687-691`), name tables as in Task 2. Test `aura_healing_taken_pct_test.cpp`.

**Interfaces:** Produces aura 43, basepoints percent (−20 → heals ×0.8), applies to direct, periodic and HealPct heals received, floor 0.

- [ ] Step 1 failing tests `[healing_taken_pct]` (direct heal 100 → 80; periodic heal per tick scaled; removal restores) → Step 2 FAIL → Step 3 implement → Step 4 pass → Step 5 commit `feat(auras): ModHealingTakenPct`.

### Task 4: Pet empowerment auras

**Files:** `aura.h` (`ModPetStatsPct = 44`, `PetPassive = 45`), `aura_effect.cpp`, `game_unit_s.*` (owner-side cached pet multipliers), `src/shared/game_server/pets/persistent_pet_spawner.cpp:156` (multiply into `SetPetStatMultipliers`), pet spawn path (apply PetPassive triggerspells to the spawned pet), `spell_target_resolver.cpp` (verify/implement targeta `Pet` (4) resolving to the caster's summoned persistent pet). Test `pet_owner_auras_test.cpp`.

**Interfaces:**
- `ModPetStatsPct`: miscvaluea 0 = damage %, 1 = health %; owner scalar `float GameUnitS::GetPetStatMultiplier(uint8 kind) const`. Applied at spawn and re-applied to the live pet on aura apply/remove (`SetPetStatMultipliers` + `RefreshStats`, health ratio preserved).
- `PetPassive`: `effect.triggerspell` is a passive spell applied to the owner's persistent pet while the owner aura exists (on apply if a pet is out, on every later spawn); removed from the pet on aura removal.
- Target `Pet` (4): resolves to the caster's active persistent pet; spell fails with a "no pet" result if none.

- [ ] **Step 1: Failing tests** `[pet_owner_auras]`: owner with ModPetStatsPct damage +30 summons pet → pet min/max damage ×1.3 vs without; health +20 → max health ×1.2; aura removed while pet out → back to base, current health clamped; PetPassive with a ModDamageTakenPct −20 spell → pet has the aura after spawn, after dismiss+resummon, not after owner aura removal; a spell with targeta Pet and no pet → cast fails.
- [ ] Step 2 FAIL → Step 3 implement → Step 4 pass (full suite) → Step 5 commit `feat(pets): owner auras that empower the persistent pet`.

### Task 5: Verify existing mechanics the kit relies on (tests first, fix only what fails)

**Files:** test `src/tests/game_server_tests/acolyte_mechanics_test.cpp`; fixes wherever a test exposes a bug.

- [ ] **Step 1: Tests** `[acolyte_mechanics]`:
  1. *Plaguebearer spread:* caster with an AddFlat ChanceOfSuccess +100 mask 0x2 aura applies a CT-like spell (Death proc, targetb TargetAreaEnemy radius 12, maxtargets 2) to A; B, C, D stand within 10 yd; A dies → exactly 2 of B/C/D carry the aura. Without the modifier → none.
  2. *Chain on mass death:* A, B, C all carry it and die in the same update → no crash, each living neighbour carries at most one instance.
  3. *Curse exclusivity:* two spells with stacking_rule 5 (CategoryExclusive) and category 3 from caster X on target T → only the newer remains; the same spell from caster Y coexists; the observer of T receives an aura-removed update for the replaced aura (check the outgoing packet/field change the client uses).
  4. *Proc on non-damaging negative spell:* passive with procflags matching a negative no-damage aura spell (Curse of the Mire shape) and procfamily 0x100 fires its trigger on the victim (needed by Grasping Dread and Lingering Dread). Record which proc flag works (`DoneSpellMagicDmgClassNeg` 0x10000 or `DoneSpellNoneDmgClassNeg` 0x1000).
  5. *Summon modifiers:* Summon effect with AddFlat AllEffects +1 on its family bit spawns basepoints+1 creatures; Duration +5000 extends their despawn.
- [ ] **Step 2:** Run; for each failing test fix the engine minimally (this is where the spec's "spread on death" item lands if the existing wiring does not work).
- [ ] **Step 3:** Full suite green; commit `test(spells): pin the mechanics the acolyte kit relies on` (+ fixes).

### Task 6: Talent trees (Summoner, Blight, Dread) in the author script

**Files:** `tools/talent_trees/author_talent_trees.py` (ACOLYTE dict, `RETIRED_RANK_SPELLS`, constants for new aura/effect ids), `tools/tests/test_talent_trees.py` if it pins node counts, `data/editor/data/{spells,talents,talent_tabs}.data` + ClientDB copies + `data/client/Locales/*/Localization.txt` via `--apply`.

**Interfaces:**
- Consumes: aura ids 42-45 (Tasks 2-4), Pet target (Task 4), proc flag from Task 5.4, trainer spell ids and family bits above (the talent mods reference trainer spells authored in Task 7 — Task 7 must run `--apply` order-independently: the talent script owns talent spells only).
- Produces: path 0 = Summoner (label key `TALENT_PATH_SUMMONER`, accent violet), path 1 Blight, path 2 Dread; all 27 nodes real (no `ph(...)` left for the acolyte).

- [ ] **Step 1:** Rewrite path 0 as the Summoner table above; Blight: Miasma (proc on Rot Field tick 0x800 → cast 24 on Instigator), Creeping Rot (Rot Field Duration +4000 mask 0x800 and proc → 1357 ModDecreaseSpeed −30 4 s on Instigator), Venomous Backlash (procflags TakenMeleeAutoAttack 0x8, chance 20 → 1334 on Instigator), Plaguebearer kept (verify its mod hits CT 0x2 and the Task 5.1 result); Dread: Tangled Tongue (active curse, ModCastingSpeedPct +30, 20 s, 30 yd, stacking_rule 5 category 3, family 0x1000), Grasping Dread (proc on Mire 0x100 → 1362 ModRoot 2 s), Wail of the Lost (active, ModFear SourceAreaEnemy r8 maxtargets 3, 3 s, 60 s cd, family 0x2000), Lingering Dread (proc on Frailty 0x40 → 1365 ModHealingTakenPct −20, 30 s, category-less). Fix Embrace of Dread's mask to `0xA92`. All acolyte German texts to Ihr/Euch.
- [ ] **Step 2:** Retire Umbra spells 1300-1309 and 82 (and 64) in `RETIRED_RANK_SPELLS` so they are neutered and talents referencing removed ranks reset (existing server logic).
- [ ] **Step 3:** `py -3.14 tools/talent_trees/author_talent_trees.py` (validate) then `--summary` → acolyte shows 27 real / 0 placeholder; then `--apply`.
- [ ] **Step 4:** `py -3.14 -m pytest tools/tests/test_talent_trees.py -q` → pass.
- [ ] **Step 5:** Commit data submodules first (`data/editor`, `data/client` on their feature branch), then the parent repo (script + gitlinks): `content(acolyte): Summoner tree and real Blight/Dread talents`.

### Task 7: Trainer spells, curse category, creatures, trainer (scratchpad author script)

**Files:** scratchpad `author_acolyte_kit.py` (not committed); data: `spells.data`, `trainers.data`, `aura_stacking_categories.data`, `units.data` (+ ClientDB copies where mirrored), Localization if spell texts are localized via proto `*_loc` (they are).

**Interfaces:** Produces the trainer spells with the ids/family bits above; trainer 4 gains them at levels 3, 4, 6, 7, 9, 10 (cost scaled like neighbours: lvl3 250, lvl4 350, lvl6 650, lvl7 750, lvl9 950, lvl10 1100); stacking category 3 "Acolyte Curses"; unit entries Gloomling (Duskling display, scale ~0.55, level from caster: use `pet_level_source` 1 if the Summon path honours it, else a level range 1-12 creature with low health) and Brood Horror (large shadow creature display; pick an existing demon/shadow model, e.g. the demon minion imported 2026-11-02 if present in `model_data`). Curses Frailty (ModStatPct −10 STA + ModDamageDonePct −5, 30 s), Mire (ModDecreaseSpeed −40, 10 s, 8 s cd) carry stacking_rule 5 / category 3.

- [ ] **Step 1:** Write the script against the live catalogs (`spell_catalog_lib.load_catalogs`), cloning attribute words from the closest existing spells (curses ← Chilled-like debuffs; Drain ← Fire Barrage 150/152; Rot Field ← a Hollow Choir PersistentAreaAura spell; Gloomlings ← an existing Summon spell). Tick spells need IgnoreLineOfSight for the zone, CanTargetDead not set.
- [ ] **Step 2:** Validate each with `validate_spell_json.py`; apply; mirror ClientDB byte copies.
- [ ] **Step 3:** Inspect each spell with `inspect_spell_catalog.py --spell-id N --pretty`, trainer 4 with `--section trainers`.
- [ ] **Step 4:** Run the data tests (`py -3.14 -m pytest tools/tests -q -k "spell or talent or acolyte or mirror"`) → pass.
- [ ] **Step 5:** Commit data then parent: `content(acolyte): six new trainer spells, curses, gloomlings`.

### Task 8: Icons — one unique icon per ability, plus the uniqueness test

**Files:** `data/client/Interface/Icons/Spells/Acolyte_*.htex` (new), spell `icon` fields (talent script for talent spells, Task 7 script for trainer spells), test `tools/tests/test_acolyte_icon_uniqueness.py` (a real repo test, not one-off tooling).

- [ ] **Step 1: Failing test:** collect player-visible acolyte abilities (classmask 4 trainer spells, talent rank spells, pet ability spells 302-307 and Gloomling/Brood spells), group rank chains by baseid and talent→proc pairs (a proc spell triggered by exactly one ability belongs to it); assert no icon is shared with any other spell in the catalog outside its own group; ignore retired spells (`RETIRED_RANK_SPELLS` + neutered). Print, don't fail, collisions entirely outside the acolyte. Currently fails on Fear/Embrace of Dread (`T_Icon_Shadow_118`) and Soul Armor/Mourning Voices (`T_Icon_Shadow_45`) plus every new spell without an icon.
- [ ] **Step 2:** Generate 128×128 icons (image-generation connector, consistent painted style matching `T_Icon_Shadow_*`: dark background, violet/green/crimson school colour per the art direction) for: the 6 trainer spells, Tangled Tongue, Wail of the Lost, Empower Duskling, Brood Horror, Dark Bond, Swarm Lore, Shadow Sympathy, Gloom Tether, Shared Night, Dark Tutelage, Soul Siphon, Miasma, Creeping Rot, Venomous Backlash, Grasping Dread, Lingering Dread, Embrace of Dread, Soul Armor (24). Normalise with `normalize_item_icon.ps1`, import with `import_spell_icon.py`. Show the user a contact sheet.
- [ ] **Step 3:** Point the spells at the icons, re-apply; test passes.
- [ ] **Step 4:** Commit data then parent: `content(acolyte): unique icons for every acolyte ability`.

### Task 9: Particles

**Files:** `tools/particle_gen/recipes/acolyte_kit.py` (new recipe file in the existing recipe pipeline, following `acolyte_blight_dread.py`), output `data/client/Particles/Acolyte/*.hpar`.

Effects (dark smoke body + saturated rim; shadow violet ≈ (0.8,0.35,1.0), blight acid green, dread crimson): `FrailtyApply`, `FrailtyAura` (sickly grey-violet motes sagging off the shoulders), `DrainChannel` (hand), `DrainBeamMotes` (target→caster stream at IMPACT on tick), `MireApply`, `MireAura` (tar-black tendrils at the feet), `PlagueApply`, `PlagueAura` (green spores), `GloomlingSummon` (two small portals), `RotFieldGround` (GROUND_ACTIVE: bubbling green-black puddle with miasma, radius 8), `RotFieldTick`, `TongueAura` (violet runes circling the mouth/head), `WailBurst` (crimson shockwave + skulls), `EmpowerAura` (on pet), `BroodSummon` (large void sigil + smoke pillar), `RootTendrils`, `HealingBlight` (Lingering Dread aura), `SharedNightHeal` (on pet).

- [ ] Step 1 write recipes → Step 2 render previews with `tools/particle_gen/preview.py` and validate (`inspect_hpar.py`) → Step 3 extend `tools/tests/test_acolyte_visual_data.py` expectations if it lists particles → Step 4 commit.

### Task 10: Sounds (layered)

**Files:** `tools/sfx_gen/recipes/acolyte.py` (extend LAYERS/MIXES/SINGLES), output `data/client/Sound/Spells/Acolyte/*.wav`, generated layers under `generated/acolyte_kit/sfx/layers` (not committed).

Signature sounds (CAST_SUCCEEDED / IMPACT one-shots): Frailty (sickly exhale + cracked bell + whisper), Drain (channel loop single + tick suck), Mire (tar squelch + sub + rope creak), Wasting Plague (wet cough-like spore burst + hiss + shimmer), Call Gloomlings (chittering + portal whoosh), Rot Field (cast: acid splash + bubbling + sub), Tangled Tongue (garbled reversed whisper + knot tighten + chime), Wail (spectral wail + sub boom), Empower (growl + void swell), Brood Horror (deep roar + portal rip + sub). Each: separately generated layers, 4 takes, measured pick, mixed with `layer.py`; casting loops (Drain, Rot Field cast, Tangled Tongue cast if it has one) as SINGLES loop takes. Hand the poll-and-download loop to a subagent with a session-map file.

- [ ] Step 1 prompts in recipe (≤450 chars; `test_sfx_recipes.py` guards) → Step 2 generate + fetch + measure + mix → Step 3 audition reel `generated/acolyte_kit/Acolyte_Kit_Audition.wav`, sent to the user (they can override later; do not block on it) → Step 4 recipe tests pass → Step 5 commit.

### Task 11: Visualizations + sound catalog wiring

**Files:** `tools/acolyte_visuals/author_acolyte_visuals.py` (new visualization ids 141-160 and sound entries), `docs/acolyte_spell_visuals.md` (new section), `tools/tests/test_acolyte_visual_data.py`, talent script `visualization_id` for talent actives/procs (with `# Acolyte - <name>` comments; the script's drift check).

Kits per spell (all with CastLoop + hand particle + loop sound on CASTING where there is a cast/channel, CastRelease + sound on CAST_SUCCEEDED):
- Curses (Frailty, Mire, Tangled Tongue): IMPACT apply particle + sound; AURA_IDLE distinct per curse, TARGET scope, silent.
- Drain Essence: CHANNELING kit `CastLoop` + `DrainChannel` + loop sound; tick spell 1332 IMPACT `DrainBeamMotes` silent.
- Wasting Plague: IMPACT + AURA_IDLE spores, AURA_TICK silent tint.
- Call Gloomlings / Gloom Tether / Brood Horror: CAST_SUCCEEDED summon effects + sound.
- Rot Field: GROUND_ACTIVE `RotFieldGround` + kit light, GROUND_EXPIRED fade; tick 1337 IMPACT silent.
- Wail: CAST_SUCCEEDED `WailBurst` + sound; fear aura reuses Fear's AURA_IDLE kit (132's idle) only via its own visualization id.
- Empower Duskling: CAST_SUCCEEDED caster + IMPACT/AURA_IDLE on pet.
- Procs (1344, 1349, 1357, 1362, 1365): silent IMPACT/AURA_IDLE particles.

- [ ] Step 1 extend the script; validate → Step 2 `--apply` (writes editor + ClientDB `sounds.data`, `spell_visualizations.data`, `spells.data` visualization ids) → Step 3 `py -3.14 -m pytest tools/tests/test_acolyte_visual_data.py tools/tests/test_talent_trees.py -q` → Step 4 commit data, then parent.

### Task 12: E2E scenario and in-client check

**Files:** `e2e/scenarios/acolyte_kit.lua` (follow `e2e/README.md`; character class line `-- e2e-class: 3`, keep generated names ≤ 12 letters).

- [ ] **Step 1:** Scenario: level-12 acolyte learns all trainer spells (GM learn), targets a dummy: Frailty → aura present; Mire → replaces Frailty (only one curse); Drain Essence → caster health rises; Wasting Plague + CT both present; Rot Field ticks the dummy; Call Gloomlings → 2 summons owned by the player; with Swarm Lore talent → 3; Tangled Tongue on a casting creature → cast bar longer.
- [ ] **Step 2:** `powershell -File tools/e2e/e2e_run.ps1` (needs `$env:MMO_E2E_MYSQL_PASSWORD`; build `e2e_client login_server realm_server world_server` first) → green.
- [ ] **Step 3:** In-client visual check per [[client-visual-verification]] memory notes: open field (`worldport 0 345 1.3 600 0`, `settime 12:00`, reset after), cast each new spell, screenshot cast/impact/aura/ground; send screenshots to the user. Fix anything that does not read.
- [ ] **Step 4:** Commit scenario.

### Task 13: Gate, ship, push

- [ ] **Step 1:** `/gate` (fast tier) in the worktree → green.
- [ ] **Step 2:** Push the data submodule commits to their GitHub `master` first (merge feature branch into master via `merge-tree`/`commit-tree` if master moved; check binary `.data` conflicts by parsed comparison), confirm with `git ls-remote`.
- [ ] **Step 3:** `/ship` (merges into develop), then `git -C data/client merge-base --is-ancestor <gitlink> origin/master` for both, then push develop (`--recurse-submodules=no` after the ls-remote check). The user asked for commit + push.
- [ ] **Step 4:** Update memory (`acolyte-spell-visuals` → note the kit overhaul, ids).
