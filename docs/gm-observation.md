# GM player observation

How a game master watches a player or group, in the open world or inside a dungeon
instance, without being seen and without influencing the run. Design:
`docs/superpowers/specs/2026-10-10-gm-observation-design.md`.

Requires a server built with `MMO_WITH_DEV_COMMANDS=ON`; the commands are typed in the
client console.

## Commands

| Command | Min. gm level | Effect |
|---|---|---|
| `invisible on\|off` | 1 (Gm) | Hides your character from players and creatures. Other GMs still see you. |
| `godmode on\|off` | 2 (SeniorGm) | Damage immunity, so AoE effects (which ignore visibility) cannot hurt you. |
| `port <name>` | 1 (Gm) | Teleports you next to the named player. If the player is online you join **their instance**, even when you are not in their group or are in another copy of that dungeon. |

The recommended observation setup is `invisible on`, `godmode on`, then `port <name>`.

Turn `invisible on` **before** porting in. Whether you count as an instance player is decided
when you enter the instance: toggling invisibility inside it keeps you counted (or uncounted)
as you were on entry until you leave.

## What is hidden, what is not

Hidden:
- Your character is not spawned for non-GM players (unit visibility `Off`), also not while
  stealth auras are applied or removed.
- Creatures ignore you: no idle aggro, no alert, no combat target selection.
- Scripted encounters ignore you: trigger targets (random / nearest / all players, random
  unit, summons attacking the nearest player) never pick you. Encounter broadcast messages
  still reach you.
- Area triggers are ignored while you are invisible: you never start scripted events and
  portal triggers do not move you. Use `port` / `worldport` to travel.
- You do not count as an instance player: no player-count change, no instance timers, no
  enter-instance trigger, no effect on `OnAllPlayersDead`.
- Joining by `port <name>` writes **no** dungeon binding: your own group's binding and your
  personal lock are untouched. If you lead a group, members entering that dungeon do not
  follow you into the visited instance either.

Not hidden (known limits):
- Friends list, guild roster, whispers and chat channels still show you online.
- Your own active actions are broadcast unfiltered: spell casts, `/say`, emotes and loot
  rolls are visible to players. Only watch.
- AoE effects do not check visibility; that is why godmode is expected.

## Persistence

Both flags are stored per character in `characters.gm_flags` (bit 1 = invisible,
bit 2 = godmode). They survive teleports, map changes and relogs. On every world join with
an active flag the realm sends a system chat message ("GM invisibility is on." / "Godmode is
on."), so a relogged GM knows they are still hidden. Flags are sanitized against the
account's gm level on every world join: below 2 (SeniorGm) godmode is removed, and an
account without a gm level has both flags forced to 0, so a demoted account never keeps
state it can no longer turn off.

After a relog inside a dungeon you were only visiting, you land in a fresh copy of your
own (the visit wrote no binding). Just run `port <name>` again.

Turn the flags off again with `invisible off` / `godmode off` when you are done.

## Testing

Automated: `e2e/scenarios/gm_observer_persistence.lua` checks that both flags survive a
cross-map port and a relog, and that a hostile creature ignores an invisible GM (with a
visible, mortal GM as positive control).

Joining another player's instance needs two characters in the world at once, which the
E2E harness cannot do, so that part is a manual test with two clients:

1. Player A enters a dungeon.
2. GM B (not grouped with A, and ideally grouped with someone else) runs `invisible on`,
   `godmode on`, `port A`. B lands next to A in A's instance.
3. A does not see B; mobs ignore B.
4. B's group binding is unchanged.
5. B repeats `port A` from a different copy of the same dungeon and again lands in A's
   instance.
