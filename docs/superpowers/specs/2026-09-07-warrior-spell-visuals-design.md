# Warrior Spell Visuals — Rebuild

**Date:** 2026-09-07
**Branch:** `feature/warrior-spell-visuals`
**Status:** Approved design

## Problem

The warrior visualization pass shipped in `data/client` commit `e66cbc6` and `data/editor`
commit `b56c18e` does not look or sound right in game. Four independent defects:

### 1. Every particle effect fails validation

`tools/warrior_visuals/build_assets.py` hand-packed `.hpar` bytes with `struct.pack`
instead of going through `tools/particle_gen/hpar.py`, so the effects never met
`inspect_hpar.py --check`. All ten fail:

| Material | Effects | Defect |
|---|---|---|
| `Particles/Additive.hmat` | SteelImpact, HeavyImpact, CleaveBurst, RallyBurst, GuardBurst | Typed `Unlit`, so `Material::Apply` selects `BlendMode::Opaque`. These render as hard occluding rectangles. |
| `Particles/IceSmoke.hmat` | BloodImpact, ChargeDust, ShockwaveDust, RageBurst, DreadBurst | Depth-write is on, so translucent particles cull each other. |

Confirmed in game: blocky solid particles everywhere, and the Shockwave reading as a dark
dust cloud.

### 2. The effects are ten copies of one effect

Every `.hpar` is a single box-shaped burst emitter differing only in colour, count and
size. A shockwave has the same silhouette as a blood spurt. By contrast `LevelUp.hpar`,
authored through the real pipeline, layers five emitters across core / glow / ground flare /
burst sparks / rising motes.

No recipes were checked in, so none of it can be retuned.

### 3. Animations are speed-ramped

`SpellVisualizationService::ApplyAnimationToActor` computes
`playRate = clipLength / duration_ms`. The kit durations were guessed rather than measured:

| Ability | Clip | Real length | Kit `duration_ms` | Resulting rate |
|---|---|---|---|---|
| Rend, Crippling Strike | `Attack_1H_01` | 0.97 s | 550 ms | 1.76× |
| Execute | `Attack_1H_02` | 0.97 s | 650 ms | 1.49× |
| Cleave | `Attack_1H_02` | 0.97 s | 600 ms | 1.62× |
| Skullbash | `UnarmedAttack01` | 0.70 s | 400 ms | 1.75× |
| Shield Slam | `UnarmedAttack01` | 0.70 s | 500 ms | 1.40× |
| Battlecry, Demoralizing Shout | `CastRelease` | 0.63 s ♂ / 0.53 s ♀ | 650 ms | 0.97× ♂ / 0.82× ♀ |

Everything is time-warped, and the shouts warp differently per gender.

### 4. Sounds are two incompatible populations

- **Real recordings** (48 kHz stereo, RMS ≈ 0.09): Strike, Rend, Cleave, Charge.
- **numpy-synthesised placeholders** (44.1 kHz mono, RMS ≈ 0.035, ~9 dB quieter):
  Battlecry, Bloodrush, DemoralizingShout, Execute, LastStand, Provoke, ShieldSlam,
  Shockwave, Skullbash.

Half the kit whispers while the other half punches.

Separately, kit sounds play by raw file path through `IAudio::PlaySound3D`, bypassing the
`SoundEntry` catalog. Spell 8 `Strike` is cast by every warrior *and* eight creature types
(Skeleton Warrior, Barrow Skeleton, Roadside Marauder and others), so the same waveform
repeats endlessly with no pitch or file variation.

## Non-problems (verified, do not "fix")

- **Spell coverage is complete.** All 14 active warrior abilities have kits. The 23
  uncovered warrior spells are passive talents and correctly have none.
- **Bone names are correct.** `spine_03`, `hand_r`, `hand_l`, `foot_l`, `head` all exist on
  both the Human and Orc skeletons.
- **The Orc animation gap is latent, not live.** `Attack_1H_01/02` and `CastRelease` exist
  only on the Human rigs (`OrcMale` has 8 clips, `OrcFemale` has 1), but `Orc` and
  `Undead Human` are both `disabled=True` in `races.data`, so Human is the only playable
  race. The one spell creatures cast is `Strike`, whose kit has no animation. **No C++
  animation fallback is needed**; adding one would be dead code.
- **Kit contents never cross the wire.** Only visualization *ids* are sent
  (`PlaySpellVisual`). Schema changes below therefore need **no `ProtocolVersion` bump**;
  `tools/protocol_version_check.py` confirms this at gate time.

## Design

### A. `tools/sfx_gen/` — generated-audio pipeline

A sibling to `tools/particle_gen`, reusable beyond warrior. Generation itself runs through
the ElevenLabs MCP connector (`eleven_text_to_sound_v2`, ~12 credits ≈ $0.003 per take),
which a script cannot call; the checked-in tooling covers the deterministic half.

| File | Purpose |
|---|---|
| `postprocess.py` | trim silence, fade, peak-normalise, emit 44.1 kHz mono 16-bit PCM WAV |
| `fetch.py` | signed URL list → download → postprocess |
| `recipes/warrior.py` | the prompt table: prompt, duration, target peak, pitch range per sound |
| `README.md` | the loop, and why postprocessing is mandatory |

**Postprocessing is not optional.** A verification take came back at −24 dBFS peak with
1.06 s of trailing near-silence on a 1.2 s clip. Raw output is inaudible in game and wastes
memory. `postprocess.py` must:

1. Trim leading and trailing silence at a −45 dBFS threshold, keeping ≤ 10 ms of pre-roll
   so the transient is not clipped.
2. Apply a 2 ms fade-in and a 15 ms fade-out to prevent clicks.
3. Peak-normalise to the per-sound target (default −3 dBFS; impacts louder than shouts).
4. Resample to 44.1 kHz, downmix to mono, write 16-bit PCM.

**Audition gate:** every sound is auditioned by the user as a single concatenated WAV
before anything is written into `data/client`. Generated-audio quality varies per take;
4 variations are generated per sound and the best is selected.

### B. Kit sounds routed through the `SoundEntry` catalog

`SoundEntryPlayer::PlayEntry(soundId, position)` already implements everything needed —
shuffle-bag file selection (`NextShuffledFileIndex`), pitch randomisation from
`pitch_min`/`pitch_max`, per-entry volume, 3D min/max distance, and category routing. The
work is wiring, not new audio machinery.

**Schema** — add to `SpellKit` in **both** protos at the same field number (13 is free in
each; fields 1–12 are currently identical):

```proto
// Sound catalog entries to play for this kit. Preferred over the legacy `sounds`
// file-path list: entries carry category, 3D distances, volume and pitch variance.
repeated uint32 sound_ids = 13;
```

- `src/shared/proto_data/spell_visualizations.proto`
- `src/shared/client_data/spell_visualizations.proto`

**Service** — `spell_visualization_service.{h,cpp}`:

- `Initialize(project, audio)` gains a `SoundEntryPlayer*` parameter.
  `world_state.cpp:333` already holds `m_soundEntryPlayer` as a member.
- Kit playback prefers `sound_ids`, routing each through `PlayEntry(id, actorPosition)`.
- The legacy `sounds` string path is left **untouched** — 25 existing non-warrior
  visualizations still use it.
- **A kit sets one or the other, never both.** When `sound_ids` is non-empty the legacy
  `sounds` list on that kit is ignored, so a half-migrated kit cannot double-trigger. Every
  warrior kit clears `sounds` as it gains `sound_ids`.
- Looped kits keep the existing fade-in/fade-out and per-actor single-loop bookkeeping;
  only the channel's origin changes.

**Editor** — `spell_visualization_editor_window.cpp` gains sound-entry id fields with name
lookup, alongside the existing file-path list.

**Data** — new `SoundEntry` rows starting at id 21 (19 entries exist, max id 20). Shaped
like the existing `Footstep - *` entries, which already use this pattern:

- `category = SOUND_EFFECTS`, `is_3d = true`
- `min_distance = 5`, `max_distance = 30` — matching the values currently hardcoded at the
  `PlaySound3D` call site, so audible range does not change
- `pitch_min`/`pitch_max`: 0.96–1.04 on cooldowns, 0.92–1.08 on the high-frequency Strike
- **`Strike` carries three files in one entry** so the shuffle bag gives genuine variation
  rather than pitch wobble on one waveform

**Sound inventory** — 13 entries, 15 generated files (Strike contributes three):

| Entry | Character | Notes |
|---|---|---|
| Strike | sword landing on flesh and mail | 3 files; widest pitch range; must stay short and unfatiguing |
| Rend | tearing slash, wet | |
| Execute | heavy committed finishing blow | loudest of the impacts |
| Skullbash | blunt cracking impact | short |
| ShieldSlam | steel shield bash, dry metal slam | |
| Cleave | wide sweeping arc through multiple bodies | |
| Charge | armoured sprint into a collision | longest impact |
| Shockwave | ground slam with an expanding low rumble | |
| Battlecry | horn-like rallying swell | no voice |
| Bloodrush | surging rage, low whoosh and pulse | no voice |
| Provoke | sharp aggressive pressure burst | no voice |
| DemoralizingShout | dark oppressive pressure wave | no voice |
| LastStand | steel bracing, defensive resolve | no voice |

Shouts are deliberately voiceless: a baked-in human voice would clash across race and gender
combinations. Per-race vocalisations remain future work for the existing voice-line system.

### C. Particles rebuilt through `tools/particle_gen`

One checked-in recipe per effect in `tools/particle_gen/recipes/`. Materials are restricted
to the translucent, depth-write-off `Particle_{Beam,Glow,Ring,Star}.hmi` instances.
`Particles/Additive.hmat` is never used.

Art direction: **grounded physicality with a warm accent**. Strikes are dust, grit, sparks,
blood and steel. The rage and shout cooldowns carry a restrained warm-crimson accent so the
big buttons read as empowered at a glance. The engine has no additive blending and no
bloom, so volumetric layers stay at peak alpha 0.2–0.4 and density carries brightness.

| Effect | Used by | Design |
|---|---|---|
| `SteelImpact` | Strike, Skullbash, Cleave impact | Deliberately cheapest — the most-fired effect in the game. Two emitters: tight stretched spark burst + one brief soft flash. |
| `HeavyImpact` | Execute, Shield Slam | Heavier spark fan + low-alpha dust puff + ground ring. |
| `BloodImpact` | Rend, Crippling Strike | Dark crimson droplet spray arcing down under gravity, plus fine mist. No smoke. |
| `CleaveBurst` | Cleave (caster) | Horizontal crescent sweep fanning to the character's right. |
| `ChargeDust` | Charge | Ground puffs plus a backward-drifting dust trail. |
| `ShockwaveDust` | Shockwave | Expanding ground ring (`HorizontalBillboard`, radial velocity) + low dust wall + upward grit. The signature effect. |
| `RallyBurst` | Battlecry | Warm accent: upward flare + ground pulse + rising motes. |
| `RageBurst` | Bloodrush | Warm accent: tight crimson body burst + embers. |
| `DreadBurst` | Provoke, Demoralizing Shout | Dark desaturated expanding pressure ring — deliberate cold contrast to the warm rally pair. |
| `GuardBurst` | Last Stand | Steel-blue defensive dome pulse. |

Each effect must pass `inspect_hpar.py --check --one-shot` and be reviewed as a
`preview.py --figure` contact sheet before it is considered done.

### D. Animations — data only

- Remove `duration_ms` from every kit animation so clips play at native rate.
  `ApplyAnimationToActor` only rescales when the field is present.
- Re-time the caster-scoped `delay_ms` values against measured clip lengths so staggered
  kits land on the intended beat. Target-scoped `IMPACT` kits are keyed off the server hit
  packet, not the caster animation, so their small delays are cosmetic.

**Accepted limitation.** `HumanMale` carries 21 clips whose only action entries are
`Attack_1H_01`, `Attack_1H_02`, `UnarmedAttack01`, `CastRelease` and `CastLoop`. There is no
shield-bash and no ground-slam clip. Shield Slam and Shockwave get best-effort mappings
within that set, and the chosen mapping is called out for review. A real fix requires new
animation art and is out of scope here.

## Verification

- `inspect_hpar.py --check --one-shot` clean on all ten effects.
- `preview.py --figure` contact sheet reviewed for each effect against the pitfalls
  checklist: scale versus the 1.8-unit silhouette, intended silhouette, anchored base,
  fades rather than cuts, terminates at all.
- Audition WAV delivered to the user before any audio lands in `data/client`.
- Data written to **both** `data/editor/data/` and `data/client/ClientDB/`.
- `python tools/protocol_version_check.py` confirms no version bump is required.
- Client builds; `/gate` green before any merge to `develop`.

## Risks

| Risk | Handling |
|---|---|
| Generated SFX quality varies per take | 4 variations per sound, best selected, user auditions before commit |
| Final `delay_ms` beat cannot be judged offline | Values derived from measured clip lengths; flagged for an in-game pass |
| No shield-bash / ground-slam animation exists | Documented as an art gap; best-effort mapping called out |
| `Additive.hmat` remains broken for other effects | Out of scope. `ResetTalents.hpar` and `FrostImpact.hpar` still use it; noted, not fixed here |

---

## Branch status at merge (2026-09-08)

Every signal this work is responsible for is green:

| Check | Result |
|---|---|
| `protocol` / `protocol_tests` | pass — no `ProtocolVersion` bump required |
| `build` (client + editor) | pass |
| unit tests | 29 suites pass |
| `tools/tests` | 72 pass (43 before this branch) |
| all 10 warrior particle effects | **0 problems** from `inspect_hpar.py --check --one-shot` |
| editor vs client datasets | content-identical for `spell_visualizations.data` and `sounds.data` |

**The gate is nonetheless RED on its `e2e` step, for a pre-existing defect this branch did
not introduce.** Merged deliberately, with that understood.

### The failure

```
Assertion failed: m_players.find(player->GetCharacterGuid()) == m_players.end()
  src/world_server/player_manager.cpp:10
  reached via RealmConnector::OnPlayerCharacterJoin  (realm_connector.cpp:774)
```

The realm sends a character-join for a character already registered on the world node. The
world server aborts; the realm then reports "No world node available" and every remaining
scenario fails.

The E2E suite passed **24/24** earlier on this branch (at `87e56584`), so the race is
intermittent rather than constant.

### Why it is not this branch

- `git diff --name-only develop...HEAD` touches **zero** server-tier files.
- `src/world_server/player_manager.cpp` was last modified **2022-01-09**.
- The only server-visible change here is one added optional proto field
  (`SpellKit.sound_ids`), which cannot affect session lifecycle.
- The older crash reports on the dev machine (2026-08-13) are a *different* bug — an access
  violation in `SingleCastState::OnCastFinished`.

Not proven: that it reproduces on `develop`. That test (checkout + rebuild + E2E) was
offered and deliberately skipped.

### The cascade, and how to avoid chasing it

When the world server aborts, teardown leaves `login_server` and `realm_server` orphaned and
`pids.json` is already gone — so `tools/e2e/e2e_down.ps1` cannot clean them. Those orphans
then hold the e2e ports and the *next* run fails at startup with
`Could not bind on tcp port 16279/16280`, producing a completely different failure
signature. Five consecutive runs failed five different ways for this reason.

Before any gate run, clear processes holding **13724, 16279, 16280, 18090, 18092, 18129**.
Never touch anything on the dev ports 3724, 6279, 6280, 8090, 8092, 8130.

### Diagnostic traps hit while investigating

- `tools/gate/last_report.json` and `e2e/runtime/logs/summary.json` are written **with a
  BOM**; `json.load()` fails unless given `encoding='utf-8-sig'`.
- The per-scenario `.jsonl` transcripts are **not rewritten when the client dies early**, so
  they go stale and appear to show a passing run that never happened. Check mtimes. The live
  signals are `<scenario>.out.log` and `e2e/runtime/<tier>/logs/*.log`.
- `<tier>_server.out.err.log` carries the assertion text; `<tier>_server.out.log` carries the
  run log. Both are empty in the ordinary case.

### Deferred follow-ups

- `_fast_ring` lives in `warrior_abilities.py` but qualifies for use in `warrior_impacts.py`
  too: "Heavy Ring" has a 4.67× size ratio, above its own documented ≥4× threshold. Moving it
  to `warrior_common.py` and applying it there changes `.hpar` bytes and needs a fresh visual
  pass.
- `AbilityStrike03.wav` / `AbilityStrike04.wav` are unreferenced but retained at the user's
  explicit request.
- Shield Slam (`UnarmedAttack01`) and Shockwave (`CastRelease`) are best-effort animation
  mappings: the `HumanMale` rig has 21 clips and neither a shield-bash nor a ground-slam.
  Fixing properly needs new animation art.
- `preview.py` stamps every particle as a soft disc rather than its real sprite, so a ring's
  hollow-ness is unverifiable offline and wants an in-game look.
