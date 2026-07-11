# NPC Gossip Voice Lines (Client-Local) — Design

**Date:** 2026-07-11
**Status:** Approved

## Goal

When the player clicks a friendly NPC (left click to select, or right click to interact),
the client plays a short "gossip" voice line ("Hello", "How can I help?") positioned at the
NPC's location. After repeatedly clicking the same unit, funny "pissed" voice lines play
instead ("Can't you bother somebody else?"). The whole system is client-local: no server,
network protocol, or Lua changes.

## Data Model

Two new optional fields on `ModelDataEntry`, added with **identical field numbers** to both
schema mirrors (`src/shared/proto_data/model_data.proto` and
`src/shared/client_data/model_data.proto` — ClientDB field-number invariant):

```proto
// SoundEntry id played when the player clicks/interacts with a friendly unit
// using this display model. 0 = no gossip sound (default).
optional uint32 gossip_sound_id = 6 [default = 0];

// SoundEntry id played instead of gossip_sound_id after the player has pestered
// the same unit repeatedly. 0 = keep playing the normal gossip sound (default).
optional uint32 gossip_pissed_sound_id = 7 [default = 0];
```

Rationale for hanging the data off the model display id: it is already replicated to the
client (`object_fields::DisplayId`), and voice lines naturally follow the model (a human
male innkeeper model sounds the same wherever it is used).

Each field references one `SoundEntry`. Variety comes from the existing multi-file support
on `SoundEntry` (a random file is picked per playback). 3D positioning, min/max attenuation
distance, pitch variation, and the `VOICE` category are existing SoundEntry features — the
audio layer needs no changes. Gossip entries are expected to be authored as 3D + VOICE.

## Client System: `UnitGossipVoice`

New files `src/mmo_client/systems/unit_gossip_voice.h/.cpp`, modeled on `CastErrorVoice`:

- Singleton (`UnitGossipVoice::Get()`), wired up wherever `CastErrorVoice` is initialized.
- `Initialize(SoundEntryPlayer* player, const proto_client::ModelDataManager* models)` —
  nullptrs disable the system.
- `OnUnitClicked(GameUnitC& unit)` — the single entry point.
- `Reset()` — clears state (called on world leave, mirroring CastErrorVoice lifecycle).

### State (single slot)

| Field | Purpose |
|---|---|
| `m_pesteredGuid` | Guid of the unit currently being clicked. |
| `m_clickCount` | Counted clicks on that unit. |
| `m_lastClickTime` | GameTime of the last *counted* click (for the 30s decay). |
| `m_busyUntil` | GameTime until which the current voice line is (likely) still playing, derived from `SoundEntryPlayer::GetEntryLength`. |

### Per-click logic (in `OnUnitClicked`)

1. Resolve the unit's `object_fields::DisplayId` to a `ModelDataEntry`; bail out silently if
   no entry or `gossip_sound_id == 0`.
2. **Busy gate:** if `now < m_busyUntil`, ignore the click entirely — no sound, no counter
   increment (user decision).
3. **Reset rule:** if the clicked guid differs from `m_pesteredGuid`, or more than 30
   seconds passed since `m_lastClickTime`, reset `m_clickCount` to 0 and re-track the guid.
   (Selecting a different unit therefore resets annoyance implicitly.)
4. Increment `m_clickCount`. Clicks 1–3 play `gossip_sound_id`; clicks 4+ play
   `gossip_pissed_sound_id`, falling back to `gossip_sound_id` when the pissed id is 0
   (user decision: pissed from the 4th click).
5. Play via `SoundEntryPlayer::PlayEntry(soundId, unitPosition)` using the unit's world
   position. The existing 3D out-of-range gate keeps far-away clicks silent. Update
   `m_busyUntil = now + GetEntryLength(soundId)` only when playback actually started.

## Trigger Point

`PlayerController::OnMouseUp` (the `m_mouseMoved <= 16` click branch): when the hovered
object is a **friendly, alive, non-player unit**, call
`UnitGossipVoice::Get().OnUnitClicked(unit)` exactly once per click, for both left and
right button. Right-click interaction (`InteractWithObject`) flows through the same
mouse-up path, so there is no double-fire. Player characters are excluded even though they
have display ids, so shared race models never gossip on players.

## Editor

`ModelEditorWindow` (`src/mmo_edit/editor_windows/model_editor_window.cpp`) gets two
sound-entry id inputs ("Gossip Sound", "Gossip Pissed Sound"). `ZoneEditorWindow` already
has a filtered sound-entry combo (`DrawSoundEntryCombo`) as a private helper; extract it to
a small shared editor helper so both windows use the same picker.

## Error Handling

- Unknown/missing SoundEntry id: `SoundEntryPlayer` already logs a warning; the system
  stays silent (same behavior as CastErrorVoice).
- No model data entry for the display id: silent no-op.
- System not initialized (no audio): silent no-op.

## Testing

Audio playback is not covered by the unit-test or E2E harness (consistent with
CastErrorVoice). Verification is manual: assign a gossip + pissed sound entry to an NPC
model in the editor, export, click the NPC in-game — first three clicks play gossip lines,
fourth+ plays pissed lines, clicks during playback do nothing, and the counter resets
after selecting another unit or 30 seconds of leaving the NPC alone.

## Out of Scope

- Server, network protocol, Lua bindings.
- Cooldown/hostility/reputation-dependent lines.
- Per-creature (as opposed to per-model) voice overrides.
- Authoring the actual sound content (sound entries are assigned over time by the user).

## Rejected Alternatives

- **Hooking `GameUnitC` target-change signals:** re-clicks on the already-selected unit do
  not fire a change signal, and click-count state would be scattered across objects.
- **Lua-driven implementation:** would require new bindings for display-id data and
  positional audio playback for no gain; C++ system matches the CastErrorVoice precedent.
