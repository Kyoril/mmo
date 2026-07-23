# NPC Goodbye Voice Line — Design

**Date:** 2026-07-23
**Status:** Approved

## Goal

NPCs can play a "goodbye" voice line when the player closes an NPC dialog — but only
when the interaction actually ends, not when one dialog of the same NPC transitions
into another (gossip menu → quest detail, gossip menu → vendor window, etc.).

Companion feature to the existing client-local hello/pissed gossip voice lines
(`UnitGossipVoice`, shipped 2026-07-11).

## Decisions (user-confirmed)

- **Scope — whole interaction chain:** the gossip/quest frame, vendor window and
  trainer window all count as "the dialog". The goodbye plays when the last open
  window of that NPC closes without a follow-up dialog opening.
- **Voice overlap — skip:** if the NPC's hello line is (likely) still playing when
  the dialog closes, the goodbye is skipped entirely (consistent with the existing
  click-throttle rule). No queueing, no overlap.
- **Approach:** fully client-local C++, extending the existing `UnitGossipVoice`
  system. No network protocol changes, no server changes.

## Data

New field on `ModelDataEntry` in **both** proto mirrors (field numbers must match —
see proto-data field-number invariant):

```proto
// SoundEntry id played when the player closes this NPC's dialog for real.
optional uint32 goodbye_sound_id = 8 [default = 0];
```

- `src/shared/proto_data/model_data.proto`
- client mirror under `src/shared/client_data/`
- Default 0 = silent (feature opt-in per model display, like hello/pissed).

**Editor:** third picker "Goodbye Sound" in ModelEditorWindow's Audio section,
reusing the shared `DrawSoundEntryCombo` helper
(`src/mmo_edit/editor_windows/sound_entry_combo.h`).

## Core logic — `UnitGossipVoice`

Two new notifications:

- `OnNpcDialogOpened(ObjectGuid guid)`
- `OnNpcDialogClosed(ObjectGuid guid)`

Behavior:

1. **On close:** remember the guid and arm a **500 ms** grace timer
   ("pending goodbye").
2. **On open with the same guid** while pending: cancel the pending goodbye.
   This covers gossip→vendor / gossip→trainer transitions (server sends
   `GossipComplete` followed by the next window's packet), gossip page reloads,
   and quest-list navigation.
3. **On open with a different guid:** do NOT cancel — NPC A still says goodbye
   while NPC B greets the player (3D audio separates them).
4. **When the timer fires:**
   - unit no longer exists (despawned/left view) → silent no-op;
   - unit dead or hello line still playing (`m_busyUntil` in the future) →
     silent no-op (skip rule);
   - otherwise resolve the unit's display model → `goodbye_sound_id`; if non-zero,
     play 3D at the unit's position via `SoundEntryPlayer` and bump `m_busyUntil`
     so an immediate re-click doesn't talk over the goodbye.
5. **`Reset()`** (called from `WorldState::OnLeave`) also clears any pending
   goodbye.
6. Playing/cancelling emits a `DLOG` line for manual verification.

Timer mechanism: whatever `UnitGossipVoice` can reach with least ceremony — a
deferred check driven from the world state tick or the client's timer queue
(implementation detail for the plan; the singleton currently has no per-frame
update).

## Hook points

**Close** (all routed to `OnNpcDialogClosed`):

- `QuestClient::CloseQuest` — capture `m_questGiverGuid` **before** clearing it.
- `VendorClient::CloseVendor` (guid check already guards the no-vendor case).
- `TrainerClient::CloseTrainer`.
- Vendor/trainer error paths that fire `VENDOR_CLOSED` / `TRAINER_CLOSED` with a
  session that never opened do NOT count as closes (no dialog was open).

**Open** (all routed to `OnNpcDialogOpened`):

- `QuestClient` packet handlers that fire `QUEST_GREETING`, `GOSSIP_SHOW`,
  `QUEST_DETAIL`, `QUEST_REQUEST_ITEMS`, `QUEST_OFFER_REWARDS`.
- `VendorClient::OnListInventory` success path (`VENDOR_SHOW`).
- `TrainerClient` trainer-list handler (`TRAINER_SHOW`).

**Lua-only close path:** the QuestFrame close button / Escape only calls
`HideUIPanel` — C++ is never notified today (and `QuestClient` state goes stale;
pre-existing). Fix: add an `OnHide` script handler on QuestFrame in
`QuestFrame.xml` calling a new Lua binding `NotifyQuestDialogClosed()` registered
by `QuestClient`, which routes into `CloseQuest()`. `CloseQuest` must be
idempotent (early-out when `m_questGiverGuid == 0`) so the server-driven close
path — which also ends up hiding the frame and re-firing `OnHide` — cannot
double-fire the goodbye. Note `CloseQuest` fires `QUEST_FINISHED`, whose Lua
handler calls `HideUIPanel` on an already-hidden frame; `OnHide` re-entry then
hits the idempotency early-out.

## Edge cases

- **Walking out of range** auto-closes vendor/trainer (`PlayerController`
  movement check) → goodbye plays 3D at the NPC, naturally attenuated.
- **NPC despawns** before the grace timer fires → no-op.
- **Accepting a quest** closes the frame via server `GossipComplete` → counts as
  a real close, goodbye plays (desired).
- **Panel switches inside QuestFrame** (greeting → detail → rewards) never hide
  the frame, but each incoming panel packet fires an "open" anyway — harmless,
  and it cancels any stray pending goodbye.

## Testing

- Client singleton with rendering/audio deps — no meaningful unit-test surface;
  verification is manual in-game using the `DLOG` play/cancel lines:
  1. open gossip → close button → goodbye plays after ~0.5 s;
  2. open gossip → "browse goods" → vendor opens, no goodbye; close vendor →
     goodbye plays;
  3. spam open/close while hello still playing → goodbye skipped;
  4. accept a quest → goodbye plays;
  5. walk away from an open vendor → goodbye plays attenuated.
- Build stays green (client + editor + servers); protos regenerate cleanly.

## Content authoring

Like hello/pissed lines: silent until the user assigns a SoundEntry (3D, VOICE
category) to `goodbye_sound_id` on model display entries in the editor.
