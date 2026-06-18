# MMO Material File Formats

All integer and float fields are little-endian.

## HTEX 1.0

The texture header is 142 bytes:

| Field | Encoding |
|---|---|
| Magic | `HTEX` |
| Version | `uint32`, currently `0x0100` |
| Pixel format | `uint8` |
| Has mips | `uint8` |
| Width, height | two `uint16` |
| Mip offsets | sixteen `uint32` |
| Mip lengths | sixteen `uint32` |

Pixel format IDs:

| ID | Format |
|---:|---|
| 0 | RGB; current importer commonly stores four bytes per pixel |
| 1 | RGBA |
| 2 | DXT1 / BC1 |
| 3 | DXT5 / BC3 |
| 4 | float RGB |
| 5 | float RGBA |
| 6 | R8 |
| 7 | RG8 |
| 8 | BC4 |
| 9 | BC5 |

`htex_tool.py preview` supports every format above. BC5 and RG8 are displayed as reconstructed normal maps.

## Chunked Material Files

Each chunk is:

```text
char magic[4]
uint32 payload_size
byte payload[payload_size]
```

The tools preserve unrecognized chunks exactly.

## HMAT

The first chunk is `HMAT`, containing a `uint32` version. Known versions are `0x0100` through `0x0500`.

Common chunks:

| Magic | Purpose |
|---|---|
| `NAME` | `uint8`-length UTF-8 asset name |
| `ATTR` | material flags and material type |
| `TEXT` | referenced texture list |
| `SPAR` | scalar parameter defaults |
| `VPAR` | vector parameter defaults |
| `TPAR` | texture parameter defaults |
| `VRTX` | compiled vertex shader variants |
| `PIXL` | compiled pixel shader variants |
| `GRPH` | editable material graph |

Version `0x0500` groups shader bytecode by platform. Older versions use profile strings such as `D3D_SM5`.

### GRPH

The graph begins with:

```text
uint32 node_count
uint32 next_generated_id
uint32 root_node_id  // 0xffffffff means none
```

Each node stores its FNV-1a type ID, node state, ordered pins, links, and ordered property values. The property schema is positional and comes from `material_node.h`. Material-function nodes additionally store their `.hmf` path before normal node data; their pin lists come from the referenced function's `INPS` and `OUTP` chunks.

Graph changes do not update `VRTX`, `PIXL`, `TEXT`, `SPAR`, `VPAR`, or `TPAR`. Only `mmo_edit` compilation should regenerate those runtime chunks.

## HMI

The first chunk is `HMIT`, currently version `0x0100`.

| Magic | Purpose |
|---|---|
| `NAME` | instance asset name |
| `PRNT` | parent HMAT or HMI asset path |
| `ATTR` | inherited/render-state attributes |
| `SPAR` | scalar overrides |
| `VPAR` | vector overrides |
| `TPAR` | texture overrides |

HMI data is self-contained and can be rewritten safely when parameter names and parent relationships are valid.

## JSON Safety

Exported JSON includes:

- `schema`
- `kind`
- `source`
- `source_sha256`
- parsed known fields
- chunk offsets and sizes

`apply-json` verifies the source hash by default. `--force` bypasses that protection and should only be used after manually reconciling changes.
