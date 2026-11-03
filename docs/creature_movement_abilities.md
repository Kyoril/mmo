# Creature movement abilities

Every unit has a set of movement abilities (`movement_ability` in
`src/shared/game/movement_ability.h`) which say what kind of terrain it can move through. Players
and creatures can walk and swim by default; creature data can take an ability away.

| Ability | Meaning |
|---|---|
| `CanWalk` | The unit moves over walkable ground. |
| `CanSwim` | The unit swims at the surface of water deep enough to swim in. Without it, water that deep is a wall: it is carved out of the navigation mesh and the unit paths around it. |

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
- Deeper water cuts the path short for a unit which can not swim: it walks up to the water's edge
  and stops there. This is only a backstop — such a unit is kept away from deep water by the
  navigation filter below. When nothing is left of the path at all (the unit already stands in deep
  water), the movement is refused; idle wandering then picks another point after the usual wait
  instead of giving up.

## How deep water blocks a unit which can not swim

Deep water is a wall for a unit without `CanSwim`, so it is carved out of the navigation mesh
rather than only rejected after the fact: `DeepWaterQueryFilter`
(`src/shared/game_server/deep_water_nav_filter.cpp`) rejects every navigation polygon with a corner
under water of at least `swim::StartDepth`, on top of the world instance's door filter. Paths and
random wander points of such a unit route *around* the water body; water shallow enough to stand in
stays passable. Units which can swim keep using the plain door filter and path straight through.

Because a swimming unit floats in the surface band, it can end up far above the navigation mesh on
the bottom of the water body. `NavMapData::GetSearchExtentY` therefore widens the vertical search
of the navigation query to `swim::NavSearchDepth` whenever an endpoint is submerged — with the
ordinary extent, a unit in water deeper than a few metres would find no polygon below itself and
could never path out of the water again.

Clients keep those units at the surface: `UnitMovement::UpdateRemoteSwimState` runs inside the
ground correction of every creature and, while the water below it is deep enough to swim in, puts
it in the surface band instead of snapping it onto the bottom. Players are left alone — the local
one runs its own swim simulation in `UpdateSwimState` / `HandleSwimming`, and a remote one is drawn
at the height its own client reports, which is also where it dives to. The swim constants are
shared by both sides in `src/shared/game/movement_ability.h`, so client and server agree on when a
unit swims.

Nothing of this is on the wire: the server sends the already lifted path points in `CreatureMove`,
so there is no protocol change.
