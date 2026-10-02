# Getting an effect on screen

Authoring a `.hpar` is only half the job — nothing in the engine plays a file just because
it exists. There are four paths; pick the least invasive one that fits.

## 1. Spell visualization kits — the data-only path

**Use this whenever the effect belongs to a spell.** No code at all: the effect is named in
the `spell_visualizations` dataset and `SpellVisualizationService` spawns it.

A `SpellKit` (see `src/shared/proto_data/spell_visualizations.proto`) carries
`repeated string particles`, an optional `attach_bone`, a `scope` (`CASTER` / `TARGET` /
`PROJECTILE_IMPACT`), and a `delay_ms`. Kits hang off a `SpellVisualization` keyed by event:
`START_CAST`, `CASTING`, `CAST_SUCCEEDED`, `IMPACT`, `AURA_APPLIED`, `AURA_REMOVED`,
`AURA_TICK`, `AURA_IDLE`, `CANCEL_CAST`.

Edit it through `mmo_edit`'s spell visualization editor, which writes
`data/editor/data/spell_visualizations.data` (and the client copy under
`data/client/ClientDB/`). `ResetTalents.hpar` reaches the screen this way.

Attachment: with `attach_bone` set and the skeleton having that bone, the emitter is bound
to the bone; otherwise it hangs off a child of the actor's scene node (feet). Cleanup is by
`CleanupEffectsForActor(actorGuid, spellId)` — so **an `AURA_APPLIED` effect must be
removed by an `AURA_REMOVED` kit**, and only looping emitters make sense there.

## 2. Server-driven visual on a unit -- the `PlaySpellVisual` packet

For events that are not spells (level up, quest completion, a discovery flourish), the server
plays a SpellVisualization **by id** and every client in sight shows it. No spell needed, and
no client code per effect.

The chain, level-up as the worked example:

1. The player levels: `GamePlayerS::RewardExperience` raises `trigger_event::OnPlayerLevelUp`
   with the new level as event data.
2. `GamePlayerS::RaiseTrigger` looks the event up in `Project`'s player-trigger index, which
   pre-buckets every trigger flagged `trigger_flags::PlayerTrigger` by the event it listens
   for -- players carry no trigger list of their own, so that flag is what makes a trigger
   global to every player. The lookup is one array index; an event nothing listens for costs
   nothing.
3. The matching trigger runs a `PlaySpellVisual` action
   (Targets: UNIT; Data: `<VISUALIZATION-ID>, [<EVENT>]`).
4. `GameUnitS::NotifyPlaySpellVisual` broadcasts the `PlaySpellVisual` packet through
   `ForEachSubscriberInSight`.
5. `WorldState::OnPlaySpellVisual` resolves the unit and calls
   `SpellVisualizationService::ApplyById`, which runs the same kit machinery as a spell.

So adding a new server-driven effect is **data only**: author the `.hpar`, add a
SpellVisualization entry naming it, add a trigger row. The only time this needs code is a new
*event* to hang it on -- one enum value in `trigger_helper.h` plus the `RaiseTrigger` call site.

Use the `IMPACT` event for these. The cast and aura events expect a matching lifecycle event
to tear their effects down, and nothing raises those for a visual played by id.

Effects from `ApplyById` are tracked under a synthetic spell id (`0x80000000 | visualizationId`)
so they can never collide with a real spell's effects on the same unit.

## 3. Projectile trail and impact

`ProjectileVisual` (same proto) has `trail_particle` and `impact_particle`. `ProjectileManager`
attaches the trail to the projectile node for the flight and spawns the impact burst on hit.
Trails should be `SIM_WORLD` and looping; impacts should be one-shot.

## 4. Direct engine code

`Scene::CreateParticleEmitter(name)` → `SetSystemParameters(params)` → attach to a node →
`Play()`. You own the lifetime: `Scene::DestroyParticleEmitter` and `DestroySceneNode`. See
`GameUnitC::UpdateSparkEmitter` for the looping-effect pattern (explicitly started and stopped
by a flag). Reach for this only when the effect genuinely has no server event behind it --
path 2 covers everything else and needs no code.

Load with `ParticleSystemSerializer` (all emitters). `ParticleEmitterSerializer` only reads
emitter 0 — it exists for old call sites and will silently drop the rest of a layered
effect.

## Where files go

- Effects: `data/client/Particles/<Name>.hpar`
- Editor presets: `Editor/ParticlePresets/<Name>.hpar` (ordinary `.hpar` files)
- Recipes: `tools/particle_gen/recipes/<name>.py`, checked in alongside

Asset paths in `material_name` / `mesh_name` and in every proto reference are registry
paths (`Particles/Additive.hmat`), not filesystem paths.

## What does *not* need doing

Adding a `.hpar` file, naming it from the spell-visualization dataset, and wiring a trigger to
it is a pure data change: no protocol version bump, no database migration, no rebuild. Only
path 4, and adding a brand-new trigger *event*, touch C++.
