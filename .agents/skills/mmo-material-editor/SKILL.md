---
name: mmo-material-editor
description: Inspect, preview, describe, create, or modify this MMO project's HTEX textures, HMAT material graphs, HMF material functions, and HMI material instances. Use for texture metadata or PNG previews; material node/pin/property analysis; shader and deferred-rendering reasoning; terrain material tuning; JSON export and validation; HMI override edits; or conservative HMAT graph edits that must preserve compiled chunks.
---

# MMO Material Editor

Use the live serializers and node declarations in `F:/mmo` as the source of truth. Do not infer binary layouts from filenames or modify asset bytes with ad hoc offsets.

## Choose The Operation

- For `.htex` metadata or a visual preview, use `scripts/htex_tool.py`.
- For `.hmat` or `.hmi` inspection, graph description, JSON export, validation, or editing, use `scripts/material_tool.py`.
- For available node types, pins, and property schemas, generate the catalog from the live C++ header with `material_tool.py node-catalog`.
- For shader semantics, deferred G-buffer channels, sampler behavior, and safety boundaries, read [material-system.md](references/material-system.md).
- For exact file/chunk layouts and command behavior, read [file-formats.md](references/file-formats.md).
- For terrain detail work, read [terrain-detail-workflow.md](references/terrain-detail-workflow.md).

Run commands from the repository root unless a command passes `--source-root`.

## Inspect Textures

```powershell
python .agents/skills/mmo-material-editor/scripts/htex_tool.py inspect `
  data/client/Textures/Tilesets/T_Dirt_01_C.htex --json

python .agents/skills/mmo-material-editor/scripts/htex_tool.py preview `
  data/client/Textures/Tilesets/T_Dirt_01_C.htex `
  --output artifacts/T_Dirt_01_C.png
```

Preview all referenced albedo, normal, mask, roughness, and splat textures before proposing graph changes. For BC5/RG8 normal maps, the PNG reconstructs Z from XY.

## Inspect Materials And Instances

```powershell
python .agents/skills/mmo-material-editor/scripts/material_tool.py inspect `
  data/client/Models/Grass.hmat

python .agents/skills/mmo-material-editor/scripts/material_tool.py inspect `
  data/client/Models/FalwynPlains_Terrain_Inst.hmi --json

python .agents/skills/mmo-material-editor/scripts/material_tool.py node-catalog `
  --output artifacts/material-node-catalog.json
```

When describing a graph:

1. Identify the root Material node and connected Base Color, Normal, Roughness, Metallic, Specular, Emissive, Opacity, and Opacity Mask paths.
2. Trace texture and parameter nodes through arithmetic, masks, lerps, and material functions.
3. State which values are HMI-overridable and which require an HMAT graph change.
4. Check sampler type: `0` is color; `1` is normal and reconstructs tangent-space Z.
5. Mention missing or stale shader variants when the shader summary exposes them.

## Edit An HMI

Export, edit only the intended JSON values, validate, then write a new file:

```powershell
python .agents/skills/mmo-material-editor/scripts/material_tool.py export-json `
  data/client/Models/FalwynPlains_Terrain_Inst.hmi `
  --output artifacts/terrain-instance.json

python .agents/skills/mmo-material-editor/scripts/material_tool.py validate-json `
  artifacts/terrain-instance.json

python .agents/skills/mmo-material-editor/scripts/material_tool.py apply-json `
  artifacts/terrain-instance.json `
  --output data/client/Models/FalwynPlains_Terrain_Inst.hmi `
  --overwrite
```

Keep `source_sha256`. The writer rejects changed source assets unless `--force` is explicitly justified. Prefer HMI edits for texture substitutions, tiling, strength, tint, roughness, and other exposed parameters.

## Edit An HMAT Graph

Export and inspect the graph JSON before changing it. Preserve node and pin IDs unless intentionally adding or removing graph objects. Links may be asymmetric in existing files because several inputs can fan out from one output.

```powershell
python .agents/skills/mmo-material-editor/scripts/material_tool.py export-json `
  data/client/Textures/Tilesets/TerrainGrass.hmat `
  --output artifacts/terrain-material.json

python .agents/skills/mmo-material-editor/scripts/material_tool.py validate-json `
  artifacts/terrain-material.json

python .agents/skills/mmo-material-editor/scripts/material_tool.py apply-json `
  artifacts/terrain-material.json `
  --output artifacts/TerrainGrass.modified.hmat `
  --allow-stale-shaders
```

`apply-json` changes only `GRPH` and preserves compiled shader and unknown chunks byte-for-byte. The resulting graph and runtime shader bytecode can disagree. Always open and save the modified HMAT in `mmo_edit` to compile shaders and regenerate parameter tables before placing it in production data.

Do not use HMAT JSON writes when:

- The graph export contains an `error`.
- A referenced `.hmf` is missing.
- The desired node or property is absent from the generated live catalog.
- The editor cannot be used afterward to recompile.

## Create Materials

Prefer creating the initial HMAT in `mmo_edit`, then use this skill to inspect and tune it. A valid runtime HMAT requires platform shader compilation, which the JSON tool intentionally does not fabricate.

Create HMI assets by exporting a nearby instance with the same parent, changing `name`, `parent`, attributes, and override arrays, then applying to a new output path. Ensure override names exist in the parent material.

## Verify

After every write:

1. Re-run `inspect` on the output.
2. Export it again and compare intended fields.
3. For HMAT, recompile/save in `mmo_edit`.
4. Preview in the material editor and in the actual deferred world view.
5. Check color, normal orientation, tiling repetition, roughness response, and all required shader variants.
