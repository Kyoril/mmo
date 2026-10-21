# Mesh ambient sounds

Meshes can carry looping positional sounds: a torch crackles, a campfire burns, a portal hums.
They are authored once on the mesh asset (`.hmsh`), and every placement of that mesh plays them:
plain world placements, world model group meshes, doodads, prop-kit props and nested world models.
The feature is client-only and cosmetic: no server involvement and no network traffic.

Design: [superpowers/specs/2026-10-20-mesh-ambient-sounds-design.md](superpowers/specs/2026-10-20-mesh-ambient-sounds-design.md).

## Authoring

1. Create a `SoundEntry` (Sound Editor) with category **Ambience**, **3D** and **Looped** checked,
   and a `max_distance` that matches the source: about 8–10 m for a torch, 15–20 m for a campfire
   or portal. Fade in/out are optional; the runtime fades at least 0.3 s either way.
   Use mono, seamlessly looping files. Do not trim or fade loop files, because that breaks the seam.
2. Open the mesh in the mesh editor. Under **Details > Sounds**, choose **Add Sound**, pick the entry and
   move the offset to where the sound comes from (the selected emitter shows as an axis in the
   viewport). Volume scales the entry's volume. Max Distance overrides the entry's range (0 = entry).
3. **Save Mesh**. The collision recipe is kept.

Unattended: `mmo_edit --set-mesh-sounds <asset.hmsh> "<id>:<x>,<y>,<z>[:<volume>[:<maxDistance>]];..."`
(an empty spec `""` removes all sounds). It logs to `set_mesh_sounds.log`.

An entry that is not looped and 3D stays silent in game; the editor warns about it next to the
emitter.

## Runtime

`AmbientSoundEmitters` (`src/shared/game_client/`) is owned by `WorldState`. It re-selects
the audible sources 4 times a second, nearest first. At most **12** voices play at once, and at most
**4** of them share a sound entry, so a hall of identical torches plays only the closest few. A
voice keeps playing until the player is 10 % beyond its range, then fades out. Each voice starts
at a random point of its loop, so identical sources never play in lockstep.

The volume follows the Ambience slider and mute.

## File format

Optional `SNDS` chunk in the mesh file: `uint8 layout (1)`, `uint16 count`, then for each emitter
`uint32 soundEntryId, float3 offset, float volume, float maxDistance`. It is only written at mesh
version 0x0302, from which readers skip unknown chunks. Older clients, the servers and
nav_build load these meshes unchanged. A chunk with an unknown layout is skipped with a warning.

Not covered yet: creature and game-object display meshes, instanced foliage, and playback in the
editor's world view.
