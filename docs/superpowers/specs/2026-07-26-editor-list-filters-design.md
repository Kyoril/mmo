# Editor List Filters — Design

**Date:** 2026-07-26
**Status:** Approved
**Scope:** mmo_edit only (no proto, server, or client changes)

## Goal

Add property-based filtering to the entry list panel of the spell editor and item
editor so designers can narrow the list by game-data properties (item class,
quality, aura type, …) instead of only name/ID text search.

## Background

Both editors derive from the shared template base
`EditorEntryWindowBase<T1, T2>` (`src/mmo_edit/editor_windows/editor_entry_window_base.h`),
whose `Draw()` renders the left-hand list panel: Add/Duplicate/Remove buttons, a
name-or-ID search box, an entry count, and the filtered list box. Filtering is
currently text-only.

## Approach (chosen: A)

Extend the shared base with per-editor filter hooks:

- `virtual bool DrawFilters()` — renders the editor-specific filter widgets.
  Returns `true` when any filter value changed this frame. Default: renders
  nothing, returns `false`.
- `virtual bool MatchesFilters(const T2& entry) const` — predicate joined (AND)
  with the existing name/ID search. Default: `true`.
- `virtual int ActiveFilterCount() const` — number of non-default filters, used
  for the header badge. Default: `0`.

Rejected alternatives: bespoke per-editor list panels (duplicates the filtering
loop) and a declarative filter-descriptor framework (dependent dropdowns and
range filters don't fit a flat descriptor model).

## Shared base changes

- Between the search bar and the entry count, render a
  `CollapsingHeader` labelled `Filters` — with `Filters (N active)` when
  `ActiveFilterCount() > 0` — containing `DrawFilters()` and a **Reset Filters**
  button. Reset is implemented via a fourth virtual `ResetFilters()`
  (default: no-op).
- The filter pass calls `MatchesFilters(entry)` in addition to the search-text
  match.
- Targeted cleanup: the current rebuild logic has a "cached" and an "uncached"
  branch that both recompute the full filtered index list every frame. Collapse
  to a single filter pass per frame (entry counts are a few thousand at most;
  behaviour is unchanged, code is halved).

## Item editor filters

All dropdowns default to "Any"; all active filters AND-combine. State lives in
member variables of `ItemEditorWindow`.

| Filter | Source of options | Match rule |
|---|---|---|
| Item class | project item classes (data-driven) | `entry.itemclass() == selected` |
| Item subclass | project item subclasses, narrowed to the chosen class (same pattern as the details pane); disabled until a class is chosen | `entry.subclass() == selected` |
| Quality | `s_itemQualityStrings` | `entry.quality() == selected` |
| Inventory type | existing inventory-type name table | `entry.inventorytype() == selected` |
| Stat type | `s_statTypeStrings` | any `entry.stats(i).type() == selected` |
| Required level min/max | two int inputs, 0 = no bound | `requiredlevel` within bounds |

Changing the class filter resets the subclass filter (it may no longer be valid).

## Spell editor filters

State lives in member variables of `SpellEditorWindow`.

| Filter | Source of options | Match rule |
|---|---|---|
| Effect type | `s_spellEffectNames` | any `effects(i).type() == selected` |
| Aura type | `s_auraTypeNames` | any effect's `aura() == selected` |
| Kind | Any / Ability / Passive | `attributes(0)` bit `0x10` (Ability) or `0x40` (Passive) set |
| Class | project classes | `classmask & (1 << (classId - 1))` — mask-0 (unrestricted) spells are excluded |
| Race | project races | `racemask & (1 << (raceId - 1))` — same exclusion rule |
| Spell level min/max | two int inputs, 0 = no bound | `spelllevel` within bounds |

**Spell family is intentionally omitted:** the proto field `family` is never
written or displayed by the editor (only the raw `familyflags`/`procfamily` hex
masks are), so a family dropdown would always filter on 0. Revisit if family
authoring is ever added.

## Non-goals

- No persistence of filter state across editor sessions.
- No changes to other entry editors (units, quests, …) — they inherit the
  no-op defaults and are unaffected, but can adopt the hooks later.
- No query-token syntax in the search box.

## Testing

Manual, in the editor: each filter alone, combinations, filter + text search,
class→subclass narrowing and reset, badge count, Reset Filters, and that
selection is preserved/cleared sanely when the selected entry is filtered out
(existing behaviour: the list box shows no selection; the details pane still
shows the selected entry).
