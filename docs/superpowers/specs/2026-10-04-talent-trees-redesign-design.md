# Talent Trees Redesign — Design

Date: 2026-10-04 · Branch: `feature/talent-trees-redesign`

The user asked for this while away from the keyboard, so the design calls below were made
without a review round. Read them as decisions that are easy to revisit.

## Goals

1. Talents that feel impactful: one point = one noticeable change. No more "5 ranks of
   -0.1 s cast time". Multi-rank talents are capped at 2 ranks and kept rare.
2. Each class tree gets three equally weighted paths. Fire and Arcane (mage) get as many
   real talents as Frost.
3. A layout that is fun to look at: a constellation around a class emblem, inspired by
   Alabaster Dawn / Baldur's Gate 3, instead of three loose columns.
4. Placeholder talents fill the layout now. They show the planned design but cannot be
   learned yet.
5. All five classes (Mage, Warrior, Cleric, Acolyte, Scout) get the same treatment.

## Constraints that shaped it

* A class earns **9 talent points** by class rank 10. Reaching a capstone costs 6 points:
  the spine root, the spine middle, 3 more points anywhere in the tree, then the capstone.
  So a player can finish one path and dip into a second.
* Real talents may only use mechanics the server runs today (spell modifiers, stat auras,
  `ProcTriggerSpell`, active spells) plus the small engine additions listed below.
  Anything that needs a new mechanic becomes a placeholder.
* Spell modifiers match by family flags (all classes use family 0). Where two spells of one
  class shared a flag bit, the spell gets its own bit (Scout: Twin Slash, Throwing Knife,
  Quick Cut, Evasive Step; Acolyte: Shadowbolt, Fear).

## Engine additions

1. **Spell crits for plain damage spells.** `HandleSchoolDamage` never rolled crits (it had
   a `TODO`), so "+crit" talents did nothing. It now rolls
   `spell_default_crit_chance` (combat settings, default 5%) + `CritChance` modifiers +
   the victim's `ModCritChanceTaken`, deals `spell_crit_multiplier` (default 1.5x), sends
   `damage_flags::Crit`, and passes `proc_ex_flags::CriticalHit` to procs. The client
   already renders the crit flag. Nothing changes on the wire.
2. **`CritDamageBonus` spell modifier** (op 12). Scales the bonus part of a crit, for spell
   damage, weapon-damage spells and heals.
3. Weapon-damage spells pass `CriticalHit` to their `DoneSpellMeleeDmgClass` proc event,
   so "on crit" procs work for melee techniques as well.
4. **`TalentEntry.placeholder`** (bool, field 11). The server refuses to learn it, and the
   client shows it dimmed with a "Coming soon" line in the tooltip.
5. **`TalentEntry.accent_color`** (ARGB, field 12). Sets the path color for the node's
   lines and glow. Falls back to a color by spell school.
6. **`TalentTabEntry` hub + labels**: `hub_x`/`hub_y` (fields 9/10) place the class emblem
   (`icon`). Spokes run from the hub to every root talent, with faint tier rings around it.
   `repeated TalentTabLabel labels` (field 11: `text` localization key, `x`, `y`,
   `color`) prints the path names.

These are data-file schema fields only: editor data, ClientDB, and the matching
`client_data` subset. No packet changes, so no protocol bump.

## Layout (per class)

Hub (class emblem) at (1150, 900) on a 2300x1420 canvas. Three paths 120° apart (top,
lower-left, lower-right). The talent window is about twice as wide as it is high, so the
rings are ellipses: horizontal distances are stretched by 1.4. Without that the two lower
paths, which run mostly sideways, were cramped (the first iteration in the offline preview).
Each path has 9 nodes on four rings:

```
                [Cap]              r=720  req 5  (child of M)
          [C1]         [C2]        r=570  req 4  (children of B1 / B2)
      [B1]      [M]      [B2]      r=410  req 2  (B1<-A1, M<-A2, B2<-A3)
   [A1]        [A2]        [A3]    r=245  req 0  (spokes from hub)
```

Path labels sit just outside each capstone. Links take the path's accent color. Learned
links get a soft glow under a bright core; unlit links are thin and dimmed; links into a
placeholder are thin grey. Node size is 0.8 for flank nodes, 0.95 for spine nodes and 1.2
for capstones.

The rings, path dividers and star dust come from one generated 2048x1024 texture
(`tools/talent_trees/make_backdrop.py` -> `Interface/GameUI/TalentConstellation.htex`),
placed on the canvas around the hub. That is one frame instead of hundreds of line segments.
`tools/talent_trees/preview.py` renders all five trees offline the way the client draws
them, for iterating without the dev stack.

## Content

The single source of truth is `tools/talent_trees/author_talent_trees.py`. It holds every
talent, spell, icon, position and the 4-locale texts. Each run backs up the data, then
writes editor data + ClientDB byte-identically. Single-rank talents use spell rank 0 /
baseid 0 (see the spell-rank-zero rule). Existing talent ids and spells are reused where a
talent survives. New spells use ids 1000+ and new talents use ids 100+, to avoid clashes
with parallel branches.

The script prints a per-class overview (`--summary`) that is copied into the PR / report.

## UI

* `TalentFrame.lua`: constellation rendering (rings, spokes, hub emblem, labels, colored
  glowing links, placeholder styling). Also fixes the initial view: it was computed in
  `OnLoad` before the viewport had a size, so the tree sat in the top-left corner.
* New localized strings: `TALENT_COMING_SOON` and the path label keys for all 4 locales.

## Verification

* Unit test (`game_server_tests`): spell damage crits, the CritDamageBonus math, and
  learning a placeholder talent is refused.
* Python tool test: the authoring script's data validates. Editor data and ClientDB are
  identical, every icon exists, every talent prerequisite is valid, and every non-placeholder
  talent rank spell exists.
* E2E scenario `mage_talent_procs`: Shatter marks Frost Nova targets, Heating Up buffs
  on Fire Blast, and Ignite fires on a Fire crit (which also proves spell crits work).
  The harness has no talent-learning call, so it learns the passive spells via GM.
* Visual check in the real client: screenshots of each class tree.
