# Three-Phase Pose Emotes (Start / Loop / End) — Design

**Date:** 2026-07-13
**Status:** Approved

## Problem

Pose emotes (sit, sleep, kneel) currently loop a single animation clip. Emotes like
/sleep need three phases: a transition-in clip (lay down), a loop clip (sleeping), and a
transition-out clip (stand up).

## Decisions

- **Explicit proto fields only** — no clip-name-convention auto-detection.
- **Instant cancel on movement** — the end animation only plays on a voluntary stand-up
  (stand while stationary). Movement/jump blends straight into the run animation.
- **Scope: Pose + PoseVariant emotes** — OneShot and Mood emotes stay single-clip.

## Data Model

Two new optional fields on `EmoteEntry` in `src/shared/proto_data/emotes.proto` and the
client mirror `src/shared/client_data/emotes.proto` (identical tags):

```proto
// Clip played once when entering the pose (e.g. "SleepStart" = lay down).
optional string animationstart = 14;
// Clip played once on a voluntary stand-up (e.g. "SleepEnd" = stand up).
optional string animationend = 15;
```

Proto2 optional fields are backward compatible; existing `.data` files load unchanged.
The Emote Editor's Animation section gets two matching text fields.

## Pose Entry Resolution (client refactor)

`GameUnitC::ResolvePoseAnimation` currently resolves a loop *clip* (variant field's
emote, else convention name "Sit"/"Sleep"/"Kneel"). Because start/end clips live on
emote entries, resolution changes to resolve an *entry*:

1. The selected pose-variant entry (SitPoseEmote / SleepPoseEmote field), if set and its
   loop clip exists on the mesh.
2. Else the Pose-type catalog entry whose `standstate` matches the unit's stand state.
3. Else the convention clip name as last-resort fallback — loop only, no transitions.

The resolved emote id is stored (`m_activePoseEmoteId`) so the exit animation always
comes from the entry that was active while posing, even if selection fields change.

## Client Sequencing (GameUnitC — purely cosmetic)

No server, protocol, or DB changes; stand-state replication is untouched.

- **Enter:** set the loop clip as the locked-loop pose animation (as today) and
  simultaneously play `animationstart` through the existing one-shot slot. The one-shot
  machinery already masks regular states while playing and blends back into the locked
  loop when it ends.
- **Voluntary exit** (stand state → Stand while stationary): release the pose lock, then
  play `animationend` as a one-shot, which blends back to idle.
- **Movement cancel:** if the unit is moving when the pose breaks, skip the end clip.
  If a pose-exit one-shot is still playing when movement starts (remote-player packet
  ordering), fast-forward it to its end so the run animation is never masked. A tracked
  `m_poseTransitionState` pointer distinguishes pose transitions from other one-shots.
- **No transition:** units spawning / mesh-swapping already in a pose go straight to the
  loop (`RefreshPoseAnimation(withTransition=false)` from the OnMeshChanged path).
  `/pose` variant cycling while already posing swaps the loop instantly.

## Content

Author `animationstart`/`animationend` on baseline Sit (4), Sleep (5), Kneel (6) entries
in `data/editor/data/emotes.data` ("SitStart"/"SitEnd", "SleepStart"/"SleepEnd",
"KneelStart"/"KneelEnd"). Clips are not yet authored on character models; everything
no-ops gracefully until they exist, matching the rest of the emote system.

## Testing

Animation phasing is client-visual; the headless e2e client cannot assert it. Existing
`emote_smoke.lua` field assertions stay green. Verification: client build plus editor
round-trip of the new fields.
