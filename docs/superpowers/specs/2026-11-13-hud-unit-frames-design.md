# HUD unit frames in the Wayfinder theme

## Intent

The Wayfinder HUD restyled the player rails, the target frame and the action bars. The cast bar sat
at the right edge of the action bar, the target frame showed its resource as a 12 px line and no
cast, and the pet and party frames still wore the older themes' look. This brings them in line.

## Goals

- Player cast bar horizontally centred above the bars.
- Target frame shows the target's resource as a readable bar with its value, and the target's cast
  or channel, for any target.
- Pet and party frames use the Wayfinder meters, with the same smoothed fills and damage trail as
  the player and target bars.

## Non-goals

- Restyling the BlueGold and Legacy themes. They only gain the target cast bar, in their existing
  cast bar look, above the target frame.
- Restyling the pet action bar buttons.
- Any change to what goes on the wire. The cast data already reaches the client for nameplates.

## Approaches

1. **Restyle in `Themes/Wayfinder/Layout.lua`, plus a small Lua API (chosen).** The theme already
   restyles shared frames at load with `CopyVisuals` and anchors. The pet, party and target frames
   keep one XML and one script for all themes. The target's cast state is polled from Lua through
   three new `UnitHandle` methods, as the nameplate does in C++.
2. A Wayfinder-only set of unit frame XML files. Rejected: it duplicates the frames' scripts and
   event wiring per theme.
3. A C++ frame type for the target cast bar, like `NameplateFrame`. Rejected: more code for the same
   result, and the layout would no longer be editable with `ui_test`.

## Design

- `UnitHandle::GetCastSpellName`, `GetCastProgress`, `IsCastInterrupted` expose `UnitCastInfo`.
- `TargetCastBar` is a child of `TargetFrame`, driven from the frame's `OnUpdate`
  (`TargetFrame_UpdateCastBar`). A failed cast shows "Interrupted" for the flash duration.
- Wayfinder stacks health, resource and cast bar below the name and sizes the frame to the bars
  that are shown (`Wayfinder_LayoutTargetFrame`), so the debuff row follows. A target without a
  resource gets no empty bar.
- The cast bar is anchored above the pet bar while that is shown, otherwise above the top action
  bar, because the pet bar reaches into the centre.
- Pet frame: no portrait; name, level diamond on the health bar as on the target frame, thin
  resource and experience bars.
- Party frame: no backdrop; per member a name, a health meter with percentage and a thin resource
  bar. The member height is the theme metric `GameUITheme.PartyMemberHeight`.

## Verification

`tools/ui_test/scenarios/wayfinder_hud.lua` shows a casting target, a pet, a party of three and the
player's cast. The target cast bar against a live casting unit needs a check in the real client.
