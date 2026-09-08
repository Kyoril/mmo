# Warrior spell presentation audit and implementation

Date: 2026-09-06, sounds and cleanup updated 2026-09-07. Status: first playable presentation
pass installed; in-game art/audio acceptance still required.

## What was missing

The audit followed Warrior class 1, class grants, warrior trainers, warrior talent tabs/ranks,
class masks and triggered-spell dependencies in the live editor catalogs. It found 14 active
combat abilities / 15 spell entries including both Strike ranks. Of those entries, **11 had no
visualization reference, all 15 had no particle kits, and all 15 had no usable sound kits**.
Three entries had animation kits: Cleave reused Wounding Strike, while Provoke and
Demoralizing Shout reused generic caster visualization 9. Bloodrush used visualization 4,
containing an empty sound path and a red aura tint.

Cleave's old target animation was also attached to CAST_SUCCEEDED, but the client dispatches
that event with an empty target list, so that target kit could not run. Auto-attack already
has a separate animation controller; an absent visualization on Attack 153 is not evidence
that auto-attacks lack animation.

Passive talents, armor/weapon proficiencies, Dodge and Block should remain silent when learned
or restored on login. Open, Open Door and class-change utility spells are not warrior combat
presentation and were left alone. Empty Deep Wound visualization entries 5 and 6 are not
referenced by this audited warrior ability set; their existence alone does not make them
active warrior skills.

## Installed presentation

The class identity is physical: steel sparks, brief earthy dust, subdued dark-red aggression
and muted gold/steel rally cues. No fireballs, lingering spell fog, constant lights, or large
idle auras. All particle files are finite bursts (24-79 particles per invocation, spawned via
`bursts`, not continuous emission), with zero looping sound and every emitter's own `loop`
flag off. Individual particles live up to about 1.10 seconds (the longest is the "Rally
Motes" emitter in Battlecry's burst); most effects finish well under a second. Effects are
feedback, not radius telegraphs.

Every ability has a dedicated new visualization. All existing shared visualization entries
remain intact. Both `data/editor/data/` and `data/client/ClientDB/` were updated using their
respective protobuf schemas. `tools/warrior_visuals/author_visuals.py` writes only
`spell_visualizations.data` in both trees -- it never opens or writes `spells.data` -- so each
spell's own `visualization_id` has to already point at the id the script authors for it; the
script checks that link on every run and refuses to proceed if it has drifted.

No kit sets `duration_ms`. `ApplyAnimationToActor` turns a set `duration_ms` into
`playRate = clipLength / duration`, so a guessed value time-warps the clip -- that is exactly
what happened in an earlier pass, which ran every warrior animation 1.4x to 1.8x too fast.
This branch removed every guessed `duration_ms` value and `tools/tests/test_warrior_visual_data.py`
keeps new ones from creeping back in.

| Ability (spell IDs -> visualization) | Cast | Impact / aura | Sound |
|---|---|---|---|
| Strike (8, 71 -> 26) | No caster kit; the existing next-swing attack animation remains responsible for the swing. | Steel spark burst at target `spine_03`. | Shuffles between `Strike01/02/03.wav` (catalog entry "Warrior - Strike", id 21). |
| Battlecry (9 -> 27) | `CastRelease`, plus a gold rally burst on the caster 150 ms after cast starts. | Aura recipients each get a short rally burst (no sound). | `Battlecry.wav` (id 29) on the caster burst. |
| Rend (19 -> 28) | `Attack_1H_01`. | Dark-red target burst at `spine_03`. | `Rend.wav` (id 22). |
| Charge (48 -> 29) | Departure dust burst on successful cast; no anim override -- movement is left to the existing movement controller. | -- | `Charge.wav` (id 27). |
| Execute (50 -> 30) | `Attack_1H_02`. | Larger steel/amber impact at `spine_03`. | `Execute.wav` (id 23). |
| Bloodrush (62 -> 31) | Crimson vortex burst on the caster; no overlay animation, so it never interrupts attacking. | -- | `Bloodrush.wav` (id 30). |
| Crippling Strike (69 -> 32) | `Attack_1H_01`. | Same blood-impact particle as Rend, attached to target `foot_l`. | Reuses Rend's catalog entry (id 22); no dedicated entry. |
| Skullbash (70 -> 33) | `UnarmedAttack01`. | Steel impact at target `head`. | `Skullbash.wav` (id 24). |
| Shockwave (122 -> 34) | `CastRelease`, plus a caster-centered dust-and-ring burst 260 ms after cast starts (CastRelease is 0.63 s; that lands about 40% into the clip, near its release beat). Works without an explicit unit target. | -- | `Shockwave.wav` (id 28). |
| Shield Slam (142 -> 35) | `UnarmedAttack01`. | Heavy impact at target `spine_03`. | `ShieldSlam.wav` (id 25). |
| Last Stand (205 -> 36) | Steel-blue activation burst on the caster; no attack-disrupting gesture. | -- | `LastStand.wav` (id 33). |
| Cleave (209 -> 37) | `Attack_1H_02`, plus a warm burst from `hand_r` 300 ms after cast starts (Attack_1H_02 is 0.97 s; contact lands roughly a third in). | Reuses Strike's steel-impact particle at target `spine_03`, with no separate sound. | `Cleave.wav` (id 26) on the caster burst only. |
| Provoke (216 -> 38) | `CastRelease`, plus a dark dread burst on the caster 150 ms after cast starts. | Target gets a matching dread burst (no sound). | `Provoke.wav` (id 31). |
| Demoralizing Shout (217 -> 39) | `CastRelease`, plus a dark dread burst on the caster 150 ms after cast starts. | Aura recipients each get a matching dread burst (no sound). | `DemoralizingShout.wav` (id 32). |

None of these delays are tuned against a verified animation contact frame -- they are an
initial feel adjustment and must be played to confirm. The implementation does not change
when damage actually occurs; the confirmed-hit-list limitation in the "Remaining work"
section below still applies.

Humanoid impact sockets `spine_03`, `head`, and `foot_l` were checked against all four player
skeletons. The existing service falls back to the actor root when a creature lacks a socket;
that is functional but may place an impact near its feet. Cleave uses the verified `hand_r`
socket.

## Installed sounds

Fifteen sounds are generated by `tools/sfx_gen/recipes/warrior.py` (its `SOUNDS` table has 15
entries: three Strike variations plus one each for the other twelve abilities), written to
`data/client/Sound/Spells/Warrior/`. `tools/warrior_visuals/author_sounds.py` turns those
files into **thirteen** sound catalog entries (ids 21-33) -- fewer than the file count because
entry 21, "Warrior - Strike", bundles `Strike01/02/03.wav` as a shuffle bag so the most
frequent ability sound in the game does not repeat identically on every swing.

The sounds are generated with the ElevenLabs `eleven_text_to_sound_v2` model via the prompt
table in `tools/sfx_gen/recipes/warrior.py`, not recorded or sampled from any existing source.
Every prompt names its layers explicitly (a metallic/impact layer, an arcane-energy layer, a
sub-bass layer, and a tail) and asks for a designed tail -- an earlier attempt that asked for
"dry close-mic, no reverb, single isolated" foley came back as thin, flat metallic clanks, and
`tools/tests/test_sfx_recipes.py::test_no_prompt_reintroduces_the_foley_clause` fails the
build if that phrasing returns. Shout-type sounds are deliberately voiceless; a baked-in human
voice would clash across race/gender combinations, and per-race vocalisations belong in the
existing voice-line system instead, not here.

Format: mono PCM16, 44.1 kHz. `tools/sfx_gen/postprocess.py` trims silence, applies short
anti-click fades, and peak-normalises every clip to a per-sound target between -2.0 and
-4.5 dBFS (impacts sit hotter than sustained cooldown cues), which lands around 0.60-0.79
linear peak amplitude on the installed files. Real installed durations range from under a
second (Skullbash, ~0.9 s) up to about 3 seconds (Shockwave, ~3.0 s, the longest). All are
one-shots.
Crippling Strike deliberately shares Rend's catalog entry; Strike's three ranks are the
shuffle bag described above. No sound plays on every bleed tick or aura recipient.

To listen to the set, build an audition reel with `tools/sfx_gen/fetch.py --audition
<out.wav> <wav> [<wav> ...]` (e.g. `generated/warrior_visuals/audition.wav`); see
`tools/sfx_gen/README.md` for the generate/fetch/audition loop. `AbilityStrike03.wav` and
`AbilityStrike04.wav` under the same directory are earlier, now-unreferenced files kept on
disk intentionally; they are not part of the 15-sound generated set above and nothing in the
catalog points at them.

## Remaining engine and animation work

1. **Animation coverage is the largest visual asset gap.** Human male/female both have
   Attack_1H_01, Attack_1H_02, UnarmedAttack01 and CastRelease. Only human female has
   EmoteRoar. Orc male has UnarmedAttack01 but no armed attacks or CastRelease; orc female has
   only Idle. The service skips missing animations with a warning, while particles/audio still
   work. Import/retarget the missing base combat set for the orcs, plus shout, stomp, left-arm
   shield bash, low sweep and 2H attacks across all playable rigs. Do not relabel a punch as a
   finished shield-slam animation.
2. **Select animation by weapon/rig.** SpellKit currently holds one literal animation name.
   The 1H attack fallback is therefore not an appropriate finished 2H presentation. A semantic
   animation action with per-rig/weapon resolution would avoid copying kits per equipment
   type. Current animation overlays and movement blending need live QA.
3. **Charge arrival timing.** For instant spells the client dispatches IMPACT directly from
   SpellGo, not from physical arrival. No fake arrival burst was added. Bind a dedicated
   arrival event to charge movement completion before adding collision audio or target dust.
4. **Confirmed hits and multiple targets.** Instant IMPACT uses the explicit unit target, not
   the complete resolved hit list. Cleave cannot reliably show its secondary impact;
   Shockwave's caster burst and Demoralizing Shout's aura events avoid depending on that list.
   It also means presentation is not authoritative evidence of a successful hit versus
   miss/avoidance. Add confirmed hit-result target data and contact-notify timing before
   claiming exact synchronization.
5. **Target attachment fallback.** Standard humanoid sockets were verified; animals may use
   different names. A normalized impact-height/socket resolver would make torso and leg
   effects reliable on every creature.
6. **No sustained visual aura state in this pass.** Brief activation feedback plus the
   existing aura UI avoids introducing persistent tint cleanup/order problems. Bloodrush's
   former full-body red tint is replaced by a short pulse. Last Stand has no persistent shield
   dome; Rend has no repeating blood cloud.
7. **Voice identity still needs support.** This branch added the sound-entry/random-selection
   reference that was previously missing: Strike's catalog entry shuffles between three files
   instead of playing one repeated sound, and `sound_ids` (catalog entries), not raw file
   paths, is what every warrior kit uses. Race/sex-aware voice dispatch for the shout-type
   abilities is still not wired up; the current non-vocal layers are intentionally
   character-neutral until that lands.
8. **Final texture art.** Current sparks/smoke reuse existing materials
   (`Particles/Particle_Beam.hmi`, `Particle_Glow.hmi`, `Particle_Ring.hmi`,
   `Particle_Star.hmi`). Purpose-made slash, dust-ring and droplet textures, with a brief
   mesh/ribbon arc for Cleave, will be more recognizable than these generic bursts.

Two supporting runtime changes are included in `spell_visualization_service.cpp`: finished
one-shot emitters and their owned nodes are retired during Update, including when audio is
unavailable; one-shot audio starts at authored volume instead of losing its transient to a
fade-in (looping audio still fades in over about 0.33 seconds, unaffected). Listen to existing
non-warrior caster spells during QA as well, since the fade change is not warrior-specific.

## Validation and handoff

- All particle kits reference `sound_ids` (catalog entries), never raw sound paths, and never
  both `sounds` and `sound_ids` on the same kit -- enforced by both the authoring scripts and
  `tools/tests/test_warrior_visual_data.py`.
- No kit sets `duration_ms`, and no kit or emitter loops -- a one-shot particle system with an
  emitter-level loop never reports finished and leaks its owning scene node; both the kit-level
  and emitter-level loop flags are checked separately (the emitter-level check runs
  `tools/particle_gen/inspect_hpar.py`'s `check()` against every referenced `.hpar`, which also
  catches burst spawn demand exceeding `max_particles`, a colour curve that does not fade to
  alpha 0, and a material that is missing, non-translucent, or has depth-write on).
- `tools/warrior_visuals/author_visuals.py` writes only `spell_visualizations.data`; it never
  touches `spells.data`. It validates that every spell's existing `visualization_id` still
  points at the visualization id being authored for it before writing anything, parses and
  validates both the editor and client datasets before writing either, and takes a timestamped
  backup under `generated/warrior_visuals/backup_*` first. `tools/warrior_visuals/author_sounds.py`
  follows the same discipline for `sounds.data`.
- Sound and particle paths resolve; sounds are finite mono PCM WAVs; particle files are
  bounded non-looping bursts. Every particle emitter uses one of the four known-translucent
  particle materials -- `Particles/Additive.hmat` is typed Unlit and the engine renders it
  opaque, which turned every particle in an earlier pass into a hard occluding rectangle.
- Debug `game_client` and full `mmo_client` builds succeed, along with `mmo_edit`.
- No live in-game visual or listening acceptance was performed as part of this cleanup pass.
  The generated audition reel is provided for listening review.

Reopen the editor before saving catalogs so an already-open editor does not write stale data
over the changes. Restart the newly built client to reload ClientDB. The editor and client
assets are separate Git submodules; retain their modifications as well as the root
scripts/documentation/runtime changes when committing.

For iteration, regenerate particle effects with
`python tools/particle_gen/recipes/warrior_abilities.py` and
`python tools/particle_gen/recipes/warrior_impacts.py` (shared tuning helpers live in
`tools/particle_gen/recipes/warrior_common.py`, which writes nothing itself). Regenerate audio
through the ElevenLabs MCP connector plus `tools/sfx_gen/fetch.py` postprocessing, per
`tools/sfx_gen/README.md`, using `tools/sfx_gen/recipes/warrior.py` as the prompt table. Then
run `python tools/warrior_visuals/author_visuals.py` and
`python tools/warrior_visuals/author_sounds.py` to validate; `--apply` on either installs the
result with a timestamped backup. Generated drafts, backups and the audition reel are local,
gitignored outputs under `generated/warrior_visuals/`; the durable authoring sources are in
`tools/particle_gen/recipes/`, `tools/sfx_gen/recipes/` and `tools/warrior_visuals/`, and
installed assets live in the data submodules.

### Focused in-game acceptance route

Use a human warrior first with 1H/shield. Compare both Strike ranks, then Rend, Crippling
Strike, Execute, Skullbash and Shield Slam. Confirm one effect per cast/contact and no stuck
movement/overlay after rapidly chaining attacks. Test Charge at minimum and maximum range;
only its departure should emit in this pass. Test Cleave on two enemies and
Shockwave/Demoralizing Shout on several enemies without a selected target. Cast Battlecry with
party members, then Bloodrush and Last Stand during combat. Repeat on both human models and
available orc models, with a 2H weapon as an explicit fallback check. Finally spam impacts on
a surviving target, change targets/despawn them and inspect emitter/node counts; check
existing mage one-shot sounds for appropriate levels after the fade correction.
