# particle_gen

Python tooling for the client's `.hpar` particle systems
(`data/client/Particles/*.hpar`).

| File | Purpose |
|---|---|
| `hpar.py` | read/write library, mirrors `src/shared/scene_graph/particle_emitter_serializer.cpp` |
| `inspect_hpar.py` | dump an effect as text, or as an editable Python recipe (`--as-recipe`) |
| `preview.py` | CPU-simulate an effect and render a contact sheet PNG |
| `make_sprites.py` | generate the soft glow/beam/ring sprites and import them as DXT5 `.htex` |
| `recipes/` | mostly one script per authored effect, each writing its own `.hpar`; some scripts write several related effects (e.g. `warrior_abilities.py` writes seven), and shared helper modules such as `warrior_common.py` write nothing themselves |

Requires `numpy` and `Pillow` (already needed by `tools/terrain_gen`).

```bash
# inspect
python tools/particle_gen/inspect_hpar.py data/client/Particles/LevelUp.hpar

# rebuild an effect from its recipe
python tools/particle_gen/recipes/level_up.py

# preview it (then look at the PNG); prints each emitter's resolved blend mode
python tools/particle_gen/preview.py data/client/Particles/LevelUp.hpar --figure

# validate structure, particle budgets, fade-out and material blend modes
python tools/particle_gen/inspect_hpar.py data/client/Particles/LevelUp.hpar --check --one-shot
```

`hpar.py` round-trips every v2.0 file in `data/client/Particles/` byte-for-byte, except
that files written before the trailing `mesh_name` field grow by the 2 bytes of an empty
string. Legacy v1.0 files are read and upgraded to v2.0 on write.

See the `particle-author` skill (`.claude/skills/particle-author/`) for the authoring
workflow, the field reference, effect recipes, and how effects get triggered in game.
