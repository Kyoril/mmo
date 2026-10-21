# Creature movement abilities

Every unit has a set of movement abilities (`movement_ability` in
`src/shared/game/movement_ability.h`) which say what kind of terrain it can move through. Players
and creatures can walk and swim by default; creature data can take an ability away.

| Ability | Meaning |
|---|---|
| `CanWalk` | The unit moves over walkable ground. |
| `CanSwim` | The unit swims at the surface of water deep enough to swim in. Without it, water that deep blocks its movement like a wall. |

## Authoring

A creature's abilities come from `UnitEntry::extraflags` (`unit_extra_flags`):

| Bit | Flag | Effect |
|---|---|---|
| `0x01` | `CannotSwim` | Takes `CanSwim` away. |

In the editor (mmo_edit) the flag is the *Can Swim* checkbox in *Creature > Movement*. It is on for
every creature unless the flag is set, so all existing creatures swim.

## How swimming works

The navigation mesh is baked on the ground, so a path through a lake leads along its bottom. When
`UnitMover` has a path, `ApplyWaterToPath` (`src/shared/game_server/water_path.cpp`) looks up the
map's water data (`ServerWaterMap`) for every path point:

- Water less than `swim::StartDepth` (1.5) above a point is waded through; the point stays on the
  ground.
- Deeper water lifts the point to `surface - swim::SurfaceMargin` (1.5) for a unit which can swim,
  and the unit is flagged `movement_flags::Swimming` for the movement. A unit which ends its
  movement in such water keeps treading water there.
- Deeper water refuses the whole movement for a unit which can not swim. Idle wandering picks
  another point after the usual wait instead of giving up.

Clients keep those units at the surface: `UnitMovement::UpdateRemoteSwimState` runs inside the
ground correction of every unit the local client does not simulate and, while the water below the
unit is deep enough to swim in, puts it in the surface band instead of snapping it onto the bottom.
The locally controlled player is not affected — it runs its own swim simulation in
`UpdateSwimState` / `HandleSwimming`. The swim constants are shared by both sides in
`src/shared/game/movement_ability.h`, so client and server agree on when a unit swims.

Nothing of this is on the wire: the server sends the already lifted path points in `CreatureMove`,
so there is no protocol change.
