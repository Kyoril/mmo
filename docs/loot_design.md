# Loot table design rules

Loot tables live in `data/editor/data/*_loot.data` (`unit_loot.data` for creatures). They are
rolled by `LootInstance::GenerateItemsFromEntry` (`src/shared/game_server/loot_instance.cpp:88`).

## How a group rolls

A **group yields at most one item**. Per group the engine rolls one value in `[0, 100)` and then:

- definitions with `dropchance == 0` form an *equal-chance pool*;
- definitions with `dropchance > 0` qualify when their chance is `>= ` the rolled value;
- if any weighted definition qualified, one of those is picked at random, otherwise one of the
  equal-chance pool is picked; a group whose definitions are all weighted and none qualified
  yields nothing.

Two consequences used by the content below:

- a group made only of `dropchance == 0` definitions yields **exactly one** of them, always;
- so does a group that contains a definition with `dropchance == 100`.

## Rule: a boss drops exactly one rare item

**Every boss kill yields exactly one rare (blue, `quality == 3`) item — never none, never two.**
This is the player-visible promise of a boss fight: the kill is worth a blue, and which blue it is
is the only question. It is binding for every boss loot table, and a table that can drop two blues
or no blue at all is a bug, not a design choice. (Maintainer decision on bug `b36cb848`, where
`Cantor Veyr` dropped two blues from one corpse.)

In practice this means: put **all** of a boss's rare items into a single group, and give that group
no definitions of any other quality. Everything else the boss may drop — greens, quest items, the
rare epic — goes into a separate group, where it cannot add a second blue.

The boss tables as of this rule:

| Loot entry | Boss(es) | Rare group |
|---|---|---|
| 2 | Thaldrik Flamegnash (1) | items 32, 34, 35; item 32 at `dropchance = 100` |
| 28 | Sevrin Wax (81), Cantor Veyr (88) | items 116-119, all equal-chance |
| 29 | Ossuar (84), Brother Oswin (86) | items 146-148, all equal-chance |
| 30 | Choirmistress Vell (85), Sister Mereth (87) | items 149-151, all equal-chance |

Epics are not covered by this rule: loot entry 28 additionally holds the dungeon's epic
(item 152, `dropchance = 6`) in its extras group, so a lucky kill is one blue *plus* a purple.

Entry 28 used to list item 136 (*Seal of the Barrow-King*) among its extras as well, which is how a
kill could drop two blues. It was removed; the ring is the reward of quest 53 *The Barrow-King* and
stays obtainable there. Do not put it, or any other rare, back into an extras group.

Trash and non-boss creatures have no guaranteed quality at all; the rule is about bosses.
