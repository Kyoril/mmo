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
- **Voice overlap — skip (same NPC only):** if the SAME NPC's own line is (likely)
  still playing when the goodbye would fire, the goodbye is skipped entirely — the
  NPC never talks over themselves. No queueing. A *different* NPC's line does not
  suppress the goodbye (decision 3 below); the service tracks which unit owns the
  current busy window (`m_busyGuid`). The click gate for hello lines remains
  global (pre-existing behavior, unchanged).
- **Approach:** fully client-local C++, extending the existing `UnitGossipVoice`
  system. No network protocol changes, no server changes.

## Data

New field on `ModelDataEntry` in **both** proto mirrors (field numbers must match —
see proto-data field-number invariant):

```proto
// SoundEntry id played when the player closes this NPC's dialog for real.
optional uint32 goodbye_sound_id = 9 [default = 0];
```

(Field 8 is already taken by `animation_profile` — corrected from the first
draft of this spec.)

- `src/shared/proto_data/model_data.proto`
- client mirror under `src/shared/client_data/`
- Default 0 = silent (feature opt-in per model display, like hello/pissed).

**Editor:** third picker "Goodbye Sound" in ModelEditorWindow's Audio section,
reusing the shared `DrawSoundEntryCombo` helper
(`src/mmo_edit/editor_windows/sound_entry_combo.h`).

## Core logic — `UnitGossipVoice`

Two new notifications, each tagged with the dialog *source* (an enum:
`Quest`, `Vendor`, `Trainer`, `Bank`):

- `OnNpcDialogOpened(npc_dialog_source::Type source, ObjectGuid guid)`
- `OnNpcDialogClosed(npc_dialog_source::Type source, ObjectGuid guid)`

**Why sources (correction to the first draft):** for Vendor/Trainer gossip
actions the world server does NOT send `GossipComplete` — the quest frame is
only displaced client-side *after* the vendor/trainer window shows, so the
quest-dialog "close" can arrive *after* the vendor "open". A pure
pending-cancel would mis-fire there. Instead the service tracks which guid
each source currently has open (`m_openDialogGuids[source]`), and a close only
arms the goodbye when the guid is no longer open in ANY source slot. The Bank
source is included because the Banker gossip action opens the bank window the
same way.

Behavior:

1. **On open:** if the source slot currently holds a DIFFERENT non-zero guid,
   that old npc's dialog was displaced without an explicit close (e.g. the
   gossip frame stays visible but now shows another npc) — treat it as an
   implicit close first (same still-open-elsewhere check, arms the old npc's
   goodbye). Then record `m_openDialogGuids[source] = guid`; if a goodbye is
   pending for this (new) guid, cancel it (covers gossip page reloads and
   close-then-open transitions like `GossipComplete` + follow-up window).
2. **On close:** clear the source slot; if the guid is still open in another
   slot, do nothing (the conversation continues in another window). Otherwise
   arm a **500 ms** grace timer ("pending goodbye").
3. **Open with a different guid:** does NOT cancel a pending goodbye — NPC A
   still says goodbye while NPC B greets the player (3D audio separates them).
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
- `BankClient::CloseBank`.
- Vendor/trainer error paths that fire `VENDOR_CLOSED` / `TRAINER_CLOSED` with a
  session that never opened do NOT count as closes (no dialog was open).

**Open** (all routed to `OnNpcDialogOpened`, at the synchronous point where the
packet handler stores the npc guid — NOT where the async UI event fires):

- `QuestClient` handlers `OnGossipMenu`, `OnQuestGiverQuestList`,
  `OnQuestGiverQuestDetails`, `OnQuestGiverOfferReward`,
  `OnQuestGiverRequestItems` (not `OnQuestGiverQuestComplete` — that is a
  turn-in toast which immediately calls `CloseQuest`).
- `VendorClient::OnListInventory` success path (where `m_vendorGuid` is set).
- `TrainerClient::OnTrainerList` (where `m_trainerGuid` is set).
- `BankClient::OnShowBank` (where `m_bankerGuid` is set).

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

`VendorFrame.xml` and `BankFrame.xml` already follow this exact pattern
(`OnHide` → `CloseVendor()` / `CloseBank()`); **`TrainerFrame.xml` does not**
and gets the same `OnHide` → `CloseTrainer()` handler added (binding already
exists).

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
