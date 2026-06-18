# Material And Shader System

## Sources Of Truth

- Runtime material serialization: `src/shared/scene_graph/material_serializer.cpp`
- Runtime instance serialization: `src/shared/scene_graph/material_instance_serializer.cpp`
- Editable graph: `src/mmo_edit/editors/material_editor/material_graph.cpp`
- Nodes, pins, properties: `src/mmo_edit/editors/material_editor/material_node.h`
- D3D11 generation: `src/shared/graphics_d3d11/material_compiler_d3d11.cpp`
- Metal generation: `src/shared/graphics_metal/material_compiler_metal.mm`
- Deferred lighting: `src/shared/deferred_shading/shaders/PS_DeferredLighting.hlsl`

Regenerate the node catalog whenever `material_node.h` changes.

## Material Root Inputs

The surface Material node exposes:

- Base Color
- Metallic
- Specular
- Roughness
- Emissive Color
- Opacity
- Opacity Mask
- Normal

Root properties control lit/unlit, two-sided, shadows, depth test/write, translucency, UI, and masking.

## Deferred G-Buffer

The generated G-buffer layout is:

| Render target | Channels |
|---|---|
| Albedo | RGB base color |
| Normal | RGB encoded world normal; A linear depth |
| Material | R metallic; G roughness; B specular; A ambient occlusion |
| Emissive | RGB emissive |

Current generated material code writes ambient occlusion as `1.0`; there is no Material root AO pin. Do not promise AO texture control without an engine/compiler change.

Default disconnected values:

- Metallic: `0.0`
- Roughness: `1.0`
- Specular: `0.5`
- Opacity: `1.0`
- Base color: white
- Emissive: black

## Normal Sampling

`SamplerType::Color` is ID `0`; `SamplerType::Normal` is ID `1`.

Normal sampling uses RG, remaps it to `[-1, 1]`, and reconstructs Z with:

```text
sqrt(saturate(1 - dot(xy, xy)))
```

The result is transformed from tangent to world space at the root. Use normal sampler mode for BC5/RG8 and normal-map textures. A color sampler connected to Normal produces incorrect lighting.

## Parameters And Instances

Only Scalar Parameter, Vector Parameter, and Texture Parameter nodes become HMI-overridable runtime parameters. Constants and plain Texture nodes are graph-local.

Parameter names are contracts:

- Keep them unique within a material.
- Preserve spelling and case when editing an HMI.
- Recompile the HMAT after adding, deleting, or renaming parameter nodes.
- Inspect the parent material before creating an override.

## Shader Variants

Vertex variants:

- Default
- SkinnedLow
- SkinnedMedium
- SkinnedHigh
- UI
- Instanced

Pixel variants:

- Forward
- GBuffer
- ShadowMap
- UI

Terrain and instanced foliage commonly need Default or Instanced vertex variants plus GBuffer and ShadowMap pixel variants. Inspect the shader summary after editor compilation.

## Graph Editing Boundary

The JSON tool can represent and rewrite the editable `GRPH` chunk. It cannot compile HLSL/MSL or synchronize runtime parameter and texture tables. Therefore:

- HMI edits can be final after validation and in-engine review.
- HMAT graph edits are intermediate source edits.
- Saving the HMAT in `mmo_edit` is mandatory after a graph write.
