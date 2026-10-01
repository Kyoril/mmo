# World Dressing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let an agent dress confirmed atlas places with props (`.wobj`) and trees (`.hfol`) on its own, with per-pass undo, placement checks, walkability checks and visual review packets. Then dress five sites on the Oakenshire ring.

**Architecture:**
- Python extends the spec-1 `worldkit` package under `tools/world/`:
  - binary readers and writers;
  - an asset catalog with an offline numpy renderer;
  - a prop linter and a template placer;
  - a pass model with manifests, and a `dress.py` CLI.
- One small C++ console tool, `src/nav_query`, wraps `mmo::nav::Map` for path and on-mesh queries.
- The existing `nav_builder` builds a scratch navmesh into `generated/world/nav/`.

**Tech Stack:**
- Python 3.14 with numpy and Pillow. The gate's interpreter is `python`; in Claude sessions use `py -3.14`.
- unittest.
- C++17 (cxxopts, nlohmann/json, Detour via `nav_mesh`), built with CMake and `MMO_BUILD_TOOLS=ON`.

**Spec:** [docs/superpowers/specs/2026-10-01-world-dressing-design.md](../specs/2026-10-01-world-dressing-design.md)

## Global Constraints

- **Branch:**
  - Work on `feature/world-dressing`, created from `feature/content-foundation`, which carries worldkit.
  - Never push.
  - Merge only through `/gate` + `/ship`.
- **Copyright header:** every new source file starts with `# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`, or the `//` form in C++. In Python files with a shebang, the header goes after the shebang.
- **Python style:**
  - worldkit modules use 4-space indents.
  - Test files in `tools/tests/` use tabs, like the existing ones.
  - Tests import fixtures as `import world_fixtures as fx`.
- **C++ style:** Allman braces; tabs; `m_camelCase` members; PascalCase methods; camelCase free functions in anonymous namespaces; `#pragma once`.
- **Running tests:**
  - All world tests: `cd tools/tests; py -3.14 -m unittest discover -s . -p "test_world_*.py"`.
  - One file: `cd tools/tests; py -3.14 -m unittest test_world_dressing_formats -v`.
- **Generated files:**
  - Floats in drafts and manifests are rounded to 0.01.
  - JSON is written UTF-8, 2-space indent, `ensure_ascii=False`, with a trailing newline.
- **Never commit `data/client`.** Passes write into it, but the user commits accepted passes. Manifests under `data/world/passes/` are committed on the feature branch.
- **mmo_edit must be closed** for `apply`, `undo` and `publish-nav`. If it is open, ask the user to close it. Never kill the process.
- **The real navmesh is the user's.** `data/editor/nav` is only rebuilt by `dress.py publish-nav --yes`, when the user asks for it.
- **Magics as stored on disk** (compare raw bytes):

  | Format | Magics |
  |---|---|
  | `.wobj` | `REVW`, `HSMW`, `OMWW` |
  | `.hfol` | `REVF`, `HSMF`, `SNIF` |
  | `.hmsh` | `MESH`, `VERT`, `INDX`, `SUBM`, `TAGS`, `LEKS`, `LLOC` |
  | `.hwmo` | `REVM`, `DHOM`, `PGOM`, and inside a group `MNGM` and `FRMM` |

- **Deviations from the spec:**
  1. **The renderer draws triangles two-sided.** A z-buffer resolves visibility, so there is no back-face culling. This is robust to mirrored meshes and gives the same image for closed meshes.
  2. **Catalog footprints are used only by the prop lint.** The spawn lint from spec 1 keeps its heuristic footprints, so the placement baseline stays stable. The spec's §4.6 sentence is read as applying to props.

## File map

| Path | Responsibility |
|---|---|
| `tools/world/worldkit/formats/chunks.py` (modify) | adds `Cursor.raw`, `Cursor.strz32` and the byte builders `chunk_bytes`, `str8_bytes`, `str16_bytes` |
| `tools/world/worldkit/constants.py` (modify) | adds `entity_page_index(x, z)` |
| `tools/world/worldkit/paths.py` (modify) | client, foliage, asset-cache, pass, manifest, review, template and material-script locations |
| `tools/world/worldkit/formats/hfol.py` | `.hfol` read, write, append and remove |
| `tools/world/worldkit/formats/wobj.py` (modify) | `.wobj` writer, entity file path, unique ids |
| `tools/world/worldkit/formats/hmsh.py` | `.hmsh` geometry, materials, collision flag |
| `tools/world/worldkit/formats/hwmo.py` | `.hwmo` bounds and group mesh references |
| `tools/world/worldkit/geometry.py` | quaternions, yaw/tilt, TRS matrices |
| `tools/world/worldkit/materials.py` | resolves a material's base texture and decodes it (reuses the material skill's scripts) |
| `tools/world/worldkit/tags.py` + `tools/world/asset_tags.json` | tag rules and per-asset placement defaults |
| `tools/world/worldkit/assets.py` | asset catalog and its cache, asset selection, geometry cache |
| `tools/world/worldkit/meshrender.py` | numpy rasterizer |
| `tools/world/worldkit/foliage.py` | per-world foliage loading |
| `tools/world/worldkit/previews.py` | contact sheets, asset views, site previews |
| `tools/world/worldkit/prop_lint.py` | prop placement checks |
| `tools/world/worldkit/templates.py` + `tools/world/templates/*.json` | site templates and the placer |
| `tools/world/worldkit/dressing.py` | pass documents, fingerprint, editor guard, apply, undo |
| `tools/world/worldkit/nav.py` + `tools/world/nav_routes.json` | scratch nav build, `nav_query` client, walkability checks |
| `tools/world/worldkit/packet.py` | review packets |
| `tools/world/worldkit/dress_commands.py` | plan, check, nav-check and render orchestration |
| `tools/world/dress.py` | the CLI |
| `tools/world/prop_lint.py` | world prop lint CLI, used by the audit |
| `src/nav_query/` | the C++ query tool |
| `tools/gate/content_audit.py` (modify) | `props` domain |
| `.agents/skills/world-dresser/SKILL.md` | the agent workflow |
| `tools/tests/test_world_dressing_*.py`, `tools/tests/world_fixtures.py` (modify) | tests and fixtures |

---

### Task 1: Branch, chunk helpers and the `.hfol` format

**Files:**
- Modify: `tools/world/worldkit/formats/chunks.py` (append helpers)
- Modify: `tools/world/worldkit/constants.py` (append `entity_page_index`)
- Modify: `tools/world/worldkit/paths.py` (append path helpers)
- Create: `tools/world/worldkit/formats/hfol.py`
- Modify: `tools/tests/world_fixtures.py` (append `hfol_bytes`)
- Test: `tools/tests/test_world_dressing_formats.py`

**Interfaces:**
- Produces:
  - `chunks.chunk_bytes(magic: bytes, payload: bytes) -> bytes`, `str8_bytes(text) -> bytes`, `str16_bytes(text) -> bytes`;
  - `Cursor.raw(count) -> memoryview`, `Cursor.strz32() -> str`;
  - `constants.entity_page_index(x, z) -> int`;
  - `paths.client_root(repo)`, `foliage_dir(directory, repo)`, `assets_cache_dir(repo)`, `passes_dir(repo)`, `manifests_dir(repo)`, `review_dir(repo)`, `templates_dir(repo)`, `material_scripts(repo)`, `world_tools(repo)`;
  - `hfol.FoliageInstance(unique_id, mesh, position, rotation, scale, collides=True)`, `hfol.FoliageFile(version, meshes, instances)`, `parse_hfol(data, source) -> FoliageFile`, `write_hfol(ff) -> bytes`, `append_instances(ff, new) -> FoliageFile`, `remove_instances(ff, ids) -> FoliageFile`, `load_hfol(path) -> FoliageFile`, `instance_hash(inst) -> str`.

- [ ] **Step 1: Create the branch**

```bash
git checkout feature/content-foundation
git checkout -b feature/world-dressing
```

- [ ] **Step 2: Add the fixture builder.** Append to `tools/tests/world_fixtures.py`:

```python
def hfol_bytes(meshes, instances, version=2) -> bytes:
	"""Builds a .hfol file. instances: iterable of (unique_id, mesh_index, position, rotation(w,x,y,z), scale, collides)."""
	names = b"".join(str16(m) for m in meshes)
	body = struct.pack("<I", len(instances))
	for unique_id, index, position, rotation, scale, collides in instances:
		body += struct.pack("<QI", unique_id, index) + struct.pack("<3f", *position) + struct.pack("<4f", *rotation)
		body += struct.pack("<3f", *scale)
		if version >= 2:
			body += struct.pack("<B", 1 if collides else 0)
	return chunk(b"REVF", struct.pack("<I", version)) + chunk(b"HSMF", names) + chunk(b"SNIF", body)
```

- [ ] **Step 3: Write the failing tests.** Create `tools/tests/test_world_dressing_formats.py`:

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the dressing formats: .hfol foliage, the .wobj writer, .hmsh meshes and .hwmo world models.

	python tools/tests/test_world_dressing_formats.py
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit.formats.chunks import FormatError  # noqa: E402
from worldkit.formats.hfol import FoliageInstance, append_instances, parse_hfol, remove_instances, write_hfol  # noqa: E402
from worldkit.paths import foliage_dir  # noqa: E402

TREES = ["Models/Trees/A.hmsh", "Models/Trees/B.hmsh"]
IDENTITY = (1.0, 0.0, 0.0, 0.0)


def sample_instances(second_collides=False):
	return [
		(11, 0, (1.0, 2.0, 3.0), IDENTITY, (1.0, 1.0, 1.0), True),
		(12, 1, (4.5, 0.25, -7.0), (0.70710677, 0.0, 0.70710677, 0.0), (1.5, 1.5, 1.5), second_collides),
		(13, 0, (9.0, 1.0, 9.0), IDENTITY, (0.8, 0.8, 0.8), True),
	]


class FoliageFormatTests(unittest.TestCase):
	def test_round_trip_is_byte_identical(self):
		data = fx.hfol_bytes(TREES, sample_instances())
		self.assertEqual(write_hfol(parse_hfol(data)), data)

	def test_parse_fields(self):
		ff = parse_hfol(fx.hfol_bytes(TREES, sample_instances()))
		self.assertEqual(ff.meshes, TREES)
		second = ff.instances[1]
		self.assertEqual((second.unique_id, second.mesh, second.collides), (12, "Models/Trees/B.hmsh", False))
		self.assertAlmostEqual(second.position[0], 4.5)

	def test_version_1_is_upgraded_to_version_2(self):
		ff = parse_hfol(fx.hfol_bytes(TREES, sample_instances(), version=1))
		self.assertTrue(all(i.collides for i in ff.instances))
		self.assertEqual(write_hfol(ff), fx.hfol_bytes(TREES, sample_instances(second_collides=True)))

	def test_append_then_remove_restores_the_original_bytes(self):
		data = fx.hfol_bytes(TREES, sample_instances())
		new = [FoliageInstance(99, "Models/Trees/C.hmsh", (0.0, 0.0, 0.0), IDENTITY, (1.0, 1.0, 1.0)),
			   FoliageInstance(98, "Models/Trees/A.hmsh", (5.0, 0.0, 5.0), IDENTITY, (1.0, 1.0, 1.0))]
		grown = append_instances(parse_hfol(data), new)
		self.assertEqual(grown.meshes, TREES + ["Models/Trees/C.hmsh"])
		self.assertEqual([i.unique_id for i in grown.instances[-2:]], [99, 98])
		self.assertEqual(write_hfol(remove_instances(parse_hfol(write_hfol(grown)), {99, 98})), data)

	def test_remove_keeps_mesh_names_still_in_use(self):
		ff = parse_hfol(fx.hfol_bytes(TREES, sample_instances()))
		self.assertEqual(remove_instances(ff, {11}).meshes, TREES)
		self.assertEqual(remove_instances(ff, {12}).meshes, ["Models/Trees/A.hmsh"])

	def test_duplicate_ids_and_bad_mesh_indices_are_rejected(self):
		ff = parse_hfol(fx.hfol_bytes(TREES, sample_instances()))
		with self.assertRaises(ValueError):
			append_instances(ff, [FoliageInstance(11, "Models/Trees/A.hmsh", (0.0, 0.0, 0.0), IDENTITY, (1.0, 1.0, 1.0))])
		with self.assertRaises(FormatError):
			parse_hfol(fx.hfol_bytes(["A"], [(1, 5, (0.0, 0.0, 0.0), IDENTITY, (1.0, 1.0, 1.0), True)]))

	@fx.requires_live_data
	def test_shipped_foliage_round_trips(self):
		for path in sorted(foliage_dir("Development").glob("*.hfol")):
			data = path.read_bytes()
			ff = parse_hfol(data, str(path))
			if ff.version == 2:
				self.assertEqual(write_hfol(ff), data, path.name)


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 4: Run the tests to verify they fail**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_formats -v`
Expected: ERROR, `ModuleNotFoundError: No module named 'worldkit.formats.hfol'`.

- [ ] **Step 5: Add the chunk helpers.** Append to `tools/world/worldkit/formats/chunks.py`. The two `Cursor` methods go inside the `Cursor` class:

```python
    def raw(self, count: int) -> memoryview:
        """The next `count` bytes as-is (vertex and index blocks)."""
        return self._take(count)

    def strz32(self) -> str:
        """u32 length, that many bytes, then one NUL not counted in the length (world-model strings)."""
        text = bytes(self._take(self.u32())).decode("utf-8")
        if self.u8() != 0:
            raise FormatError(f"{self._source}: chunk {self._magic!r} string is not NUL-terminated")
        return text


def chunk_bytes(magic: bytes, payload: bytes) -> bytes:
    """One chunk as the engine's ChunkWriter writes it: 4 magic bytes, u32 payload size, payload."""
    return magic + struct.pack("<I", len(payload)) + payload


def str8_bytes(text: str) -> bytes:
    raw = text.encode("utf-8")
    return struct.pack("<B", len(raw)) + raw


def str16_bytes(text: str) -> bytes:
    raw = text.encode("utf-8")
    return struct.pack("<H", len(raw)) + raw
```

- [ ] **Step 6: Add the page index and path helpers.** Append to `tools/world/worldkit/constants.py`:

```python
def entity_page_index(x: float, z: float) -> int:
    """Folder index of the page holding world point (x, z): (page_x << 8) | page_z, as mmo_edit names them."""
    page_x, page_z = page_of(x, z)
    return (page_x << 8) | page_z
```

Append to `tools/world/worldkit/paths.py`:

```python
def client_root(repo: Path = REPO) -> Path:
    return repo / "data" / "client"


def foliage_dir(directory: str, repo: Path = REPO) -> Path:
    return world_root(directory, repo) / "Foliage"


def world_tools(repo: Path = REPO) -> Path:
    return repo / "tools" / "world"


def templates_dir(repo: Path = REPO) -> Path:
    return world_tools(repo) / "templates"


def assets_cache_dir(repo: Path = REPO) -> Path:
    return repo / "generated" / "world" / "assets"


def passes_dir(repo: Path = REPO) -> Path:
    """Drafts, previews and scratch output of dressing passes (gitignored)."""
    return repo / "generated" / "world" / "passes"


def manifests_dir(repo: Path = REPO) -> Path:
    """Tracked manifests of applied dressing passes."""
    return repo / "data" / "world" / "passes"


def review_dir(repo: Path = REPO) -> Path:
    return repo / "generated" / "world" / "review"


def material_scripts(repo: Path = REPO) -> Path:
    """The material skill's scripts (material_tool.py, htex_tool.py), reused for textures."""
    return repo / ".agents" / "skills" / "mmo-material-editor" / "scripts"
```

- [ ] **Step 7: Implement `.hfol`.** Create `tools/world/worldkit/formats/hfol.py`:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Per-page foliage instances (.hfol), mirroring src/shared/game_common/world_foliage.cpp.

Layout (magics as stored on disk; the C++ MakeChunkMagic literals appear byte-reversed):
  REVF  u32 version (1 or 2)
  HSMF  mesh names: str16 repeated until the chunk ends (no count)
  SNIF  u32 n, then n x (u64 id, u32 mesh index, 3f position, 4f rotation (w,x,y,z), 3f scale,
        version 2 only: u8 collides)
mmo_edit writes version 2 with the mesh-name table in first-use order of the instances, so appending
new instances (and new names) at the end produces exactly the file the editor itself would write.
"""

from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path

from .chunks import Cursor, FormatError, chunk_bytes, iter_chunks, str16_bytes

VERSION = 2
_INSTANCE = struct.Struct("<QI3f4f3fB")


@dataclass(frozen=True)
class FoliageInstance:
    unique_id: int
    mesh: str
    position: tuple[float, float, float]
    rotation: tuple[float, float, float, float]   # (w, x, y, z)
    scale: tuple[float, float, float]
    collides: bool = True


@dataclass
class FoliageFile:
    version: int
    meshes: list[str]
    instances: list[FoliageInstance]


def parse_hfol(data: bytes, source: str = "<hfol>") -> FoliageFile:
    chunks = iter_chunks(data, source)
    if not chunks or chunks[0][0] != b"REVF":
        raise FormatError(f"{source}: expected a REVF version chunk first")
    version = Cursor(chunks[0][1], source, b"REVF").u32()
    if version not in (1, 2):
        raise FormatError(f"{source}: unsupported foliage version {version} (known: 1, 2)")
    meshes: list[str] = []
    instances: list[FoliageInstance] = []
    for magic, payload in chunks[1:]:
        cur = Cursor(payload, source, magic)
        if magic == b"HSMF":
            while not cur.done():
                meshes.append(cur.str16())
        elif magic == b"SNIF":
            for _ in range(cur.u32()):
                unique_id, index = cur.u64(), cur.u32()
                position = (cur.f32(), cur.f32(), cur.f32())
                rotation = (cur.f32(), cur.f32(), cur.f32(), cur.f32())
                scale = (cur.f32(), cur.f32(), cur.f32())
                collides = cur.u8() != 0 if version >= 2 else True
                if index >= len(meshes):
                    raise FormatError(f"{source}: instance {unique_id} uses mesh index {index} of {len(meshes)}")
                instances.append(FoliageInstance(unique_id, meshes[index], position, rotation, scale, collides))
            if not cur.done():
                raise FormatError(f"{source}: {cur.remaining()} trailing bytes in SNIF")
        # Unknown chunks are skipped, like the engine's loader (m_ignoreUnhandledChunks).
    return FoliageFile(version, meshes, instances)


def write_hfol(ff: FoliageFile) -> bytes:
    """Writes version 2. Every instance's mesh must be in ff.meshes."""
    index = {name: i for i, name in enumerate(ff.meshes)}
    names = b"".join(str16_bytes(name) for name in ff.meshes)
    body = struct.pack("<I", len(ff.instances))
    for inst in ff.instances:
        body += _INSTANCE.pack(inst.unique_id, index[inst.mesh], *inst.position, *inst.rotation, *inst.scale,
                               1 if inst.collides else 0)
    return chunk_bytes(b"REVF", struct.pack("<I", VERSION)) + chunk_bytes(b"HSMF", names) + chunk_bytes(b"SNIF", body)


def append_instances(ff: FoliageFile, new: list[FoliageInstance]) -> FoliageFile:
    taken = {i.unique_id for i in ff.instances}
    meshes = list(ff.meshes)
    for inst in new:
        if inst.unique_id in taken:
            raise ValueError(f"foliage instance id {inst.unique_id} already exists")
        taken.add(inst.unique_id)
        if inst.mesh not in meshes:
            meshes.append(inst.mesh)
    return FoliageFile(VERSION, meshes, [*ff.instances, *new])


def remove_instances(ff: FoliageFile, ids: set[int]) -> FoliageFile:
    """Drops the given instances; a mesh name goes only when a removed instance used it and none is left."""
    kept = [i for i in ff.instances if i.unique_id not in ids]
    removed_meshes = {i.mesh for i in ff.instances if i.unique_id in ids}
    still_used = {i.mesh for i in kept}
    meshes = [m for m in ff.meshes if m not in removed_meshes or m in still_used]
    return FoliageFile(VERSION, meshes, kept)


def load_hfol(path: Path) -> FoliageFile:
    """The page's foliage, or an empty version-2 file when the page has none yet."""
    path = Path(path)
    if not path.is_file():
        return FoliageFile(VERSION, [], [])
    return parse_hfol(path.read_bytes(), str(path))


def instance_hash(inst: FoliageInstance) -> str:
    """Identity of an instance's placement at float32 precision (what the file stores)."""
    packed = _INSTANCE.pack(inst.unique_id, 0, *inst.position, *inst.rotation, *inst.scale, 1 if inst.collides else 0)
    return hashlib.sha1(inst.mesh.encode("utf-8") + packed).hexdigest()
```

- [ ] **Step 8: Run the tests to verify they pass**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_formats -v`
Expected: all `FoliageFormatTests` pass, including the live round trip when `data/client` is checked out.

- [ ] **Step 9: Run the whole world suite.** The snapshot code digest includes `formats/*.py`, so caches rebuild, and nothing else may break.

Run: `cd tools/tests; py -3.14 -m unittest discover -s . -p "test_world_*.py"`
Expected: OK.

- [ ] **Step 10: Commit**

```bash
git add tools/world/worldkit/formats/chunks.py tools/world/worldkit/formats/hfol.py tools/world/worldkit/constants.py tools/world/worldkit/paths.py tools/tests/world_fixtures.py tools/tests/test_world_dressing_formats.py
git commit -m "feat(worldkit): .hfol foliage reader/writer with append and exact undo"
```

---

### Task 2: `.wobj` writer

**Files:**
- Modify: `tools/world/worldkit/formats/wobj.py`
- Test: `tools/tests/test_world_dressing_formats.py` (add a class)

**Interfaces:**
- Consumes: `chunk_bytes`, `str8_bytes`, `str16_bytes` (Task 1); `entity_page_index` (Task 1); `paths.entities_dir`.
- Produces:
  - `wobj_bytes(*, kind, unique_id, asset, position, rotation, scale, name="", category="") -> bytes`;
  - `entity_file(directory, unique_id, x, z, repo=REPO) -> Path`;
  - `new_unique_id(taken: set[int], rng: random.Random) -> int`.

- [ ] **Step 1: Write the failing tests.** Add these imports to the top of `test_world_dressing_formats.py`:

```python
import random
import tempfile

from worldkit.formats.wobj import entity_file, new_unique_id, parse_wobj, wobj_bytes  # noqa: E402
```

and this class before `if __name__`:

```python
class EntityWriterTests(unittest.TestCase):
	def test_mesh_round_trip(self):
		with tempfile.TemporaryDirectory() as tmp:
			path = Path(tmp) / "1.wobj"
			path.write_bytes(wobj_bytes(kind="mesh", unique_id=0xABCDEF0123, asset="Models/Desert/Rocks/R.hmsh",
										position=(10.5, 2.25, -3.0), rotation=(0.9238795, 0.0, 0.3826834, 0.0),
										scale=(1.2, 1.2, 1.2), category="dressing/quarry"))
			entity = parse_wobj(path)
		self.assertEqual((entity.kind, entity.unique_id, entity.asset, entity.category, entity.material_overrides),
						 ("mesh", 0xABCDEF0123, "Models/Desert/Rocks/R.hmsh", "dressing/quarry", ()))
		self.assertAlmostEqual(entity.rotation[2], 0.3826834, places=6)
		self.assertAlmostEqual(entity.scale[0], 1.2, places=6)

	def test_layout_matches_the_editor_writer_fixture(self):
		for kind, asset in (("mesh", "Models/Test/Crate.hmsh"), ("wmo", "Models/Mine/Mine_01.hwmo")):
			mine = wobj_bytes(kind=kind, unique_id=7, asset=asset, position=(1.0, 2.0, 3.0), rotation=(1.0, 0.0, 0.0, 0.0),
							  scale=(1.0, 1.0, 1.0), name="Crate", category="Props")
			self.assertEqual(mine, fx.wobj_bytes(kind=kind, unique_id=7, asset=asset, name="Crate", category="Props"), kind)

	def test_entity_file_uses_the_editor_page_folders(self):
		path = entity_file("W", 42, 100.0, 600.0, repo=Path("/r"))
		self.assertEqual(path.name, "42.wobj")
		self.assertEqual(path.parent.name, str((32 << 8) | 33))   # x 100 -> page 32, z 600 -> page 33

	def test_unique_ids_are_fresh_and_nonzero(self):
		taken = {1, 2}
		rng = random.Random(3)
		ids = {new_unique_id(taken, rng) for _ in range(200)}
		self.assertEqual(len(ids), 200)
		self.assertTrue(ids.isdisjoint({0, 1, 2}))
		self.assertTrue(ids <= taken)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_formats.EntityWriterTests -v`
Expected: ImportError for `wobj_bytes`.

- [ ] **Step 3: Implement the writer.** In `tools/world/worldkit/formats/wobj.py`:
  - Change the docstring's first line to `"""Placed world entity (.wobj) parser and writer, mirroring src/shared/game_common/world_entity_loader.cpp and mmo_edit's Save.`
  - Extend the imports:

```python
import random
import struct
import time

from ..constants import entity_page_index
from ..paths import REPO, entities_dir
from .chunks import Cursor, FormatError, chunk_bytes, iter_chunks, str8_bytes, str16_bytes
```

  - Append:

```python
def wobj_bytes(*, kind: str, unique_id: int, asset: str, position, rotation, scale, name: str = "",
               category: str = "") -> bytes:
    """A version-3 entity file exactly as mmo_edit writes it (no material overrides)."""
    if kind not in ("mesh", "wmo"):
        raise ValueError(f"unknown entity kind {kind!r}")
    body = struct.pack("<Q", unique_id) + str16_bytes(asset)
    body += struct.pack("<3f4f3f", *position, *rotation, *scale)
    if kind == "mesh":
        body += struct.pack("<B", 0)
    body += str8_bytes(name) + str16_bytes(category)
    return chunk_bytes(b"REVW", struct.pack("<I", 3)) + chunk_bytes(b"HSMW" if kind == "mesh" else b"OMWW", body)


def entity_file(directory: str, unique_id: int, x: float, z: float, repo: Path = REPO) -> Path:
    """Entities/<pageIndex>/<uniqueId>.wobj; the folder is the page of the entity's position."""
    return entities_dir(directory, repo) / str(entity_page_index(x, z)) / f"{unique_id}.wobj"


def new_unique_id(taken: set[int], rng: random.Random) -> int:
    """mmo_edit's scheme (EntityFactory::GenerateUniqueId): 16 bits of the ms clock over 48 random bits.
    The id is added to `taken`."""
    while True:
        value = ((int(time.time() * 1000) & 0xFFFF) << 48) | rng.getrandbits(48)
        if value and value not in taken:
            taken.add(value)
            return value
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_formats -v`
Expected: OK.

- [ ] **Step 5: Commit**

```bash
git add tools/world/worldkit/formats/wobj.py tools/tests/test_world_dressing_formats.py
git commit -m "feat(worldkit): .wobj writer in the editor's layout"
```

---

### Task 3: Mesh (`.hmsh`) and world-model (`.hwmo`) readers

**Files:**
- Create: `tools/world/worldkit/formats/hmsh.py`
- Create: `tools/world/worldkit/formats/hwmo.py`
- Modify: `tools/tests/world_fixtures.py` (append the builders)
- Test: `tools/tests/test_world_dressing_formats.py` (add classes)

**Interfaces:**
- Produces:
  - `hmsh.Submesh(name, material, positions (n,3) f32, uvs (n,2) f32, indices (m,) u32)`;
  - `hmsh.MeshData(version, submeshes, has_collision)`, with `.bounds -> (min ndarray(3), max ndarray(3))`;
  - `hmsh.parse_hmsh(path) -> MeshData`;
  - `hwmo.MeshRef(mesh, position, rotation, scale, visible)`;
  - `hwmo.WorldModelData(version, bounds_min, bounds_max, mesh_refs)`;
  - `hwmo.parse_hwmo(path) -> WorldModelData`.

- [ ] **Step 1: Add the fixture builders.** Append to `tools/tests/world_fixtures.py`:

```python
def vertex_block(positions, uvs=None) -> bytes:
	"""u32 count + 64-byte vertices: pos 3f, colour u32, uv 2f, w f, normal 3f, binormal 3f, tangent 3f."""
	data = b""
	for i, p in enumerate(positions):
		uv = uvs[i] if uvs is not None else (0.0, 0.0)
		data += struct.pack("<3fI2ff3f3f3f", *p, 0xFFFFFFFF, *uv, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0)
	return struct.pack("<I", len(positions)) + data


def hmsh_bytes(submeshes, version=0x301, collision=True) -> bytes:
	"""submeshes: iterable of (name, material, positions, uvs, indices, index32). Mirrors MeshSerializer (v3 SUBM)."""
	parts = [chunk(b"MESH", struct.pack("<I", version))]
	if collision:
		parts.append(chunk(b"LLOC", b"1HVB" + b"\x00" * 12))
	for name, material, positions, uvs, indices, index32 in submeshes:
		body = str8(name) + str16(material) + struct.pack("<B", 0)
		if version >= 0x301:
			body += struct.pack("<B", 1)
		body += vertex_block(positions, uvs)
		body += struct.pack("<BIB", 1, len(indices), 1 if index32 else 0)
		body += struct.pack(f"<{len(indices)}{'I' if index32 else 'H'}", *indices)
		body += struct.pack("<I", 0)
		parts.append(chunk(b"SUBM", body))
	return b"".join(parts)


def legacy_hmsh_bytes(positions, indices, material) -> bytes:
	"""Version 0x200: shared VERT, INDX (flag 1 = 16-bit) and an index-range SUBM."""
	return (chunk(b"MESH", struct.pack("<I", 0x200)) + chunk(b"VERT", vertex_block(positions))
			+ chunk(b"INDX", struct.pack("<IB", len(indices), 1) + struct.pack(f"<{len(indices)}H", *indices))
			+ chunk(b"SUBM", str16(material) + struct.pack("<II", 0, len(indices))))


def strz32(text: str) -> bytes:
	raw = text.encode("utf-8")
	return struct.pack("<I", len(raw)) + raw + b"\x00"


def hwmo_bytes(bounds_min, bounds_max, refs) -> bytes:
	"""refs: iterable of (mesh, position, rotation(w,x,y,z), scale). One group with one FRMM."""
	header = struct.pack("<8I", 0, 1, 0, 0, 0, 0, 0, 0) + struct.pack("<3f", *bounds_min) + struct.pack("<3f", *bounds_max)
	header += struct.pack("<II", 0, 0)
	frmm = struct.pack("<I", len(refs))
	for mesh, position, rotation, scale in refs:
		frmm += strz32(mesh) + strz32("") + strz32("") + struct.pack("<3f4f3fB", *position, *rotation, *scale, 1)
	group = b"\x00" * 68 + chunk(b"MNGM", b"Group_01\x00") + chunk(b"FRMM", frmm)
	return chunk(b"REVM", struct.pack("<I", 0x200)) + chunk(b"DHOM", header) + chunk(b"PGOM", group)


CUBE_POSITIONS = [(x, y, z) for x in (-1.0, 1.0) for y in (0.0, 2.0) for z in (-1.0, 1.0)]
CUBE_INDICES = [0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5, 0, 4, 5, 0, 5, 1, 2, 3, 7, 2, 7, 6, 0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3]


def cube_hmsh(material="Textures/Test/Cube.hmi", collision=True, size=1.0) -> bytes:
	"""A 2x2x2 cube (scaled by size) standing on y = 0, one submesh."""
	positions = [(x * size, y * size, z * size) for x, y, z in CUBE_POSITIONS]
	return hmsh_bytes([("Cube0", material, positions, None, CUBE_INDICES, False)], collision=collision)
```

- [ ] **Step 2: Write the failing tests.** Add to the imports of `test_world_dressing_formats.py`:

```python
from worldkit.formats.hmsh import parse_hmsh  # noqa: E402
from worldkit.formats.hwmo import parse_hwmo  # noqa: E402
from worldkit.paths import client_root  # noqa: E402
```

and the classes:

```python
def write(tmp: str, name: str, data: bytes) -> Path:
	path = Path(tmp) / name
	path.write_bytes(data)
	return path


class MeshReaderTests(unittest.TestCase):
	def setUp(self):
		self._tmp = tempfile.TemporaryDirectory()
		self.tmp = self._tmp.name

	def tearDown(self):
		self._tmp.cleanup()

	def test_cube_bounds_material_and_collision(self):
		mesh = parse_hmsh(write(self.tmp, "c.hmsh", fx.cube_hmsh()))
		lo, hi = mesh.bounds
		self.assertEqual(lo.tolist(), [-1.0, 0.0, -1.0])
		self.assertEqual(hi.tolist(), [1.0, 2.0, 1.0])
		self.assertTrue(mesh.has_collision)
		(sub,) = mesh.submeshes
		self.assertEqual((sub.material, len(sub.positions), len(sub.indices)), ("Textures/Test/Cube.hmi", 8, 36))

	def test_32bit_indices_uvs_and_no_collision(self):
		data = fx.hmsh_bytes([("S", "M.hmi", [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 0.0, 1.0)],
							   [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0)], [0, 1, 2], True)], collision=False)
		mesh = parse_hmsh(write(self.tmp, "t.hmsh", data))
		self.assertFalse(mesh.has_collision)
		self.assertEqual(mesh.submeshes[0].indices.tolist(), [0, 1, 2])
		self.assertEqual(mesh.submeshes[0].uvs[1].tolist(), [1.0, 0.0])

	def test_legacy_shared_vertex_layout(self):
		mesh = parse_hmsh(write(self.tmp, "l.hmsh", fx.legacy_hmsh_bytes(fx.CUBE_POSITIONS, fx.CUBE_INDICES, "Old.hmi")))
		self.assertEqual((mesh.version, mesh.submeshes[0].material, len(mesh.submeshes[0].indices)), (0x200, "Old.hmi", 36))

	def test_broken_files_raise(self):
		bad_index = fx.hmsh_bytes([("S", "M", [(0.0, 0.0, 0.0)] * 3, None, [0, 1, 7], False)])
		with self.assertRaises(FormatError):
			parse_hmsh(write(self.tmp, "b.hmsh", bad_index))
		with self.assertRaises(FormatError):
			parse_hmsh(write(self.tmp, "x.hmsh", fx.cube_hmsh()[:-10]))

	def test_world_model_bounds_and_refs(self):
		data = fx.hwmo_bytes((-25.0, -1.0, -39.0), (25.0, 12.0, 40.0),
							 [("Models/Mine/gold_mine_greybox.hmsh", (1.0, 0.0, 2.0), (1.0, 0.0, 0.0, 0.0), (1.0, 1.0, 1.0))])
		model = parse_hwmo(write(self.tmp, "m.hwmo", data))
		self.assertEqual((model.bounds_min, model.bounds_max), ((-25.0, -1.0, -39.0), (25.0, 12.0, 40.0)))
		self.assertEqual([r.mesh for r in model.mesh_refs], ["Models/Mine/gold_mine_greybox.hmsh"])
		self.assertEqual(model.mesh_refs[0].position, (1.0, 0.0, 2.0))

	@fx.requires_live_data
	def test_shipped_tent_and_mine(self):
		tent = parse_hmsh(client_root() / "Models/FalwynPlains/Props/Camp/SM_hc_Camptent_B.hmsh")
		self.assertTrue(tent.has_collision)
		self.assertEqual(len(tent.submeshes), 2)
		self.assertEqual(tent.submeshes[0].material, "Textures/FalwynPlains/Props/CampTent.hmi")
		self.assertEqual(len(tent.submeshes[0].positions), 1050)
		mine = parse_hwmo(client_root() / "Models/Mine/Mine_01.hwmo")
		self.assertIn("Models/Mine/gold_mine_greybox.hmsh", [r.mesh for r in mine.mesh_refs])
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_formats.MeshReaderTests -v`
Expected: ImportError.

- [ ] **Step 4: Implement `hmsh.py`.** Create `tools/world/worldkit/formats/hmsh.py`:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Static mesh (.hmsh) geometry, mirroring src/shared/scene_graph/mesh_serializer.cpp.

Chunks (magic as on disk): MESH (u32 version 0x100/0x200/0x300/0x301), VERT (shared vertices),
INDX (legacy shared indices, version < 0x300; a non-zero flag means 16-bit), LEKS (skeleton name),
LLOC (collision BVH: only its presence matters here), SUBM (one per submesh), TAGS.
Vertices are 64 bytes from version 0x200 (position 3f @0, colour u32 @12, uv 2f @16, w @24, normal,
binormal, tangent) and 40 bytes in 0x100. A v3 SUBM is: str8 name, str16 material, u8 shared,
(0x301: u8 visible), own vertex block unless shared, u8 has indices [u32 count, u8 size (0 = 16-bit,
1 = 32-bit), indices], u32 bone assignments x 10 bytes. Bounds are not stored; they come from vertices.
Pre-chunk legacy files (e.g. Models/Stump_03) raise FormatError.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np

from .chunks import Cursor, FormatError, iter_chunks

VERSIONS = (0x100, 0x200, 0x300, 0x301)


@dataclass
class Submesh:
    name: str
    material: str
    positions: np.ndarray   # (n, 3) float32, model space
    uvs: np.ndarray         # (n, 2) float32
    indices: np.ndarray     # (m,) uint32 triangle list into positions


@dataclass
class MeshData:
    version: int
    submeshes: list[Submesh]
    has_collision: bool

    @property
    def bounds(self) -> tuple[np.ndarray, np.ndarray]:
        points = [s.positions for s in self.submeshes if len(s.positions)]
        if not points:
            return np.zeros(3, np.float32), np.zeros(3, np.float32)
        everything = np.concatenate(points)
        return everything.min(axis=0), everything.max(axis=0)


def _vertices(cur: Cursor, stride: int) -> tuple[np.ndarray, np.ndarray]:
    count = cur.u32()
    raw = np.frombuffer(cur.raw(count * stride), dtype="<f4").reshape(count, stride // 4)
    return raw[:, 0:3].astype(np.float32), raw[:, 4:6].astype(np.float32)


def _indices(cur: Cursor, count: int, sixteen_bit: bool) -> np.ndarray:
    width = 2 if sixteen_bit else 4
    return np.frombuffer(cur.raw(count * width), dtype="<u2" if sixteen_bit else "<u4").astype(np.uint32)


def _check_indices(source: str, sub: Submesh) -> Submesh:
    if len(sub.indices) and int(sub.indices.max()) >= len(sub.positions):
        raise FormatError(f"{source}: submesh {sub.name!r} index {int(sub.indices.max())} >= {len(sub.positions)} vertices")
    return sub


def _submesh_v3(cur: Cursor, version: int, stride: int, shared: tuple[np.ndarray, np.ndarray], source: str) -> Submesh:
    name = cur.str8()
    material = cur.str16()
    uses_shared = cur.u8() != 0
    if version >= 0x301:
        cur.u8()   # visible by default
    positions, uvs = shared if uses_shared else _vertices(cur, stride)
    indices = np.zeros(0, np.uint32)
    if cur.u8():
        count = cur.u32()
        size = cur.u8()
        if size not in (0, 1):
            raise FormatError(f"{source}: submesh {name!r} has unknown index size {size}")
        indices = _indices(cur, count, size == 0)
    cur.raw(cur.u32() * 10)   # bone assignments: u32 vertex, u16 bone, f32 weight
    if not cur.done():
        raise FormatError(f"{source}: {cur.remaining()} trailing bytes in submesh {name!r}")
    return _check_indices(source, Submesh(name, material, positions, uvs, indices))


def parse_hmsh(path: Path) -> MeshData:
    path = Path(path)
    source = str(path)
    chunks = iter_chunks(path.read_bytes(), source)
    if not chunks or chunks[0][0] != b"MESH":
        raise FormatError(f"{source}: not a chunked mesh")
    version = Cursor(chunks[0][1], source, b"MESH").u32()
    if version not in VERSIONS:
        raise FormatError(f"{source}: unsupported mesh version 0x{version:x}")
    stride = 40 if version < 0x200 else 64
    shared = (np.zeros((0, 3), np.float32), np.zeros((0, 2), np.float32))
    shared_indices = np.zeros(0, np.uint32)
    has_collision = False
    submeshes: list[Submesh] = []
    for magic, payload in chunks[1:]:
        cur = Cursor(payload, source, magic)
        if magic == b"VERT":
            shared = _vertices(cur, stride)
        elif magic == b"INDX":
            count = cur.u32()
            shared_indices = _indices(cur, count, cur.u8() != 0)
        elif magic == b"LLOC":
            has_collision = True
        elif magic == b"SUBM":
            if version < 0x300:
                material = cur.str16()
                start, end = cur.u32(), cur.u32()
                sub = Submesh(f"submesh{len(submeshes)}", material, shared[0], shared[1], shared_indices[start:end])
                submeshes.append(_check_indices(source, sub))
            else:
                submeshes.append(_submesh_v3(cur, version, stride, shared, source))
    return MeshData(version, submeshes, has_collision)
```

- [ ] **Step 5: Implement `hwmo.py`.** Create `tools/world/worldkit/formats/hwmo.py`:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""World model (.hwmo) bounds and mesh references, mirroring src/shared/scene_graph/world_model_serializer.cpp.

Every magic is written byte-reversed (REVM, DHOM, PGOM, ...). DHOM (MOHD) is 64 bytes with the model
AABB at offset 32 (min 3f, max 3f). A PGOM (MOGP) group payload is a 68-byte header followed by
sub-chunks; its FRMM (MMRF) sub-chunk lists the group's mesh references: u32 count, then per ref
strz32 mesh, strz32 name, strz32 material override, 3f position, 4f rotation (w,x,y,z), 3f scale,
u8 visible. Doodads (MODD) and child models (RWCM) are not read: previews draw group meshes only.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

from .chunks import Cursor, FormatError, iter_chunks

VERSIONS = (0x200, 0x201)
_GROUP_HEADER = 68


@dataclass(frozen=True)
class MeshRef:
    mesh: str
    position: tuple[float, float, float]
    rotation: tuple[float, float, float, float]   # (w, x, y, z)
    scale: tuple[float, float, float]
    visible: bool


@dataclass
class WorldModelData:
    version: int
    bounds_min: tuple[float, float, float]
    bounds_max: tuple[float, float, float]
    mesh_refs: list[MeshRef]


def _mesh_refs(payload: memoryview, source: str) -> list[MeshRef]:
    cur = Cursor(payload, source, b"FRMM")
    refs = []
    for _ in range(cur.u32()):
        mesh = cur.strz32()
        cur.strz32()   # instance name
        cur.strz32()   # material override
        position = (cur.f32(), cur.f32(), cur.f32())
        rotation = (cur.f32(), cur.f32(), cur.f32(), cur.f32())
        scale = (cur.f32(), cur.f32(), cur.f32())
        refs.append(MeshRef(mesh, position, rotation, scale, cur.u8() != 0))
    if not cur.done():
        raise FormatError(f"{source}: {cur.remaining()} trailing bytes in FRMM")
    return refs


def parse_hwmo(path: Path) -> WorldModelData:
    path = Path(path)
    source = str(path)
    chunks = iter_chunks(path.read_bytes(), source)
    if not chunks or chunks[0][0] != b"REVM":
        raise FormatError(f"{source}: expected a REVM version chunk first")
    version = Cursor(chunks[0][1], source, b"REVM").u32()
    if version not in VERSIONS:
        raise FormatError(f"{source}: unsupported world model version 0x{version:x}")
    bounds = None
    refs: list[MeshRef] = []
    for magic, payload in chunks[1:]:
        if magic == b"DHOM":
            if len(payload) != 64:
                raise FormatError(f"{source}: MOHD header is {len(payload)} bytes, expected 64")
            bounds = (struct.unpack_from("<3f", payload, 32), struct.unpack_from("<3f", payload, 44))
        elif magic == b"PGOM":
            if len(payload) < _GROUP_HEADER:
                raise FormatError(f"{source}: group chunk shorter than its header")
            for sub_magic, sub in iter_chunks(bytes(payload[_GROUP_HEADER:]), source):
                if sub_magic == b"FRMM":
                    refs.extend(_mesh_refs(sub, source))
    if bounds is None:
        raise FormatError(f"{source}: no MOHD header")
    return WorldModelData(version, tuple(bounds[0]), tuple(bounds[1]), refs)
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_formats -v`
Expected: OK. The live test confirms the tent: 2 submeshes and 1050 vertices.

- [ ] **Step 7: Commit**

```bash
git add tools/world/worldkit/formats/hmsh.py tools/world/worldkit/formats/hwmo.py tools/tests/world_fixtures.py tools/tests/test_world_dressing_formats.py
git commit -m "feat(worldkit): .hmsh geometry and .hwmo reference readers"
```

---

### Task 4: Geometry helpers and base-texture resolution

**Files:**
- Create: `tools/world/worldkit/geometry.py`
- Create: `tools/world/worldkit/materials.py`
- Modify: `tools/tests/world_fixtures.py` (append `htex_rgba_bytes`)
- Test: `tools/tests/test_world_dressing_assets.py`

**Interfaces:**
- Produces:
  - `geometry.quat_mul(a, b)`, `quat_axis_angle(axis, degrees)`;
  - `quat_from_yaw_tilt(yaw, pitch=0.0, roll=0.0) -> (w,x,y,z)`, with rotation matrix R = Rx(pitch)·Rz(roll)·Ry(yaw);
  - `quat_to_matrix(q) -> ndarray(3,3)`, `yaw_of_quat(q) -> float` (degrees), `trs_matrix(position, rotation, scale) -> ndarray(4,4)`;
  - `materials.resolve_base_texture(material, client, parse=None) -> str | None`;
  - `load_texture(texture, client, max_size=256) -> ndarray(h,w,4) | None`;
  - `TextureCache(client, max_size=256).for_material(material) -> ndarray | None`.

- [ ] **Step 1: Add the fixture.** Append to `tools/tests/world_fixtures.py`:

```python
def htex_rgba_bytes(pixels) -> bytes:
	"""An uncompressed RGBA (format 1) .htex with one mip. pixels: (h, w, 4) uint8 array."""
	pixels = np.asarray(pixels, np.uint8)
	height, width = pixels.shape[:2]
	offsets = [142] + [0] * 15
	lengths = [width * height * 4] + [0] * 15
	header = b"HTEX" + struct.pack("<IBBHH", 0x100, 1, 0, width, height)
	header += struct.pack("<16I", *offsets) + struct.pack("<16I", *lengths)
	return header + pixels.tobytes()
```

- [ ] **Step 2: Write the failing tests.** Create `tools/tests/test_world_dressing_assets.py`:

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for geometry helpers, texture resolution, asset tags and the asset catalog.

	python tools/tests/test_world_dressing_assets.py
"""

import math
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit.geometry import quat_from_yaw_tilt, quat_to_matrix, trs_matrix, yaw_of_quat  # noqa: E402
from worldkit.materials import load_texture, resolve_base_texture  # noqa: E402
from worldkit.paths import client_root  # noqa: E402


class GeometryTests(unittest.TestCase):
	def test_yaw_round_trip_and_forward(self):
		for yaw in (0.0, 30.0, 135.0, -60.0):
			q = quat_from_yaw_tilt(yaw)
			self.assertAlmostEqual((yaw_of_quat(q) - yaw + 180.0) % 360.0 - 180.0, 0.0, places=4)
		forward = quat_to_matrix(quat_from_yaw_tilt(90.0)) @ np.array([0.0, 0.0, 1.0])
		self.assertTrue(np.allclose(forward, [1.0, 0.0, 0.0], atol=1e-6))   # yaw 90 faces +X (east)

	def test_tilt_maps_up_to_the_terrain_normal(self):
		normal = np.array([-0.3, 1.0, 0.2])
		normal /= np.linalg.norm(normal)
		roll = -math.degrees(math.asin(normal[0]))
		pitch = math.degrees(math.atan2(normal[2], normal[1]))
		up = quat_to_matrix(quat_from_yaw_tilt(40.0, pitch, roll)) @ np.array([0.0, 1.0, 0.0])
		self.assertTrue(np.allclose(up, normal, atol=1e-6))

	def test_trs_matrix(self):
		m = trs_matrix((10.0, 0.0, 5.0), quat_from_yaw_tilt(90.0), (2.0, 2.0, 2.0))
		self.assertTrue(np.allclose(m @ np.array([0.0, 0.0, 1.0, 1.0]), [12.0, 0.0, 5.0, 1.0], atol=1e-6))


class MaterialTests(unittest.TestCase):
	def test_override_beats_parent_and_names_are_ranked(self):
		materials = {
			"Mat/Child.hmi": {"parent": "Mat/Base.hmat", "textures": [], "texture_parameters": [{"name": "BaseColor", "texture": "Tex/Child_D.htex"}]},
			"Mat/Base.hmat": {"parent": None, "textures": ["Tex/Direct.htex"], "texture_parameters": [
				{"name": "Normal", "texture": "Tex/N.htex"}, {"name": "BaseColor", "texture": "Tex/Base_D.htex"}]},
			"Mat/Plain.hmat": {"parent": None, "textures": ["Tex\\Direct.htex"], "texture_parameters": []},
			"Mat/Hinted.hmat": {"parent": None, "textures": [], "texture_parameters": [{"name": "Grass_BaseColor", "texture": "Tex/G.htex"}]},
		}

		def parse(path, _):
			key = path.relative_to(Path("/c")).as_posix()
			return materials[key], [], b""

		client = Path("/c")
		exists = lambda path: path.relative_to(client).as_posix() in materials  # noqa: E731
		self.assertEqual(resolve_base_texture("Mat/Child.hmi", client, parse=parse, exists=exists), "Tex/Child_D.htex")
		self.assertEqual(resolve_base_texture("Mat/Base.hmat", client, parse=parse, exists=exists), "Tex/Base_D.htex")
		self.assertEqual(resolve_base_texture("Mat/Plain.hmat", client, parse=parse, exists=exists), "Tex/Direct.htex")
		self.assertEqual(resolve_base_texture("Mat/Hinted.hmat", client, parse=parse, exists=exists), "Tex/G.htex")
		self.assertIsNone(resolve_base_texture("Mat/Missing.hmi", client, parse=parse, exists=exists))

	def test_load_uncompressed_texture(self):
		pixels = np.zeros((2, 2, 4), np.uint8)
		pixels[0, 1] = (255, 0, 0, 255)
		with tempfile.TemporaryDirectory() as tmp:
			(Path(tmp) / "t.htex").write_bytes(fx.htex_rgba_bytes(pixels))
			image = load_texture("t.htex", Path(tmp))
		self.assertEqual(image.shape, (2, 2, 4))
		self.assertEqual(image[0, 1].tolist(), [255, 0, 0, 255])

	@fx.requires_live_data
	def test_shipped_tent_material(self):
		texture = resolve_base_texture("Textures/FalwynPlains/Props/CampTent.hmi", client_root())
		self.assertEqual(texture, "Textures/FalwynPlains/Props/Camp/T_hc_CampTent_D.htex")
		image = load_texture(texture, client_root())
		self.assertIsNotNone(image)
		self.assertEqual(image.shape[2], 4)


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_assets -v`
Expected: ImportError, `worldkit.geometry`.

- [ ] **Step 4: Implement `geometry.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Rotations for placed props. Quaternions are (w, x, y, z), like .wobj and .hfol files.

Yaw turns about +Y; yaw 0 faces +Z (south) and yaw 90 faces +X (east). Tilt is pitch about X and roll
about Z, applied after the yaw: R = Rx(pitch) . Rz(roll) . Ry(yaw).
"""

from __future__ import annotations

import math

import numpy as np

Quat = tuple[float, float, float, float]


def quat_mul(a: Quat, b: Quat) -> Quat:
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return (aw * bw - ax * bx - ay * by - az * bz,
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw)


def quat_axis_angle(axis, degrees: float) -> Quat:
    half = math.radians(degrees) * 0.5
    x, y, z = axis
    s = math.sin(half)
    return (math.cos(half), x * s, y * s, z * s)


def quat_from_yaw_tilt(yaw: float, pitch: float = 0.0, roll: float = 0.0) -> Quat:
    tilt = quat_mul(quat_axis_angle((1.0, 0.0, 0.0), pitch), quat_axis_angle((0.0, 0.0, 1.0), roll))
    return quat_mul(tilt, quat_axis_angle((0.0, 1.0, 0.0), yaw))


def quat_to_matrix(q) -> np.ndarray:
    w, x, y, z = q
    n = math.sqrt(w * w + x * x + y * y + z * z) or 1.0
    w, x, y, z = w / n, x / n, y / n, z / n
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])


def yaw_of_quat(q) -> float:
    """Heading in degrees of the rotated +Z axis (0 = +Z, 90 = +X)."""
    forward = quat_to_matrix(q) @ np.array([0.0, 0.0, 1.0])
    return math.degrees(math.atan2(forward[0], forward[2]))


def trs_matrix(position, rotation, scale) -> np.ndarray:
    m = np.eye(4)
    m[:3, :3] = quat_to_matrix(rotation) * np.asarray(scale, float)[None, :]
    m[:3, 3] = position
    return m
```

- [ ] **Step 5: Implement `materials.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Base-colour texture of a mesh's material, for the offline renderer.

A submesh names a material instance (.hmi) or material (.hmat). Its base colour is the first texture
parameter whose name says so (BaseColor, Diffuse, Albedo, ...), searched in the instance's overrides
first and then up its PRNT chain, falling back to the first directly sampled texture (TEXT). Parsing
reuses the material skill's tools (material_tool.parse_material, htex_tool.read_htex/decode_mip).
Anything unresolvable yields None; the renderer then shades the mesh plainly.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np

from .paths import REPO, material_scripts

_NAME_PRIORITY = ("basecolor", "diffuse", "albedo", "albedo01", "texture", "color")
_NAME_HINTS = ("basecolor", "albedo", "diffuse", "color")
_MAX_PARENTS = 8


def _tools():
    folder = str(material_scripts(REPO))
    if folder not in sys.path:
        sys.path.insert(0, folder)
    import htex_tool      # noqa: E402  (tracked skill script)
    import material_tool  # noqa: E402
    return material_tool, htex_tool


def _norm(path) -> str | None:
    return str(path).replace("\\", "/") if path else None


def resolve_base_texture(material: str, client: Path, parse=None, exists=None) -> str | None:
    if parse is None:
        material_tool, _ = _tools()
        parse = material_tool.parse_material
        errors = (material_tool.FormatError, ValueError, OSError, struct.error)
    else:
        errors = (ValueError, OSError, KeyError)
    exists = exists or (lambda path: path.is_file())
    params: list[dict] = []
    direct: list[str] = []
    current, seen = _norm(material), set()
    while current and current not in seen and len(seen) < _MAX_PARENTS:
        seen.add(current)
        path = Path(client) / current
        if not exists(path):
            break
        try:
            info, _, _ = parse(path, None)
        except errors:
            break
        params += info.get("texture_parameters") or []
        direct += [t for t in (info.get("textures") or []) if t]
        current = _norm(info.get("parent"))
    by_name: dict[str, str] = {}
    for param in params:
        by_name.setdefault(str(param.get("name", "")).lower(), _norm(param.get("texture")))
    for name in _NAME_PRIORITY:
        if by_name.get(name):
            return by_name[name]
    for name, texture in by_name.items():
        if texture and any(hint in name for hint in _NAME_HINTS):
            return texture
    return _norm(direct[0]) if direct else None


def load_texture(texture: str, client: Path, max_size: int = 256) -> np.ndarray | None:
    path = Path(client) / texture
    if not path.is_file():
        return None
    _, htex_tool = _tools()
    try:
        info = htex_tool.read_htex(path)
        mips = sorted(info["mips"], key=lambda m: m["index"])
        if not mips:
            return None
        pick = next((m for m in mips if max(m["width"], m["height"]) <= max_size), mips[-1])
        image = htex_tool.decode_mip(info, pick["index"])
    except (ValueError, OSError, struct.error, KeyError):
        return None
    return np.asarray(image.convert("RGBA"), dtype=np.uint8)


class TextureCache:
    """Material -> decoded base texture, resolved once per process."""

    def __init__(self, client: Path, max_size: int = 256):
        self._client = Path(client)
        self._max_size = max_size
        self._by_material: dict[str, np.ndarray | None] = {}
        self._by_texture: dict[str, np.ndarray | None] = {}

    def texture_name(self, material: str) -> str | None:
        return resolve_base_texture(material, self._client)

    def for_material(self, material: str) -> np.ndarray | None:
        if material not in self._by_material:
            name = self.texture_name(material)
            if name and name not in self._by_texture:
                self._by_texture[name] = load_texture(name, self._client, self._max_size)
            self._by_material[material] = self._by_texture.get(name) if name else None
        return self._by_material[material]
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_assets -v`
Expected: OK. If the live tent test fails because `parse_material` returns different keys, print `material_tool.parse_material(Path("data/client/Textures/FalwynPlains/Props/CampTent.hmi"), None)[0].keys()`. Then adjust the key names in `resolve_base_texture` and in the fake dict of `test_override_beats_parent_and_names_are_ranked` together.

- [ ] **Step 7: Commit**

```bash
git add tools/world/worldkit/geometry.py tools/world/worldkit/materials.py tools/tests/world_fixtures.py tools/tests/test_world_dressing_assets.py
git commit -m "feat(worldkit): rotation helpers and material base-texture resolution"
```

---

### Task 5: Asset tags and the asset catalog

**Files:**
- Create: `tools/world/worldkit/tags.py`, `tools/world/asset_tags.json`, `tools/world/worldkit/assets.py`
- Modify: `tools/world/worldkit/__main__.py` (an `assets` command)
- Test: `tools/tests/test_world_dressing_assets.py` (add classes)

**Interfaces:**
- Consumes: `parse_hmsh`, `parse_hwmo` (Task 3); `resolve_base_texture` (Task 4); `paths.client_root`, `paths.assets_cache_dir`.
- Produces:
  - `tags.AssetTags(tags, scale, align_to_slope, max_slope, sink, may_overlap, collides_override, footprint_scale)`;
  - `tags.TagRules(rules).for_asset(path) -> AssetTags`, `tags.load_tag_rules(path=TAGS_PATH) -> TagRules`;
  - `tags.VOCABULARY`, `tags.NEVER_PICK`;
  - `assets.AssetInfo`, with fields `path, kind, bounds_min, bounds_max, has_collision, submeshes, materials, textures, references, error` and properties `size, size_class, height, base_offset, footprint, radius`;
  - `assets.build_catalog(repo=REPO, rebuild=False) -> dict[str, AssetInfo]`;
  - `assets.select_assets(catalog, rules, tags=(), exclude=(), size=None, allow=None) -> list[str]`;
  - `assets.GeometryCache(repo).mesh(rel) -> MeshData` and `.world_model(rel) -> WorldModelData`.

- [ ] **Step 1: Write the failing tests.** Add to the imports of `test_world_dressing_assets.py`:

```python
from worldkit.assets import build_catalog, select_assets  # noqa: E402
from worldkit.tags import TagRules, load_tag_rules  # noqa: E402
```

and the classes:

```python
def asset_repo(tmp: str) -> Path:
	repo = Path(tmp)
	models = repo / "data" / "client" / "Models" / "Test"
	models.mkdir(parents=True)
	(models / "Cube.hmsh").write_bytes(fx.cube_hmsh())
	(models / "BigRock.hmsh").write_bytes(fx.cube_hmsh(collision=False, size=2.5))
	(models / "Shed.hwmo").write_bytes(fx.hwmo_bytes((-4.0, 0.0, -3.0), (4.0, 5.0, 3.0),
		[("Models/Test/Cube.hmsh", (0.0, 0.0, 0.0), (1.0, 0.0, 0.0, 0.0), (1.0, 1.0, 1.0))]))
	(models / "Broken.hmsh").write_bytes(b"MESH\x00\x01")
	return repo


class TagRuleTests(unittest.TestCase):
	def test_later_rules_override_and_tags_accumulate(self):
		rules = TagRules([
			{"match": "Models/Desert/Rocks/*", "tags": ["rock"], "sink": 0.4, "max_slope": 90, "may_overlap": ["rock"]},
			{"match": "*/SM_Rock_Cliff*", "tags": ["cliff_rock"], "sink": 0.8},
		])
		tags = rules.for_asset("Models/Desert/Rocks/SM_Rock_Cliff01.hmsh")
		self.assertEqual(tags.tags, frozenset({"rock", "cliff_rock"}))
		self.assertEqual((tags.sink, tags.max_slope, tags.may_overlap), (0.8, 90, frozenset({"rock"})))
		self.assertEqual(rules.for_asset("Models/Other/X.hmsh").tags, frozenset())

	def test_unknown_tags_and_fields_are_rejected(self):
		with self.assertRaises(ValueError):
			TagRules([{"match": "*", "tags": ["spaceship"]}])
		with self.assertRaises(ValueError):
			TagRules([{"match": "*", "colour": "red"}])

	def test_shipped_rules_load(self):
		self.assertTrue(load_tag_rules().rules)


class CatalogTests(unittest.TestCase):
	def setUp(self):
		self._tmp = tempfile.TemporaryDirectory()
		self.repo = asset_repo(self._tmp.name)

	def tearDown(self):
		self._tmp.cleanup()

	def test_entries(self):
		catalog = build_catalog(self.repo)
		cube = catalog["Models/Test/Cube.hmsh"]
		self.assertEqual((cube.kind, cube.has_collision, cube.submeshes, cube.size_class), ("mesh", True, 1, "medium"))
		self.assertEqual((cube.height, cube.base_offset, cube.footprint), (2.0, 0.0, (-1.0, -1.0, 1.0, 1.0)))
		self.assertEqual(catalog["Models/Test/BigRock.hmsh"].size_class, "large")
		shed = catalog["Models/Test/Shed.hwmo"]
		self.assertEqual((shed.kind, shed.references, shed.has_collision), ("wmo", ("Models/Test/Cube.hmsh",), True))
		self.assertIsNotNone(catalog["Models/Test/Broken.hmsh"].error)

	def test_cache_is_reused_and_invalidated(self):
		build_catalog(self.repo)
		path = self.repo / "data" / "client" / "Models" / "Test" / "Cube.hmsh"
		path.write_bytes(fx.cube_hmsh(size=0.5, collision=False))   # different size, so the cache stamp changes
		entry = build_catalog(self.repo)["Models/Test/Cube.hmsh"]
		self.assertEqual((entry.size_class, entry.has_collision), ("small", False))

	def test_select(self):
		catalog = build_catalog(self.repo)
		rules = TagRules([{"match": "Models/Test/*", "tags": ["rock"]}, {"match": "*/Shed*", "tags": ["building"]},
						  {"match": "*/BigRock*", "tags": ["prototype"]}])
		self.assertEqual(select_assets(catalog, rules, tags=["rock"], exclude=["building"]), ["Models/Test/Cube.hmsh"])
		self.assertEqual(select_assets(catalog, rules, tags=["rock"], size="large"), [])
		self.assertEqual(select_assets(catalog, rules, allow=["Models/Test/BigRock.hmsh"]), ["Models/Test/BigRock.hmsh"])
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_assets -v`
Expected: ImportError, `worldkit.assets`.

- [ ] **Step 3: Implement `tags.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Asset tags and placement defaults (tools/world/asset_tags.json).

Each rule has a case-insensitive glob `match` on the asset path (relative to data/client, '/'
separators; '*' also crosses folders) and any of the fields below. Rules apply in file order: tags
accumulate, every other field is overwritten by later matches. The user's edits to the JSON win over
anything an agent wrote; agents only add rules for assets that have none.
"""

from __future__ import annotations

import fnmatch
import json
from dataclasses import dataclass, replace
from pathlib import Path

from .paths import REPO

TAGS_PATH = REPO / "tools" / "world" / "asset_tags.json"
VOCABULARY = frozenset({
    "rock", "cliff_rock", "rubble", "ruin", "wall", "tower", "building", "tent", "camp", "fire", "crate", "barrel",
    "cart", "tool", "fence", "light", "tree", "tree_dead", "bush", "plant", "herb", "mine", "bridge", "pier",
    "dungeon", "food", "furniture", "prototype",
})
NEVER_PICK = frozenset({"prototype", "dungeon"})   # only when a query asks for them by tag or allow-list
_FIELDS = {"match", "tags", "scale", "align_to_slope", "max_slope", "sink", "may_overlap", "collides_override",
           "footprint_scale"}


@dataclass(frozen=True)
class AssetTags:
    tags: frozenset = frozenset()
    scale: tuple[float, float] = (1.0, 1.0)
    align_to_slope: bool = False
    max_slope: float = 20.0
    sink: float = 0.05             # metres the base may go into the ground
    may_overlap: frozenset = frozenset()
    collides_override: bool | None = None
    footprint_scale: float = 1.0   # trees: only the trunk blocks (e.g. 0.15)


class TagRules:
    def __init__(self, rules: list[dict]):
        for index, rule in enumerate(rules):
            unknown = set(rule) - _FIELDS
            if unknown or "match" not in rule:
                raise ValueError(f"asset tag rule {index}: unknown fields {sorted(unknown)} or no 'match'")
            bad = (set(rule.get("tags", [])) | set(rule.get("may_overlap", []))) - VOCABULARY
            if bad:
                raise ValueError(f"asset tag rule {index}: tags {sorted(bad)} are not in the vocabulary")
        self.rules = rules

    def for_asset(self, path: str) -> AssetTags:
        result = AssetTags()
        lowered = path.replace("\\", "/").lower()
        for rule in self.rules:
            if not fnmatch.fnmatchcase(lowered, rule["match"].lower()):
                continue
            fields = {}
            for key, value in rule.items():
                if key == "match":
                    continue
                if key == "tags":
                    fields["tags"] = result.tags | frozenset(value)
                elif key == "may_overlap":
                    fields["may_overlap"] = frozenset(value)
                elif key == "scale":
                    fields["scale"] = (float(value[0]), float(value[1]))
                else:
                    fields[key] = value
            result = replace(result, **fields)
        return result


def load_tag_rules(path: Path = TAGS_PATH) -> TagRules:
    doc = json.loads(Path(path).read_text(encoding="utf-8"))
    return TagRules(doc.get("rules", []))
```

- [ ] **Step 4: Create the starting `tools/world/asset_tags.json`.** Task 15 refines it from the contact sheets.

```json
{
  "version": 1,
  "_comment": "Asset tags and placement defaults for dressing (worldkit/tags.py). Later rules override earlier ones; tags accumulate. The user's edits win.",
  "rules": [
    {"match": "Models/Prototyping/*", "tags": ["prototype"]},
    {"match": "Models/Cube/*", "tags": ["prototype"]},
    {"match": "Models/Floor/*", "tags": ["prototype"]},
    {"match": "Models/Dungeon/*", "tags": ["dungeon"]},
    {"match": "Models/Desert/Rocks/*", "tags": ["rock"], "scale": [0.6, 1.6], "align_to_slope": true, "max_slope": 90, "sink": 0.4, "may_overlap": ["rock", "cliff_rock", "plant", "bush", "herb"]},
    {"match": "Models/Stones/*", "tags": ["rock"], "scale": [0.7, 1.4], "align_to_slope": true, "max_slope": 60, "sink": 0.2, "may_overlap": ["rock", "plant", "herb"]},
    {"match": "Models/FalwynPlains/Trees/*", "tags": ["tree"], "scale": [0.8, 1.25], "max_slope": 30, "sink": 0.3, "footprint_scale": 0.15, "may_overlap": ["plant", "bush", "herb"]},
    {"match": "Models/Trees/*", "tags": ["tree"], "scale": [0.8, 1.25], "max_slope": 30, "sink": 0.3, "footprint_scale": 0.15, "may_overlap": ["plant", "bush", "herb"]},
    {"match": "Models/Swamp/SM_tree_*", "tags": ["tree"], "scale": [0.8, 1.2], "max_slope": 30, "sink": 0.3, "footprint_scale": 0.15, "may_overlap": ["plant", "bush", "herb"]},
    {"match": "Models/Swamp/SM_dry_tree_*", "tags": ["tree_dead"], "scale": [0.8, 1.2], "max_slope": 35, "sink": 0.3, "footprint_scale": 0.15, "may_overlap": ["plant", "bush", "herb", "rock"]},
    {"match": "Models/FalwynPlains/Plants/*", "tags": ["plant"], "scale": [0.8, 1.3], "max_slope": 40, "sink": 0.05, "may_overlap": ["plant", "bush", "herb", "rock", "tree", "tree_dead"], "collides_override": false},
    {"match": "Models/FalwynPlains/Plants/Bush_*", "tags": ["bush"]},
    {"match": "Models/FalwynPlains/Plants/Shrub_*", "tags": ["bush"]},
    {"match": "Models/FalwynPlains/Plants/*Flower*", "tags": ["herb"]},
    {"match": "Models/FalwynPlains/Props/Camp/*", "tags": ["camp"], "max_slope": 12},
    {"match": "*/SM_hc_Camptent*", "tags": ["tent"], "max_slope": 10},
    {"match": "*/SM_hc_CampFire*", "tags": ["fire"]},
    {"match": "*/SM_hc_Target*", "tags": ["tool"]},
    {"match": "*/SM_hc_WoodCrate*", "tags": ["crate"]},
    {"match": "*/SM_hc_WoodBarrel*", "tags": ["barrel"]},
    {"match": "*/SM_hc_Wagon*", "tags": ["cart"], "max_slope": 10},
    {"match": "*/SM_hc_WoodcutterStump*", "tags": ["tool"]},
    {"match": "*/SM_hc_Haystack*", "tags": ["camp"]},
    {"match": "*Fence*", "tags": ["fence"]},
    {"match": "*Lantern*", "tags": ["light"]},
    {"match": "Models/FalwynPlains/Props/Food/*", "tags": ["food"]},
    {"match": "Models/FalwynPlains/Props/Dishes/*", "tags": ["food"]},
    {"match": "Models/FalwynPlains/Props/Furniture/*", "tags": ["furniture"]},
    {"match": "Models/FalwynPlains/Buildings/*", "tags": ["building"], "max_slope": 8},
    {"match": "*/Building_Tower_*", "tags": ["tower"]},
    {"match": "*/Fortress_Wall*", "tags": ["wall"]},
    {"match": "*/Foundation_*", "tags": ["ruin"], "max_slope": 15, "sink": 0.3, "may_overlap": ["ruin", "rubble", "rock"]},
    {"match": "*/Bridge_*", "tags": ["bridge"]},
    {"match": "*/SM_hc_Pier*", "tags": ["pier"]},
    {"match": "Models/Mine/*", "tags": ["mine"]}
  ]
}
```

- [ ] **Step 5: Implement `assets.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Catalog of placeable assets (.hmsh, .hwmo under data/client/Models) for dressing.

Cached in generated/world/assets/catalog.json, keyed by each file's size and mtime plus a hash of the
parser code, so only changed assets are re-parsed. A WMO has collision when any mesh it references does.
"""

from __future__ import annotations

import dataclasses
import hashlib
import json
import math
from dataclasses import dataclass, replace
from pathlib import Path

from .formats.chunks import FormatError
from .formats.hmsh import MeshData, parse_hmsh
from .formats.hwmo import WorldModelData, parse_hwmo
from .materials import resolve_base_texture
from .paths import REPO, assets_cache_dir, client_root
from .tags import NEVER_PICK, TagRules

CATALOG_VERSION = 1
SIZE_SMALL = 1.5
SIZE_MEDIUM = 4.0
_SUFFIXES = (".hmsh", ".hwmo")


@dataclass(frozen=True)
class AssetInfo:
    path: str                                   # relative to data/client, '/' separators
    kind: str                                   # "mesh" | "wmo"
    bounds_min: tuple[float, float, float]
    bounds_max: tuple[float, float, float]
    has_collision: bool
    submeshes: int
    materials: tuple[str, ...]
    textures: tuple[str | None, ...]
    references: tuple[str, ...]                 # meshes a WMO places
    error: str | None = None

    @property
    def size(self) -> float:
        return max(self.bounds_max[i] - self.bounds_min[i] for i in range(3))

    @property
    def size_class(self) -> str:
        return "small" if self.size < SIZE_SMALL else "medium" if self.size < SIZE_MEDIUM else "large"

    @property
    def height(self) -> float:
        return self.bounds_max[1] - self.bounds_min[1]

    @property
    def base_offset(self) -> float:
        """How far the origin sits above the lowest vertex."""
        return -self.bounds_min[1]

    @property
    def footprint(self) -> tuple[float, float, float, float]:
        """Model-space rectangle under the asset: (min_x, min_z, max_x, max_z)."""
        return (self.bounds_min[0], self.bounds_min[2], self.bounds_max[0], self.bounds_max[2])

    @property
    def radius(self) -> float:
        x0, z0, x1, z1 = self.footprint
        return max(math.hypot(x, z) for x in (x0, x1) for z in (z0, z1))


def _code_digest() -> str:
    digest = hashlib.sha1(f"assets-v{CATALOG_VERSION}".encode())
    package = Path(__file__).parent
    for path in sorted([package / "assets.py", package / "materials.py", package / "formats" / "hmsh.py",
                        package / "formats" / "hwmo.py", package / "formats" / "chunks.py"]):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def _describe(path: Path, rel: str, client: Path) -> AssetInfo:
    kind = "mesh" if path.suffix.lower() == ".hmsh" else "wmo"
    try:
        if kind == "mesh":
            mesh: MeshData = parse_hmsh(path)
            lo, hi = mesh.bounds
            materials = tuple(s.material for s in mesh.submeshes)
            textures = tuple(resolve_base_texture(m, client) for m in materials)
            return AssetInfo(rel, kind, tuple(float(v) for v in lo), tuple(float(v) for v in hi), mesh.has_collision,
                             len(mesh.submeshes), materials, textures, ())
        model: WorldModelData = parse_hwmo(path)
        references = tuple(sorted({r.mesh.replace("\\", "/") for r in model.mesh_refs}))
        return AssetInfo(rel, kind, tuple(model.bounds_min), tuple(model.bounds_max), False, 0, (), (), references)
    except (FormatError, OSError) as exc:
        return AssetInfo(rel, kind, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), False, 0, (), (), (), error=str(exc))


def _from_dict(data: dict) -> AssetInfo:
    return AssetInfo(data["path"], data["kind"], tuple(data["bounds_min"]), tuple(data["bounds_max"]),
                     data["has_collision"], data["submeshes"], tuple(data["materials"]), tuple(data["textures"]),
                     tuple(data["references"]), data.get("error"))


def build_catalog(repo: Path = REPO, rebuild: bool = False) -> dict[str, AssetInfo]:
    client = client_root(repo)
    cache_file = assets_cache_dir(repo) / "catalog.json"
    digest = _code_digest()
    cached: dict = {}
    if cache_file.is_file() and not rebuild:
        try:
            doc = json.loads(cache_file.read_text(encoding="utf-8"))
            if doc.get("digest") == digest:
                cached = doc.get("assets", {})
        except json.JSONDecodeError:
            cached = {}
    entries: dict[str, AssetInfo] = {}
    stamps: dict[str, str] = {}
    models = client / "Models"
    for path in sorted(models.rglob("*")) if models.is_dir() else []:
        if path.suffix.lower() not in _SUFFIXES or not path.is_file():
            continue
        rel = path.relative_to(client).as_posix()
        stat = path.stat()
        stamp = f"{stat.st_size}:{stat.st_mtime_ns}"
        hit = cached.get(rel)
        entries[rel] = _from_dict(hit["info"]) if hit and hit.get("stamp") == stamp else _describe(path, rel, client)
        stamps[rel] = stamp
    for rel, info in list(entries.items()):
        if info.kind == "wmo" and not info.error:
            collides = any(entries.get(ref) is not None and entries[ref].has_collision for ref in info.references)
            entries[rel] = replace(info, has_collision=collides)
    cache_file.parent.mkdir(parents=True, exist_ok=True)
    payload = {rel: {"stamp": stamps[rel], "info": dataclasses.asdict(info)} for rel, info in entries.items()}
    cache_file.write_text(json.dumps({"version": CATALOG_VERSION, "digest": digest, "assets": payload}), encoding="utf-8")
    return entries


def select_assets(catalog: dict[str, AssetInfo], rules: TagRules, tags=(), exclude=(), size: str | None = None,
                  allow=None) -> list[str]:
    """Usable assets matching a template role's query. An allow-list bypasses tags and size."""
    wanted, unwanted = set(tags), set(exclude)
    out = []
    for rel, info in sorted(catalog.items()):
        if info.error:
            continue
        if allow is not None:
            if rel in allow:
                out.append(rel)
            continue
        have = rules.for_asset(rel).tags
        if not wanted <= have or have & unwanted:
            continue
        if have & NEVER_PICK and not wanted & NEVER_PICK:
            continue
        if size and info.size_class != size:
            continue
        out.append(rel)
    return out


class GeometryCache:
    """Parsed meshes and world models by catalog path, kept for the process lifetime."""

    def __init__(self, repo: Path = REPO):
        self._client = client_root(repo)
        self._meshes: dict[str, MeshData | None] = {}
        self._models: dict[str, WorldModelData | None] = {}

    def mesh(self, rel: str) -> MeshData | None:
        if rel not in self._meshes:
            try:
                self._meshes[rel] = parse_hmsh(self._client / rel)
            except (FormatError, OSError):
                self._meshes[rel] = None
        return self._meshes[rel]

    def world_model(self, rel: str) -> WorldModelData | None:
        if rel not in self._models:
            try:
                self._models[rel] = parse_hwmo(self._client / rel)
            except (FormatError, OSError):
                self._models[rel] = None
        return self._models[rel]
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_assets -v`
Expected: OK.

- [ ] **Step 7: Add the `assets` CLI command.** In `tools/world/worldkit/__main__.py`, register a subparser following the existing `query`/`layers` pattern. Read the file first and mirror its parser variable names. The subparser and handler:

```python
    assets = sub.add_parser("assets", help="build the asset catalog and print a summary")
    assets.add_argument("--rebuild", action="store_true", help="ignore the cache")
    assets.add_argument("--untagged", action="store_true", help="list assets no tag rule matches")
    assets.set_defaults(handler=run_assets)
```

```python
def run_assets(args) -> int:
    from .assets import build_catalog
    from .tags import load_tag_rules
    catalog = build_catalog(rebuild=args.rebuild)
    rules = load_tag_rules()
    errors = [i for i in catalog.values() if i.error]
    print(f"{len(catalog)} assets, {len(errors)} unreadable, "
          f"{sum(1 for i in catalog.values() if i.has_collision)} with collision, "
          f"{sum(1 for i in catalog.values() if i.kind == 'mesh' and not i.error and not any(i.textures))} meshes without a resolved texture")
    for info in errors[:20]:
        print(f"  unreadable: {info.path}: {info.error}")
    if args.untagged:
        for rel in sorted(catalog):
            if not catalog[rel].error and not rules.for_asset(rel).tags:
                print(f"  untagged: {rel}")
    return 0
```

Run: `cd tools/world; py -3.14 -m worldkit assets --untagged`
Expected:
- Prints about 578 assets.
- Unreadable entries include `Models/Stump_03/Stump_03.hmsh`.
- Prints the untagged list.
- Takes under 2 minutes on the first run and seconds after that. Note the first-run time in the commit message.

- [ ] **Step 8: Commit**

```bash
git add tools/world/worldkit/tags.py tools/world/asset_tags.json tools/world/worldkit/assets.py tools/world/worldkit/__main__.py tools/tests/test_world_dressing_assets.py
git commit -m "feat(worldkit): asset catalog with tags, cache and selection"
```

---

### Task 6: Offline renderer and asset images

**Files:**
- Create: `tools/world/worldkit/meshrender.py`
- Create: `tools/world/worldkit/previews.py` (asset part)
- Modify: `tools/world/worldkit/__main__.py` (`assets --sheets`, `assets --views`)
- Test: `tools/tests/test_world_dressing_render.py`

**Interfaces:**
- Consumes: `AssetInfo`, `GeometryCache` (Task 5); `TextureCache` (Task 4); `trs_matrix` (Task 4).
- Produces:
  - `meshrender.Camera(eye, target, width, height, fov_deg=40.0, ortho_height=None, up=(0,1,0))`;
  - `meshrender.DrawMesh(positions, indices, uvs=None, texture=None, color=(170,170,170), tri_colors=None, object_id=0)`;
  - `meshrender.render(meshes, camera, background=(28,28,28)) -> (rgb uint8 (h,w,3), ids int32 (h,w))`;
  - `meshrender.outline(rgb, ids, highlight, color=(255,220,60)) -> rgb`;
  - `previews.asset_meshes(rel, catalog, geometry, textures, matrix, object_id=0) -> list[DrawMesh]`;
  - `previews.contact_sheet(rels, catalog, geometry, textures, tile=128, columns=8, title="") -> Image`;
  - `previews.asset_views(rel, catalog, geometry, textures, size=320) -> Image`;
  - `previews.write_contact_sheets(catalog, geometry, textures, out_dir) -> list[Path]`.

- [ ] **Step 1: Write the failing tests.** Create `tools/tests/test_world_dressing_render.py`:

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the offline rasterizer, asset images and site previews.

	python tools/tests/test_world_dressing_render.py
"""

import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit.assets import GeometryCache, build_catalog  # noqa: E402
from worldkit.materials import TextureCache  # noqa: E402
from worldkit.meshrender import Camera, DrawMesh, outline, render  # noqa: E402
from worldkit.paths import client_root  # noqa: E402
from worldkit.previews import asset_views, contact_sheet  # noqa: E402

QUAD = np.array([[-1.0, -1.0, 0.0], [1.0, -1.0, 0.0], [1.0, 1.0, 0.0], [-1.0, 1.0, 0.0]])
QUAD_TRIS = np.array([[0, 1, 2], [0, 2, 3]])
FRONT = Camera(eye=(0.0, 0.0, 5.0), target=(0.0, 0.0, 0.0), width=64, height=64, ortho_height=4.0)


class RasterizerTests(unittest.TestCase):
	def test_quad_covers_the_centre_not_the_corner(self):
		rgb, ids = render([DrawMesh(QUAD, QUAD_TRIS, color=(200, 0, 0), object_id=3)], FRONT)
		self.assertEqual(ids[32, 32], 3)
		self.assertEqual(ids[1, 1], -1)
		self.assertGreater(rgb[32, 32, 0], 50)
		self.assertEqual(rgb[32, 32, 1], 0)
		self.assertEqual(rgb[1, 1].tolist(), [28, 28, 28])

	def test_depth_keeps_the_nearer_quad(self):
		near = DrawMesh(QUAD + [0.0, 0.0, 1.0], QUAD_TRIS, color=(0, 200, 0), object_id=1)
		far = DrawMesh(QUAD, QUAD_TRIS, color=(200, 0, 0), object_id=2)
		_, ids = render([near, far], FRONT)
		self.assertEqual(ids[32, 32], 1)
		_, ids = render([far, near], FRONT)
		self.assertEqual(ids[32, 32], 1)

	def test_texture_quadrants_and_perspective(self):
		texture = np.zeros((2, 2, 4), np.uint8)
		texture[0, 0] = (255, 0, 0, 255)
		texture[0, 1] = (0, 255, 0, 255)
		texture[1, 0] = (0, 0, 255, 255)
		texture[1, 1] = (255, 255, 0, 255)
		uvs = np.array([[0.0, 1.0], [1.0, 1.0], [1.0, 0.0], [0.0, 0.0]])
		camera = Camera(eye=(0.0, 0.0, 4.0), target=(0.0, 0.0, 0.0), width=64, height=64)
		rgb, _ = render([DrawMesh(QUAD, QUAD_TRIS, uvs=uvs, texture=texture)], camera)
		top_left, bottom_right = rgb[26, 26], rgb[38, 38]
		self.assertGreater(top_left[0], top_left[1])        # red texel in the upper left
		self.assertGreater(bottom_right[1], bottom_right[2])  # yellow texel in the lower right

	def test_deterministic_and_outline(self):
		mesh = DrawMesh(QUAD, QUAD_TRIS, color=(100, 100, 100), object_id=7)
		a, ids = render([mesh], FRONT)
		b, _ = render([mesh], FRONT)
		self.assertTrue(np.array_equal(a, b))
		marked = outline(a, ids, {7})
		self.assertTrue((marked == [255, 220, 60]).all(axis=2).any())
		self.assertEqual(marked[32, 32].tolist(), a[32, 32].tolist())


class AssetImageTests(unittest.TestCase):
	def test_sheet_and_views(self):
		with tempfile.TemporaryDirectory() as tmp:
			repo = Path(tmp)
			models = repo / "data" / "client" / "Models" / "Test"
			models.mkdir(parents=True)
			(models / "Cube.hmsh").write_bytes(fx.cube_hmsh())
			catalog = build_catalog(repo)
			geometry, textures = GeometryCache(repo), TextureCache(client_root(repo))
			sheet = contact_sheet(["Models/Test/Cube.hmsh"], catalog, geometry, textures, title="Test")
			views = asset_views("Models/Test/Cube.hmsh", catalog, geometry, textures)
		self.assertGreater(sheet.width, 100)
		self.assertGreater(len(set(np.asarray(sheet.convert("RGB")).reshape(-1, 3)[:, 0].tolist())), 3)
		self.assertEqual(views.width, 3 * 320)


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_render -v`
Expected: ImportError, `worldkit.meshrender`.

- [ ] **Step 3: Implement `meshrender.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""A small numpy rasterizer for asset contact sheets and site previews.

Z-buffered, two-sided (no culling: robust to mirrored meshes), Lambert-shaded from the north-west
(north = -Z, west = -X), nearest-neighbour texturing with perspective-correct UVs. Deterministic: the
same meshes and camera always give the same pixels. Not engine-accurate; good enough to tell assets apart.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

NEAR = 0.05
LIGHT = np.array([-1.0, 1.6, -1.0]) / np.linalg.norm([-1.0, 1.6, -1.0])
AMBIENT = 0.35


@dataclass
class Camera:
    eye: tuple[float, float, float]
    target: tuple[float, float, float]
    width: int
    height: int
    fov_deg: float = 40.0
    ortho_height: float | None = None   # world units visible vertically; None = perspective
    up: tuple[float, float, float] = (0.0, 1.0, 0.0)


@dataclass
class DrawMesh:
    positions: np.ndarray                  # (n, 3) world space
    indices: np.ndarray                    # (m, 3) or flat (3m,) triangle list
    uvs: np.ndarray | None = None          # (n, 2)
    texture: np.ndarray | None = None      # (h, w, 4) uint8
    color: tuple[int, int, int] = (170, 170, 170)
    tri_colors: np.ndarray | None = None   # (m, 3) per-triangle colours (terrain)
    object_id: int = 0


def _basis(camera: Camera):
    eye = np.asarray(camera.eye, float)
    forward = np.asarray(camera.target, float) - eye
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, np.asarray(camera.up, float))
    right /= np.linalg.norm(right)
    return eye, forward, right, np.cross(right, forward)


def _project(points: np.ndarray, camera: Camera):
    eye, forward, right, up = _basis(camera)
    rel = points - eye
    xv, yv, depth = rel @ right, rel @ up, rel @ forward
    if camera.ortho_height:
        scale = camera.height / camera.ortho_height
        return camera.width / 2 + xv * scale, camera.height / 2 - yv * scale, depth, np.ones_like(depth)
    focal = (camera.height / 2) / math.tan(math.radians(camera.fov_deg) / 2)
    safe = np.where(depth > NEAR, depth, NEAR)
    return camera.width / 2 + xv * focal / safe, camera.height / 2 - yv * focal / safe, depth, 1.0 / safe


def render(meshes: list[DrawMesh], camera: Camera, background=(28, 28, 28)) -> tuple[np.ndarray, np.ndarray]:
    width, height = camera.width, camera.height
    rgb = np.empty((height, width, 3), np.float32)
    rgb[:] = background
    depth = np.full((height, width), np.inf, np.float64)
    ids = np.full((height, width), -1, np.int32)
    ortho = camera.ortho_height is not None
    for mesh in meshes:
        positions = np.asarray(mesh.positions, float)
        tris = np.asarray(mesh.indices, np.int64).reshape(-1, 3)
        if not len(tris):
            continue
        sx, sy, d, winv = _project(positions, camera)
        normals = np.cross(positions[tris[:, 1]] - positions[tris[:, 0]], positions[tris[:, 2]] - positions[tris[:, 0]])
        lengths = np.linalg.norm(normals, axis=1)
        shade = AMBIENT + (1.0 - AMBIENT) * np.abs(normals @ LIGHT) / np.where(lengths > 1e-12, lengths, 1.0)
        tex = mesh.texture
        for k, (a, b, c) in enumerate(tris):
            if not ortho and min(d[a], d[b], d[c]) <= NEAR:
                continue
            xs_t, ys_t = (sx[a], sx[b], sx[c]), (sy[a], sy[b], sy[c])
            x0, x1 = max(int(math.floor(min(xs_t))), 0), min(int(math.ceil(max(xs_t))), width - 1)
            y0, y1 = max(int(math.floor(min(ys_t))), 0), min(int(math.ceil(max(ys_t))), height - 1)
            if x0 > x1 or y0 > y1:
                continue
            area = (xs_t[1] - xs_t[0]) * (ys_t[2] - ys_t[0]) - (xs_t[2] - xs_t[0]) * (ys_t[1] - ys_t[0])
            if abs(area) < 1e-9:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            w0 = ((xs_t[1] - gx) * (ys_t[2] - gy) - (xs_t[2] - gx) * (ys_t[1] - gy)) / area
            w1 = ((xs_t[2] - gx) * (ys_t[0] - gy) - (xs_t[0] - gx) * (ys_t[2] - gy)) / area
            w2 = 1.0 - w0 - w1
            inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not inside.any():
                continue
            if ortho:
                z = w0 * d[a] + w1 * d[b] + w2 * d[c]
                iw = None
            else:
                iw = w0 * winv[a] + w1 * winv[b] + w2 * winv[c]
                z = 1.0 / np.where(iw > 0, iw, 1e-12)
            region = (slice(y0, y1 + 1), slice(x0, x1 + 1))
            mask = inside & (z < depth[region])
            if not mask.any():
                continue
            if tex is not None and mesh.uvs is not None:
                uv = np.asarray(mesh.uvs, float)
                if ortho:
                    u = w0 * uv[a, 0] + w1 * uv[b, 0] + w2 * uv[c, 0]
                    v = w0 * uv[a, 1] + w1 * uv[b, 1] + w2 * uv[c, 1]
                else:
                    u = (w0 * uv[a, 0] * winv[a] + w1 * uv[b, 0] * winv[b] + w2 * uv[c, 0] * winv[c]) / iw
                    v = (w0 * uv[a, 1] * winv[a] + w1 * uv[b, 1] * winv[b] + w2 * uv[c, 1] * winv[c]) / iw
                th, tw = tex.shape[:2]
                tx = np.clip((np.mod(u, 1.0) * tw).astype(np.int64), 0, tw - 1)
                ty = np.clip((np.mod(v, 1.0) * th).astype(np.int64), 0, th - 1)
                colour = tex[ty, tx, :3].astype(np.float32) * shade[k]
            else:
                base = mesh.tri_colors[k] if mesh.tri_colors is not None else mesh.color
                colour = np.broadcast_to(np.asarray(base, np.float32) * shade[k], mask.shape + (3,))
            depth[region][mask] = z[mask]
            rgb[region][mask] = colour[mask]
            ids[region][mask] = mesh.object_id
    return np.clip(rgb, 0, 255).astype(np.uint8), ids


def outline(rgb: np.ndarray, ids: np.ndarray, highlight: set[int], color=(255, 220, 60)) -> np.ndarray:
    """Draws a 1-pixel border around every object whose id is in `highlight`."""
    if not highlight:
        return rgb
    mask = np.isin(ids, list(highlight))
    edge = np.zeros_like(mask)
    edge[1:, :] |= mask[1:, :] & ~mask[:-1, :]
    edge[:-1, :] |= mask[:-1, :] & ~mask[1:, :]
    edge[:, 1:] |= mask[:, 1:] & ~mask[:, :-1]
    edge[:, :-1] |= mask[:, :-1] & ~mask[:, 1:]
    out = rgb.copy()
    out[edge] = color
    return out
```

- [ ] **Step 4: Implement the asset half of `previews.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Pictures for the agent and the review packet: asset contact sheets, asset views, site previews.

All images are north-aware: views name their compass direction (north = -Z, east = +X).
"""

from __future__ import annotations

import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from .assets import AssetInfo, GeometryCache
from .geometry import trs_matrix
from .materials import TextureCache
from .meshrender import Camera, DrawMesh, render

_LABEL = (230, 230, 230)
_MAX_WMO_DEPTH = 2


def _font(size: int):
    return ImageFont.load_default(size=size)


def _transform(points: np.ndarray, matrix: np.ndarray) -> np.ndarray:
    return points @ matrix[:3, :3].T + matrix[:3, 3]


def asset_meshes(rel: str, catalog: dict[str, AssetInfo], geometry: GeometryCache, textures: TextureCache,
                 matrix: np.ndarray, object_id: int = 0, _depth: int = 0) -> list[DrawMesh]:
    """World-space draw meshes of one placed asset (a mesh, or a WMO's group meshes)."""
    info = catalog.get(rel)
    if info is None or info.error:
        return []
    if info.kind == "wmo":
        model = geometry.world_model(rel)
        if model is None or _depth >= _MAX_WMO_DEPTH:
            return []
        out = []
        for ref in model.mesh_refs:
            if ref.visible:
                child = matrix @ trs_matrix(ref.position, ref.rotation, ref.scale)
                out += asset_meshes(ref.mesh.replace("\\", "/"), catalog, geometry, textures, child, object_id, _depth + 1)
        return out
    mesh = geometry.mesh(rel)
    if mesh is None:
        return []
    return [DrawMesh(_transform(sub.positions.astype(float), matrix), sub.indices.reshape(-1, 3), uvs=sub.uvs,
                     texture=textures.for_material(sub.material), object_id=object_id)
            for sub in mesh.submeshes if len(sub.indices)]


def _framing(info: AssetInfo) -> tuple[np.ndarray, float]:
    lo, hi = np.asarray(info.bounds_min, float), np.asarray(info.bounds_max, float)
    return (lo + hi) / 2.0, max(float(np.linalg.norm(hi - lo)) / 2.0, 0.25)


def _thumbnail(rel, catalog, geometry, textures, tile: int) -> np.ndarray:
    info = catalog[rel]
    centre, radius = _framing(info)
    direction = np.array([1.0, 0.55, 1.0]) / np.linalg.norm([1.0, 0.55, 1.0])   # from the south-east, above
    camera = Camera(eye=tuple(centre + direction * radius * 2.8), target=tuple(centre), width=tile, height=tile)
    rgb, _ = render(asset_meshes(rel, catalog, geometry, textures, np.eye(4)), camera)
    return rgb


def contact_sheet(rels: list[str], catalog, geometry, textures, tile: int = 128, columns: int = 8,
                  title: str = "") -> Image.Image:
    """One tile per asset: 3/4 view from the south-east, file name, size in metres, C = has collision."""
    label_h, header = 30, 24 if title else 0
    rows = max(1, math.ceil(len(rels) / columns))
    sheet = Image.new("RGB", (columns * tile, header + rows * (tile + label_h)), (18, 18, 18))
    draw = ImageDraw.Draw(sheet)
    if title:
        draw.text((6, 4), title, fill=_LABEL, font=_font(14))
    for index, rel in enumerate(rels):
        x, y = (index % columns) * tile, header + (index // columns) * (tile + label_h)
        info = catalog[rel]
        if not info.error:
            sheet.paste(Image.fromarray(_thumbnail(rel, catalog, geometry, textures, tile)), (x, y))
        name = Path(rel).stem
        draw.text((x + 3, y + tile + 2), name[:22], fill=_LABEL, font=_font(11))
        extra = "unreadable" if info.error else f"{info.size:.1f} m{'  C' if info.has_collision else ''}"
        draw.text((x + 3, y + tile + 15), extra, fill=(160, 160, 160), font=_font(11))
    return sheet


def asset_views(rel: str, catalog, geometry, textures, size: int = 320) -> Image.Image:
    """Front (looking north), side (looking west) and top (north up) orthographic views with a 1 m grid."""
    info = catalog[rel]
    centre, radius = _framing(info)
    extent = max(info.size, 0.5) * 1.25
    views = [("front (looking north)", np.array([0.0, 0.0, 1.0]), (0.0, 1.0, 0.0)),
             ("side (looking west)", np.array([1.0, 0.0, 0.0]), (0.0, 1.0, 0.0)),
             ("top (north up)", np.array([0.0, 1.0, 0.0]), (0.0, 0.0, -1.0))]
    image = Image.new("RGB", (size * 3, size + 22), (18, 18, 18))
    draw = ImageDraw.Draw(image)
    meshes = asset_meshes(rel, catalog, geometry, textures, np.eye(4))
    for index, (label, direction, up) in enumerate(views):
        camera = Camera(eye=tuple(centre + direction * radius * 4.0), target=tuple(centre), width=size, height=size,
                        ortho_height=extent, up=up)
        rgb, ids = render(meshes, camera)
        scale = size / extent
        horizontal = 2 if index == 1 else 0                                 # side view: horizontal axis is Z
        vertical = 2 if index == 2 else 1                                   # top view: vertical axis is Z
        grid = np.zeros(ids.shape, bool)
        for axis, along_rows in ((horizontal, False), (vertical, True)):
            lo = centre[axis] - extent / 2
            for line in range(int(math.floor(lo)), int(math.ceil(lo + extent)) + 1):
                pixel = int(round((line - lo) * scale))
                if 0 <= pixel < size:
                    if along_rows:
                        grid[size - 1 - pixel if axis == 1 else pixel, :] = True
                    else:
                        grid[:, pixel if index != 1 else size - 1 - pixel] = True
        rgb[grid & (ids < 0)] = (48, 48, 48)
        image.paste(Image.fromarray(rgb), (index * size, 22))
        draw.text((index * size + 6, 4), label, fill=_LABEL, font=_font(12))
    return image


def write_contact_sheets(catalog: dict[str, AssetInfo], geometry: GeometryCache, textures: TextureCache,
                         out_dir: Path) -> list[Path]:
    """One sheet per model folder: generated/world/assets/sheets/<folder with _>.png."""
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    folders: dict[str, list[str]] = {}
    for rel in sorted(catalog):
        folders.setdefault(rel.rsplit("/", 1)[0], []).append(rel)
    written = []
    for folder, rels in folders.items():
        path = out_dir / (folder.replace("/", "_") + ".png")
        contact_sheet(rels, catalog, geometry, textures, title=folder).save(path)
        written.append(path)
    return written
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_render -v`
Expected: OK. If `test_texture_quadrants_and_perspective` fails on the vertical orientation, the convention is that V = 0 is the texture's top row (D3D). Fix the sampling in `render` (`ty`), not the test.

- [ ] **Step 6: Add CLI flags.** Extend the `assets` subparser in `__main__.py`:

```python
    assets.add_argument("--sheets", action="store_true", help="write contact sheets to generated/world/assets/sheets")
    assets.add_argument("--views", metavar="ASSET", help="write front/side/top views of one asset")
    assets.add_argument("--out", type=Path, help="output file for --views")
```

Then add to the end of `run_assets` before `return 0`:

```python
    if args.sheets or args.views:
        from .assets import GeometryCache
        from .materials import TextureCache
        from .paths import assets_cache_dir, client_root
        from .previews import asset_views, write_contact_sheets
        geometry, textures = GeometryCache(), TextureCache(client_root())
        if args.sheets:
            paths = write_contact_sheets(catalog, geometry, textures, assets_cache_dir() / "sheets")
            print(f"wrote {len(paths)} contact sheets to {assets_cache_dir() / 'sheets'}")
        if args.views:
            out = args.out or assets_cache_dir() / "views" / (Path(args.views).stem + ".png")
            out.parent.mkdir(parents=True, exist_ok=True)
            asset_views(args.views, catalog, geometry, textures).save(out)
            print(f"wrote {out}")
```

`Path` must be imported in `__main__.py`.

Run: `cd tools/world; py -3.14 -m worldkit assets --sheets` and open `generated/world/assets/sheets/Models_FalwynPlains_Props_Camp.png` with the Read tool.
Expected: recognisable tents, campfires and so on, textured. Then run `py -3.14 -m worldkit assets --views Models/FalwynPlains/Props/Camp/SM_hc_Camptent_B.hmsh` and look at the result: three views with a grid.

- [ ] **Step 7: Commit**

```bash
git add tools/world/worldkit/meshrender.py tools/world/worldkit/previews.py tools/world/worldkit/__main__.py tools/tests/test_world_dressing_render.py
git commit -m "feat(worldkit): offline mesh renderer, contact sheets and asset views"
```

---

### Task 7: Foliage loading and site previews

**Files:**
- Create: `tools/world/worldkit/foliage.py`
- Modify: `tools/world/worldkit/previews.py` (site previews)
- Test: `tools/tests/test_world_dressing_render.py` (add a class)

**Interfaces:**
- Consumes: `load_hfol`, `FoliageFile`, `FoliageInstance` (Task 1); `paths.foliage_dir`; `WorldQuery` (spec 1); `quat_from_yaw_tilt`, `trs_matrix` (Task 4).
- Produces:
  - `foliage.page_foliage_path(directory, page_index, repo=REPO) -> Path`;
  - `foliage.load_world_foliage(directory, repo=REPO) -> dict[int, FoliageFile]`, keyed by page index;
  - `foliage.all_instances(foliage) -> list[FoliageInstance]`;
  - `previews.item_matrix(item) -> ndarray(4,4)`;
  - `previews.site_previews(query, catalog, geometry, textures, center, radius, entities, instances, items, count=3, size=(960, 640)) -> list[(label, Image)]`. Here `items` are draft item dicts; entities and instances whose ids are in the items are skipped, so an applied pass is not drawn twice.

- [ ] **Step 1: Write the failing test.** Add to the imports of `test_world_dressing_render.py`:

```python
from worldkit.foliage import load_world_foliage  # noqa: E402
from worldkit.previews import site_previews  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402
```

and the class:

```python
class SitePreviewTests(unittest.TestCase):
	def test_previews_show_the_new_item_outlined(self):
		with tempfile.TemporaryDirectory() as tmp:
			repo = Path(tmp)
			models = repo / "data" / "client" / "Models" / "Test"
			models.mkdir(parents=True)
			(models / "Cube.hmsh").write_bytes(fx.cube_hmsh())
			fx.make_world(repo, "P", {(32, 32): dict(water={0: (1, 0xFFFFFFFFFFFFFFFF)}, water_heights=np.full((129, 129), 1.0, np.float32))},
						  entities=[dict(asset="Models/Test/Cube.hmsh", position=(60.0, 0.0, 50.0), unique_id=9)])
			foliage = repo / "data" / "client" / "Worlds" / "P" / "P" / "Foliage"
			foliage.mkdir(parents=True)
			(foliage / f"{(32 << 8) | 32}.hfol").write_bytes(fx.hfol_bytes(["Models/Test/Cube.hmsh"], [(77, 0, (45.0, 0.0, 45.0), (1.0, 0.0, 0.0, 0.0), (1.0, 1.0, 1.0), True)]))
			snapshot = build_snapshot("P", repo=repo)
			catalog = build_catalog(repo)
			world_foliage = load_world_foliage("P", repo)
			item = {"role": "r", "asset": "Models/Test/Cube.hmsh", "store": "wobj", "position": [50.0, 0.0, 50.0],
					"yaw": 30.0, "tilt": [0.0, 0.0], "scale": 1.5, "collides": True}
			images = site_previews(WorldQuery(snapshot), catalog, GeometryCache(repo), TextureCache(client_root(repo)),
								   (50.0, 50.0), 20.0, snapshot.entities, [i for f in world_foliage.values() for i in f.instances],
								   [item], count=2, size=(320, 200))
		self.assertEqual(len(images), 2)
		label, image = images[0]
		self.assertIn("looking", label)
		pixels = np.asarray(image)
		self.assertTrue((pixels == [255, 220, 60]).all(axis=2).any())   # the new item is outlined
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_render.SitePreviewTests -v`
Expected: ImportError, `worldkit.foliage`.

- [ ] **Step 3: Implement `foliage.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Foliage of a world: one .hfol per page in Worlds/<W>/<W>/Foliage/<pageIndex>.hfol."""

from __future__ import annotations

from pathlib import Path

from .formats.hfol import FoliageFile, FoliageInstance, load_hfol
from .paths import REPO, foliage_dir


def page_foliage_path(directory: str, page_index: int, repo: Path = REPO) -> Path:
    return foliage_dir(directory, repo) / f"{page_index}.hfol"


def load_world_foliage(directory: str, repo: Path = REPO) -> dict[int, FoliageFile]:
    folder = foliage_dir(directory, repo)
    if not folder.is_dir():
        return {}
    return {int(path.stem): load_hfol(path) for path in sorted(folder.glob("*.hfol")) if path.stem.isdigit()}


def all_instances(foliage: dict[int, FoliageFile]) -> list[FoliageInstance]:
    return [inst for ff in foliage.values() for inst in ff.instances]
```

- [ ] **Step 4: Add the site previews.** Append to `tools/world/worldkit/previews.py`, and add `from .geometry import quat_from_yaw_tilt` to its imports:

```python
_KIND_COLOURS = {"grass": (88, 130, 62), "forest_floor": (70, 105, 52), "path": (150, 120, 80), "road": (160, 135, 95),
                 "dirt": (130, 100, 70), "mud": (100, 80, 60), "rock": (125, 125, 120), "sand": (194, 178, 128),
                 "snow": (235, 235, 240)}
_WATER = (60, 110, 190)
_FIRST_ITEM_ID = 1000
_TERRAIN_STEP = 2.0


def _compass(dx: float, dz: float) -> str:
    angle = (math.degrees(math.atan2(dx, -dz)) + 360.0) % 360.0   # 0 = north (-Z), 90 = east (+X)
    return ["north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west"][int((angle + 22.5) // 45) % 8]


def terrain_meshes(query, cx: float, cz: float, radius: float) -> list[DrawMesh]:
    """Ground (coloured by terrain kind, holes left open) and water surfaces around a point."""
    steps = int(math.ceil(radius / _TERRAIN_STEP))
    xs = cx + (np.arange(-steps, steps + 1)) * _TERRAIN_STEP
    zs = cz + (np.arange(-steps, steps + 1)) * _TERRAIN_STEP
    n = len(xs)
    heights = np.full((n, n), np.nan)
    for j, z in enumerate(zs):
        for i, x in enumerate(xs):
            h = query.height_at(float(x), float(z))
            if h is not None:
                heights[j, i] = h
    positions = np.stack([np.repeat(xs[None, :], n, 0), np.nan_to_num(heights), np.repeat(zs[:, None], n, 1)], -1).reshape(-1, 3)
    tris, colours, water_quads = [], [], []
    for j in range(n - 1):
        for i in range(n - 1):
            if np.isnan(heights[j:j + 2, i:i + 2]).any():
                continue
            mx, mz = float(xs[i] + _TERRAIN_STEP / 2), float(zs[j] + _TERRAIN_STEP / 2)
            if query.hole_at(mx, mz):
                continue
            a, b, c, d = j * n + i, j * n + i + 1, (j + 1) * n + i, (j + 1) * n + i + 1
            colour = _KIND_COLOURS.get(query.terrain_kind_at(mx, mz) or "grass", _KIND_COLOURS["grass"])
            tris += [(a, b, d), (a, d, c)]
            colours += [colour, colour]
            depth = query.water_depth_at(mx, mz)
            if depth > 0.05:
                level = float(np.mean(heights[j:j + 2, i:i + 2])) + depth
                water_quads.append((float(xs[i]), float(zs[j]), level))
    meshes = [DrawMesh(positions, np.asarray(tris, np.int64).reshape(-1, 3), tri_colors=np.asarray(colours, np.float32),
                       object_id=-2)] if tris else []
    if water_quads:
        points, faces = [], []
        for x, z, level in water_quads:
            base = len(points)
            points += [(x, level, z), (x + _TERRAIN_STEP, level, z), (x, level, z + _TERRAIN_STEP),
                       (x + _TERRAIN_STEP, level, z + _TERRAIN_STEP)]
            faces += [(base, base + 1, base + 3), (base, base + 3, base + 2)]
        meshes.append(DrawMesh(np.asarray(points, float), np.asarray(faces, np.int64), color=_WATER, object_id=-3))
    return meshes


def item_matrix(item: dict) -> np.ndarray:
    pitch, roll = item.get("tilt", [0.0, 0.0])
    scale = float(item["scale"])
    return trs_matrix(item["position"], quat_from_yaw_tilt(float(item["yaw"]), float(pitch), float(roll)), (scale, scale, scale))


def _item_id(item: dict) -> int | None:
    value = item.get("unique_id")
    return int(value, 16) if isinstance(value, str) else value


def site_previews(query, catalog, geometry, textures, center, radius: float, entities, instances, items,
                  count: int = 3, size=(960, 640)) -> list[tuple[str, Image.Image]]:
    """Perspective views around a site: terrain, existing props and trees, and the draft items outlined."""
    from .meshrender import outline
    cx, cz = center
    ground = query.height_at(cx, cz) or 0.0
    reach = radius * 1.4
    skip = {_item_id(i) for i in items if _item_id(i) is not None}
    meshes = terrain_meshes(query, cx, cz, reach)
    for entity in entities:
        if entity.unique_id in skip or math.hypot(entity.position[0] - cx, entity.position[2] - cz) > reach:
            continue
        meshes += asset_meshes(entity.asset, catalog, geometry, textures,
                               trs_matrix(entity.position, entity.rotation, entity.scale), object_id=-4)
    for inst in instances:
        if inst.unique_id in skip or math.hypot(inst.position[0] - cx, inst.position[2] - cz) > reach:
            continue
        meshes += asset_meshes(inst.mesh, catalog, geometry, textures, trs_matrix(inst.position, inst.rotation, inst.scale),
                               object_id=-5)
    highlight = set()
    for index, item in enumerate(items):
        meshes += asset_meshes(item["asset"], catalog, geometry, textures, item_matrix(item), object_id=_FIRST_ITEM_ID + index)
        highlight.add(_FIRST_ITEM_ID + index)
    out = []
    for k in range(count):
        azimuth = math.radians(30.0 + k * 360.0 / count)
        eye = (cx + math.sin(azimuth) * radius * 1.7, ground + radius * 0.9, cz + math.cos(azimuth) * radius * 1.7)
        camera = Camera(eye=eye, target=(cx, ground + 2.0, cz), width=size[0], height=size[1], fov_deg=50.0)
        rgb, ids = render(meshes, camera, background=(150, 180, 215))
        image = Image.fromarray(outline(rgb, ids, highlight))
        label = f"preview {k + 1} - looking {_compass(cx - eye[0], cz - eye[2])}"
        ImageDraw.Draw(image).text((8, 6), label, fill=(20, 20, 20), font=_font(14))
        out.append((label, image))
    return out
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_render -v`
Expected: OK.

- [ ] **Step 6: Commit**

```bash
git add tools/world/worldkit/foliage.py tools/world/worldkit/previews.py tools/tests/test_world_dressing_render.py
git commit -m "feat(worldkit): site previews with terrain, water, props and outlined draft items"
```

---

### Task 8: Prop placement checks

**Files:**
- Create: `tools/world/worldkit/prop_lint.py`
- Test: `tools/tests/test_world_dressing_lint.py`

**Interfaces:**
- Consumes: `Violation` (`worldkit/lint.py`); `AssetInfo` (Task 5); `TagRules`, `AssetTags` (Task 5); `WorldQuery`; `SpawnRecord`, `is_grid_pack`; `atlas.contains`; `terrain_kinds.ROAD_KINDS`, `SUBMERGED_DEPTH`; `quat_to_matrix`, `yaw_of_quat` (Task 4); `FoliageFile` (Task 1).
- Produces:
  - `PlacedProp(key, asset, store, position, rotation, scale, collides)`, with properties `.x` and `.z`;
  - `props_from_entities(entities) -> list[PlacedProp]`, `props_from_foliage(foliage) -> list[PlacedProp]`;
  - `footprint_corners(prop, info, tags) -> ndarray(4,3)`, `bottom_center(prop, info) -> ndarray(3)`;
  - `PropContext(query, catalog, rules, existing, spawns, poi=None, roads=())`;
  - `lint_prop(prop, context, others) -> list[Violation]`, `lint_props(props, context) -> list[Violation]`, `lint_world_props(context) -> list[Violation]`;
  - constants `FLOAT_TOLERANCE = 0.3`, `ROAD_CLEARANCE = 3.0`, `WATER_LIMIT = 0.3`.

- [ ] **Step 1: Write the failing tests.** Create `tools/tests/test_world_dressing_lint.py`:

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the prop placement checks.

	python tools/tests/test_world_dressing_lint.py
"""

import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit.assets import build_catalog  # noqa: E402
from worldkit.constants import CELL_SIZE, PIXEL_SIZE  # noqa: E402
from worldkit.geometry import quat_from_yaw_tilt  # noqa: E402
from worldkit.prop_lint import PlacedProp, PropContext, lint_props, lint_world_props, props_from_entities  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402
from worldkit.spawns import SpawnRecord, spawn_key  # noqa: E402
from worldkit.tags import TagRules  # noqa: E402
from worldkit.terrain_kinds import TerrainKinds  # noqa: E402

RULES = TagRules([
	{"match": "models/test/cube*", "tags": ["crate"], "max_slope": 15, "sink": 0.05},
	{"match": "models/test/rock*", "tags": ["rock"], "max_slope": 90, "sink": 0.4, "align_to_slope": True, "may_overlap": ["rock"]},
])


def lint_world(repo: Path):
	"""Page 32_32: flat at 0 with a 45 degree ramp for x >= 300, water in tile (6, 6), a hole in tile (8, 0),
	a painted road strip x 100..110, and an existing cube at (150, 150)."""
	outer = np.zeros((129, 129), np.float32)
	outer[:, 72:] = (np.arange(57, dtype=np.float32) * CELL_SIZE)[None, :]
	inner = ((outer[:-1, :-1] + outer[:-1, 1:] + outer[1:, :-1] + outer[1:, 1:]) * 0.25).astype(np.float32)
	layers = np.full((1009, 1009), 0x000000FF, np.uint32)
	layers[:, int(100 / PIXEL_SIZE):int(110 / PIXEL_SIZE)] = 0x0000FF00
	fx.make_world(repo, "L", {(32, 32): dict(outer=outer, inner=inner, layers=layers, materials=["Models/Terrain/B.hmi"] * 256,
										  holes={8: 0xFFFFFFFFFFFFFFFF}, water={102: (1, 0xFFFFFFFFFFFFFFFF)},
										  water_heights=np.full((129, 129), 2.0, np.float32))},
				  entities=[dict(asset="Models/Test/Cube.hmsh", position=(150.0, 0.0, 150.0), unique_id=5)])
	models = repo / "data" / "client" / "Models" / "Test"
	models.mkdir(parents=True)
	(models / "Cube.hmsh").write_bytes(fx.cube_hmsh())
	(models / "Rock.hmsh").write_bytes(fx.cube_hmsh(collision=False))


def spawn(x, z):
	return SpawnRecord(spawn_key("unit", 0, 1, "S", x, z), 0, "unit", 1, 0, 0, "S", x, 0.0, z, True, 0, 30000, ())


def prop(x, z, y=0.0, asset="Models/Test/Cube.hmsh", collides=True, yaw=0.0, key="new"):
	return PlacedProp(key, asset, "wobj", (x, y, z), quat_from_yaw_tilt(yaw), 1.0, collides)


class PropLintTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls._tmp = tempfile.TemporaryDirectory()
		repo = Path(cls._tmp.name)
		lint_world(repo)
		snapshot = build_snapshot("L", repo=repo)
		cls.query = WorldQuery(snapshot, kinds=TerrainKinds({"Models/Terrain/B.hmi": ["grass", "road", "rock", "dirt"]}))
		cls.catalog = build_catalog(repo)
		cls.existing = props_from_entities(snapshot.entities)

	@classmethod
	def tearDownClass(cls):
		cls._tmp.cleanup()

	def context(self, poi=None, roads=(), spawns=()):
		return PropContext(self.query, self.catalog, RULES, self.existing, list(spawns), poi, list(roads))

	def rules(self, props, **kwargs):
		return {(v.rule, v.severity) for v in lint_props(props, self.context(**kwargs))}

	def test_clean(self):
		self.assertEqual(self.rules([prop(50.0, 50.0)]), set())

	def test_floating_and_buried(self):
		self.assertIn(("prop_floating", "error"), self.rules([prop(50.0, 50.0, y=1.0)]))
		self.assertIn(("prop_buried", "error"), self.rules([prop(50.0, 50.0, y=-1.0)]))

	def test_slope_allows_aligned_rocks(self):
		self.assertIn(("prop_slope", "error"), self.rules([prop(320.0, 50.0, y=self.query.height_at(320.0, 50.0))]))
		rock = prop(320.0, 80.0, asset="Models/Test/Rock.hmsh", y=self.query.height_at(320.0, 80.0))
		self.assertNotIn(("prop_slope", "error"), self.rules([rock]))

	def test_overlap(self):
		self.assertIn(("prop_overlap", "error"), self.rules([prop(150.5, 150.0)]))
		rocks = [prop(60.0, 60.0, asset="Models/Test/Rock.hmsh", key="a"), prop(60.5, 60.0, asset="Models/Test/Rock.hmsh", key="b")]
		self.assertNotIn(("prop_overlap", "error"), self.rules(rocks))

	def test_roads(self):
		self.assertIn(("prop_on_road", "error"), self.rules([prop(105.0, 300.0)]))
		self.assertIn(("prop_on_road", "warning"), self.rules([prop(105.0, 300.0, collides=False)]))
		self.assertIn(("prop_on_road", "error"), self.rules([prop(50.0, 401.5)], roads=[[[0.0, 400.0], [90.0, 400.0]]]))

	def test_water_hole_edge(self):
		self.assertIn(("prop_in_water", "error"), self.rules([prop(216.0, 216.0)]))
		self.assertIn(("prop_over_hole", "error"), self.rules([prop(283.0, 16.0)]))
		self.assertIn(("prop_terrain_edge", "error"), self.rules([prop(2.0, 250.0)]))

	def test_spawn_poi_grid_unknown(self):
		self.assertIn(("prop_on_spawn", "warning"), self.rules([prop(60.0, 70.0)], spawns=[spawn(60.0, 70.0)]))
		poi = {"id": "p", "center": [50.0, 50.0], "radius": 20.0}
		self.assertIn(("prop_outside_poi", "warning"), self.rules([prop(50.0, 90.0)], poi=poi))
		grid = [prop(160.0 + i * 14.0, 200.0 + j * 14.0, key=f"g{i}{j}") for i in range(3) for j in range(3)]
		self.assertIn(("prop_grid_pattern", "warning"), self.rules(grid))
		self.assertIn(("prop_unknown_asset", "error"), self.rules([prop(50.0, 50.0, asset="Models/Nope.hmsh")]))

	def test_world_lint_covers_existing_props(self):
		violations = lint_world_props(self.context())
		self.assertEqual([v for v in violations if v.severity == "error"], [])


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_lint -v`
Expected: ImportError, `worldkit.prop_lint`.

- [ ] **Step 3: Implement `prop_lint.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Placement checks for props and trees (spec 2, section 4.6).

Every check uses the prop's rotated footprint rectangle from the asset catalog (bounds x scale, trees
shrunk to their trunk by footprint_scale), not just its origin. Errors are placements a player would
notice as broken; warnings are design smells. Tags relax rules (sink, max_slope, may_overlap).
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np

from .atlas import contains
from .geometry import quat_to_matrix
from .lint import Violation
from .spawns import is_grid_pack
from .terrain_kinds import ROAD_KINDS, SUBMERGED_DEPTH

FLOAT_TOLERANCE = 0.3
ROAD_CLEARANCE = 3.0
WATER_LIMIT = 0.3
GRID_MIN = 5


@dataclass(frozen=True)
class PlacedProp:
    key: str                                       # "wobj:<id>", "hfol:<id>" or "<pass>:<index>"
    asset: str
    store: str                                     # "wobj" | "hfol"
    position: tuple[float, float, float]
    rotation: tuple[float, float, float, float]    # (w, x, y, z)
    scale: float
    collides: bool

    @property
    def x(self) -> float:
        return self.position[0]

    @property
    def z(self) -> float:
        return self.position[2]


@dataclass
class PropContext:
    query: object
    catalog: dict
    rules: object
    existing: list[PlacedProp]
    spawns: list = field(default_factory=list)
    poi: dict | None = None
    roads: list = field(default_factory=list)      # atlas road point lists [[x, z], ...]


def props_from_entities(entities) -> list[PlacedProp]:
    return [PlacedProp(f"wobj:{e.unique_id}", e.asset.replace("\\", "/"), "wobj", e.position, e.rotation,
                       max(abs(e.scale[0]), abs(e.scale[2])), True) for e in entities]


def props_from_foliage(foliage: dict) -> list[PlacedProp]:
    return [PlacedProp(f"hfol:{i.unique_id}", i.mesh.replace("\\", "/"), "hfol", i.position, i.rotation,
                       max(abs(i.scale[0]), abs(i.scale[2])), i.collides) for ff in foliage.values() for i in ff.instances]


def footprint_corners(prop: PlacedProp, info, tags) -> np.ndarray:
    """World positions of the four bottom corners of the (tag-shrunk) footprint rectangle."""
    x0, z0, x1, z1 = info.footprint
    fs = tags.footprint_scale
    y = info.bounds_min[1]
    local = np.array([[x0 * fs, y, z0 * fs], [x1 * fs, y, z0 * fs], [x1 * fs, y, z1 * fs], [x0 * fs, y, z1 * fs]]) * prop.scale
    return local @ quat_to_matrix(prop.rotation).T + np.asarray(prop.position, float)


def bottom_center(prop: PlacedProp, info) -> np.ndarray:
    return quat_to_matrix(prop.rotation) @ np.array([0.0, info.bounds_min[1] * prop.scale, 0.0]) + np.asarray(prop.position, float)


def _quads_overlap(a: np.ndarray, b: np.ndarray) -> bool:
    """Separating-axis test for two convex quads given as (4, 2) arrays."""
    for poly in (a, b):
        for i in range(4):
            edge = poly[(i + 1) % 4] - poly[i]
            axis = np.array([-edge[1], edge[0]])
            pa, pb = a @ axis, b @ axis
            if pa.max() <= pb.min() + 1e-6 or pb.max() <= pa.min() + 1e-6:
                return False
    return True


def _inside_quad(point, quad: np.ndarray) -> bool:
    sign = None
    for i in range(4):
        edge = quad[(i + 1) % 4] - quad[i]
        cross = edge[0] * (point[1] - quad[i][1]) - edge[1] * (point[0] - quad[i][0])
        if abs(cross) < 1e-9:
            continue
        if sign is None:
            sign = cross > 0
        elif (cross > 0) != sign:
            return False
    return True


def _segment_distance(px, pz, ax, az, bx, bz) -> float:
    dx, dz = bx - ax, bz - az
    length = dx * dx + dz * dz
    t = 0.0 if length == 0 else max(0.0, min(1.0, ((px - ax) * dx + (pz - az) * dz) / length))
    return math.hypot(px - (ax + t * dx), pz - (az + t * dz))


def _painted_road(query, x: float, z: float) -> bool:
    return query.terrain_kind_at(x, z) in ROAD_KINDS and query.water_depth_at(x, z) <= SUBMERGED_DEPTH


def lint_prop(prop: PlacedProp, context: PropContext, others: list[PlacedProp]) -> list[Violation]:
    found: list[Violation] = []
    name = prop.asset.rsplit("/", 1)[-1]

    def add(rule: str, severity: str, text: str) -> None:
        found.append(Violation(rule, severity, prop.key, f"{name} at ({prop.x:.1f}, {prop.z:.1f}): {text}", prop.x, prop.z))

    info = context.catalog.get(prop.asset)
    if info is None or info.error:
        add("prop_unknown_asset", "error", "asset is not in the catalog or unreadable")
        return found
    q = context.query
    tags = context.rules.for_asset(prop.asset)
    corners = footprint_corners(prop, info, tags)
    quad = corners[:, [0, 2]]
    points = [(prop.x, prop.z)] + [(float(c[0]), float(c[2])) for c in corners]
    if any(not q.has_terrain(px, pz) for px, pz in points) or not q.edge_clear(prop.x, prop.z):
        add("prop_terrain_edge", "error", "footprint reaches the terrain edge")
        return found
    if q.hole_at(prop.x, prop.z):
        add("prop_over_hole", "error", "stands over a terrain hole")
    depth = q.water_depth_at(prop.x, prop.z)
    if depth > WATER_LIMIT and not tags.tags & {"pier", "bridge"}:
        add("prop_in_water", "error", f"base is under {depth:.1f} m of water")
    gap = max(float(c[1]) - q.height_at(float(c[0]), float(c[2])) for c in corners)
    if gap > FLOAT_TOLERANCE:
        add("prop_floating", "error", f"a footprint corner floats {gap:.2f} m above the ground")
    bottom = bottom_center(prop, info)
    buried = q.height_at(prop.x, prop.z) - float(bottom[1])
    if buried > tags.sink + FLOAT_TOLERANCE:
        add("prop_buried", "error", f"base is {buried:.2f} m below the ground (allowed {tags.sink:.2f} m)")
    slope = max(q.slope_at(px, pz) or 0.0 for px, pz in points)
    if slope > tags.max_slope:
        add("prop_slope", "error", f"ground slope {slope:.0f} deg exceeds {tags.max_slope:.0f} deg for this asset")
    radius = info.radius * prop.scale * tags.footprint_scale
    near_road = any(_painted_road(q, px, pz) for px, pz in points) or any(
        _segment_distance(prop.x, prop.z, *road[i], *road[i + 1]) - radius < ROAD_CLEARANCE
        for road in context.roads for i in range(len(road) - 1))
    if near_road:
        add("prop_on_road", "error" if prop.collides else "warning", "stands on or next to a road")
    for other in others:
        if other.key == prop.key:
            continue
        other_info = context.catalog.get(other.asset)
        if other_info is None or other_info.error:
            continue
        other_tags = context.rules.for_asset(other.asset)
        reach = radius + other_info.radius * other.scale * other_tags.footprint_scale
        if math.hypot(other.x - prop.x, other.z - prop.z) > reach:
            continue
        if tags.tags & other_tags.may_overlap or other_tags.tags & tags.may_overlap:
            continue
        if _quads_overlap(quad, footprint_corners(other, other_info, other_tags)[:, [0, 2]]):
            add("prop_overlap", "error", f"overlaps {other.asset.rsplit('/', 1)[-1]} ({other.key})")
            break
    for record in context.spawns:
        if record.active and _inside_quad((record.x, record.z), quad):
            add("prop_on_spawn", "warning", f"covers the spawn point of {record.name or record.key}")
            break
    if context.poi is not None and not contains(context.poi, prop.x, prop.z):
        add("prop_outside_poi", "warning", f"outside the place '{context.poi.get('id')}'")
    return found


def _grid_warnings(props: list[PlacedProp]) -> list[Violation]:
    by_asset: dict[str, list[PlacedProp]] = {}
    for prop in props:
        by_asset.setdefault(prop.asset, []).append(prop)
    out = []
    for asset, group in sorted(by_asset.items()):
        if len(group) >= GRID_MIN and is_grid_pack(group, min_size=GRID_MIN):
            first = min(group, key=lambda p: p.key)
            out.append(Violation("prop_grid_pattern", "warning", f"grid:{first.key}",
                                 f"{len(group)} x {asset.rsplit('/', 1)[-1]} are evenly spaced like a machine-made grid",
                                 first.x, first.z))
    return out


def _sorted(violations: list[Violation]) -> list[Violation]:
    return sorted(violations, key=lambda v: (v.severity != "error", v.rule, v.subject))


def lint_props(props: list[PlacedProp], context: PropContext) -> list[Violation]:
    """New props checked against the world and against each other."""
    others = [*context.existing, *props]
    out = [v for prop in props for v in lint_prop(prop, context, others)]
    return _sorted(out + _grid_warnings(props))


def lint_world_props(context: PropContext) -> list[Violation]:
    """Every existing prop and tree, for the content audit's props domain."""
    out = [v for prop in context.existing for v in lint_prop(prop, context, context.existing)]
    return _sorted(out + _grid_warnings(context.existing))
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_lint -v`
Expected: OK. If a threshold test fails, fix the fixture coordinates rather than the thresholds. The thresholds are in the spec.

- [ ] **Step 5: Commit**

```bash
git add tools/world/worldkit/prop_lint.py tools/tests/test_world_dressing_lint.py
git commit -m "feat(worldkit): prop placement checks (ground contact, slope, overlap, roads, water)"
```

---

### Task 9: Site templates and the placer

**Files:**
- Create: `tools/world/worldkit/templates.py`
- Create: `tools/world/templates/quarry.json`, `cave_mouth.json`, `waterfall_basin.json`, `hunting_camp.json`, `abbey_surround.json`
- Test: `tools/tests/test_world_dressing_templates.py`

**Interfaces:**
- Consumes: `select_assets` (Task 5); `lint_prop`, `PlacedProp`, `PropContext` (Task 8); `quat_from_yaw_tilt`, `quat_to_matrix` (Task 4); `ROAD_KINDS`; `paths.templates_dir`.
- Produces:
  - `Role`, `Template`, `load_template(name, directory=None) -> Template`;
  - `settle(info, tags, query, x, z, yaw, scale) -> (y, pitch, roll) | None`;
  - `item_rotation(item) -> quat`, `item_to_prop(item, key) -> PlacedProp`;
  - `PlacementResult(items, gaps, entry)`;
  - `place(template, anchor, radius, context, seed, keep_away=()) -> PlacementResult`.
- **Draft item dict**, used by every later task: `{"role", "asset", "store", "position": [x, y, z], "yaw", "tilt": [pitch, roll], "scale", "collides"}`.

- [ ] **Step 1: Write the failing tests.** Create `tools/tests/test_world_dressing_templates.py`:

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for site templates and the placer.

	python tools/tests/test_world_dressing_templates.py
"""

import json
import math
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402
from test_world_dressing_lint import RULES, lint_world  # noqa: E402

from worldkit.assets import build_catalog  # noqa: E402
from worldkit.prop_lint import PropContext, lint_props, props_from_entities  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402
from worldkit.templates import item_to_prop, load_template, place, settle  # noqa: E402
from worldkit.terrain_kinds import TerrainKinds  # noqa: E402

TEMPLATE = {
	"version": 1, "name": "t", "description": "test", "entry": "fire", "clear": [3.0], "notes": ["n"],
	"roles": [
		{"name": "fire", "query": {"tags": ["crate"]}, "count": [1, 1], "spacing": 0, "store": "wobj", "rule": {"type": "at_anchor", "offset": [5.0, 0.0]}},
		{"name": "stones", "query": {"tags": ["rock"]}, "count": [6, 6], "spacing": 4.0, "store": "hfol", "rule": {"type": "scatter", "radius_fraction": 0.9}},
		{"name": "near", "query": {"tags": ["crate"]}, "count": [2, 2], "spacing": 3.0, "store": "wobj", "rule": {"type": "near_role", "role": "fire", "distance": [3.0, 6.0]}},
		{"name": "nothing", "query": {"tags": ["bridge"]}, "count": [1, 1], "spacing": 0, "store": "wobj", "rule": {"type": "scatter"}},
	],
}


class TemplateTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls._tmp = tempfile.TemporaryDirectory()
		repo = Path(cls._tmp.name)
		lint_world(repo)
		snapshot = build_snapshot("L", repo=repo)
		cls.query = WorldQuery(snapshot, kinds=TerrainKinds({"Models/Terrain/B.hmi": ["grass", "road", "rock", "dirt"]}))
		cls.catalog = build_catalog(repo)
		cls.poi = {"id": "site", "center": [50.0, 60.0], "radius": 30.0}
		cls.context = PropContext(cls.query, cls.catalog, RULES, props_from_entities(snapshot.entities), [], cls.poi, [])
		folder = repo / "templates"
		folder.mkdir()
		(folder / "t.json").write_text(json.dumps(TEMPLATE), encoding="utf-8")
		cls.template = load_template("t", folder)

	@classmethod
	def tearDownClass(cls):
		cls._tmp.cleanup()

	def run_place(self, seed=4):
		return place(self.template, (50.0, 60.0), 30.0, self.context, seed)

	def test_deterministic_per_seed(self):
		self.assertEqual(self.run_place(4).items, self.run_place(4).items)
		self.assertNotEqual(self.run_place(4).items, self.run_place(5).items)

	def test_roles_rules_and_gaps(self):
		result = self.run_place()
		roles = [i["role"] for i in result.items]
		self.assertEqual(roles.count("fire"), 1)
		self.assertEqual(roles.count("stones"), 6)
		fire = next(i for i in result.items if i["role"] == "fire")
		self.assertEqual(fire["position"][0], 55.0)
		self.assertEqual(result.entry, (55.0, 60.0))
		for item in result.items:
			if item["role"] == "near":
				self.assertLessEqual(math.hypot(item["position"][0] - 55.0, item["position"][2] - 60.0), 6.01)
		stones = [i for i in result.items if i["role"] == "stones"]
		for a in stones:
			for b in stones:
				if a is not b:
					self.assertGreaterEqual(math.hypot(a["position"][0] - b["position"][0], a["position"][2] - b["position"][2]), 4.0)
		self.assertIn({"role": "nothing", "kind": "asset", "wanted": 1, "placed": 0}, result.gaps)
		self.assertTrue(all(i["store"] == "hfol" for i in stones))

	def test_clear_circle_and_lint_clean(self):
		result = self.run_place()
		for item in result.items:
			if item["role"] != "fire":
				self.assertGreaterEqual(math.hypot(item["position"][0] - 50.0, item["position"][2] - 60.0), 3.0)
		props = [item_to_prop(item, f"d:{i}") for i, item in enumerate(result.items)]
		self.assertEqual([v for v in lint_props(props, self.context) if v.severity == "error"], [])

	def test_settle_on_flat_ground(self):
		info = self.catalog["Models/Test/Cube.hmsh"]
		y, pitch, roll = settle(info, RULES.for_asset("Models/Test/Cube.hmsh"), self.query, 50.0, 50.0, 0.0, 1.0)
		self.assertAlmostEqual(y, -0.025, places=3)
		self.assertEqual((pitch, roll), (0.0, 0.0))

	def test_shipped_templates_load(self):
		for name in ("quarry", "cave_mouth", "waterfall_basin", "hunting_camp", "abbey_surround"):
			self.assertTrue(load_template(name).roles, name)


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_templates -v`
Expected: ImportError, `worldkit.templates`.

- [ ] **Step 3: Implement `templates.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Site templates (tools/world/templates/<name>.json) and the placer that fits one onto a place.

A template lists roles. Each role queries assets by tag, wants a count, keeps a spacing to its own
items, says where its items go (rule) and whether they are props (wobj) or trees/plants (hfol).
Rules: at_anchor, ring, scatter, cluster, against_cliff, along_path, near_role. The placer is
deterministic for a seed, tries several spots per item, and keeps the first one that passes every
placement error check; unfillable roles become gaps (kind "asset": nothing matches the query;
kind "space": no valid spot), never silent drops.
"""

from __future__ import annotations

import json
import math
import random
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from .assets import select_assets
from .geometry import quat_from_yaw_tilt, quat_to_matrix
from .paths import templates_dir
from .prop_lint import PlacedProp, PropContext, lint_prop
from .terrain_kinds import ROAD_KINDS

RULE_TYPES = {"at_anchor", "ring", "scatter", "cluster", "against_cliff", "along_path", "near_role"}
ATTEMPTS = 40
GRID_STEP = 2.0
_DIRECTIONS = [(math.cos(a), math.sin(a)) for a in (i * math.pi / 4.0 for i in range(8))]


@dataclass(frozen=True)
class Role:
    name: str
    tags: tuple[str, ...]
    exclude: tuple[str, ...]
    size: str | None
    assets: tuple[str, ...] | None
    count: tuple[int, int]
    spacing: float
    store: str
    rule: dict
    scale: tuple[float, float] | None
    yaw: tuple[float, float] | None


@dataclass(frozen=True)
class Template:
    name: str
    description: str
    entry: str                   # "anchor" or a role name (its first item)
    clear: tuple[float, ...]     # radii around the anchor that stay empty
    notes: tuple[str, ...]       # copied into the review packet (e.g. "awaiting water")
    roles: tuple[Role, ...]


@dataclass
class PlacementResult:
    items: list[dict]
    gaps: list[dict]
    entry: tuple[float, float]


def load_template(name: str, directory: Path | None = None) -> Template:
    path = Path(directory or templates_dir()) / f"{name}.json"
    doc = json.loads(path.read_text(encoding="utf-8"))
    roles = []
    for raw in doc["roles"]:
        rule = raw["rule"]
        if rule.get("type") not in RULE_TYPES:
            raise ValueError(f"{path}: role {raw['name']!r} has unknown rule {rule.get('type')!r}")
        if raw.get("store", "wobj") not in ("wobj", "hfol"):
            raise ValueError(f"{path}: role {raw['name']!r} store must be wobj or hfol")
        query = raw.get("query", {})
        roles.append(Role(raw["name"], tuple(query.get("tags", [])), tuple(query.get("exclude", [])), query.get("size"),
                          tuple(query["assets"]) if "assets" in query else None, tuple(raw["count"]),
                          float(raw.get("spacing", 0.0)), raw.get("store", "wobj"), rule,
                          tuple(raw["scale"]) if "scale" in raw else None, tuple(raw["yaw"]) if "yaw" in raw else None))
    return Template(doc["name"], doc.get("description", ""), doc.get("entry", "anchor"), tuple(doc.get("clear", [])),
                    tuple(doc.get("notes", [])), tuple(roles))


def _local_rect(info, tags, scale):
    x0, z0, x1, z1 = info.footprint
    fs = tags.footprint_scale * scale
    return [(x0 * fs, z0 * fs), (x1 * fs, z0 * fs), (x1 * fs, z1 * fs), (x0 * fs, z1 * fs)]


def settle(info, tags, query, x: float, z: float, yaw: float, scale: float):
    """Height and tilt that put the asset on the ground: aligned assets follow the terrain normal; others
    stand upright with their lowest footprint corner on the ground. Both sink by half the tag's allowance."""
    ground = query.height_at(x, z)
    if ground is None:
        return None
    pitch = roll = 0.0
    if tags.align_to_slope:
        samples = [query.height_at(x + 1.0, z), query.height_at(x - 1.0, z), query.height_at(x, z + 1.0), query.height_at(x, z - 1.0)]
        if None in samples:
            return None
        normal = np.array([-(samples[0] - samples[1]) / 2.0, 1.0, -(samples[2] - samples[3]) / 2.0])
        normal /= np.linalg.norm(normal)
        roll = -math.degrees(math.asin(float(normal[0])))
        pitch = math.degrees(math.atan2(float(normal[2]), float(normal[1])))
        rotation = quat_to_matrix(quat_from_yaw_tilt(yaw, pitch, roll))
        y = ground - float((rotation @ np.array([0.0, info.bounds_min[1] * scale, 0.0]))[1]) - tags.sink * 0.5
    else:
        rotation = quat_to_matrix(quat_from_yaw_tilt(yaw))
        grounds = [ground]
        for lx, lz in _local_rect(info, tags, scale):
            wx, _, wz = rotation @ np.array([lx, 0.0, lz])
            h = query.height_at(x + float(wx), z + float(wz))
            if h is None:
                return None
            grounds.append(h)
        y = min(grounds) - tags.sink * 0.5 - info.bounds_min[1] * scale
    return round(y, 3), round(pitch, 2), round(roll, 2)


def item_rotation(item: dict):
    pitch, roll = item.get("tilt", [0.0, 0.0])
    return quat_from_yaw_tilt(float(item["yaw"]), float(pitch), float(roll))


def item_to_prop(item: dict, key: str) -> PlacedProp:
    return PlacedProp(key, item["asset"], item["store"], tuple(item["position"]), item_rotation(item),
                      float(item["scale"]), bool(item["collides"]))


def _in_disc(rng: random.Random, cx: float, cz: float, radius: float) -> tuple[float, float]:
    angle = rng.uniform(0.0, 2.0 * math.pi)
    distance = radius * math.sqrt(rng.random())
    return cx + math.cos(angle) * distance, cz + math.sin(angle) * distance


def _grid(cx, cz, radius):
    steps = int(radius // GRID_STEP)
    for j in range(-steps, steps + 1):
        for i in range(-steps, steps + 1):
            x, z = cx + i * GRID_STEP, cz + j * GRID_STEP
            if math.hypot(x - cx, z - cz) <= radius:
                yield x, z


def _cliff_spots(query, cx, cz, radius, min_slope, offset):
    spots = []
    for x, z in _grid(cx, cz, radius):
        own = query.slope_at(x, z)
        if own is None or own > 35.0:
            continue
        for dx, dz in _DIRECTIONS:
            nx, nz = x + dx * offset, z + dz * offset
            slope = query.slope_at(nx, nz)
            if slope is not None and slope >= min_slope:
                spots.append((x, z, math.degrees(math.atan2(x - nx, z - nz))))   # facing away from the cliff
                break
    return spots


def _path_spots(query, roads, cx, cz, radius):
    spots = [(x, z) for x, z in _grid(cx, cz, radius) if query.terrain_kind_at(x, z) in ROAD_KINDS]
    for road in roads:
        for (ax, az), (bx, bz) in zip(road, road[1:]):
            steps = max(1, int(math.hypot(bx - ax, bz - az) // GRID_STEP))
            for k in range(steps + 1):
                x, z = ax + (bx - ax) * k / steps, az + (bz - az) * k / steps
                if math.hypot(x - cx, z - cz) <= radius:
                    spots.append((x, z))
    return spots


def _candidate(role: Role, anchor, radius, context: PropContext, rng: random.Random, by_role: dict, state: dict):
    rule = role.rule
    kind = rule["type"]
    ax, az = anchor
    if kind == "at_anchor":
        ox, oz = rule.get("offset", [0.0, 0.0])
        return ax + ox, az + oz, None
    if kind == "ring":
        angle = rng.uniform(0.0, 2.0 * math.pi)
        distance = rng.uniform(float(rule["r1"]), float(rule["r2"]))
        return ax + math.cos(angle) * distance, az + math.sin(angle) * distance, None
    if kind == "scatter":
        x, z = _in_disc(rng, ax, az, radius * float(rule.get("radius_fraction", 0.9)))
        return x, z, None
    if kind == "cluster":
        if "centre" not in state:
            state["centre"] = _in_disc(rng, ax, az, radius * float(rule.get("radius_fraction", 0.6)))
        x, z = _in_disc(rng, *state["centre"], float(rule.get("spread", 4.0)))
        return x, z, None
    if kind == "against_cliff":
        if "cliff" not in state:
            state["cliff"] = _cliff_spots(context.query, ax, az, radius, float(rule.get("min_cliff_slope", 40.0)),
                                          float(rule.get("max_offset", 3.0)))
        return rng.choice(state["cliff"]) if state["cliff"] else None
    if kind == "along_path":
        if "path" not in state:
            state["path"] = _path_spots(context.query, context.roads, ax, az, radius)
        if not state["path"]:
            return None
        px, pz = rng.choice(state["path"])
        lo, hi = rule.get("offset", [2.0, 5.0])
        angle = rng.uniform(0.0, 2.0 * math.pi)
        distance = rng.uniform(float(lo), float(hi))
        return px + math.cos(angle) * distance, pz + math.sin(angle) * distance, None
    if kind == "near_role":
        bases = by_role.get(rule["role"], [])
        if not bases:
            return None
        base = rng.choice(bases)["position"]
        lo, hi = rule.get("distance", [2.0, 5.0])
        angle = rng.uniform(0.0, 2.0 * math.pi)
        distance = rng.uniform(float(lo), float(hi))
        return base[0] + math.cos(angle) * distance, base[2] + math.sin(angle) * distance, None
    raise ValueError(f"unknown rule {kind!r}")


def place(template: Template, anchor, radius: float, context: PropContext, seed: int, keep_away=()) -> PlacementResult:
    """Fits the template onto a site. keep_away: extra (x, z, r) circles no item may enter."""
    rng = random.Random(seed)
    items: list[dict] = []
    props: list[PlacedProp] = []
    gaps: list[dict] = []
    by_role: dict[str, list[dict]] = {}
    circles = [(anchor[0], anchor[1], r) for r in template.clear] + list(keep_away)
    for role in template.roles:
        assets = select_assets(context.catalog, context.rules, tags=role.tags, exclude=role.exclude, size=role.size,
                               allow=list(role.assets) if role.assets is not None else None)
        if role.store == "hfol":
            assets = [a for a in assets if a.endswith(".hmsh")]
        wanted = rng.randint(*role.count)
        if not assets:
            gaps.append({"role": role.name, "kind": "asset", "wanted": wanted, "placed": 0})
            continue
        state: dict = {}
        exempt_clear = role.rule["type"] == "at_anchor"
        for _ in range(wanted):
            for _attempt in range(ATTEMPTS):
                spot = _candidate(role, anchor, radius, context, rng, by_role, state)
                if spot is None:
                    break
                x, z, facing = spot
                if not exempt_clear and any(math.hypot(x - cx, z - cz) < r for cx, cz, r in circles):
                    continue
                if any(math.hypot(x - o["position"][0], z - o["position"][2]) < role.spacing for o in by_role.get(role.name, [])):
                    continue
                asset = rng.choice(assets)
                info, tags = context.catalog[asset], context.rules.for_asset(asset)
                lo, hi = role.scale or tags.scale
                scale = round(rng.uniform(lo, hi), 2)
                if facing is not None:
                    yaw = facing + rng.uniform(-20.0, 20.0)
                else:
                    yaw = rng.uniform(*(role.yaw or (0.0, 360.0)))
                settled = settle(info, tags, context.query, x, z, yaw, scale)
                if settled is None:
                    continue
                y, pitch, roll = settled
                collides = tags.collides_override if tags.collides_override is not None else info.has_collision
                item = {"role": role.name, "asset": asset, "store": role.store,
                        "position": [round(x, 2), round(y, 2), round(z, 2)], "yaw": round(yaw % 360.0, 2),
                        "tilt": [pitch, roll], "scale": scale, "collides": bool(collides)}
                prop = item_to_prop(item, f"draft:{len(items)}")
                problems = [v for v in lint_prop(prop, context, [*context.existing, *props])
                            if v.severity == "error" or v.rule == "prop_outside_poi"]
                if problems:
                    continue
                items.append(item)
                props.append(prop)
                by_role.setdefault(role.name, []).append(item)
                break
        placed = len(by_role.get(role.name, []))
        if placed < wanted:
            gaps.append({"role": role.name, "kind": "space", "wanted": wanted, "placed": placed})
    entry = tuple(anchor)
    if template.entry != "anchor" and by_role.get(template.entry):
        first = by_role[template.entry][0]["position"]
        entry = (first[0], first[2])
    return PlacementResult(items, gaps, entry)
```

- [ ] **Step 4: Create the five pilot templates** in `tools/world/templates/`:

`quarry.json`:
```json
{
  "version": 1,
  "name": "quarry",
  "description": "A working quarry at a cliff foot: cut-rock piles against the cliff, loose stones, a work tent, a fire, crates, barrels, a cart and tools.",
  "entry": "anchor",
  "clear": [],
  "notes": [],
  "roles": [
    {"name": "stone_piles", "query": {"tags": ["rock"], "size": "medium"}, "count": [4, 6], "spacing": 4.0, "store": "wobj", "rule": {"type": "against_cliff", "min_cliff_slope": 38, "max_offset": 4.0}},
    {"name": "loose_stones", "query": {"tags": ["rock"], "size": "small"}, "count": [6, 10], "spacing": 2.0, "store": "wobj", "rule": {"type": "scatter", "radius_fraction": 0.8}},
    {"name": "tent", "query": {"tags": ["tent"]}, "count": [1, 1], "spacing": 0, "store": "wobj", "rule": {"type": "ring", "r1": 6.0, "r2": 14.0}},
    {"name": "fire", "query": {"tags": ["fire"]}, "count": [1, 1], "spacing": 0, "store": "wobj", "rule": {"type": "near_role", "role": "tent", "distance": [3.0, 6.0]}},
    {"name": "crates", "query": {"tags": ["crate"]}, "count": [2, 4], "spacing": 1.5, "store": "wobj", "rule": {"type": "near_role", "role": "tent", "distance": [2.0, 6.0]}},
    {"name": "barrels", "query": {"tags": ["barrel"]}, "count": [1, 3], "spacing": 1.2, "store": "wobj", "rule": {"type": "near_role", "role": "crates", "distance": [1.0, 3.0]}},
    {"name": "cart", "query": {"tags": ["cart"]}, "count": [1, 1], "spacing": 0, "store": "wobj", "rule": {"type": "ring", "r1": 4.0, "r2": 12.0}},
    {"name": "tools", "query": {"tags": ["tool"]}, "count": [1, 3], "spacing": 2.0, "store": "wobj", "rule": {"type": "near_role", "role": "stone_piles", "distance": [1.5, 3.0]}}
  ]
}
```

`cave_mouth.json`:
```json
{
  "version": 1,
  "name": "cave_mouth",
  "description": "A hidden-looking opening at a cliff: large rocks framing a gap that stays walkable, cliff rocks, rubble and bushes partly screening it.",
  "entry": "anchor",
  "clear": [2.5],
  "notes": ["The cave itself (interior, kobolds, chest) belongs to the content spec; this pass only frames its mouth."],
  "roles": [
    {"name": "frame_rocks", "query": {"tags": ["rock"], "size": "large"}, "count": [2, 3], "spacing": 3.0, "store": "wobj", "rule": {"type": "ring", "r1": 3.0, "r2": 6.0}},
    {"name": "cliff_rocks", "query": {"tags": ["rock"], "size": "medium"}, "count": [3, 5], "spacing": 3.0, "store": "wobj", "rule": {"type": "against_cliff", "min_cliff_slope": 40, "max_offset": 3.0}},
    {"name": "rubble", "query": {"tags": ["rock"], "size": "small"}, "count": [4, 8], "spacing": 1.5, "store": "wobj", "rule": {"type": "scatter", "radius_fraction": 0.7}},
    {"name": "screen", "query": {"tags": ["bush"]}, "count": [3, 6], "spacing": 2.0, "store": "hfol", "rule": {"type": "ring", "r1": 4.0, "r2": 10.0}}
  ]
}
```

`waterfall_basin.json`:
```json
{
  "version": 1,
  "name": "waterfall_basin",
  "description": "Cliff rocks around a basin-shaped spot at a cliff foot, herbs and a few trees nearby. The water itself is added later.",
  "entry": "anchor",
  "clear": [4.0],
  "notes": ["Awaiting water: paint the pool with the editor's water brush (or a later waterfall feature); the falling water does not exist in the engine yet."],
  "roles": [
    {"name": "basin_rim", "query": {"tags": ["rock"], "size": "medium"}, "count": [5, 8], "spacing": 2.5, "store": "wobj", "rule": {"type": "ring", "r1": 4.0, "r2": 7.0}},
    {"name": "cliff_rocks", "query": {"tags": ["rock"], "size": "large"}, "count": [2, 4], "spacing": 4.0, "store": "wobj", "rule": {"type": "against_cliff", "min_cliff_slope": 40, "max_offset": 3.0}},
    {"name": "herbs", "query": {"tags": ["plant"]}, "count": [4, 8], "spacing": 1.5, "store": "hfol", "rule": {"type": "ring", "r1": 5.0, "r2": 12.0}},
    {"name": "trees", "query": {"tags": ["tree"]}, "count": [2, 4], "spacing": 5.0, "store": "hfol", "rule": {"type": "ring", "r1": 10.0, "r2": 18.0}}
  ]
}
```

`hunting_camp.json`:
```json
{
  "version": 1,
  "name": "hunting_camp",
  "description": "A small hunters' camp: one or two tents round a fire, crates and barrels, a few trees.",
  "entry": "fire",
  "clear": [],
  "notes": [],
  "roles": [
    {"name": "fire", "query": {"tags": ["fire"]}, "count": [1, 1], "spacing": 0, "store": "wobj", "rule": {"type": "at_anchor"}},
    {"name": "tents", "query": {"tags": ["tent"]}, "count": [1, 2], "spacing": 6.0, "store": "wobj", "rule": {"type": "near_role", "role": "fire", "distance": [4.0, 7.0]}},
    {"name": "supplies", "query": {"tags": ["crate"]}, "count": [1, 3], "spacing": 1.5, "store": "wobj", "rule": {"type": "near_role", "role": "tents", "distance": [2.0, 4.0]}},
    {"name": "barrels", "query": {"tags": ["barrel"]}, "count": [1, 2], "spacing": 1.2, "store": "wobj", "rule": {"type": "near_role", "role": "tents", "distance": [2.0, 4.0]}},
    {"name": "trees", "query": {"tags": ["tree"]}, "count": [3, 6], "spacing": 6.0, "store": "hfol", "rule": {"type": "ring", "r1": 10.0, "r2": 20.0}}
  ]
}
```

`abbey_surround.json`:
```json
{
  "version": 1,
  "name": "abbey_surround",
  "description": "Ruins around a dungeon entrance: wall pieces, foundations, a tower stand-in for the bell tower, rubble and dead trees. The anchor (the teleport) stays clear.",
  "entry": "anchor",
  "clear": [3.0],
  "notes": ["Asset gaps: there is no abbey, church or bell-tower art; a Building_Tower stands in for the bell tower."],
  "roles": [
    {"name": "walls", "query": {"tags": ["wall"]}, "count": [4, 6], "spacing": 4.0, "store": "wobj", "rule": {"type": "ring", "r1": 6.0, "r2": 14.0}},
    {"name": "foundations", "query": {"tags": ["ruin"]}, "count": [3, 5], "spacing": 3.0, "store": "wobj", "rule": {"type": "ring", "r1": 4.0, "r2": 12.0}},
    {"name": "bell_tower", "query": {"tags": ["tower"]}, "count": [1, 1], "spacing": 0, "store": "wobj", "rule": {"type": "ring", "r1": 10.0, "r2": 16.0}},
    {"name": "rubble", "query": {"tags": ["rock"], "size": "small"}, "count": [6, 10], "spacing": 1.5, "store": "wobj", "rule": {"type": "scatter", "radius_fraction": 0.8}},
    {"name": "dead_trees", "query": {"tags": ["tree_dead"]}, "count": [1, 3], "spacing": 6.0, "store": "hfol", "rule": {"type": "ring", "r1": 12.0, "r2": 20.0}}
  ]
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_templates -v`
Expected: OK.

- [ ] **Step 6: Commit**

```bash
git add tools/world/worldkit/templates.py tools/world/templates tools/tests/test_world_dressing_templates.py
git commit -m "feat(worldkit): site templates and deterministic placer; five pilot templates"
```

---

### Task 10: Dressing passes: documents, apply and undo

**Files:**
- Create: `tools/world/worldkit/dressing.py`
- Test: `tools/tests/test_world_dressing_passes.py`

**Interfaces:**
- Consumes:
  - `wobj_bytes`, `entity_file`, `new_unique_id` (Task 2);
  - `FoliageInstance`, `append_instances`, `remove_instances`, `write_hfol`, `load_hfol`, `parse_hfol`, `instance_hash` (Task 1);
  - `page_foliage_path` (Task 7); `item_rotation` (Task 9);
  - `paths.passes_dir`, `manifests_dir`, `client_root`, `terrain_dir`, `entities_dir`, `foliage_dir`;
  - `constants.entity_page_index`.
- Produces:
  - `PassError`, `DRAFT_VERSION = 1`;
  - `new_pass_id(poi_id, repo=REPO, today=None) -> str`;
  - `new_draft(pass_id, map_id, world, poi_id, template, seed, anchor, keep_away, entry, items, gaps, notes, fingerprint) -> dict`;
  - `save_doc(doc, repo=REPO) -> None`, `load_doc(pass_id, repo=REPO) -> dict`, `list_docs(repo=REPO) -> list[dict]`;
  - `world_fingerprint(directory, repo=REPO) -> str`, `editor_running(probe=None) -> bool`;
  - `apply_pass(doc, repo=REPO, probe=None, rng=None) -> dict`;
  - `UndoReport(removed, changed, missing)`, `undo_pass(doc, repo=REPO, force=False, probe=None) -> UndoReport`.
- **Statuses:** `planned`, `applied`, `applied-unchecked`, `undone`, `partially-undone`.
- The working copy is `generated/world/passes/<id>/draft.json`. Once a pass is applied, a tracked copy goes to `data/world/passes/<id>.json`.

- [ ] **Step 1: Write the failing tests.** Create `tools/tests/test_world_dressing_passes.py`:

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for dressing pass documents, apply and undo.

	python tools/tests/test_world_dressing_passes.py
"""

import random
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit.dressing import PassError, apply_pass, load_doc, new_draft, new_pass_id, save_doc, undo_pass, world_fingerprint  # noqa: E402
from worldkit.formats.wobj import parse_wobj  # noqa: E402
from worldkit.paths import foliage_dir, manifests_dir  # noqa: E402

PAGE = (32 << 8) | 32
NOT_RUNNING = lambda: False  # noqa: E731
RUNNING = lambda: True  # noqa: E731


def items():
	return [
		{"role": "a", "asset": "Models/Test/Cube.hmsh", "store": "wobj", "position": [50.0, 0.0, 50.0], "yaw": 30.0, "tilt": [0.0, 0.0], "scale": 1.2, "collides": True},
		{"role": "b", "asset": "Models/Test/Shed.hwmo", "store": "wobj", "position": [70.0, 0.0, 50.0], "yaw": 0.0, "tilt": [0.0, 0.0], "scale": 1.0, "collides": True},
		{"role": "c", "asset": "Models/Trees/A.hmsh", "store": "hfol", "position": [60.0, 0.0, 60.0], "yaw": 90.0, "tilt": [2.0, -1.0], "scale": 0.9, "collides": True},
		{"role": "c", "asset": "Models/Trees/New.hmsh", "store": "hfol", "position": [62.0, 0.0, 60.0], "yaw": 10.0, "tilt": [0.0, 0.0], "scale": 1.1, "collides": False},
	]


class PassTests(unittest.TestCase):
	def setUp(self):
		self._tmp = tempfile.TemporaryDirectory()
		self.repo = Path(self._tmp.name)
		fx.make_world(self.repo, "D", {(32, 32): {}})
		folder = foliage_dir("D", self.repo)
		folder.mkdir(parents=True)
		self.hfol = folder / f"{PAGE}.hfol"
		self.original = fx.hfol_bytes(["Models/Trees/A.hmsh"], [(5, 0, (1.0, 0.0, 1.0), (1.0, 0.0, 0.0, 0.0), (1.0, 1.0, 1.0), True)])
		self.hfol.write_bytes(self.original)
		self.doc = new_draft(new_pass_id("site", self.repo, today="20261001"), 0, "D", "site", "t", 1, [50.0, 50.0], [],
							 [55.0, 50.0], items(), [], [], world_fingerprint("D", self.repo))
		save_doc(self.doc, self.repo)

	def tearDown(self):
		self._tmp.cleanup()

	def apply(self):
		return apply_pass(self.doc, self.repo, probe=NOT_RUNNING, rng=random.Random(1))

	def test_pass_ids_count_up(self):
		self.assertEqual(self.doc["pass_id"], "20261001-site-1")
		self.assertEqual(new_pass_id("site", self.repo, today="20261001"), "20261001-site-2")

	def test_apply_writes_entities_and_foliage(self):
		doc = self.apply()
		self.assertEqual(doc["status"], "applied")
		cube = parse_wobj(self.repo / "data" / "client" / doc["items"][0]["file"])
		self.assertEqual((cube.kind, cube.asset, cube.category), ("mesh", "Models/Test/Cube.hmsh", "dressing/t"))
		self.assertEqual(parse_wobj(self.repo / "data" / "client" / doc["items"][1]["file"]).kind, "wmo")
		self.assertTrue((manifests_dir(self.repo) / f"{doc['pass_id']}.json").is_file())
		self.assertEqual(load_doc(doc["pass_id"], self.repo)["status"], "applied")
		self.assertNotEqual(self.hfol.read_bytes(), self.original)

	def test_undo_restores_everything(self):
		doc = self.apply()
		report = undo_pass(doc, self.repo, probe=NOT_RUNNING)
		self.assertEqual((len(report.removed), report.changed, report.missing), (4, [], []))
		self.assertEqual(self.hfol.read_bytes(), self.original)
		self.assertFalse((self.repo / "data" / "client" / doc["items"][0]["file"]).exists())
		self.assertEqual(doc["status"], "undone")

	def test_undo_keeps_what_the_user_changed(self):
		doc = self.apply()
		changed = self.repo / "data" / "client" / doc["items"][0]["file"]
		changed.write_bytes(changed.read_bytes()[:-1] + b"\x01")
		(self.repo / "data" / "client" / doc["items"][1]["file"]).unlink()
		report = undo_pass(doc, self.repo, probe=NOT_RUNNING)
		self.assertEqual((len(report.changed), len(report.missing), len(report.removed)), (1, 1, 2))
		self.assertTrue(changed.exists())
		self.assertEqual(doc["status"], "partially-undone")

	def test_guards(self):
		with self.assertRaises(PassError):
			apply_pass(self.doc, self.repo, probe=RUNNING)
		bad = dict(self.doc, checks={"placement": [{"severity": "error", "rule": "prop_floating"}], "walkability": []})
		with self.assertRaises(PassError):
			apply_pass(bad, self.repo, probe=NOT_RUNNING)
		stale = dict(self.doc, world_fingerprint="0" * 40)
		with self.assertRaises(PassError):
			apply_pass(stale, self.repo, probe=NOT_RUNNING)

	def test_failed_apply_rolls_back(self):
		self.doc["items"].append({"role": "x", "asset": "Models/Test/Bad.txt", "store": "wobj", "position": [1.0, 0.0, 1.0],
								  "yaw": 0.0, "tilt": [0.0, 0.0], "scale": 1.0, "collides": False})
		with self.assertRaises(PassError):
			self.apply()
		self.assertEqual(self.hfol.read_bytes(), self.original)
		self.assertEqual(list((self.repo / "data" / "client" / "Worlds" / "D" / "D" / "Entities").glob("*/*.wobj")), [])
		self.assertEqual(self.doc["status"], "planned")


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_passes -v`
Expected: ImportError, `worldkit.dressing`.

- [ ] **Step 3: Implement `dressing.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Dressing passes: draft/manifest documents, the world fingerprint, the editor guard, apply and undo.

A pass is one template placed on one atlas place. Its document lives in
generated/world/passes/<id>/draft.json; once applied, a tracked copy goes to data/world/passes/<id>.json.
Apply writes one .wobj per prop and appends trees to the page .hfol files, recording every unique id
and hash; undo removes exactly those, keeping anything the user changed since (unless forced).
"""

from __future__ import annotations

import hashlib
import json
import os
import random
import subprocess
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

from .constants import entity_page_index
from .foliage import page_foliage_path
from .formats.chunks import FormatError
from .formats.hfol import FoliageInstance, append_instances, instance_hash, load_hfol, parse_hfol, remove_instances, write_hfol
from .formats.wobj import entity_file, new_unique_id, wobj_bytes
from .paths import REPO, client_root, entities_dir, foliage_dir, manifests_dir, passes_dir, terrain_dir
from .templates import item_rotation

DRAFT_VERSION = 1
APPLIED = ("applied", "applied-unchecked", "partially-undone")


class PassError(RuntimeError):
    pass


def new_pass_id(poi_id: str, repo: Path = REPO, today: str | None = None) -> str:
    stamp = today or datetime.now().strftime("%Y%m%d")
    prefix = f"{stamp}-{poi_id}-"
    used = [p.name for p in passes_dir(repo).glob(prefix + "*")] + [p.stem for p in manifests_dir(repo).glob(prefix + "*.json")]
    numbers = [int(name[len(prefix):]) for name in used if name[len(prefix):].isdigit()]
    return f"{prefix}{max(numbers, default=0) + 1}"


def new_draft(pass_id, map_id, world, poi_id, template, seed, anchor, keep_away, entry, items, gaps, notes, fingerprint) -> dict:
    return {"version": DRAFT_VERSION, "pass_id": pass_id, "map": map_id, "world": world, "poi": poi_id,
            "template": template, "seed": seed, "anchor": list(anchor), "keep_away": list(keep_away), "entry": list(entry),
            "created": datetime.now().isoformat(timespec="seconds"), "world_fingerprint": fingerprint, "items": items,
            "gaps": gaps, "notes": list(notes), "checks": {"placement": [], "walkability": []}, "status": "planned"}


def _round(value):
    if isinstance(value, float):
        return round(value, 2)
    if isinstance(value, list):
        return [_round(v) for v in value]
    if isinstance(value, dict):
        return {k: (v if k in ("written_hash", "world_fingerprint") else _round(v)) for k, v in value.items()}
    return value


def _paths(pass_id: str, repo: Path) -> tuple[Path, Path]:
    return passes_dir(repo) / pass_id / "draft.json", manifests_dir(repo) / f"{pass_id}.json"


def save_doc(doc: dict, repo: Path = REPO) -> None:
    text = json.dumps(_round(doc), indent=2, ensure_ascii=False) + "\n"
    work, tracked = _paths(doc["pass_id"], repo)
    work.parent.mkdir(parents=True, exist_ok=True)
    work.write_text(text, encoding="utf-8", newline="\n")
    if doc["status"] != "planned":
        tracked.parent.mkdir(parents=True, exist_ok=True)
        tracked.write_text(text, encoding="utf-8", newline="\n")


def load_doc(pass_id: str, repo: Path = REPO) -> dict:
    for path in _paths(pass_id, repo):
        if path.is_file():
            return json.loads(path.read_text(encoding="utf-8"))
    raise PassError(f"no pass {pass_id!r} in {passes_dir(repo)} or {manifests_dir(repo)}")


def list_docs(repo: Path = REPO) -> list[dict]:
    ids = {p.parent.name for p in passes_dir(repo).glob("*/draft.json")} | {p.stem for p in manifests_dir(repo).glob("*.json")}
    return [load_doc(pass_id, repo) for pass_id in sorted(ids)]


def world_fingerprint(directory: str, repo: Path = REPO) -> str:
    digest = hashlib.sha1(b"world-v1")
    sources = sorted(terrain_dir(directory, repo).glob("*.tile")) + sorted(entities_dir(directory, repo).glob("*/*.wobj"))
    sources += sorted(foliage_dir(directory, repo).glob("*.hfol"))
    for path in sources:
        stat = path.stat()
        digest.update(f"{path.relative_to(repo).as_posix()}|{stat.st_size}|{stat.st_mtime_ns}\n".encode())
    return digest.hexdigest()


def _default_probe() -> bool:
    if os.name == "nt":
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq mmo_edit.exe", "/NH"], capture_output=True, text=True).stdout
        return "mmo_edit.exe" in out.lower()
    return subprocess.run(["pgrep", "-x", "mmo_edit"], capture_output=True).returncode == 0


def editor_running(probe=None) -> bool:
    return bool((probe or _default_probe)())


def _guard_editor(probe) -> None:
    if editor_running(probe):
        raise PassError("mmo_edit is running: close it first (it keeps pages in memory and would overwrite foliage on save, "
                        "and it only discovers new prop files at startup)")


def _taken_ids(directory: str, repo: Path) -> set[int]:
    taken = {int(p.stem) for p in entities_dir(directory, repo).glob("*/*.wobj") if p.stem.isdigit()}
    for path in foliage_dir(directory, repo).glob("*.hfol"):
        taken |= {i.unique_id for i in load_hfol(path).instances}
    return taken


def _instance(item: dict, unique_id: int) -> FoliageInstance:
    scale = float(item["scale"])
    return FoliageInstance(unique_id, item["asset"], tuple(item["position"]), item_rotation(item), (scale, scale, scale),
                           bool(item["collides"]))


def apply_pass(doc: dict, repo: Path = REPO, probe=None, rng: random.Random | None = None) -> dict:
    if doc["status"] != "planned":
        raise PassError(f"pass {doc['pass_id']} is {doc['status']}, only planned passes can be applied")
    _guard_editor(probe)
    if world_fingerprint(doc["world"], repo) != doc["world_fingerprint"]:
        raise PassError("the world changed since this draft was planned or checked: run `dress.py check` first")
    if any(v.get("severity") == "error" for v in doc["checks"]["placement"]):
        raise PassError("the draft has placement errors: fix the draft and run `dress.py check`")
    rng = rng or random.Random()
    client = client_root(repo)
    taken = _taken_ids(doc["world"], repo)
    written: list[Path] = []
    originals: dict[Path, bytes | None] = {}
    created: list[str] = []
    try:
        for item in doc["items"]:
            if item["store"] != "wobj":
                continue
            suffix = item["asset"].rsplit(".", 1)[-1].lower()
            if suffix not in ("hmsh", "hwmo"):
                raise PassError(f"{item['asset']}: only .hmsh and .hwmo can be placed")
            unique_id = new_unique_id(taken, rng)
            x, _, z = item["position"]
            path = entity_file(doc["world"], unique_id, x, z, repo)
            if path.exists():
                raise PassError(f"{path} already exists")
            scale = float(item["scale"])
            data = wobj_bytes(kind="mesh" if suffix == "hmsh" else "wmo", unique_id=unique_id, asset=item["asset"],
                              position=item["position"], rotation=item_rotation(item), scale=(scale, scale, scale),
                              category=f"dressing/{doc['template']}")
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            written.append(path)
            item.update(unique_id=f"0x{unique_id:016x}", file=path.relative_to(client).as_posix(),
                        written_hash=hashlib.sha1(data).hexdigest())
        by_page: dict[int, list[dict]] = {}
        for item in doc["items"]:
            if item["store"] == "hfol":
                if not item["asset"].lower().endswith(".hmsh"):
                    raise PassError(f"{item['asset']}: foliage must be a .hmsh")
                by_page.setdefault(entity_page_index(item["position"][0], item["position"][2]), []).append(item)
        for page, page_items in sorted(by_page.items()):
            path = page_foliage_path(doc["world"], page, repo)
            originals[path] = path.read_bytes() if path.is_file() else None
            instances = []
            for item in page_items:
                inst = _instance(item, new_unique_id(taken, rng))
                instances.append(inst)
                item.update(unique_id=f"0x{inst.unique_id:016x}", file=path.relative_to(client).as_posix(),
                            written_hash=instance_hash(inst))
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(write_hfol(append_instances(load_hfol(path), instances)))
            if originals[path] is None:
                created.append(path.relative_to(client).as_posix())
    except (PassError, OSError, ValueError, FormatError) as exc:
        for path in written:
            path.unlink(missing_ok=True)
        for path, original in originals.items():
            if original is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(original)
        for item in doc["items"]:
            for key in ("unique_id", "file", "written_hash"):
                item.pop(key, None)
        raise exc if isinstance(exc, PassError) else PassError(f"apply failed and was rolled back: {exc}") from exc
    doc["created_files"] = created
    doc["status"] = "applied"
    doc["applied"] = datetime.now().isoformat(timespec="seconds")
    save_doc(doc, repo)
    return doc


@dataclass
class UndoReport:
    removed: list[str] = field(default_factory=list)
    changed: list[str] = field(default_factory=list)
    missing: list[str] = field(default_factory=list)


def undo_pass(doc: dict, repo: Path = REPO, force: bool = False, probe=None) -> UndoReport:
    if doc["status"] not in APPLIED:
        raise PassError(f"pass {doc['pass_id']} is {doc['status']}, nothing to undo")
    _guard_editor(probe)
    client = client_root(repo)
    report = UndoReport()
    kept: list[str] = []
    for item in doc["items"]:
        if item["store"] != "wobj" or "file" not in item:
            continue
        path = client / item["file"]
        if not path.exists():
            report.missing.append(item["file"])
        elif hashlib.sha1(path.read_bytes()).hexdigest() != item["written_hash"] and not force:
            report.changed.append(item["file"])
            kept.append(item["unique_id"])
        else:
            path.unlink()
            report.removed.append(item["file"])
    by_file: dict[str, list[dict]] = {}
    for item in doc["items"]:
        if item["store"] == "hfol" and "file" in item:
            by_file.setdefault(item["file"], []).append(item)
    for rel, file_items in sorted(by_file.items()):
        path = client / rel
        if not path.is_file():
            report.missing += [f"{rel}#{i['unique_id']}" for i in file_items]
            continue
        ff = parse_hfol(path.read_bytes(), str(path))
        current = {i.unique_id: i for i in ff.instances}
        remove = set()
        for item in file_items:
            unique_id = int(item["unique_id"], 16)
            label = f"{rel}#{item['unique_id']}"
            if unique_id not in current:
                report.missing.append(label)
            elif instance_hash(current[unique_id]) != item["written_hash"] and not force:
                report.changed.append(label)
                kept.append(item["unique_id"])
            else:
                remove.add(unique_id)
                report.removed.append(label)
        result = remove_instances(ff, remove)
        if not result.instances and rel in doc.get("created_files", []):
            path.unlink()
        else:
            path.write_bytes(write_hfol(result))
    doc["status"] = "partially-undone" if kept else "undone"
    doc["kept_after_undo"] = kept
    doc["undone"] = datetime.now().isoformat(timespec="seconds")
    save_doc(doc, repo)
    return report
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_passes -v`
Expected: OK. `test_guards` needs the stale fingerprint to differ, and it does, because the stored value is `"0" * 40`.

- [ ] **Step 5: Commit**

```bash
git add tools/world/worldkit/dressing.py tools/tests/test_world_dressing_passes.py
git commit -m "feat(worldkit): dressing passes with tracked manifests, exact undo and editor guard"
```

---

### Task 11: `nav_query` tool and walkability checks

**Files:**
- Create: `src/nav_query/CMakeLists.txt`, `src/nav_query/main.cpp`
- Modify: `src/CMakeLists.txt:28-37` (add the subdirectory)
- Create: `tools/world/worldkit/nav.py`, `tools/world/nav_routes.json`
- Test: `tools/tests/test_world_dressing_nav.py`

**Interfaces:**
- Consumes: `mmo::nav::Map` (`src/shared/nav_mesh/map.h`); `AssetRegistry`; `Violation`; `WorldQuery`.
- Produces:
  - **CLI:** `nav_query --nav <dir containing <World>.map> --world <World>`. It prints `{"ready":true,"pages":N}`, then answers one JSON line per request:
    - `{"op":"path","from":[x,y,z],"to":[x,y,z]}` → `{"ok":true,"length":L,"points":[[x,y,z],...]}` or `{"ok":false}`;
    - `{"op":"on_mesh","at":[x,y,z],"radius":r}` → `{"ok":true,"nearest":[x,y,z],"distance":d}` or `{"ok":false}`.
  - **`nav.py`:**
    - `NavError`, `tool_exe(name, repo=REPO) -> Path`;
    - `build_nav(directory, out_root, repo=REPO, runner=subprocess.run) -> Path` (returns `<out_root>/nav`), `scratch_nav_root(repo) -> Path` (`generated/world`);
    - `NavQuery(nav_dir, world, repo=REPO, popen=subprocess.Popen, exe=None)`, with `.path(a, b) -> float | None`, `.on_mesh(p, radius=2.0) -> float | None`, `.close()`, and context-manager support;
    - `Route(id, name, points, baseline_length)`, `load_routes(path=ROUTES_PATH) -> list[Route]`, `save_routes(routes, path=ROUTES_PATH)`;
    - `route_length(nav, query, points) -> float | None`;
    - `walkability_violations(nav, query, routes, sites, spawns) -> list[Violation]`, where sites are dicts `{"name", "x", "z", "radius"}`.

- [ ] **Step 1: Write the C++ tool.** Create `src/nav_query/CMakeLists.txt`:

```cmake
add_exe(nav_query)
target_link_libraries(nav_query base log math assets binary_io_hdrs nav_mesh)
set_property(TARGET nav_query PROPERTY FOLDER "tools")
```

Create `src/nav_query/main.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "assets/asset_registry.h"
#include "base/typedefs.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"
#include "math/vector3.h"
#include "nav_mesh/map.h"

#include "cxxopts/cxxopts.hpp"
#include <nlohmann/json.hpp>

#include "DetourNavMeshQuery.h"

#include <iostream>
#include <string>
#include <vector>

namespace mmo
{
	namespace
	{
		/// Reads an [x, y, z] number array; returns false when the value has another shape.
		bool readPoint(const nlohmann::json& value, Vector3& outPoint)
		{
			if (!value.is_array() || value.size() != 3 || !value[0].is_number() || !value[1].is_number() || !value[2].is_number())
			{
				return false;
			}

			outPoint = Vector3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
			return true;
		}

		nlohmann::json writePoint(const Vector3& point)
		{
			return nlohmann::json::array({ point.x, point.y, point.z });
		}

		nlohmann::json failure(const char* reason)
		{
			return { { "ok", false }, { "error", reason } };
		}

		nlohmann::json pathQuery(const nav::Map& map, const nlohmann::json& request)
		{
			Vector3 from;
			Vector3 to;
			if (!request.contains("from") || !request.contains("to") || !readPoint(request["from"], from) || !readPoint(request["to"], to))
			{
				return failure("path needs from and to as [x, y, z]");
			}

			std::vector<Vector3> points;
			if (!map.FindPath(from, to, points, false) || points.size() < 2)
			{
				return { { "ok", false } };
			}

			float length = 0.0f;
			nlohmann::json out = { { "ok", true }, { "points", nlohmann::json::array() } };
			for (size_t i = 0; i < points.size(); ++i)
			{
				if (i > 0)
				{
					length += (points[i] - points[i - 1]).GetLength();
				}

				out["points"].push_back(writePoint(points[i]));
			}

			out["length"] = length;
			return out;
		}

		nlohmann::json onMeshQuery(const nav::Map& map, const nlohmann::json& request)
		{
			Vector3 at;
			if (!request.contains("at") || !readPoint(request["at"], at))
			{
				return failure("on_mesh needs at as [x, y, z]");
			}

			const float radius = request.contains("radius") && request["radius"].is_number() ? request["radius"].get<float>() : 2.0f;
			const float center[3] = { at.x, at.y, at.z };
			const float extents[3] = { radius, 5.0f, radius };
			dtQueryFilter filter;
			dtPolyRef polygon = 0;
			float nearest[3] = { 0.0f, 0.0f, 0.0f };
			if (dtStatusFailed(map.GetNavMeshQuery().findNearestPoly(center, extents, &filter, &polygon, nearest)) || polygon == 0)
			{
				return { { "ok", false } };
			}

			const Vector3 point(nearest[0], nearest[1], nearest[2]);
			return { { "ok", true }, { "nearest", writePoint(point) }, { "distance", (point - at).GetLength() } };
		}

		nlohmann::json answer(const nav::Map& map, const std::string& line)
		{
			const nlohmann::json request = nlohmann::json::parse(line, nullptr, false);
			if (request.is_discarded() || !request.is_object() || !request.contains("op") || !request["op"].is_string())
			{
				return failure("expected a JSON object with an op");
			}

			const std::string op = request["op"].get<std::string>();
			if (op == "path")
			{
				return pathQuery(map, request);
			}

			if (op == "on_mesh")
			{
				return onMeshQuery(map, request);
			}

			return failure("unknown op");
		}
	}
}

/// Answers navmesh queries (JSON lines on stdin, one JSON line per answer on stdout) for tools/world.
int main(int argc, char* argv[])
{
	auto logOptions = mmo::g_DefaultConsoleLogOptions;
	mmo::g_DefaultLog.signal().connect([&logOptions](const mmo::LogEntry& entry)
	{
		printLogEntry(std::cerr, entry, logOptions);
	});

	std::string navDirectory;
	std::string worldName;

	cxxopts::Options options("nav_query", "Navmesh path and on-mesh queries as JSON lines");
	options.add_options()
		("n,nav", "directory holding <World>.map and <World>/XX_YY.nav", cxxopts::value<std::string>(navDirectory))
		("w,world", "world name", cxxopts::value<std::string>(worldName));

	try
	{
		options.parse(argc, argv);
	}
	catch (const cxxopts::OptionException& e)
	{
		ELOG(e.what());
		return 1;
	}

	if (navDirectory.empty() || worldName.empty())
	{
		ELOG("nav_query needs --nav and --world");
		return 1;
	}

	mmo::AssetRegistry::Initialize(navDirectory, {});
	if (!mmo::AssetRegistry::HasFile(worldName + ".map"))
	{
		ELOG("No " << worldName << ".map in " << navDirectory);
		mmo::AssetRegistry::Destroy();
		return 1;
	}

	int exitCode = 0;
	{
		mmo::nav::Map map(worldName);
		const mmo::int32 pages = map.LoadAllPages();
		if (pages <= 0)
		{
			ELOG("No navigation pages loaded for " << worldName);
			exitCode = 1;
		}
		else
		{
			std::cout << nlohmann::json({ { "ready", true }, { "pages", pages } }).dump() << std::endl;
			std::string line;
			while (std::getline(std::cin, line))
			{
				if (!line.empty())
				{
					std::cout << mmo::answer(map, line).dump() << std::endl;
				}
			}
		}
	}

	mmo::AssetRegistry::Destroy();
	return exitCode;
}
```

In `src/CMakeLists.txt`, inside `if (MMO_BUILD_TOOLS)`, after `add_subdirectory(terrain_tool)`, add:

```cmake
	add_subdirectory(nav_query)
```

- [ ] **Step 2: Build it**

Run: `cmake -S . -B build; cmake --build build --config Release -t nav_query`
Expected: `nav_query.vcxproj -> H:\mmo\bin\Release\nav_query.exe`, with no warnings from `src/nav_query`.

- [ ] **Step 3: Build a scratch navmesh once and smoke-test the tool.** Record the time in the commit message, since the risk section of the spec asks for it.

```powershell
Measure-Command { & bin\Release\nav_builder.exe -d data\client -w Development -o generated\world }
'{"op":"path","from":[300,8,560],"to":[100,4,505]}' | bin\Release\nav_query.exe --nav generated\world\nav --world Development
```

Expected: `generated/world/nav/Development.map` and `generated/world/nav/Development/*.nav` exist. The query prints a `ready` line, then a path line with `"ok":true` and a length of roughly 210–260 m.

- [ ] **Step 4: Write the failing Python tests.** Create `tools/tests/test_world_dressing_nav.py`:

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the nav_query client and the walkability checks.

	python tools/tests/test_world_dressing_nav.py
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit.nav import NavQuery, Route, load_routes, save_routes, walkability_violations  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402
from worldkit.spawns import SpawnRecord, spawn_key  # noqa: E402


class FakeProcess:
	"""Stands in for nav_query: answers from a function."""

	def __init__(self, respond):
		self.respond = respond
		self.lines = [json.dumps({"ready": True, "pages": 1}) + "\n"]
		outer = self

		class In:
			def write(self, text):
				outer.lines.append(json.dumps(outer.respond(json.loads(text))) + "\n")

			def flush(self):
				pass

			def close(self):
				pass

		class Out:
			def readline(self):
				return outer.lines.pop(0) if outer.lines else ""

		self.stdin, self.stdout = In(), Out()

	def wait(self, timeout=None):
		return 0


class FakeNav:
	"""Straight-line paths except across a wall at x = 500; nothing is on the mesh near (0, 0)."""

	def path(self, a, b):
		if (a[0] < 500) != (b[0] < 500):
			return None
		return ((a[0] - b[0]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5

	def on_mesh(self, p, radius=2.0):
		return None if abs(p[0]) < 5 and abs(p[2]) < 5 else 0.2


def spawn(x, z):
	return SpawnRecord(spawn_key("unit", 0, 1, "S", x, z), 0, "unit", 1, 0, 0, "S", x, 0.0, z, True, 0, 30000, ())


class NavTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls._tmp = tempfile.TemporaryDirectory()
		fx.make_world(Path(cls._tmp.name), "N", {(31, 31): {}, (32, 31): {}, (31, 32): {}, (32, 32): {}})
		cls.query = WorldQuery(build_snapshot("N", repo=Path(cls._tmp.name)))

	@classmethod
	def tearDownClass(cls):
		cls._tmp.cleanup()

	def test_client_protocol(self):
		answers = {"path": {"ok": True, "length": 12.5, "points": []}, "on_mesh": {"ok": True, "nearest": [0, 0, 0], "distance": 0.4}}
		nav = NavQuery(Path("nav"), "W", popen=lambda *a, **k: FakeProcess(lambda r: answers[r["op"]]), exe=Path("nav_query"))
		self.assertEqual(nav.path((0, 0, 0), (1, 0, 1)), 12.5)
		self.assertEqual(nav.on_mesh((0, 0, 0)), 0.4)
		nav.close()
		missing = NavQuery(Path("nav"), "W", popen=lambda *a, **k: FakeProcess(lambda r: {"ok": False}), exe=Path("nav_query"))
		self.assertIsNone(missing.path((0, 0, 0), (1, 0, 1)))

	def test_routes_sites_and_spawns(self):
		routes = [Route("ok", "fine", [[100.0, 100.0], [200.0, 100.0]], 100.0),
				  Route("cut", "crosses the wall", [[400.0, 100.0], [600.0, 100.0]], None),
				  Route("long", "got longer", [[100.0, 200.0], [150.0, 200.0]], 30.0)]
		sites = [{"name": "near", "x": 120.0, "z": 120.0, "radius": 30.0},
				 {"name": "behind_wall", "x": 520.0, "z": 300.0, "radius": 30.0}]
		violations = walkability_violations(FakeNav(), self.query, routes, sites, [spawn(130.0, 125.0), spawn(-1.0, 1.0)])
		found = {(v.rule, v.subject) for v in violations}
		self.assertIn(("route_broken", "route:cut"), found)
		self.assertIn(("route_longer", "route:long"), found)
		self.assertIn(("site_unreachable", "site:behind_wall"), found)
		self.assertNotIn(("site_unreachable", "site:near"), found)
		self.assertNotIn("route:ok", {v.subject for v in violations})

	def test_routes_file_round_trip(self):
		with tempfile.TemporaryDirectory() as tmp:
			path = Path(tmp) / "routes.json"
			save_routes([Route("a", "A", [[1.0, 2.0], [3.0, 4.0]], 2.83)], path)
			self.assertEqual(load_routes(path)[0].baseline_length, 2.83)

	def test_shipped_routes_load(self):
		self.assertTrue(load_routes())


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 5: Run the tests to verify they fail**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_nav -v`
Expected: ImportError, `worldkit.nav`.

- [ ] **Step 6: Implement `nav.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Walkability: navmesh builds (nav_builder) and path / on-mesh queries (nav_query).

Checks run on a scratch build in generated/world/nav; the real navmesh in data/editor/nav is only
rebuilt by `dress.py publish-nav`. Protected routes (tools/world/nav_routes.json) must stay walkable,
each dressed site must be reachable from the route network, and spawns near a site must stay on the mesh.
"""

from __future__ import annotations

import json
import math
import os
import subprocess
from dataclasses import dataclass
from pathlib import Path

from .lint import Violation
from .paths import REPO, client_root

ROUTES_PATH = REPO / "tools" / "world" / "nav_routes.json"
LONGER_FACTOR = 1.25
DETOUR_FACTOR = 3.0
SPAWN_OFF_MESH = 1.5
_NETWORK_STEP = 10.0


class NavError(RuntimeError):
    pass


def tool_exe(name: str, repo: Path = REPO) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    for config in ("Release", "Debug"):
        path = repo / "bin" / config / f"{name}{suffix}"
        if path.is_file():
            return path
    raise NavError(f"{name}{suffix} not found in bin/Release or bin/Debug: build it with MMO_BUILD_TOOLS=ON")


def scratch_nav_root(repo: Path = REPO) -> Path:
    return repo / "generated" / "world"


def build_nav(directory: str, out_root: Path, repo: Path = REPO, runner=subprocess.run) -> Path:
    """Runs nav_builder for a whole world; outputs land in <out_root>/nav/<World>/ and <out_root>/nav/<World>.map."""
    exe = tool_exe("nav_builder", repo)
    out_root.mkdir(parents=True, exist_ok=True)
    proc = runner([str(exe), "-d", str(client_root(repo)), "-w", directory, "-o", str(out_root)],
                  capture_output=True, text=True)
    if proc.returncode != 0:
        raise NavError(f"nav_builder failed ({proc.returncode}): {(proc.stdout + proc.stderr)[-1500:]}")
    return out_root / "nav"


class NavQuery:
    def __init__(self, nav_dir: Path, world: str, repo: Path = REPO, popen=subprocess.Popen, exe: Path | None = None):
        exe = exe or tool_exe("nav_query", repo)
        self._proc = popen([str(exe), "--nav", str(nav_dir), "--world", world], stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1)
        ready = self._read()
        if not ready.get("ready"):
            raise NavError(f"nav_query did not start for {world} in {nav_dir}")

    def _read(self) -> dict:
        line = self._proc.stdout.readline()
        if not line:
            raise NavError("nav_query exited (is the navmesh built?)")
        return json.loads(line)

    def _ask(self, request: dict) -> dict:
        self._proc.stdin.write(json.dumps(request) + "\n")
        self._proc.stdin.flush()
        return self._read()

    def path(self, a, b) -> float | None:
        answer = self._ask({"op": "path", "from": [float(v) for v in a], "to": [float(v) for v in b]})
        return float(answer["length"]) if answer.get("ok") else None

    def on_mesh(self, point, radius: float = 2.0) -> float | None:
        answer = self._ask({"op": "on_mesh", "at": [float(v) for v in point], "radius": radius})
        return float(answer["distance"]) if answer.get("ok") else None

    def close(self) -> None:
        self._proc.stdin.close()
        self._proc.wait(timeout=10)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


@dataclass
class Route:
    id: str
    name: str
    points: list            # [[x, z], ...]
    baseline_length: float | None


def load_routes(path: Path = ROUTES_PATH) -> list[Route]:
    doc = json.loads(Path(path).read_text(encoding="utf-8"))
    return [Route(r["id"], r["name"], r["points"], r.get("baseline_length")) for r in doc["routes"]]


def save_routes(routes: list[Route], path: Path = ROUTES_PATH) -> None:
    doc = {"version": 1, "routes": [{"id": r.id, "name": r.name, "points": r.points,
                                     "baseline_length": None if r.baseline_length is None else round(r.baseline_length, 2)}
                                    for r in routes]}
    Path(path).write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")


def _point(query, x: float, z: float) -> tuple[float, float, float]:
    return (x, (query.height_at(x, z) or 0.0) + 0.5, z)


def route_length(nav, query, points) -> float | None:
    total = 0.0
    for (ax, az), (bx, bz) in zip(points, points[1:]):
        leg = nav.path(_point(query, ax, az), _point(query, bx, bz))
        if leg is None:
            return None
        total += leg
    return total


def _network(routes: list[Route]) -> list[tuple[float, float]]:
    out = []
    for route in routes:
        for (ax, az), (bx, bz) in zip(route.points, route.points[1:]):
            steps = max(1, int(math.hypot(bx - ax, bz - az) // _NETWORK_STEP))
            out += [(ax + (bx - ax) * k / steps, az + (bz - az) * k / steps) for k in range(steps + 1)]
    return out


def walkability_violations(nav, query, routes: list[Route], sites: list[dict], spawns) -> list[Violation]:
    found: list[Violation] = []
    for route in routes:
        length = route_length(nav, query, route.points)
        start = route.points[0]
        if length is None:
            found.append(Violation("route_broken", "error", f"route:{route.id}", f"{route.name}: no path any more", *start))
        elif route.baseline_length and length > route.baseline_length * LONGER_FACTOR:
            found.append(Violation("route_longer", "warning", f"route:{route.id}",
                                   f"{route.name}: {length:.0f} m, was {route.baseline_length:.0f} m", *start))
    network = _network(routes)
    for site in sites:
        sx, sz = site["x"], site["z"]
        if network:
            nx, nz = min(network, key=lambda p: math.hypot(p[0] - sx, p[1] - sz))
            straight = max(math.hypot(nx - sx, nz - sz), 1.0)
            length = nav.path(_point(query, nx, nz), _point(query, sx, sz))
            if length is None or length > straight * DETOUR_FACTOR:
                text = "no path" if length is None else f"path {length:.0f} m for {straight:.0f} m straight"
                found.append(Violation("site_unreachable", "error", f"site:{site['name']}",
                                       f"{site['name']}: not reachable from the route network ({text})", sx, sz))
        for record in spawns:
            if record.active and math.hypot(record.x - sx, record.z - sz) <= site["radius"]:
                distance = nav.on_mesh((record.x, record.y, record.z), 2.0)
                if distance is None or distance > SPAWN_OFF_MESH:
                    found.append(Violation("spawn_off_mesh", "error", record.key,
                                           f"{record.name or record.key} is off the navmesh near {site['name']}", record.x, record.z))
    return found
```

- [ ] **Step 7: Create `tools/world/nav_routes.json`.** The baselines are filled by `dress.py nav-baseline` in Task 16.

```json
{
  "version": 1,
  "routes": [
    {"id": "oakenshire_to_west_gate", "name": "Oakenshire town square -> West Gate", "points": [[253.7, 568.8], [231.2, 557.7], [204.5, 514.4], [125.2, 498.7], [75.4, 499.2]], "baseline_length": null},
    {"id": "west_gate_to_old_stone_bridge", "name": "West Gate -> Old Stone Bridge (Old King's Road)", "points": [[74.9, 495.8], [32.1, 499.0], [-50.5, 496.4], [-117.2, 469.1], [-170.9, 460.3], [-252.4, 467.1], [-300.0, 432.0], [-390.0, 396.0], [-440.0, 372.0], [-480.0, 360.0]], "baseline_length": null},
    {"id": "barrowfront_to_abbey", "name": "Barrowfront Camp -> Hollow Choir abbey teleport", "points": [[-436.0, 262.0], [107.0, 345.0]], "baseline_length": null}
  ]
}
```

- [ ] **Step 8: Run the tests to verify they pass**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_nav -v`
Expected: OK.

- [ ] **Step 9: Add the live test and run it.** Append this class before `if __name__` in `test_world_dressing_nav.py`. It skips unless the scratch build from Step 3 and the tool exist.

```python
from worldkit.nav import scratch_nav_root, tool_exe  # noqa: E402


@fx.requires_live_data
class LiveNavTests(unittest.TestCase):
	def test_town_hall_to_west_gate(self):
		nav_dir = scratch_nav_root() / "nav"
		try:
			tool_exe("nav_query")
		except Exception:
			self.skipTest("nav_query is not built")
		if not (nav_dir / "Development.map").is_file():
			self.skipTest("no scratch navmesh: run nav_builder -o generated/world")
		with NavQuery(nav_dir, "Development") as nav:
			length = nav.path((300.0, 8.0, 560.0), (100.0, 4.0, 505.0))
		self.assertIsNotNone(length)
		self.assertLess(length, 600.0)
```

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_nav -v`
Expected: OK, with `LiveNavTests` passing, not skipped, because Step 3 built the scratch nav.

- [ ] **Step 10: Commit**

```bash
git add src/nav_query src/CMakeLists.txt tools/world/worldkit/nav.py tools/world/nav_routes.json tools/tests/test_world_dressing_nav.py
git commit -m "feat(nav_query): navmesh path/on-mesh query tool and walkability checks"
```

---

### Task 12: `dress.py` CLI, orchestration and review packets

**Files:**
- Create: `tools/world/worldkit/packet.py`, `tools/world/worldkit/dress_commands.py`, `tools/world/dress.py`
- Test: `tools/tests/test_world_dressing_commands.py`

**Interfaces:**
- Consumes: everything above; `open_map` (spec 1); `unit_spawn_records`, `object_spawn_records`.
- Produces:
  - `packet.write_packet(doc, images, repo=REPO) -> Path`, `packet.write_batch_index(name, docs, repo=REPO) -> Path`;
  - `dress_commands.DressSession(map_entry, query, catalog, rules, foliage, spawns, repo)`;
  - `open_session(map_id=0, repo=REPO) -> DressSession`;
  - `plan_pass(session, poi_id, template_name, seed=1, anchor=None, keep_away=(), allow_placeholder=False, render=True) -> dict`;
  - `check_pass(session, doc, render=True) -> dict`;
  - `nav_check(session, docs, runner=None, nav_factory=None) -> None`;
  - `pass_images(session, doc) -> dict[str, Image]`.
  - **CLI** `tools/world/dress.py`, with subcommands `plan`, `check`, `apply`, `undo`, `list`, `publish-nav`, `nav-baseline`, `packet-index`.

- [ ] **Step 1: Write the failing test.** Create `tools/tests/test_world_dressing_commands.py`:

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""End-to-end test of a dressing pass on a synthetic world: plan, check, apply, packet, undo.

	python tools/tests/test_world_dressing_commands.py
"""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402
from test_world_dressing_lint import RULES, lint_world  # noqa: E402
from test_world_dressing_templates import TEMPLATE  # noqa: E402

from worldkit.assets import build_catalog  # noqa: E402
from worldkit.atlas import empty_atlas  # noqa: E402
from worldkit.dress_commands import DressSession, check_pass, plan_pass  # noqa: E402
from worldkit.dressing import apply_pass, undo_pass  # noqa: E402
from worldkit.foliage import load_world_foliage  # noqa: E402
from worldkit.packet import write_packet  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402
from worldkit.terrain_kinds import TerrainKinds  # noqa: E402


class MapEntry:
	id, name, directory, instancetype = 0, "Lint World", "L", 0


class CommandTests(unittest.TestCase):
	def test_plan_check_apply_packet_undo(self):
		import json
		with tempfile.TemporaryDirectory() as tmp:
			repo = Path(tmp)
			lint_world(repo)
			folder = repo / "tools" / "world" / "templates"
			folder.mkdir(parents=True)
			(folder / "t.json").write_text(json.dumps(TEMPLATE), encoding="utf-8")
			atlas = empty_atlas(0)
			atlas.add_poi(id="site", name="Site", kind="camp", center=[50.0, 60.0], radius=30.0, status="canon", source="user")
			query = WorldQuery(build_snapshot("L", repo=repo), atlas=atlas, kinds=TerrainKinds({"Models/Terrain/B.hmi": ["grass", "road", "rock", "dirt"]}))
			session = DressSession(MapEntry(), query, build_catalog(repo), RULES, load_world_foliage("L", repo), [], repo)
			doc = plan_pass(session, "site", "t", seed=4, render=False)
			self.assertEqual(doc["status"], "planned")
			self.assertTrue(doc["items"])
			self.assertEqual([v for v in doc["checks"]["placement"] if v["severity"] == "error"], [])
			doc = check_pass(session, doc, render=False)
			doc = apply_pass(doc, repo, probe=lambda: False)
			packet = write_packet(doc, {}, repo)
			readme = (packet / "README.md").read_text(encoding="utf-8")
			self.assertIn("dress.py undo", readme)
			self.assertIn("nothing", readme)          # the asset gap is listed
			self.assertEqual(len(undo_pass(doc, repo, probe=lambda: False).removed), len(doc["items"]))


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_commands -v`
Expected: ImportError, `worldkit.dress_commands`.

- [ ] **Step 3: Implement `packet.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Review packets for dressing passes: generated/world/review/<pass-id>/ (README, images, manifest)."""

from __future__ import annotations

import json
from pathlib import Path

from .paths import REPO, review_dir


def _checks(lines: list[str], title: str, violations: list[dict]) -> None:
    lines.append(f"## {title}")
    lines.append("")
    if not violations:
        lines.append("None.")
    for v in violations:
        lines.append(f"- **{v['severity']}** `{v['rule']}`: {v['message']}")
    lines.append("")


def write_packet(doc: dict, images: dict, repo: Path = REPO) -> Path:
    folder = review_dir(repo) / doc["pass_id"]
    folder.mkdir(parents=True, exist_ok=True)
    for name, image in images.items():
        image.save(folder / name)
    (folder / "manifest.json").write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    lines = [f"# Dressing pass {doc['pass_id']}", "",
             f"- Place: `{doc['poi']}`; template `{doc['template']}`; seed {doc['seed']}; status **{doc['status']}**",
             f"- Anchor ({doc['anchor'][0]:.1f}, {doc['anchor'][1]:.1f}); entry point ({doc['entry'][0]:.1f}, {doc['entry'][1]:.1f}). North is up (-Z).",
             f"- Undo: `python tools/world/dress.py undo {doc['pass_id']}` (mmo_edit closed). "
             "Keep it: commit data/client yourself; single props can be moved or deleted in mmo_edit.", ""]
    for note in doc.get("notes", []):
        lines.append(f"> {note}")
    if doc.get("notes"):
        lines.append("")
    pictures = sorted(images)
    if pictures:
        lines += ["## Pictures", ""] + [f"![{name}]({name})" for name in pictures] + [""]
    lines += ["## Items", "", "| # | Role | Asset | Position (x, y, z) | Yaw | Scale | Stored in |", "|---|---|---|---|---|---|---|"]
    for index, item in enumerate(doc["items"]):
        x, y, z = item["position"]
        lines.append(f"| {index} | {item['role']} | {item['asset'].rsplit('/', 1)[-1]} | ({x:.1f}, {y:.1f}, {z:.1f}) | "
                     f"{item['yaw']:.0f} | {item['scale']:.2f} | {item['store']} |")
    lines.append("")
    lines += ["## Gaps", ""]
    if not doc["gaps"]:
        lines.append("None.")
    for gap in doc["gaps"]:
        reason = "no asset matches the role (art request)" if gap["kind"] == "asset" else "no valid spot found"
        lines.append(f"- `{gap['role']}`: placed {gap['placed']} of {gap['wanted']}: {reason}")
    lines.append("")
    _checks(lines, "Placement checks", doc["checks"]["placement"])
    _checks(lines, "Walkability checks", doc["checks"]["walkability"])
    if doc["status"] == "applied-unchecked":
        lines += ["**Walkability unchecked**: run `python tools/world/dress.py check --nav " + doc["pass_id"] + "`.", ""]
    (folder / "README.md").write_text("\n".join(lines), encoding="utf-8", newline="\n")
    return folder


def write_batch_index(name: str, docs: list[dict], repo: Path = REPO) -> Path:
    folder = review_dir(repo) / name
    folder.mkdir(parents=True, exist_ok=True)
    lines = [f"# Dressing batch {name}", "", "| Pass | Place | Template | Status | Items | Errors | Gaps |", "|---|---|---|---|---|---|---|"]
    for doc in docs:
        errors = sum(1 for v in doc["checks"]["placement"] + doc["checks"]["walkability"] if v["severity"] == "error")
        lines.append(f"| [{doc['pass_id']}](../{doc['pass_id']}/README.md) | {doc['poi']} | {doc['template']} | {doc['status']} | "
                     f"{len(doc['items'])} | {errors} | {len(doc['gaps'])} |")
    (folder / "README.md").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    return folder
```

- [ ] **Step 4: Implement `dress_commands.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Orchestration behind tools/world/dress.py: plan, check, walkability and pictures for passes."""

from __future__ import annotations

import json
import math
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from .assets import GeometryCache, build_catalog
from .atlas import Atlas
from .dressing import PassError, new_draft, new_pass_id, save_doc, world_fingerprint
from .foliage import all_instances, load_world_foliage
from .materials import TextureCache
from .nav import NavQuery, build_nav, load_routes, scratch_nav_root, walkability_violations
from .paths import REPO, client_root, passes_dir, templates_dir, world_tools
from .previews import site_previews
from .prop_lint import PropContext, lint_props, props_from_entities, props_from_foliage
from .spawns import object_spawn_records, unit_spawn_records
from .tags import load_tag_rules
from .templates import item_to_prop, load_template, place


@dataclass
class DressSession:
    map_entry: object
    query: object
    catalog: dict
    rules: object
    foliage: dict
    spawns: list
    repo: Path = REPO

    @property
    def directory(self) -> str:
        return self.map_entry.directory

    @property
    def atlas(self) -> Atlas:
        return self.query.atlas


def open_session(map_id: int = 0, repo: Path = REPO) -> DressSession:
    from .cli_query import open_map
    _, map_entry, query = open_map(map_id)
    if map_entry.instancetype != 0:
        raise PassError(f"map {map_id} is an instance map; dressing works on terrain maps only")
    return DressSession(map_entry, query, build_catalog(repo), load_tag_rules(), load_world_foliage(map_entry.directory, repo),
                        unit_spawn_records(map_entry) + object_spawn_records(map_entry), repo)


def _poi(session: DressSession, poi_id: str) -> dict:
    poi = session.atlas.poi(poi_id) if session.atlas else None
    if poi is None:
        raise PassError(f"no place {poi_id!r} in the atlas")
    return poi


def _context(session: DressSession, poi: dict | None) -> PropContext:
    existing = props_from_entities(session.query.snapshot.entities) + props_from_foliage(session.foliage)
    roads = [road["points"] for road in session.atlas.roads] if session.atlas else []
    return PropContext(session.query, session.catalog, session.rules, existing, session.spawns, poi, roads)


def _keep_away(session: DressSession, specs) -> list[tuple[float, float, float]]:
    circles = []
    for spec in specs:
        poi_id, _, metres = spec.partition(":")
        poi = _poi(session, poi_id)
        circles.append((poi["center"][0], poi["center"][1], float(poi.get("radius", 0.0)) + float(metres or 0.0)))
    return circles


def _lint(session: DressSession, doc: dict) -> list[dict]:
    context = _context(session, _poi(session, doc["poi"]))
    applied = {item.get("unique_id") for item in doc["items"] if item.get("unique_id")}
    context.existing = [p for p in context.existing if p.key.split(":", 1)[-1] not in {str(int(u, 16)) for u in applied}]
    props = [item_to_prop(item, f"{doc['pass_id']}:{i}") for i, item in enumerate(doc["items"])]
    return [v.to_dict() for v in lint_props(props, context)]


def _render_map(session: DressSession, doc: dict, name: str, extra: list[str]) -> Path:
    folder = passes_dir(session.repo) / doc["pass_id"]
    folder.mkdir(parents=True, exist_ok=True)
    out = folder / name
    subprocess.run([sys.executable, str(world_tools(session.repo) / "render_map.py"), "--map", str(session.map_entry.id),
                    "--poi", doc["poi"], "--margin", "60", "--out", str(out), *extra], check=True, cwd=str(world_tools(session.repo)),
                   capture_output=True)
    return out


def pass_images(session: DressSession, doc: dict) -> dict:
    poi = _poi(session, doc["poi"])
    radius = float(poi.get("radius", 30.0))
    previews = site_previews(session.query, session.catalog, GeometryCache(session.repo), TextureCache(client_root(session.repo)),
                             tuple(doc["anchor"]), radius, session.query.snapshot.entities, all_instances(session.foliage), doc["items"])
    return {f"preview_{k + 1}.png": image for k, (_, image) in enumerate(previews)}


def plan_pass(session: DressSession, poi_id: str, template_name: str, seed: int = 1, anchor=None, keep_away=(),
              allow_placeholder: bool = False, render: bool = True) -> dict:
    poi = _poi(session, poi_id)
    if poi.get("status") != "canon" and not allow_placeholder:
        raise PassError(f"place {poi_id!r} is {poi.get('status')}: dress confirmed (canon) places, or pass --allow-placeholder")
    template = load_template(template_name, templates_dir(session.repo))
    centre = tuple(anchor) if anchor else tuple(poi["center"])
    circles = _keep_away(session, keep_away)
    result = place(template, centre, float(poi.get("radius", 30.0)), _context(session, poi), seed, circles)
    doc = new_draft(new_pass_id(poi_id, session.repo), session.map_entry.id, session.directory, poi_id, template_name, seed,
                    centre, list(keep_away), result.entry, result.items, result.gaps, template.notes,
                    world_fingerprint(session.directory, session.repo))
    doc["checks"]["placement"] = _lint(session, doc)
    save_doc(doc, session.repo)
    if render:
        _render_map(session, doc, "before.png", ["--dump-state", str(passes_dir(session.repo) / doc["pass_id"] / "before.json")])
        for name, image in pass_images(session, doc).items():
            image.save(passes_dir(session.repo) / doc["pass_id"] / name)
    return doc


def check_pass(session: DressSession, doc: dict, render: bool = True) -> dict:
    """Re-lints a (hand-edited) draft and re-fingerprints the world it was checked against."""
    if doc["status"] != "planned":
        raise PassError(f"pass {doc['pass_id']} is {doc['status']}; only planned drafts are re-checked (use --nav for applied ones)")
    doc["checks"]["placement"] = _lint(session, doc)
    doc["world_fingerprint"] = world_fingerprint(session.directory, session.repo)
    save_doc(doc, session.repo)
    if render:
        for name, image in pass_images(session, doc).items():
            image.save(passes_dir(session.repo) / doc["pass_id"] / name)
    return doc


def nav_check(session: DressSession, docs: list[dict], runner=None, nav_factory=None) -> None:
    """One scratch navmesh build for all given applied passes, then their walkability checks."""
    from .dressing import save_doc as save
    try:
        nav_dir = build_nav(session.directory, scratch_nav_root(session.repo), session.repo, runner=runner or subprocess.run)
    except Exception as exc:  # nav build failures must not lose the applied state
        for doc in docs:
            doc["status"] = "applied-unchecked"
            doc["nav"] = {"error": str(exc)[-500:]}
            save(doc, session.repo)
        raise
    routes = load_routes()
    factory = nav_factory or (lambda: NavQuery(nav_dir, session.directory, session.repo))
    with factory() as nav:
        for doc in docs:
            poi = _poi(session, doc["poi"])
            site = {"name": doc["poi"], "x": doc["entry"][0], "z": doc["entry"][1], "radius": float(poi.get("radius", 30.0))}
            violations = walkability_violations(nav, session.query, routes, [site], session.spawns)
            doc["checks"]["walkability"] = [v.to_dict() for v in violations]
            doc["nav"] = {"path": str(nav_dir.relative_to(session.repo).as_posix())}
            doc["status"] = "applied"
            save(doc, session.repo)


def after_map(session: DressSession, doc: dict) -> Path:
    before = passes_dir(session.repo) / doc["pass_id"] / "before.json"
    extra = ["--diff", str(before)] if before.is_file() else []
    return _render_map(session, doc, "after.png", extra)
```

- [ ] **Step 5: Implement the CLI.** Create `tools/world/dress.py`:

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Dress confirmed atlas places with props and trees, one undoable pass at a time.

    python tools/world/dress.py plan --poi south_ridge_quarry --template quarry --seed 7 [--keep-away forest_bandit_grounds:20]
    python tools/world/dress.py check 20261001-south_ridge_quarry-1          # after editing the draft by hand
    python tools/world/dress.py apply 20261001-south_ridge_quarry-1 [--skip-nav]
    python tools/world/dress.py check --nav PASS [PASS ...]                   # one scratch nav build, walkability for all
    python tools/world/dress.py undo PASS [--force]
    python tools/world/dress.py list
    python tools/world/dress.py nav-baseline                                  # store protected-route lengths
    python tools/world/dress.py packet-index --name ring-pilot PASS [PASS ...]
    python tools/world/dress.py publish-nav --yes                             # rebuild the REAL navmesh (user request only)

apply, undo and publish-nav refuse while mmo_edit runs. data/client is never committed by this tool.
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

from worldkit.dress_commands import after_map, check_pass, nav_check, open_session, pass_images, plan_pass
from worldkit.dressing import PassError, apply_pass, editor_running, list_docs, load_doc, undo_pass
from worldkit.nav import NavQuery, build_nav, load_routes, route_length, save_routes, scratch_nav_root
from worldkit.packet import write_batch_index, write_packet
from worldkit.paths import REPO, passes_dir


def _summary(doc: dict) -> str:
    errors = [v for v in doc["checks"]["placement"] + doc["checks"]["walkability"] if v["severity"] == "error"]
    warnings = [v for v in doc["checks"]["placement"] + doc["checks"]["walkability"] if v["severity"] == "warning"]
    return (f"{doc['pass_id']}: {doc['status']}, {len(doc['items'])} items, {len(errors)} errors, "
            f"{len(warnings)} warnings, {len(doc['gaps'])} gaps")


def _packet(session, doc) -> Path:
    images = pass_images(session, doc)
    after = after_map(session, doc)
    folder = write_packet(doc, images, REPO)
    shutil.copyfile(after, folder / "after.png")
    before = passes_dir(REPO) / doc["pass_id"] / "before.png"
    if before.is_file():
        shutil.copyfile(before, folder / "before.png")
    return folder


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--map", type=int, default=0)
    sub = parser.add_subparsers(dest="command", required=True)
    plan = sub.add_parser("plan")
    plan.add_argument("--poi", required=True)
    plan.add_argument("--template", required=True)
    plan.add_argument("--seed", type=int, default=1)
    plan.add_argument("--anchor", type=float, nargs=2, metavar=("X", "Z"))
    plan.add_argument("--keep-away", action="append", default=[], metavar="POI:METRES")
    plan.add_argument("--allow-placeholder", action="store_true")
    check = sub.add_parser("check")
    check.add_argument("passes", nargs="+")
    check.add_argument("--nav", action="store_true", help="walkability for applied passes (one scratch build)")
    apply = sub.add_parser("apply")
    apply.add_argument("pass_id")
    apply.add_argument("--skip-nav", action="store_true")
    undo = sub.add_parser("undo")
    undo.add_argument("pass_id")
    undo.add_argument("--force", action="store_true")
    sub.add_parser("list")
    sub.add_parser("nav-baseline")
    index = sub.add_parser("packet-index")
    index.add_argument("--name", required=True)
    index.add_argument("passes", nargs="+")
    publish = sub.add_parser("publish-nav")
    publish.add_argument("--yes", action="store_true")
    args = parser.parse_args(argv)

    try:
        if args.command == "list":
            for doc in list_docs():
                print(_summary(doc))
            return 0
        if args.command == "packet-index":
            print(write_batch_index(args.name, [load_doc(p) for p in args.passes]))
            return 0
        if args.command == "undo":
            doc = load_doc(args.pass_id)
            report = undo_pass(doc, force=args.force)
            print(f"{doc['pass_id']}: {doc['status']}; removed {len(report.removed)}, kept (changed by you) {len(report.changed)}, "
                  f"already gone {len(report.missing)}")
            for item in report.changed:
                print(f"  kept: {item}")
            return 0
        if args.command == "publish-nav":
            if not args.yes:
                print("publish-nav rebuilds data/editor/nav (the real navmesh). Re-run with --yes when the user asked for it.")
                return 1
            if editor_running():
                raise PassError("close mmo_edit first")
            session = open_session(args.map)
            print(build_nav(session.directory, REPO / "data" / "editor", REPO))
            return 0
        session = open_session(args.map)
        if args.command == "plan":
            doc = plan_pass(session, args.poi, args.template, args.seed, args.anchor, args.keep_away, args.allow_placeholder)
            print(_summary(doc))
            print(f"draft and previews: {passes_dir() / doc['pass_id']}")
        elif args.command == "check":
            docs = [load_doc(p) for p in args.passes]
            if args.nav:
                nav_check(session, docs)
                for doc in docs:
                    print(_summary(doc), "->", _packet(session, doc))
            else:
                for doc in docs:
                    print(_summary(check_pass(session, doc)))
        elif args.command == "apply":
            doc = apply_pass(load_doc(args.pass_id))
            if args.skip_nav:
                doc["status"] = "applied-unchecked"
                from worldkit.dressing import save_doc
                save_doc(doc)
            else:
                nav_check(session, [doc])
            print(_summary(doc), "->", _packet(session, doc))
        elif args.command == "nav-baseline":
            nav_dir = scratch_nav_root() / "nav"
            routes = load_routes()
            with NavQuery(nav_dir, session.directory) as nav:
                for route in routes:
                    route.baseline_length = route_length(nav, session.query, route.points)
                    print(f"{route.id}: {route.baseline_length}")
            save_routes(routes)
        return 0
    except PassError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_commands -v`
Expected: OK.

- [ ] **Step 7: Run the whole world suite**

Run: `cd tools/tests; py -3.14 -m unittest discover -s . -p "test_world_*.py"`
Expected: OK.

- [ ] **Step 8: Commit**

```bash
git add tools/world/worldkit/packet.py tools/world/worldkit/dress_commands.py tools/world/dress.py tools/tests/test_world_dressing_commands.py
git commit -m "feat(world): dress.py CLI - plan, check, apply, undo, nav checks and review packets"
```

---

### Task 13: Props check in the content audit

**Files:**
- Create: `tools/world/prop_lint.py`
- Modify: `tools/gate/content_audit.py` (`ALL_DOMAINS`, the dispatch, the GLOBAL filter, the docstring)
- Modify: `tools/world/lint_baseline.json` (new `props` section, via the CLI)
- Test: `tools/tests/test_world_dressing_lint.py` (add a CLI smoke test)

**Interfaces:**
- Consumes: `lint_world_props`, `PropContext`, `props_from_entities`, `props_from_foliage` (Task 8); baseline helpers (spec 1).
- Produces: `python tools/world/prop_lint.py --map N [--json F] [--update-baseline] [--show-known]`. It exits 1 on new errors. The JSON shape matches `lint.py`: `known`, `new_errors`, `new_warnings`.

- [ ] **Step 1: Write the CLI.** Create `tools/world/prop_lint.py`:

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Placement lint for every prop (.wobj) and tree (.hfol) of a map, compared to the baseline.

    python tools/world/prop_lint.py --map 0
    python tools/world/prop_lint.py --map 0 --update-baseline   # accept today's findings as known

Exit code 1 when there are new errors (not in the "props" section of tools/world/lint_baseline.json).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from worldkit.assets import build_catalog
from worldkit.baseline import BASELINE_PATH, load_baseline, save_baseline, split
from worldkit.cli_query import open_map
from worldkit.foliage import load_world_foliage
from worldkit.prop_lint import PropContext, lint_world_props, props_from_entities, props_from_foliage
from worldkit.snapshot import NoTerrainError
from worldkit.spawns import object_spawn_records, unit_spawn_records
from worldkit.tags import load_tag_rules


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--map", type=int, default=0)
    parser.add_argument("--baseline", type=Path, default=BASELINE_PATH)
    parser.add_argument("--update-baseline", action="store_true")
    parser.add_argument("--json", type=Path)
    parser.add_argument("--show-known", action="store_true")
    args = parser.parse_args(argv)
    try:
        _, map_entry, query = open_map(args.map)
    except NoTerrainError as exc:
        print(f"skipped map {args.map}: {exc}")
        return 0
    if map_entry.instancetype != 0:
        print(f"skipped map {args.map}: instance map")
        return 0
    foliage = load_world_foliage(map_entry.directory)
    existing = props_from_entities(query.snapshot.entities) + props_from_foliage(foliage)
    roads = [road["points"] for road in query.atlas.roads] if query.atlas else []
    context = PropContext(query, build_catalog(), load_tag_rules(), existing,
                          unit_spawn_records(map_entry) + object_spawn_records(map_entry), None, roads)
    violations = lint_world_props(context)
    if args.update_baseline:
        count = save_baseline("props", args.map, violations, args.baseline)
        print(f"baselined {count} prop findings for map {args.map} in {args.baseline}")
        return 0
    new, known = split(violations, load_baseline("props", args.baseline))
    new_errors = [v for v in new if v.severity == "error"]
    new_warnings = [v for v in new if v.severity == "warning"]
    print(f"map {args.map} ({map_entry.name}): checked {len(existing)} props, {len(new_errors)} new errors, "
          f"{len(new_warnings)} new warnings, {len(known)} known")
    for v in new_errors:
        print(f"  ERROR   {v.rule}: {v.message}")
    for v in new_warnings:
        print(f"  WARNING {v.rule}: {v.message}")
    if args.show_known:
        for v in known:
            print(f"  known   {v.rule}: {v.message}")
    if args.json:
        args.json.write_text(json.dumps({"known": len(known), "new_errors": [v.to_dict() for v in new_errors],
                                         "new_warnings": [v.to_dict() for v in new_warnings]}, indent=2), encoding="utf-8")
    return 1 if new_errors else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Wire the audit.** In `tools/gate/content_audit.py`:
  - `ALL_DOMAINS = [*DOMAINS, "questchain", "placement", "reachability", "props", "xp"]`
  - In `audit_world`, change the map filter to `maps = [m for m in msg.entry if name not in ("placement", "props") or m.instancetype == MAP_GLOBAL]`.
  - In `main`, add before the `else:` branch:

```python
            elif name == "props":
                result = audit_world("prop_lint", "props", tmp_dir)
```

  - Extend the module docstring sentence about world checks: `... the world placement lint (placement domain, GLOBAL maps), the prop lint (props domain) and the quest reachability report ...`

- [ ] **Step 3: Baseline today's props, then check that the run is clean**

Run: `cd tools/world; py -3.14 prop_lint.py --map 0 --show-known`
Expected: it lists findings for the existing 147 props and the foliage. The likely ones are fortress towers overlapping walls, gate pieces on the road, and the prototype cylinder.

Read the error list. It must contain no Python errors, and no `prop_unknown_asset` for assets that exist; if it does, that is a catalog bug, so fix it first. Then:

Run: `cd tools/world; py -3.14 prop_lint.py --map 0 --update-baseline; py -3.14 prop_lint.py --map 0`
Expected: `0 new errors, 0 new warnings`.

Run: `cd H:/mmo; py -3.14 tools/gate/content_audit.py --domain props`
Expected: `[props] maps 1, new errors 0 ...` and `Result: GREEN`.

- [ ] **Step 4: Add the CLI smoke test.** Append to `test_world_dressing_lint.py`:

```python
@fx.requires_live_data
class PropLintCliTests(unittest.TestCase):
	def test_shipped_world_is_clean_against_the_baseline(self):
		sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "world"))
		import prop_lint  # noqa: E402
		self.assertEqual(prop_lint.main(["--map", "0"]), 0)
```

Run: `cd tools/tests; py -3.14 -m unittest test_world_dressing_lint -v`
Expected: OK.

- [ ] **Step 5: Commit**

```bash
git add tools/world/prop_lint.py tools/gate/content_audit.py tools/world/lint_baseline.json tools/tests/test_world_dressing_lint.py
git commit -m "feat(gate): props domain in the content audit, baselined"
```

---

### Task 14: The world-dresser skill and the docs

**Files:**
- Create: `.agents/skills/world-dresser/SKILL.md`
- Modify: `tools/world/README.md` (commands and files tables)
- Test: `tools/tests/test_skills_hygiene.py` must stay green; run it.

- [ ] **Step 1: Write the skill.** Create `.agents/skills/world-dresser/SKILL.md`:

````markdown
---
name: world-dresser
description: Dress confirmed world-atlas places with props and trees (quarries, camps, cave mouths, ruins) as undoable passes, using the asset catalog, site templates, placement and walkability checks, and a visual review packet. Use when asked to dress, decorate, furnish or populate a place with objects, props, rocks or trees, or to undo such a pass.
---

<essential_rules>
- Dress only places whose atlas status is `canon`, unless the user explicitly asks for a placeholder.
- **mmo_edit must be closed** for `apply`, `undo` and `publish-nav`. If it is running, ask the user to close it. Never kill it.
- Never commit `data/client`. The user commits accepted passes. Do commit the manifests under `data/world/passes/`.
- Never run `publish-nav` unless the user asked for it; it rebuilds the real navmesh.
- Look at the previews before applying. A pass with placement errors cannot be applied; fix the draft by hand instead of loosening the checks.
- Report asset gaps honestly: a role with no matching asset is an art request, never a reason to use an unrelated asset.
</essential_rules>

<world_aware_design>
1. Read `docs/world/bible.md`: the place's section and the ring dressing palette (section 3.2). Read the place's atlas entry: description, notes, radius.
2. Look at the assets you will use:
   - `cd tools/world; python -m worldkit assets --sheets` writes contact sheets to `generated/world/assets/sheets/`; open the relevant ones.
   - `python -m worldkit assets --views <asset>` shows one asset from three sides.
3. Pick or write a template in `tools/world/templates/` (see the template guide below). Keep away from neighbours with `--keep-away <poi>:<metres>`.
4. Run `python tools/world/dress.py plan ...`, then open the previews in `generated/world/passes/<id>/`. North is -Z; the previews name the direction they look in.
5. Fix what looks wrong by editing `draft.json`: move, rotate, swap or delete an item. Then run `dress.py check <id>` and look again.
6. Run `dress.py apply <id>`. When doing several passes, use `--skip-nav` and then one `dress.py check --nav <id> <id> ...` at the end.
7. Hand the user the packet: `dress.py packet-index --name <batch> <ids...>`. List the gaps, any "awaiting water" notes, and how to undo.
</world_aware_design>

<template_guide>
- A template file has: `name`, `description`, `entry` (`anchor` or a role name), `clear` (radii kept empty around the anchor), `notes`, and `roles`.
- Each role has `name`, `query` (`tags`, `exclude`, `size`: small < 1.5 m, medium < 4 m, large; or `assets`), `count` [min, max], `spacing`, `store` (`wobj` or `hfol`), `rule`, and optionally `scale` and `yaw`.
- Rules:
  - `at_anchor {offset}`
  - `ring {r1, r2}`
  - `scatter {radius_fraction}`
  - `cluster {radius_fraction, spread}`
  - `against_cliff {min_cliff_slope, max_offset}`
  - `along_path {offset}`
  - `near_role {role, distance}`
- Trees and plants go in `hfol`; everything else goes in `wobj`.
</template_guide>

<tags>
- `tools/world/asset_tags.json` maps asset globs to tags and placement defaults (`sink`, `max_slope`, `align_to_slope`, `may_overlap`, `footprint_scale`, `collides_override`).
- Add rules only for assets that have none (`python -m worldkit assets --untagged`), and only after looking at them.
- The user's edits win.
</tags>

<undo>
- `python tools/world/dress.py undo <id>` removes exactly what the pass wrote.
- Anything the user changed since is kept and listed, unless `--force`.
</undo>
````

- [ ] **Step 2: Extend `tools/world/README.md`.** Add these rows to the commands table:

```markdown
| `python -m worldkit assets [--sheets] [--views ASSET] [--untagged]` | Asset catalog summary; contact sheets and 3-view pictures in `generated/world/assets/`. |
| `python tools/world/dress.py plan/check/apply/undo/list ...` | Dressing passes: place a template on a confirmed atlas place, check it, write it, undo it. See the world-dresser skill. |
| `python tools/world/prop_lint.py --map 0` | Placement lint for every existing prop and tree, compared to the `props` baseline. |
```

and these rows to the files table:

```markdown
| `tools/world/asset_tags.json` | Asset tags and placement defaults for dressing. |
| `tools/world/templates/*.json` | Site templates (roles, counts, placement rules). |
| `tools/world/nav_routes.json` | Protected routes that dressing must keep walkable, with baseline lengths. |
| `data/world/passes/<id>.json` | Manifest of every applied dressing pass: what was written, with ids and hashes. |
| `generated/world/passes/`, `generated/world/assets/`, `generated/world/nav/` | Drafts and previews, catalog and contact sheets, scratch navmesh (gitignored). |
```

- [ ] **Step 3: Sync and check the skills**

Run: `powershell -File tools/sync_skills.ps1; powershell -File tools/sync_skills.ps1 -Check; cd tools/tests; py -3.14 -m unittest test_skills_hygiene -v`
Expected: `.claude/skills/world-dresser` exists as a junction; the check passes; the hygiene tests pass. In particular, there must be no `F:\mmo` path in the new skill.

- [ ] **Step 4: Commit**

```bash
git add .agents/skills/world-dresser tools/world/README.md
git commit -m "docs: world-dresser skill and worldkit README for dressing"
```

---

### Task 15: Asset tags v1 from the contact sheets

This is a content task. Its deliverable is a reviewed `asset_tags.json`.

**Files:**
- Modify: `tools/world/asset_tags.json`

- [ ] **Step 1: Render the sheets and list the untagged assets**

Run: `cd tools/world; py -3.14 -m worldkit assets --sheets --untagged`

- [ ] **Step 2: Look at every sheet** under `generated/world/assets/sheets/` with the Read tool. For each folder, write down which assets are:
  - rocks, by size and whether they are cliff-like;
  - ruins, walls, towers;
  - camp props, crates, barrels, carts, tools;
  - trees and dead trees;
  - bushes, plants and herbs;
  - unusable ones (broken, interior-only, prototype).

  Desert rocks are the main rock source, so check that their colour reads as grey stone rather than sand. If they look sandy, tag them `rock` anyway and note it in the commit message for the user.

- [ ] **Step 3: Edit the rules.**
  - Keep the existing rules; append specific ones later in the file.
  - Every asset under `Models/` must end up either with at least one tag, or tagged `prototype` or `dungeon`.
  - Give tents, carts, buildings, ruins and towers the right `max_slope` and `sink`.
  - Give trees `footprint_scale` 0.15.
  - Plants get `collides_override: false`.

Run: `cd tools/world; py -3.14 -m worldkit assets --untagged`
Expected: no `untagged:` lines.

- [ ] **Step 4: Run the tests**

Run: `cd tools/tests; py -3.14 -m unittest discover -s . -p "test_world_*.py"`
Expected: OK.

- [ ] **Step 5: Commit**

```bash
git add tools/world/asset_tags.json
git commit -m "data(world): asset tags v1 from the contact sheets"
```

---

### Task 16: The pilot, five passes on the Oakenshire ring

This is a content task; the user reviews the result. **Do not run this task while mmo_edit is open.** Ask the user to close it and wait for confirmation.

**Files:**
- Modify (tuning only): `tools/world/templates/*.json`
- Create: `data/world/passes/<id>.json`, one per applied pass
- Modify: `tools/world/nav_routes.json` (baselines)

- [ ] **Step 1: Baseline the protected routes before any pass.** This uses the scratch build from Task 11, rebuilt now so it matches the current world.

```powershell
bin\Release\nav_builder.exe -d data\client -w Development -o generated\world
cd tools\world; py -3.14 dress.py nav-baseline
```

Expected: three route lengths are printed, none of them `None`. If one is `None`, a route point is off the mesh; move that point onto the road in `nav_routes.json` and rerun.

Then commit:

```bash
git add tools/world/nav_routes.json
git commit -m "data(world): protected-route baselines before the ring pilot"
```

- [ ] **Step 2: Plan the five passes**

```powershell
cd tools\world
py -3.14 dress.py plan --poi south_ridge_quarry --template quarry --seed 7 --keep-away forest_bandit_grounds:20
py -3.14 dress.py plan --poi hidden_cave --template cave_mouth --seed 3
py -3.14 dress.py plan --poi ring_waterfall --template waterfall_basin --seed 5
py -3.14 dress.py plan --poi high_ridge_hunting_grounds --template hunting_camp --seed 2
py -3.14 dress.py plan --poi dungeon_entrance --template abbey_surround --seed 11 --anchor 107 345
```

Expected: each prints a summary with 0 errors, and a folder with `before.png` and `preview_1..3.png`.

- [ ] **Step 3: Review each draft visually.** Open all three previews and `before.png` per pass with the Read tool. For each site, check:
  - Does it read as intended (spec §7)?
  - Is anything floating, sunk or blocking?
  - Is the abbey teleport circle at (107, 345) clear?
  - Does the quarry keep 20 m from the bandit tents?
  - Is the cave gap open?

  Fix problems in one of these ways:
  - edit `draft.json` and run `py -3.14 dress.py check <id>`;
  - re-plan with another seed (`plan` again; delete the abandoned draft folder);
  - tune the template JSON (counts, radii, rules), then re-plan.

  Never loosen a placement check to make a draft pass.

- [ ] **Step 4: Apply all five, then one walkability run**

```powershell
py -3.14 dress.py apply <quarry-id> --skip-nav
py -3.14 dress.py apply <cave-id> --skip-nav
py -3.14 dress.py apply <waterfall-id> --skip-nav
py -3.14 dress.py apply <hunting-id> --skip-nav
py -3.14 dress.py apply <abbey-id> --skip-nav
py -3.14 dress.py check --nav <quarry-id> <cave-id> <waterfall-id> <hunting-id> <abbey-id>
```

Expected: every pass `applied`, with no walkability errors. If a site is unreachable or a route broke, undo that pass (`dress.py undo <id>`), fix the draft or the template, re-plan and re-apply.

- [ ] **Step 5: Verify the end state**

```powershell
py -3.14 prop_lint.py --map 0
py -3.14 lint.py --map 0
py -3.14 report.py --map 0
cd ..\tests; py -3.14 -m unittest discover -s . -p "test_world_*.py"
```

Expected:
- `prop_lint` shows 0 new errors. New warnings are acceptable only if they are listed and explained in the packet.
- The spawn lint and the report are unchanged from before the pilot.
- The tests pass.

Undo test (acceptance 4):
1. Plan a throwaway pass on one site with `--seed 99`.
2. Apply it.
3. Copy the touched `.hfol` file before and after.
4. Undo the pass.
5. Compare the file with the copy taken before the apply (`fc /b` on Windows, `cmp` elsewhere).

It must be byte-identical. Then delete that draft's folder.

- [ ] **Step 6: Write the batch packet and hand over**

```powershell
cd ..\world; py -3.14 dress.py packet-index --name ring-pilot <quarry-id> <cave-id> <waterfall-id> <hunting-id> <abbey-id>
```

Commit the manifests and any template tuning:

```bash
git add data/world/passes tools/world/templates
git commit -m "data(world): ring pilot - five dressing passes (manifests)"
```

Then send the user `generated/world/review/ring-pilot/README.md` and one preview per site, using SendUserFile. Tell them:
- the five sites are written to `data/client` and visible after restarting mmo_edit;
- how to keep a site (commit `data/client`), fix single props (in the editor), or throw a site away (`dress.py undo <id>`);
- the asset gaps (abbey, bell tower, cave entrance, pines, waterfall art) as art requests;
- "awaiting water" for the waterfall;
- that `publish-nav` will update the real navmesh once they are happy.

Do not commit `data/client`. Do not run `publish-nav`.

- [ ] **Step 7: Record the implementation notes.** Append an "Implementation notes" section to this plan with:
  - the first catalog build time and the nav build time;
  - texture-resolution coverage, i.e. the count of meshes without a resolved texture;
  - deviations that happened during execution;
  - template tunings made in the pilot.

Then commit:

```bash
git add docs/superpowers/plans/2026-10-01-world-dressing.md
git commit -m "docs: world dressing implementation notes"
```

---

## Self-review

**Spec coverage:**

| Spec section | Task |
|---|---|
| §4.1 catalog + tags | 5, 15 |
| §4.2 renderer, sheets, views, previews | 6, 7 |
| §4.3 writers | 1, 2 |
| §4.4 passes and CLI | 10, 12 |
| §4.5 templates | 9 |
| §4.6 prop checks + audit | 8, 13 |
| §4.7 nav | 11 |
| §4.8 packets | 12 |
| §5 safety rules 1–7 | 10 (guard, fingerprint, undo conflicts), 12 (`publish-nav --yes`, unchecked status, instance refusal in `open_session`), Global Constraints (no `data/client` commits) |
| §6 skill | 14 |
| §7 pilot | 16 |
| §8 acceptance 1–6 | 16 (Steps 4–6), gate |
| §9 testing | every task |
| §11 risks | timings recorded in Task 16, Step 7 |

**Type consistency:**
- Draft item keys are identical in Tasks 9, 10, 12 and the tests.
- `PlacedProp` fields are identical in Tasks 8, 9 and 12.
- `Violation.to_dict()` is used for every stored check.
- `unique_id` is stored as a `0x%016x` string everywhere.
