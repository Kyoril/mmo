# worldkit — world knowledge for content tooling

A read-only model of the game world for agents and scripts. It covers:

- terrain: height, slope, water, holes and road layers;
- placed props;
- spawns and quest links;
- the atlas of named places;
- lint and report checks.

The design is in `docs/superpowers/specs/2026-09-27-content-foundation-design.md`.

**Python:** use `python`, the gate's interpreter. If the Windows Store `python` alias does not launch (common inside Claude Code), use `py -3.14` instead, after running `py -3.14 -m pip install --user -r tools/world/requirements.txt`.

**Compass:** north is −Z and east is +X, as on the in-game minimap. All renders are north-up.

| Command | What it does |
|---|---|
| `cd tools/world; python -m worldkit query --map 0 --at X Z` | Everything about one point: height, slope, zone, terrain kind, water, holes, props, places, and whether it is placeable. |
| `cd tools/world; python -m worldkit layers --world Development --page 32_32 --out x.png` | Splat layer preview, used to fill `worldkit/terrain_kinds.json`. |
| `python tools/world/render_map.py --map 0 [--zone Z \| --poi ID \| --bbox X0 Z0 X1 Z1 \| --full] --out x.png` | Review map. `--dump-state` then `--diff` gives before/after images. |
| `python tools/world/lint.py --map 0 [--draft npc.json]` | Placement lint against the baseline. Exits 1 on new errors. |
| `python tools/world/report.py --map 0 --markdown r.md` | Quest reachability, the turn-in rule, and spawn/level distribution per zone and place. |
| `python tools/world/bootstrap_atlas.py --map N` | First atlas for a new map. Refuses to overwrite an existing one. |
| `python tools/world/extract_canon.py --map 0 --out c.md` | Dump of quest and NPC text, as source material for the bible. |
| `.agents/skills/mmo-npc-designer/scripts/inspect_terrain.py` | The NPC skill's point and zone query, backed by worldkit. |

## Files

| Path | Content |
|---|---|
| `data/world/atlas/map_<id>.json` | Named places, zone bands and roads. Edit as text or in mmo_edit's **Atlas** mode. Statuses: placeholder / canon / note; only the user sets canon. |
| `docs/world/bible.md` | The world bible: [E] established, [C] in-game canon, [?] open. |
| `tools/world/lint_baseline.json` | Known placement and reachability findings. Update with `--update-baseline`, per map and per section. |
| `tools/world/worldkit/terrain_kinds.json` | Which splat layer of which terrain material is a path or road. |
| `generated/world/<World>/` | Snapshot cache (gitignored). It rebuilds itself when tiles or props change. |
| `generated/world/review/<slug>/` | Review packets: `README.md` brief, before/after PNGs, `report.md`. |

The weekly content audit (`tools/gate/content_audit.py`) runs the `placement` and `reachability` domains. The quest and NPC validators call `worldkit.skill_checks`.

**Tests:** `python -m unittest discover -s tools/tests -p "test_world_*.py"`
