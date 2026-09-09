# Auto Attack Sounds — Design

Date: 2026-09-09
Status: Approved, ready for planning

## Problem

Auto attacks (melee white swings) have no audio at all. Spells route sound through
`SpellKit.sound_ids`, footsteps route through `surface_types.footstep_sound`, NPC gossip
routes through `ModelDataEntry.gossip_sound_id` — but the single most frequent action in
combat is silent. Melee combat has no physical weight and no feedback for landing,
missing, critting, or being hit.

## Goals

1. Auto attacks play sounds, authored as `SoundEntry` ids in the existing sound catalog.
2. The sound varies with the **weapon type** swung (sword vs. mace vs. unarmed vs. claw).
3. The impact sound varies with **what is being hit** (plate vs. cloth vs. flesh vs. stone).
4. Distinct sounds for miss/dodge, parry, block, and crit.
5. Per-model **combat voice**: an attacker effort grunt ("NROAAR!") and a victim pain
   react, distinct per race and gender (which comes free — each race+gender is its own
   display model) and per creature (a boar squeals, a skeleton rattles).
6. All of it authored in the editor by designers, with no code change per weapon or creature.

## Non-goals

- Ranged auto attacks. Not implemented server-side (only an inventory branch exists). When
  they land, a bow/gun subclass filled with the same fields works with no code change.
- Spell and ability sounds — already covered by `SpellKit.sound_ids`.
- Death sounds, sheathe/unsheathe sounds, impact decals or particles. These are future
  consumers of the same surface-type ids.
- The sound content itself and its assignment to subclasses and models. Content authoring
  is a separate pass after the code lands, as with the footstep system.

## Existing plumbing this builds on

| Piece | Where | What it gives us |
|---|---|---|
| `SoundEntry` catalog | `src/shared/proto_data/sounds.proto` | shuffle-bag multi-file playback, 3D attenuation, pitch/volume randomization, categories |
| `SoundEntryPlayer` | `src/shared/game_client/sound_entry_player.h` | `PlayEntry(id)` / `PlayEntry(id, position)`, injected into `GameUnitC` statically |
| `ItemSubclassEntry` | `src/shared/proto_data/item_subclasses.proto` | already drives per-weapon-subclass attack/ready/off-hand animations |
| `surface_types` | `src/shared/proto_data/surface_types.proto` | `{id, name, footstep_sound}` material vocabulary, client-exported, has an editor |
| `ModelDataEntry` | `src/shared/proto_data/model_data.proto` | per-display-model data, already carries gossip/goodbye sound ids |
| `AttackerStateUpdate` | `src/mmo_client/game_states/world_state.cpp:3981` | attacker, victim, hitInfo (crit/miss/LeftSwing), victimState (dodge/parry/block), damage |
| SwingHit notify | `GameUnitC::QueueSwingHitCallback` | the exact frame the weapon visually connects; damage numbers already sync to it |
| `DrawSoundEntryCombo` | `src/mmo_edit/editor_windows/sound_entry_combo.h:21` | searchable sound picker with Preview |

Nothing here needs a server change. Every input is data the client already holds, so there
is **no new packet, no opcode change, and no `ProtocolVersion` bump**.

## Data model

### New shared proto: `combat_sounds.proto`

Mirrored in `src/shared/client_data/combat_sounds.proto` with identical field numbers.
Follows the `loot_entry.proto` precedent of a shared message file imported by other schemas,
so both `item_subclasses.proto` and `model_data.proto` can use the same messages without a
name collision in package `mmo.proto`.

```proto
// One impact sound for a specific target material.
// target_material 0 = fallback used for any material.
message ImpactSound {
    optional uint32 target_material = 1;   // SurfaceType id
    optional uint32 sound = 2;             // SoundEntry id
}

// Per-model combat audio: natural-weapon sounds, body material, and combat voice.
message CombatSounds {
    // Natural weapon (creature claw/bite, unarmed player). Used when the swinging hand
    // holds no weapon, and as the fallback when the weapon subclass has no sound.
    optional uint32 swing_sound = 1;
    repeated ImpactSound impact_sounds = 2;
    optional uint32 crit_layer_sound = 3;
    optional uint32 miss_sound = 4;

    // What this body sounds like when struck, unless armor overrides it.
    optional uint32 hit_material = 5;       // SurfaceType id

    // Attacker effort voice.
    optional uint32 attack_voice_sound = 6;
    optional uint32 attack_voice_chance = 7 [default = 0];      // percent, per swing
    optional uint32 attack_crit_voice_sound = 8;
    optional uint32 attack_crit_voice_chance = 9 [default = 100];

    // Victim pain react.
    optional uint32 hit_voice_sound = 10;
    optional uint32 hit_voice_chance = 11 [default = 100];
    optional uint32 crit_hit_voice_sound = 12;
    optional uint32 crit_hit_voice_chance = 13 [default = 100];

    // Minimum gap between two voice lines from the same unit, in milliseconds.
    // 0 = derive from the played clip's length.
    optional uint32 voice_min_interval_ms = 14;
}
```

### `ItemSubclassEntry` additions

New fields, placed next to the existing `attackanimation` / `offhand_attackanimation` /
`readyanimation` fields they parallel:

```proto
optional uint32 swing_sound = 8;          // whoosh at swing start (weapon subclasses)
repeated ImpactSound impact_sounds = 9;   // per victim material; target_material 0 = default
optional uint32 crit_layer_sound = 10;    // extra layer on top of the impact sound
optional uint32 miss_sound = 11;          // whiff on miss / dodge
optional uint32 parry_sound = 12;         // this weapon parrying an incoming swing
optional uint32 block_sound = 13;         // this shield blocking (shield subclasses)
optional uint32 hit_material = 14;        // armor subclasses: Plate / Mail / Leather / Cloth
```

One entry carries both roles without conflict: weapon subclasses fill the swing/impact/crit/
miss/parry fields, armor subclasses fill `hit_material`, shield subclasses fill `block_sound`.

### `ModelDataEntry` addition

```proto
optional CombatSounds combat_sounds = 10;
```

### `surface_types`

**No schema change.** The table is already `{id, name, footstep_sound}` and is already
client-exported with its own editor window. It gains content rows for combat materials
(Flesh, Bone, Plate, Chainmail, Leather, Cloth, Stone, Wood, Metal) alongside the existing
floor surfaces. Accepted trade-off: those rows also appear in the terrain and material
surface-type pickers. The upside is a single material vocabulary — a stone golem struck by a
sword and a boot landing on a stone floor draw from the same concept.

### Why single sound ids, not lists

Every slot holds one `SoundEntry` id rather than a repeated list. Variation already lives
inside `SoundEntry`, which holds multiple files and picks them shuffle-bag style (each plays
once in random order, never twice in a row). `Sword_Impact_Plate` is one entry with four
WAVs, not four ids in a list.

## Runtime

### Placement

New `src/shared/game_client/combat_sound_player.{h,cpp}`, split in two:

- A **pure resolver**: data in (proto project + actor/victim context + event), sound ids out.
  No audio, no scene, no globals — directly unit-testable.
- A **thin player**: takes resolved ids, rolls chances, enforces the voice throttle, and
  hands ids to the existing `SoundEntryPlayer`.

Context structs stay small:

```cpp
struct CombatSoundActor
{
    uint32 weaponSubclass;   // 0 = unarmed / creature
    uint32 displayId;
};
```

### Feeding the resolver

`GamePlayerC::NotifyItemData` (`src/shared/game_client/game_player_c.cpp:124`) already
resolves the main-hand and off-hand item subclass in order to pick attack animations, then
discards the subclass id. It starts caching it, and additionally caches the chest slot's
`hit_material`. Creatures resolve nothing and fall through to their model. Slot clearing
resets the cached values the same way `SetWeaponAttackAnimations({})` already does.

### Victim material resolution

1. Victim is a player with a chest item whose subclass has `hit_material != 0` → that.
2. Else the victim model's `combat_sounds.hit_material`.
3. Else 0.

### Impact sound resolution

1. Attacker's weapon subclass `impact_sounds`, row matching the victim material.
2. Same subclass, row with `target_material == 0`.
3. Attacker model's `combat_sounds.impact_sounds` (matching row, then default row).
4. Nothing — silence.

Steps 3–4 mean an unarmed player and a boar both get their natural-weapon sounds through the
ordinary path, with no special case in the caller.

### Event mapping

All driven by the single `AttackerStateUpdate` packet, which already carries every input.

Two clarifications on the terms used below. **"Attacker weapon subclass"** means the subclass
of the hand that actually swung: the off-hand item's subclass when `hit_info::LeftSwing` is
set, the main-hand item's otherwise. **"Damage lands"** means the swing connected — anything
that is not a miss, dodge, or immunity — including a fully absorbed hit, which reports zero
damage but should still sound like metal meeting armor.

| Event | Sound | Owned by |
|---|---|---|
| Swing starts | `swing_sound` | attacker weapon subclass → attacker model |
| Damage lands | impact matrix | attacker weapon/model × victim material |
| Crit | impact **plus** `crit_layer_sound` | attacker weapon subclass → model |
| Miss / dodge | `miss_sound` | attacker weapon subclass → model |
| Parry | `parry_sound` | **victim's** main-hand subclass → attacker's `miss_sound` |
| Block, full (0 damage) | `block_sound` replaces the impact | **victim's** off-hand subclass |
| Block, partial | `block_sound` layers on top of the impact | **victim's** off-hand subclass |
| Immune | nothing | — |
| Swing starts | attacker effort voice, chance-rolled (crit uses the crit chance) | attacker model |
| Damage lands | victim pain react, chance-rolled (crit variant on crit) | victim model |

### Timing

- **Swing sound and attacker voice** fire at swing start, where the attack animation is
  started.
- **Impact, crit layer, miss, parry, block, and the victim's pain react** are queued on the
  attacker's **SwingHit animation notify** via the existing `QueueSwingHitCallback`, so audio
  lands on the frame the weapon visually connects. If no attack animation started (the
  off-hand suppression case), they fire immediately — the same fallback the damage numbers
  already use.

### One scope correction to existing code

The deferred callback in `world_state.cpp:4013` is gated on `isActivePlayerInvolved`, so it
only runs for the local player's own fights. Sound must not inherit that gate — two NPCs
brawling nearby have to be audible. The callback becomes: always play sound, show floating
text and fire Lua events only when the local player is involved. Range culling is free —
`SoundEntryPlayer` already refuses to start a non-looped 3D sound beyond the entry's
`max_distance`.

Positions: swing and attacker voice at the attacker's position, impact and pain react at the
victim's position.

### Voice throttle

One gate per unit shared by both effort and pain voices, so a unit never talks over itself.
After a line plays, the next is blocked until `max(voice_min_interval_ms, clipLength + 250ms)`
— the same pattern `CastErrorVoice` uses. Chance rolls use a client-side RNG; all of this runs
on the main thread (packet handler and animation notify), so no thread-local RNG concerns
apply.

## Editor

All three windows use the existing `DrawSoundEntryCombo` helper (searchable, with Preview).

- **Item Subclass Editor** (`item_subclass_editor_window.cpp`, 182 lines) gains a *Combat
  Sounds* section under the animation lists: sound combos for swing / crit layer / miss /
  parry / block, a surface-type combo for `hit_material`, and an add/remove table for the
  impact matrix (material combo + sound combo per row, the `0` row labelled "Any / default").
- **Model Editor** (`model_editor_window.cpp`) gains a *Combat Sounds* collapsing header with
  three sub-groups: Natural Weapon, Body Material, Voice (each voice sound paired with its
  chance slider). Every sub-group gets its own `PushID` scope, and no widget shares a label
  with its enclosing collapsing header — identical ImGui ids make the widget swallow clicks.
- **Surface Type Editor** — untouched.

## Back-compat

Every new field is `optional` with a 0 or empty default. Existing data files load unchanged
and behave exactly as today: silent. No data migration, no proto version bump, no file format
version bump, no protocol version bump. The client-side export list needs no change either —
`item_subclasses`, `model_data`, `surface_types` and `sounds` are all already exported.

## Testing

Unit tests in `src/tests/game_client_tests/` against the pure resolver:

- Victim material fallback chain: armor → model → 0.
- Weapon → model fallback for unarmed players and for creatures.
- Impact matrix: exact material row wins over the default row; default row used when no match.
- Crit layering: impact and crit layer both returned.
- Parry resolves from the victim's main-hand subclass, not the attacker's.
- Block resolves from the victim's off-hand subclass; replaces impact at 0 damage, layers on
  top when damage still landed.
- Chance 0 never fires and chance 100 always fires, with an injected RNG.
- Voice throttle blocks a second line inside the interval and allows one after it.

No E2E coverage: the harness client is headless with null audio, so nothing is observable
there. Final verification is manual in the real client.

## Risks

- **Noise.** Several units fighting nearby could stack sounds. Mitigated by 3D attenuation
  and `max_distance` culling per entry, and by the chance/throttle controls on voice. If it
  still feels crowded after content authoring, the fix is data, not code.
- **Shared surface-type table.** Combat materials appear in terrain pickers. Cosmetic, and
  the shared vocabulary is deliberate.
- **Silent until authored.** Like footsteps, the system does nothing visible until content
  exists. Authoring is planned as a distinct follow-up pass.
