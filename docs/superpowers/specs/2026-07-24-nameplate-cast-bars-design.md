# Nameplate Cast Bars — Design

Date: 2026-07-24
Status: Approved

## Goal

Show a Plater-style cast bar on unit nameplates: while a unit with a visible
nameplate is casting or channeling a spell, a slim progress bar with the spell
name appears below its health bar. Interrupted casts flash a brief
"Interrupted" state. This was the highest-value deferred polish item of the
nameplate system.

Client-only. No server, protocol, or database changes.

## Requirements (user-approved)

- **Scope:** every unit with a visible nameplate shows its cast bar while
  casting/channeling — enemies, neutrals, friendlies, and players alike.
  Which plates exist at all remains governed by the existing `NameplateShow*`
  filters.
- **Visuals:** slim bar attached below the health bar, spell name as small
  centered text on the bar. Casts fill left→right; channels drain right→left.
- **Interrupts:** when a tracked cast ends in failure, the bar shows a
  grey-red "Interrupted" state for 0.8 s, then hides.
- **Settings:** new `NameplateShowCastBars` cvar (default on), surfaced as a
  checkbox in Options → Gameplay next to the other nameplate toggles.
- The local player's large `PlayerCastBar` (CastBar.xml/.lua) is untouched.

## Architecture

Chosen approach: **per-unit cast state on `GameUnitC`** (over a standalone
guid→state tracker singleton, and over Lua events). Rationale: the WorldState
packet handlers already resolve the caster unit; state lifetime is tied to the
unit (no cleanup maps, despawn cleans up implicitly); a future target-frame
cast bar can reuse the same data.

### 1. Cast state on GameUnitC

`src/shared/game_client/game_unit_c.h/.cpp` gains a small value struct and
notify/read API:

```cpp
struct UnitCastInfo
{
    const proto_client::SpellEntry* spell = nullptr; // active cast, nullptr = idle
    GameTime startTime = 0;   // client clock (GetAsyncTimeMs)
    GameTime endTime = 0;
    bool channeling = false;
    GameTime interruptedAt = 0; // when the last cast ended in failure, else 0
};
```

- `NotifyCastStarted(const proto_client::SpellEntry& spell, GameTime castTimeMs)`
  — sets spell/start/end, clears channeling + interrupted.
- `NotifyChannelStarted(const proto_client::SpellEntry& spell, GameTime durationMs)`
  — same with `channeling = true`.
- `NotifyChannelUpdate(GameTime timeLeftMs)` — re-derives `endTime`
  (`now + timeLeft`); `timeLeft == 0` clears the cast (normal channel end, no
  interrupt flash).
- `NotifyCastSucceeded()` — clears the cast (no flash).
- `NotifyCastFailed()` — if a cast is currently tracked, clears it and stamps
  `interruptedAt = now`; otherwise a no-op (failures of instant casts never
  had a bar).
- `GetCastInfo()` const accessor.

Progress is **derived from timestamps at read time** — no per-frame update,
no timers. The interrupted record expires by timestamp comparison at read
time; nothing needs to clean it up.

### 2. Packet wiring (WorldState)

All five packets already arrive for every caster in view and already look up
the caster unit for spell visualization. Add the notify calls there
(`src/mmo_client/game_states/world_state.cpp`):

| Handler | Call |
|---|---|
| `OnSpellStart` (castTime > 0) | `casterUnit->NotifyCastStarted(*spell, castTime)` |
| `OnSpellGo` | `casterUnit->NotifyCastSucceeded()` |
| `OnSpellFailure` | `casterUnit->NotifyCastFailed()` |
| `OnChannelStart` (duration > 0) | `casterUnit->NotifyChannelStarted(*spell, duration)` |
| `OnChannelUpdate` | `casterUnit->NotifyChannelUpdate(timeLeft)` |

Notes:

- `OnChannelUpdate` currently only forwards to the local player's SpellCast;
  it must additionally resolve the caster unit (any unit) and notify it. The
  local-player forwarding stays unchanged.
- Verified server-side: `ChannelUpdate` is broadcast to nearby players via
  `SendPacketFromCaster` (channeling_cast_state.cpp), including the
  `timeLeft = 0` end signal — other units' channel bars therefore end
  promptly and stay live-updated on pushback.
- `OnSpellGo` fires for instant casts too; `NotifyCastSucceeded` on an idle
  unit is a harmless no-op.

### 3. Nameplate UI

**Template** (`data/client/Interface/GameUI/Nameplate.xml`, data/client
submodule): new **childless** `NameplateCastBarTemplate` (the Copy pattern
requires childless templates — see nameplate-system notes):

- ProgressBar, 240×20 ui units.
- Imagery mirrors the health bar: dark translucent backing, white gradient
  fill tinted via `ProgressColor` (default warm yellow, e.g. `FFE8B923`),
  small inset border, plus a `Caption` TextComponent section (centered,
  `wrap="false"`) rendered in a new smaller `NameplateCastFont` (same face as
  `NameplateFont`, size ~18).

**Frame** (`src/mmo_client/ui/nameplate_frame.h/.cpp`):

- New child `m_castBar` (`std::shared_ptr<ProgressBar>`), created in
  `CreateChildren` from the template via `Copy` (childless-template pattern),
  non-clickable and disabled like the other children.
- Anchored left/right to the plate, top to the **health bar's bottom** with a
  2 ui-unit gap — it hangs below the plate rect, which is fine (the highlight
  already renders outside the rect; there is no child clipping).
- `UpdateContent` each `Animate` tick reads `unit.GetCastInfo()` and the
  `NameplateShowCastBars` cvar (cached `ConsoleVar*`, looked up once):
  - Idle and no fresh interrupt → hide the bar.
  - Casting → progress `(now - start) / (end - start)` clamped to [0, 1];
    channeling → the complement (drains). Text = spell name, truncated with
    the existing `FitNameToWidth` against the bar width, re-fitted only when
    the spell or scale changes (same caching scheme as the unit name).
  - Interrupted (now − `interruptedAt` < 800 ms) → bar visible, full
    progress, `ProgressColor` grey-red (e.g. `FF8B3A3A`), text = localized
    `NAMEPLATE_INTERRUPTED`. Afterwards the bar hides again.
  - Property writes (color, text) only on change, matching the existing
    cached-color idiom in `UpdateContent`.
- C++ localization: `Localize(FrameManager::Get().GetLocalization(), "NAMEPLATE_INTERRUPTED")`
  (frame_ui localizer, already available).

### 4. Settings & localization

- `NameplateShowCastBars` cvar, default `1`, registered in
  `WorldState::RegisterGameplayCommands` beside the other `NameplateShow*`
  cvars.
- Options → Gameplay: new checkbox row wired to the cvar, following the
  existing nameplate toggle rows (Lua/XML in the data/client submodule).
- New localization keys in **all 4 locales** (enUS/deDE/frFR/ruRU):
  - `OPTIONS_NAMEPLATE_CAST_BARS` — options row label.
  - `NAMEPLATE_INTERRUPTED` — flash text.

## Edge cases

- **Unit despawns mid-cast:** plate and state are destroyed with the unit.
- **Instant casts:** `castTime == 0` sets no state; no bar flicker.
- **Failure without tracked cast** (e.g. validation failure before
  SpellStart): `NotifyCastFailed` is a no-op — no phantom interrupt flash.
- **Plate hidden by filters/distance:** cast state still tracks on the unit;
  the bar simply isn't rendered (plates re-created later read current state).
- **Local player:** has no nameplate; the big `PlayerCastBar` continues to
  serve, driven by the untouched SpellCast/Lua path.
- **Recast while flash is showing:** `NotifyCastStarted` clears
  `interruptedAt`, so the new cast wins immediately.

## Testing & verification

- Full Windows client build; existing unit tests and E2E suite stay green
  (no server changes, so no E2E behavior change expected).
- Manual in-game checklist (nameplate rendering is not E2E-assertable):
  1. Enemy caster: bar appears with spell name, fills left→right, clears on
     cast completion.
  2. Channeled cast: bar drains right→left, ends promptly on channel end.
  3. Interrupt a cast: grey-red "Interrupted" flash for ~0.8 s.
  4. `NameplateShowCastBars 0` hides all plate cast bars instantly; the
     Options checkbox round-trips.
  5. Long spell name truncates with ellipsis; UI scale changes re-fit it.
  6. Player's own big cast bar unchanged.
