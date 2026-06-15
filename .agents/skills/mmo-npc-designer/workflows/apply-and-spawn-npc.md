<required_reading>
Read these reference files now:

1. `references/npc-json-format.md`
2. `references/npc-authoring-workflow.md`
3. `references/npc-runtime-semantics.md`
</required_reading>

<process>
1. Validate the JSON draft before any write step.
2. Before any placement change, inspect the target terrain with `scripts/inspect_terrain.py`:
   - Query the intended `world_x/world_z` to get the terrain height that should become `positiony`.
   - Query the intended zone by name when the request is area-driven, so the placement is anchored to actual zone-bound terrain tiles rather than screenshots or memory.
3. Apply base unit and linked service or loot data with `scripts/apply_npc_json.py`.
4. Only apply spawns when the request actually includes placement changes. Use `--apply-spawns` intentionally because it modifies `maps.data`.
5. Prefer named spawns for idempotent updates. Unnamed spawns are treated as append-only unless the JSON explicitly uses a replacement mode supported by the apply script.
6. After apply, inspect the resulting unit again with `scripts/inspect_npc_catalog.py` and verify the critical links, loot, spells, gossip, and spawns.
</process>

<success_criteria>
- Validation passed before writes.
- Terrain height and zone coverage were checked before spawn coordinates were authored.
- The correct `.data` files were updated and optionally backed up.
- Spawn changes were applied intentionally and verified after the write.
- The final live unit inspection matches the requested behavior.
</success_criteria>
