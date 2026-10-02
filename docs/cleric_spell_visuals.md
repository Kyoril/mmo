# Cleric spell presentation overhaul

Date: 2026-10-02. Status: data, particles, sounds and engine changes installed; in-game art and
listening acceptance still required.

## What was there before

Every active cleric spell either had no visualization or pointed at the shared "Default Spell
Visualization" (9): a `CastLoop`/`CastRelease` pair with a black tint, no particles, no sound.
Healing Aura, Protective Aura and the Faithward proc had nothing at all; Resurrection pointed at
an empty entry (21). Passive talents, proficiencies, Dodge, Attack, Open and the class-change
spell are not cleric combat presentation and are left alone.

## Art direction

Radiant stylized holy light. Every effect shares a gold/ivory core so the class reads at a glance;
damage is brighter, harder and faster, healing softer and slower with a sparing green life accent,
protection cool silver-azure. The engine has no additive blending and no bloom (see
`tools/particle_gen/recipes/warrior_common.py`), so the look comes from density at low alpha,
motion, and -- new for the cleric -- **shaped sprites**.

## New engine-side assets and tooling

- **Four new sprites** built by `tools/particle_gen/make_sprites.py`: `T_Particle_Sigil` (rune
  circle for ground glyphs), `T_Particle_Rays` (sunburst), `T_Particle_Feather`,
  `T_Particle_Flame` (teardrop tongue), each with a translucent, depth-write-off material
  instance (`Particles/Particle_{Sigil,Rays,Feather,Flame}.hmi`). The script now writes the
  `.hmi` files itself (byte-identical layout to the hand-made ones) and can rebuild a subset:
  `make_sprites.py T_Particle_Sigil`.
- **`preview.py` samples the real sprite** named by a material instance's `TPAR` chunk instead of
  a generic blob, and draws stretched sprites the right way up (tip towards travel), so shaped
  sprites can be judged offline.
- **`tools/sfx_gen`**: `postprocess.process_loop` + `fetch.py --loop` level a seamless loop
  without trimming or fading it (either breaks the seam) and crossfade the wrap point.

## Engine changes (`spell_visualization_service.cpp`, `world_state.cpp`)

1. **Effects are tracked per phase.** Effects spawned by `START_CAST`/`CASTING` kits are the cast
   phase; everything else is not (`spell_visual::IsCastPhaseEvent`). The terminating events
   (`CAST_SUCCEEDED`, `CANCEL_CAST`) now only tear down the cast phase, and aura removal only the
   non-cast phase. Before, both tore down everything for (actor, spell): a cleric healing herself
   lost her own impact effect, and recasting a buff she already carried killed its idle loop.
2. **`CAST_SUCCEEDED` is dispatched before `IMPACT`** for non-projectile spells, so the caster's
   cast tint is removed before a self-impact applies its own.
3. **`ColorTint.duration_ms` is implemented** (it was in the schema but ignored). A timed tint is
   a self-removing emissive pulse: short linear attack, linear release
   (`spell_visual::TintPulseEnvelope`), tracked under its own key
   (`spell_visual::TimedTintKey`) so it never disturbs a persistent tint of the same spell.
   Because it removes itself it also works for visualizations played by id (level up etc.).
   Before this, a tint on an impact target was never removed by anything.
4. **Aura removal only stops its own looped sound.** An actor carries one looped sound; an aura
   expiring on the cleric no longer silences the cast loop of the spell she is channelling.

Covered by `src/tests/game_client_tests/test_spell_visual_rules.cpp`.

## Installed presentation

Visualizations are authored by `tools/cleric_visuals/author_visuals.py`, which also relinks each
spell's `visualization_id` (editor + ClientDB). Sounds are catalog entries 120-134 from
`tools/cleric_visuals/author_sounds.py`. Particles live in `data/client/Particles/Cleric/`
(recipes `cleric_offense.py`, `cleric_healing.py`, `cleric_auras.py`, shared `cleric_common.py`).

| Spell (ids) | Casting | Cast succeeded | Impact / aura |
|---|---|---|---|
| Healing Light (12, 61) | `CastLoop`, holy cast loop sound, `HolyHandGlow` + gold light on `hand_r`, gold ribbon, soft body glow | `CastRelease`, `HolyRelease` burst + flash, release sound | `HealingLightImpact` (soft shaft, sigil, spiralling motes), light + 0.9 s gold tint pulse at chest, heal sound |
| Smite (13, 57) | as above with `SmiteGather` (converging sparks), white-gold | `CastRelease`, release burst | `SmiteImpact` (hard light column, sigil, ring) + `SmiteFlash` at chest, bright 4.0 light, 0.4 s white tint pulse, smite sound |
| Holy Fire (58) | `HolyFireGather` in **both** hands, amber light/ribbon, fire cast loop | `CastRelease`, release burst | `HolyFireImpact` (flame pillar), amber light + tint pulse, fire sound; **AuraIdle** `HolyFireBurn` + dim amber light for the DoT; **AuraTick** `HolyFireTick` + tick sound + tint pulse |
| Divine Vitality (20) | -- (instant) | `CastRelease`, release burst | `DivineVitalityImpact` (spiralling column, sigil, feathers), light + 1.2 s tint pulse, blessing sound |
| Renewing Light (170) | -- | `CastRelease`, release burst | `RenewingLightImpact` (light shower), light + tint pulse, sound; **AuraIdle** `RenewingLightIdle`; **AuraTick** `RenewingLightTick` + soft chime + tint pulse |
| Healing Aura (55) | -- | `CastRelease`, `HealingAuraActivate` (large sigil, ring, motes), light, aura sound | **AuraApplied** `HealingAuraApply` on each recipient; **AuraIdle** `HealingAuraIdle` (very subtle) |
| Protective Aura (65) | -- | as above in silver-azure (`ProtectiveAuraActivate`) | `ProtectiveAuraApply` / `ProtectiveAuraIdle` |
| Faithward proc (99-101) | -- | -- | **AuraApplied** `FaithwardApply` at chest + light + azure tint pulse + ward sound; **AuraIdle** `FaithwardIdle` |
| Resurrection (179, vis 21) | `CastLoop`, resurrection channel loop, hand glow + light, `ResurrectionChannel` rotating sigils at the feet, ribbon, body glow | `CastRelease`, release burst | `ResurrectionImpact` (tall shaft, sigil, feathers, sunburst), strong light, 2 s tint pulse, resurrection sound |

Lifetime rules the data obeys (enforced by `author_visuals.py` and
`tools/tests/test_cleric_visual_data.py`):

- Looping particle files (`cleric_common.LOOPING_EFFECTS`) only in `CASTING` and `AURA_IDLE`
  kits -- the two phases the service tears down itself.
- Looping kits and looped catalog sounds only in the `CASTING` loop kit.
- Ribbon trails only in the cast phase (nothing retires one spawned by another event).
- Tints outside the cast phase always carry `duration_ms`.
- No animation `duration_ms` (time-warps the clip).

## Sounds

Fifteen sounds generated with ElevenLabs `eleven_text_to_sound_v2` from the prompt table in
`tools/sfx_gen/recipes/cleric.py` (4 takes each, picked on measurements: attack, peak position,
body and tail length; loops on RMS evenness and wrap-point jump). Choir layers are wordless by
rule (tested). The three cast-bar sounds are seamless loops at -9 dBFS and catalog volume
0.55-0.60 so a 10 s channel does not mask combat; ticks are the quietest one-shots with the widest
pitch variance. Regenerate via the connector, `fetch.py [--loop]`, then
`author_sounds.py --apply`. Audition reels: `generated/cleric_visuals/audition_picks.wav`
(picks) and `audition_all_takes.wav` (all 60 takes, to override a pick).

## Known limitations

- **Bone-attached emitters inherit the bone's rotation**: spawn velocities of effects on
  `hand_r` are in hand space, only gravity is world space. The hand effects rise via gravity and
  converge via direction-free terms for that reason.
- **No per-emitter offset**: chest-height effects use the `spine_03` socket or a drag-parked
  upward launch; columns from above use the cone shape (spawns along `y = height * t`).
- **`AURA_APPLIED` replays when a unit comes into view** with the aura already on it (client
  sees a "new" aura). The apply bursts are short, so this reads as a small sparkle.
- **`SpellKit.mesh_name` is still not implemented** by the service; no cleric kit uses it.
- **Animations**: only `CastLoop`/`CastRelease` exist on the human rigs, and the orc rigs have
  neither -- particles, lights and sounds still play there.
- No live in-game visual or listening acceptance yet.

## In-game acceptance route

Human cleric, target dummy plus a party member. Cast Healing Light on self and on the ally (check
the self-heal keeps its impact and glow), Smite and Holy Fire on an enemy (watch all three DoT
ticks and the burn ending with the aura), Renewing Light (idle motes + ticks for 12 s), Divine
Vitality, both auras (recipients sparkle once; idles stay subtle with five people on screen),
recast an aura while it is active (idle must survive), cancel a Healing Light by moving (hand glow,
light, ribbon and loop sound must stop), and a full 10 s Resurrection on a dead party member.

## Parallel mage branch

`feature/mage-spell-visuals` was authored at the same time and touches the same surfaces: sound
ids 82-102, new visualization ids allocated from 42, `spell_visualization_service.cpp` (its own
fix for aura effects being torn down at cast end, an `auraBound` flag) and
`tools/sfx_gen/postprocess.py` (its own loop postprocessing). The cleric set therefore uses sound
ids 120-134 and visualizations from 70. Whichever branch merges second must:

1. take develop's `.data` files in both submodules and re-run
   `tools/cleric_visuals/author_sounds.py --apply` and `author_visuals.py --apply` (ids are
   matched by name, so nothing duplicates);
2. reconcile the service: the cleric `castPhase` split is a superset of the mage `auraBound`
   flag (it also protects self-impact effects), so keep one mechanism, not both;
3. keep one loop postprocess in `postprocess.py`.
