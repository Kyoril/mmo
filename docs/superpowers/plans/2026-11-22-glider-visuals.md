# Glider Visuals Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make gliding visible and audible: a Da Vinci style back-mounted glider, a "Glide" character pose, a few particle effects and sounds, for the own player and remote players.

**Architecture:** Client only. The replicated `movement_flags::Gliding` flag (already in `MovementInfo`) drives everything: the animation controller picks a new `ANIM_SLOT_GLIDE`, and a new `GliderVisual` owned by `GameUnitC` attaches the glider mesh to the back bone, unfolds it, runs wing-tip emitters and sounds. Assets are generated with one-off scratchpad scripts (Blender 5.2, Python 3.14, ElevenLabs MCP); only assets and C++ are committed.

**Tech Stack:** C++17 (game_client, mmo_client), Catch2 (`game_client_tests`), protobuf (`animation_profiles.proto` in proto_data AND client_data), Blender 5.2 (`C:/Program Files/Blender Foundation/Blender 5.2`), `tools/particle_gen`, `tools/sfx_gen`.

**Spec:** `docs/superpowers/specs/2026-11-22-glider-visuals-design.md`

## Global Constraints

- Worktree `D:/mmo-traversal`, branch `feature/glider-visuals` (from develop 7a7bc2fb). Data submodules get their own `feature/glider-visuals` branches.
- **No protocol change.** Do not touch opcodes, packet payloads or `ProtocolVersion`.
- No new or changed shader code generation (no material compiler bump); use existing materials/HMIs.
- One-off scripts in the scratchpad only, never in `tools/`, never committed.
- Engine has no additive blending and no bloom: particle materials are the `Particles/Particle_{Beam,Glow,Ring,Star}.hmi` instances, peak alpha 0.2–0.4.
- Scene objects (entities, emitters, scene nodes) created and destroyed on the main thread only.
- Code style per CLAUDE.md (Allman, tabs, `m_camelCase`, PascalCase methods, copyright header, Doxygen on public members).
- Localization: no new UI strings expected; if any appear, add them to every locale (deDE uses Ihr/Euch/Euer).
- Asset paths (exact): mesh `Models/Gear/Glider/Glider.hmsh`; particles `Particles/GliderTrail.hpar`, `Particles/GliderOpen.hpar`, `Particles/Updraft.hpar`; animation clip name `Glide` on all four player skeletons.

## Review Focus

1. Unit dies, teleports, logs out or lands while gliding → glider and emitters vanish, wind loop stops, nothing leaks (test: `GliderFoldState` closes; `GliderVisual` teardown in `GameUnitC` destructor and `OnDisplayIdChanged`).
2. Display/model change while gliding (shapeshift, `.morph`, outfit change re-creating the entity) → glider re-attached to the new entity, not left on a dead one.
3. Model without a `Glide` clip or without a back bone (creatures, other races) → falls back to `Falling`, glider not attached, no log spam.
4. Rapid open/close/open within the 0.25 s fold → fold reverses smoothly from its current fraction, one open sound per open.
5. Many remote gliders in view → no per-frame asset file opens (load the mesh and particle params once, cache).

---

### Task 1: Glide animation slot

**Files:**
- Modify: `src/shared/proto_data/animation_profiles.proto` and `src/shared/client_data/animation_profiles.proto` (identical: `ANIM_SLOT_GLIDE = 18;`)
- Modify: `src/shared/game_client/animation/animation_context.h` (add `bool gliding{false};`)
- Modify: `src/shared/game_client/animation/animation_binding.cpp` (builtin `{ ANIM_SLOT_GLIDE, { "Glide", "Falling" } }`, looping)
- Modify: `src/shared/game_client/animation/animation_controller.cpp` (airborne branch)
- Modify: `src/shared/game_client/game_unit_c.cpp:669` area (fill `ctx.gliding = IsGliding()`)
- Modify: `src/mmo_edit/editor_windows/animation_profile_editor_window.cpp` (slot name "Glide", static_assert → `ANIM_SLOT_GLIDE + 1`)
- Create: `src/shared/game_client/animation/airborne_slot.h/.cpp`
- Test: `src/tests/game_client_tests/test_airborne_slot.cpp`

**Interfaces:**
- Produces: `proto_client::AnimationSlot SelectAirborneSlot(const AnimationContext& ctx, bool jumpStartPlaying);` (namespace `mmo`) — the controller's airborne branch calls it.

- [ ] **Step 1: Write the failing test** `[animation][glide]`:
  - gliding, verticalVelocity -2 → `ANIM_SLOT_GLIDE`
  - gliding, verticalVelocity +3 (updraft), jumpStartPlaying true → `ANIM_SLOT_GLIDE` (glide beats jump start)
  - not gliding, vv +3, jumpStartPlaying true → `ANIM_SLOT_JUMP_START`
  - not gliding, vv -2 → `ANIM_SLOT_FALL`
- [ ] **Step 2:** Build `game_client_tests`, run `bin/Debug/game_client_tests.exe "[glide]"` → fails to compile/link.
- [ ] **Step 3:** Implement `SelectAirborneSlot`, use it in `AnimationController`; add proto slot, context field, builtin binding (Glide falls back to Falling candidate), editor slot name.
- [ ] **Step 4:** Tests pass; build `mmo_client` and `mmo_edit` (proto regen in both projects).
- [ ] **Step 5: Commit** `client: Glide animation slot driven by the Gliding flag`

### Task 2: GliderVisual (mesh attach, fold, wing-tip emitters, open burst)

**Files:**
- Create: `src/shared/game_client/glider_fold_state.h/.cpp` (pure logic)
- Create: `src/shared/game_client/glider_visual.h/.cpp` (scene objects)
- Modify: `src/shared/game_client/game_unit_c.h/.cpp` (own a `std::unique_ptr<GliderVisual>`; update in `Update` after animation; destroy in destructor, in `OnDisplayIdChanged` before the entity is replaced, and when dead)
- Test: `src/tests/game_client_tests/test_glider_fold_state.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace mmo {
  namespace glider_event { enum Type { None, Opened, Closed }; }
  class GliderFoldState {
  public:
      static constexpr float FoldSeconds = 0.25f;
      /// Advances toward open (gliding) or closed; returns the edge crossed this call.
      glider_event::Type Update(bool gliding, float deltaTime);
      float GetUnfold() const;      // 0 = folded/hidden, 1 = fully open
      bool IsVisible() const;       // GetUnfold() > 0
  };
  class GliderVisual final : public NonCopyable {
  public:
      GliderVisual(Scene& scene, Entity& entity, SceneNode& entityOffsetNode, uint64 guid);
      ~GliderVisual(); // detaches and destroys everything it created
      /// @return the edge from GliderFoldState (Task 4 hangs sounds on it).
      glider_event::Type Update(bool gliding, float deltaTime);
  };
  }
  ```
- Bone: attach to `spine_03` (all four player rigs have it; `Back_M` exists too — pick whichever renders right in Task 3's check and note it in a code comment). Units whose skeleton lacks the bone get no glider.
- Fold = scale of the glider entity's attachment on the span axis from 0.05 to 1 (ease-out), hidden at 0.
- Emitters: `GliderTrail.hpar` on two child nodes at the wing tips (offsets from Task 3), playing while unfold == 1; `GliderOpen.hpar` one-shot on `Opened`. Cache the deserialized `ParticleSystemParameters` and mesh path statically (Review Focus 5). Missing asset files → skip silently.

- [ ] **Step 1: Failing tests** `[glider]`: starts folded and invisible; `Update(true, 0.1)` returns `Opened` once, unfold ≈ 0.4; reaches 1 after 0.25 s total and stays; `Update(false, …)` returns `Closed` only when reaching 0; open→close at 0.6 → reverses from 0.6 (no jump), and a re-open before 0 does NOT return a second `Opened` (Review Focus 4: one event per visible cycle); large dt clamps to [0,1].
- [ ] **Step 2:** Run → fail.
- [ ] **Step 3:** Implement `GliderFoldState`, then `GliderVisual` following the `GameUnitC::UpdateSparkEmitter` and `item_display_applier.cpp` attach patterns (unique names with guid + counter).
- [ ] **Step 4:** Tests pass; `mmo_client` builds. Smoke: client runs with no glider assets present, no errors in Client.log when gliding.
- [ ] **Step 5: Commit** `client: glider visual follows the Gliding flag`

### Task 3: Glider model and textures

**Files (data/client):**
- Create: `Models/Gear/Glider/Glider.hmsh`, `Glider_Wood.htex`, `Glider_Linen.htex`, `Glider_Wood.hmi`, `Glider_Linen.hmi` (instances of an existing lit opaque master; linen double-sided or modelled with both faces — see memory mesh-winding-cull-convention)
- Scratchpad: `glider_model.py` (Blender), textures script

**Interfaces:**
- Produces: the mesh path above; wing-tip local positions (two `Vector3`) and the attach offset/rotation written into `glider_visual.cpp` constants.

Look (from spec): a wooden spine along the back, two wings of wooden ribs with stretched linen, ~3 m span, ropes from the wing tips to hand grips, leather harness straps; Da Vinci flying machine. Convention: origin at the harness/back centre, span along ±X, forward +Z (creatures/characters face +Z), up +Y. Under ~3000 triangles.

- [ ] **Step 1:** Model + texture by script in Blender, render 3 views (front, side, ¾ from behind) with a 1.8 m human dummy; send renders to the user (SendUserFile) and iterate until it reads as Da Vinci wood + linen.
- [ ] **Step 2:** Export FBX, `mmo_edit --import-fbx <folder> <fbx> --tangents` into `Models/Gear/Glider/`, assign HMIs with `--set-submesh-material`.
- [ ] **Step 3:** Fill the attach and wing-tip constants; verify in the client or via a temporary `--capture-view` hook (revert it) that the glider sits on the back of HumanMale and OrcFemale.
- [ ] **Step 4: Commit** data/client (`Glider mesh and materials`) and the constants in the main repo.

### Task 4: Glide pose on the four player skeletons

**Files (data/client):**
- Modify: `Models/Character/Human/Male/HumanMale.skel`, `Human/Female/HumanFemale_V2.skel`, `Orc/Male/OrcMale.skel`, `Orc/Female/OrcFemale.skel` (add clip `Glide`, loop, ~2 s)
- Scratchpad: hmsh+skel reader for Blender, skel ANIM-chunk writer (format: `src/shared/scene_graph/skeleton_serializer.cpp` — MVER/MAIN/BONE/HIER/ANIM/NTFY chunks)

Pose (from spec): body tilted forward towards horizontal, arms spread holding the grips at the wing ribs, legs trailing slightly, calm sway (small roll/pitch on pelvis/spine, slight leg drift). Bone names are the shared UE-style set (`pelvis`, `spine_01..03`, `clavicle_l/r`, `upperarm_*`, `lowerarm_*`, `hand_*`, `thigh_*`, `calf_*`, `foot_*`).

- [ ] **Step 1:** Load each body (hmsh + skel bind pose) into Blender, pose the Glide keys on HumanMale with the glider from Task 3 attached; render; send to the user; iterate.
- [ ] **Step 2:** Retarget the same local rotation offsets to the other three rigs, render each with the glider; hands must meet the grips (adjust per rig).
- [ ] **Step 3:** Write clip `Glide` into the four `.skel` files; re-read each file with the engine (`mmo_edit` model editor load or a unit test loading the skeleton) to prove it deserializes and every other clip is byte-identical.
- [ ] **Step 4:** In the client test field: own player and a second character see the pose (remote).
- [ ] **Step 5: Commit** data/client `Glide pose for human and orc players`.

### Task 5: Particles and updraft columns

**Files:**
- Create (data/client): `Particles/GliderTrail.hpar`, `Particles/GliderOpen.hpar`, `Particles/Updraft.hpar` via `tools/particle_gen` (recipes in the scratchpad, `preview.py` contact sheets checked)
- Modify: `src/mmo_client/area_trigger_manager.h/.cpp` — on map load create one `Updraft.hpar` emitter per `UpdraftVolume` (centre at the trigger box/sphere bottom, sized to its footprint), destroy on map change/shutdown.

Looks: trail = thin pale wind threads streaming backwards from the tips (Beam/Glow HMI); open = short puff of dust and lint around the back (~0.5 s); updraft = slow rising faint streaks and a few leaves/motes, readable from 30 m without hiding the ground.

- [ ] **Step 1:** Author + preview the three effects; send preview sheets to the user.
- [ ] **Step 2:** Updraft emitters in `AreaTriggerManager`; verify no emitter leak across a map change (teleport map 2 → 0 → 2, emitter count back to the number of updraft triggers).
- [ ] **Step 3:** In-game check on map 2 (triggers 11–13).
- [ ] **Step 4: Commit** data + code `Glider and updraft particles`.

### Task 6: Sounds

**Files:**
- Create (data/client): `Sound/Glider/*.ogg` (or the sfx pipeline's format) for open, close, wind loop, updraft whoosh
- Modify: SoundEntry data, editor (`data/editor`) AND ClientDB (`data/client`) copies, next free ids
- Modify: `glider_visual.cpp` (play open/close at the unit on the fold edges; positioned wind loop via `SoundEntryPlayer::PlayEntryPositionedLoop` with priority > 1, position updated each frame, stopped on close/teardown); `area_trigger_manager` or player controller (updraft whoosh when the own player enters an updraft while gliding)

Generate with the ElevenLabs MCP following memories generated-sfx-pipeline and layered-ability-sounds ("dry foley" prompts; layered mix for open = wood creak + linen snap). Wind loop must loop seamlessly.

- [ ] **Step 1:** Generate, trim, loudness-match with existing ability sounds; send the files to the user.
- [ ] **Step 2:** SoundEntries (3D, category SFX; wind loop looped) in both data copies.
- [ ] **Step 3:** Wire playback; Review Focus 1: loop stops on death/teleport/logout (teardown path).
- [ ] **Step 4: Commit** data + code `Glider sounds`.

### Task 7: Verification and gate

- [ ] **Step 1:** Run the local stack + client in the test field (map 2), GM-learn 1436/1437, glide with two characters (own + remote view), capture screenshots of open, glide, updraft and send them to the user.
- [ ] **Step 2:** `tools/gate/verify.ps1 -Tier fast` (MMO_GATE_PYTHON = Python 3.14) green.
- [ ] **Step 3:** Update the spec status and memory; ready for /ship on the user's word.
