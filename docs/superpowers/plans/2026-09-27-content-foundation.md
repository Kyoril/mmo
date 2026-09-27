# Content Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give content-authoring agents a queryable, renderable, lintable model of the game world (terrain, props, spawns, named places, lore) plus an editor pin workflow, so quests and spawns are designed against the real world and reviewed on a map.

**Architecture:** A pure-Python package `worldkit` (under `tools/world/`) parses the engine's binary world formats (`.tile`, `.wobj`, `.hwld`) and the protobuf game data, caches a per-map `WorldSnapshot`, and exposes point queries, placement lint, a content report and a map renderer. Named places live in a tracked, tool-side atlas (`data/world/atlas/map_<id>.json`) edited by agents as text and by the user through a new mmo_edit "Atlas" edit mode. Lore lives in `docs/world/bible.md`. The content skills and the weekly content audit call into `worldkit`.

**Tech Stack:** Python 3.12+ (stdlib `unittest`), numpy, Pillow, protobuf (runtime for protoc 3.21 gencode); C++17 + ImGui + nlohmann json for the editor mode.

**Spec:** `docs/superpowers/specs/2026-09-27-content-foundation-design.md` (approved 2026-09-27).

## Global Constraints

- Every new source file starts with the copyright header: Python `# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`, C++ `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- C++ style (CLAUDE.md): Allman braces, braces on every `if`, tabs, `m_camelCase` members, `PascalCase` methods, `#pragma once`, Doxygen on public members, no exceptions.
- Python tests use stdlib `unittest`, live in `tools/tests/test_world_*.py` / `tools/tests/test_skills_hygiene.py`, and must pass under the gate's `python -m unittest discover -s tools/tests -p test_*.py`.
- Tests must never write into `data/editor` (a git submodule with live content) or `data/client`; use `tempfile` directories.
- Python deps: `protobuf>=4.21,<6`, `numpy>=1.24`, `Pillow>=10.1` (listed in `tools/world/requirements.txt`).
- **Interpreter note:** the gate calls `python`. In a Claude Code session the Windows-Store `python` alias can fail to launch ("cannot execute" / "Unable to create process"). Then use `py -3.14` for every `python` command in this plan, after Task 1 Step 1 installed the deps into it. Never "fix" this by editing gate scripts.
- Lint thresholds (verbatim from spec): height delta > 0.5 m warning, > 2 m error; slope > 35° error; in water / in terrain hole error; within 5 m of terrain edge error; inside entity footprint warning; regular grid pattern within a pack warning; outside referenced POI or level outside POI/zone band warning. "In water" means water depth > 1.0 m at the spawn (shoreline spawns in ankle-deep water are fine).
- Atlas file format: UTF-8, 2-space indent, `ensure_ascii=False`, trailing newline, every float rounded to 0.1, existing key order preserved, new objects written in schema key order. Status values: `placeholder | canon | note`. POI kinds: `hub | camp | ruin | lair | landmark | resource | dungeon_entrance | note`. Sources: `agent-bootstrap | agent | user`. Only the user promotes anything to `canon`.
- Quest rule (user, firm): every quest ends with a turn-in at an NPC or object; `AutoRewarded` (flag `0x20`) is an error for new or edited quests.
- No engine, protocol, server or client runtime change. The atlas is never exported to client, server or ClientDB.
- `.agents/skills` is the single tracked skill source. Never edit `.claude/skills` directly (after Task 1 it is a set of junctions).
- Commit after every task on branch `feature/content-foundation`; end commit messages with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never push.

## Deliberate narrowings of the spec (decided while planning)

- **Atlas roads are named roads only.** Road *cells* are shown in renders from the terrain-kind layer (Task 6). Turning road cells into polylines automatically is left out (YAGNI). The bootstrap creates placeholder polylines for the named roads from the user's notes (Westroad, Northroad).
- **No separate `atlas.schema.json`.** `worldkit/atlas.py` is the single definition of the schema (one source of truth). The C++ editor only edits fields and keeps unknown fields.
- **Entity footprints are per-path heuristics** (spec §5.2 fallback). World models (`.hwmo`) have walkable interiors, so they never count as a blocking footprint. They do count as "structure", and the height rule is skipped on a structure.
- **Height consistency instead of exact parity.** The new sampler reads the rendered fan surface. That is the correct surface, but it differs from the old bilinear-corner value wherever inner vertices were sculpted. So the test asserts a small *median* difference on the real world, not equality.
- **Only new errors fail.** New warnings (grid packs, level bands, naming) are reported but do not turn the audit red.
- **One baseline file with sections.** `tools/world/lint_baseline.json` holds `placement` and `reachability` sections, and each section is updated per map.
- **Indentation.** Non-test Python under `tools/world` and `.agents` uses 4 spaces, like `tools/*.py`. Tests in `tools/tests` use tabs, like the existing tests. Some listings in this plan are tab-indented; convert when writing.

## File Structure

```
tools/world/
  README.md                      how to run every CLI; interpreter note
  requirements.txt
  worldkit/__init__.py           package marker + __version__
  worldkit/constants.py          terrain constants and page/world coordinate helpers
  worldkit/paths.py              repo-relative locations of every input/output
  worldkit/formats/__init__.py
  worldkit/formats/chunks.py     chunk iteration, byte cursor, FormatError
  worldkit/formats/tile.py       .tile terrain page parser (v1 + v2)
  worldkit/formats/wobj.py       .wobj placed-entity parser (v1..v3)
  worldkit/formats/hwld.py       .hwld world header parser
  worldkit/surface.py            exact rendered-surface height/slope (4-triangle fan)
  worldkit/entities.py           loads world entities, footprint/structure heuristics
  worldkit/snapshot.py           WorldSnapshot build + npz cache
  worldkit/terrain_kinds.py      (material, layer) -> terrain kind lookup
  worldkit/terrain_kinds.json    the lookup table (filled in Task 6)
  worldkit/data.py               protobuf game data loader
  worldkit/spawns.py             SpawnRecord flattening + stable keys + packs
  worldkit/atlas.py              atlas load/validate/save/edit
  worldkit/query.py              WorldQuery point queries + placeable()
  worldkit/lint.py               placement rules -> Violation
  worldkit/baseline.py           known-violation baseline
  worldkit/report.py             reachability + content health
  worldkit/render.py             map rendering
  worldkit/skill_checks.py       checks used by the skill validators
  worldkit/__main__.py           `python -m worldkit query|layers`
  lint.py                        CLI
  report.py                      CLI
  render_map.py                  CLI
  bootstrap_atlas.py             CLI (one-shot, refuses to overwrite)
  extract_canon.py               CLI (quest/NPC text dump for the bible)
  lint_baseline.json             generated in Task 12
tools/tests/world_fixtures.py    synthetic .tile/.wobj/.hwld byte builders for tests
tools/tests/test_world_formats.py
tools/tests/test_world_surface.py
tools/tests/test_world_snapshot.py
tools/tests/test_world_atlas.py
tools/tests/test_world_query.py
tools/tests/test_world_lint.py
tools/tests/test_world_report.py
tools/tests/test_world_render.py
tools/tests/test_world_bootstrap.py
tools/tests/test_skills_hygiene.py
tools/sync_skills.ps1
data/world/atlas/map_0.json      generated in Task 13, then user-reviewed
docs/world/bible.md              written in Task 14
src/mmo_edit/editors/world_editor/edit_modes/atlas_edit_mode.h/.cpp   Task 16
```

Modified: `.agents/skills/**` (hygiene + atlas steps), `tools/gate/content_audit.py`, `tools/gate/verify.ps1`, `.agents/skills/mmo-npc-designer/scripts/inspect_terrain.py`, `.agents/skills/mmo-npc-designer/scripts/validate_npc_json.py`, `.agents/skills/mmo-quest-creator/scripts/validate_quest_json.py`, `tools/particle_gen/README.md`, `src/mmo_edit/editors/world_editor/world_editor_instance.{h,cpp}`, `.gitignore` (nothing: `generated/` is already ignored).

Shared coordinate conventions (used by every task):
- Page index: `page_x = floor(x / PAGE_SIZE) + 32`, `page_z = floor(z / PAGE_SIZE) + 32`; page origin `((page_x - 32) * PAGE_SIZE, (page_z - 32) * PAGE_SIZE)`.
- Terrain page file: `data/client/Worlds/<Dir>/<Dir>/Terrain/<page_x>_<page_z>.tile`.
- Entity file: `data/client/Worlds/<Dir>/<Dir>/Entities/<(page_x << 8) | page_z>/<uniqueId>.wobj`.
- Arrays are indexed `[z, x]` (row = z). A page has 129×129 outer vertices, 128×128 cells (4.1667 m each) and one inner (centre) vertex per cell. Tiles are 16×16 per page, 8×8 cells per tile, tile index `tx + tz * 16`, per-tile bit index `ix + iz * 8`.
- On-disk chunk magics are the C++ multi-char literal byte-reversed: `.tile` uses `MVER MCMT MCVT MCVI MCNM MCNI MCLY MCVS MCSI MHOL MCWQ MCAR`; `.wobj` uses `REVW HSMW OMWW`; `.hwld` uses `REVM HSEM TNEM TERR`.

---

### Task 1: Skill consolidation and hygiene

**Files:**
- Create: `tools/world/requirements.txt`, `tools/sync_skills.ps1`, `tools/tests/test_skills_hygiene.py`
- Move (copy then delete source): `.claude/skills/terrain-author/` → `.agents/skills/terrain-author/`, `.claude/skills/particle-author/` → `.agents/skills/particle-author/`
- Modify: every `.agents/skills/**/SKILL.md` and reference that mentions `F:\mmo`/`F:/mmo`; `.agents/skills/mmo-item-designer/scripts/inspect_item_catalog.py:128`; `.agents/skills/mmo-item-designer/scripts/validate_item_json.py:136`; `.agents/skills/mmo-item-designer/SKILL.md:23,35,84`; `.agents/skills/mmo-item-designer/references/icon-guidelines.md:32,57`; `.agents/skills/mmo-spell-designer/SKILL.md:78,103,110`; `.agents/skills/mmo-quest-creator/SKILL.md` (success criteria); `.agents/skills/mmo-npc-designer/scripts/inspect_npc_catalog.py` (row cap notice); `.agents/skills/terrain-author/references/example-layout-zone.py` (scratchpad path); `tools/gate/content_audit.py:30`; `tools/gate/verify.ps1` (after line 128 block); `tools/particle_gen/README.md:34`

**Interfaces:**
- Produces: `tools/sync_skills.ps1 [-Check] [-Force]` — makes each `.claude/skills/<name>` a junction to `.agents/skills/<name>` (skipping `source-command-*`); `-Check` only reports and always exits 0.
- Produces: `content_audit.SKILLS == REPO / ".agents" / "skills"`.

- [ ] **Step 1: Make a working interpreter available**

Run: `python -c "import google.protobuf, numpy, PIL; print('ok')"`
If that fails to launch (Store alias), run instead:
```powershell
py -3.14 -m pip install --user "protobuf>=4.21,<6" "numpy>=1.24" "Pillow>=10.1"
py -3.14 -c "import google.protobuf, numpy, PIL; print('ok')"
```
Expected: `ok`. From here on, "`python`" means whichever of the two printed `ok`.

Create `tools/world/requirements.txt`:
```
protobuf>=4.21,<6
numpy>=1.24
Pillow>=10.1
```

- [ ] **Step 2: Write the failing hygiene test**

Create `tools/tests/test_skills_hygiene.py`:
```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Keeps the tracked skill source (.agents/skills) free of machine-specific paths and
harness-specific instructions, and pins the tooling that must read it.

.claude/skills is gitignored and becomes a set of junctions to .agents/skills
(tools/sync_skills.ps1), so anything checked here is what every agent actually reads.

	python tools/tests/test_skills_hygiene.py
"""

import re
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SKILLS = REPO / ".agents" / "skills"
TEXT_SUFFIXES = {".md", ".py", ".yaml", ".ps1"}


def skill_text_files():
	for path in sorted(SKILLS.rglob("*")):
		if path.is_file() and path.suffix in TEXT_SUFFIXES and "__pycache__" not in path.parts:
			yield path


class SkillHygieneTests(unittest.TestCase):
	def test_no_machine_specific_repo_paths(self):
		offenders = []
		for path in skill_text_files():
			text = path.read_text(encoding="utf-8", errors="replace")
			if re.search(r"F:[\\/]mmo", text):
				offenders.append(str(path.relative_to(REPO)))
		self.assertEqual(offenders, [], "skills must not hardcode F:\\mmo; derive the repo root instead")

	def test_no_codex_only_tool_instructions(self):
		offenders = []
		for path in skill_text_files():
			if path.parent.name.startswith("source-command-") or path.name == "openai.yaml":
				continue
			text = path.read_text(encoding="utf-8", errors="replace")
			for needle in ("codex_app.", ".codex\\", ".codex/", "built-in `image_gen` tool"):
				if needle in text:
					offenders.append(f"{path.relative_to(REPO)}: {needle}")
		self.assertEqual(offenders, [])

	def test_skill_names_match_directories(self):
		for skill_md in sorted(SKILLS.glob("*/SKILL.md")):
			text = skill_md.read_text(encoding="utf-8")
			match = re.search(r"^name:\s*\"?([^\"\n]+)\"?\s*$", text, re.MULTILINE)
			self.assertIsNotNone(match, f"{skill_md} has no name: frontmatter")
			self.assertEqual(match.group(1).strip(), skill_md.parent.name)

	def test_authoring_skills_are_tracked(self):
		for name in ("terrain-author", "particle-author", "mmo-quest-creator", "mmo-npc-designer"):
			self.assertTrue((SKILLS / name / "SKILL.md").is_file(), f"{name} must live in .agents/skills")

	def test_quest_skill_has_no_object_counter_contradiction(self):
		text = (SKILLS / "mmo-quest-creator" / "SKILL.md").read_text(encoding="utf-8")
		self.assertNotIn("lack of native object-use counters", text)

	def test_content_audit_reads_tracked_skills(self):
		sys.path.insert(0, str(REPO / "tools" / "gate"))
		import content_audit
		self.assertEqual(content_audit.SKILLS, REPO / ".agents" / "skills")


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 3: Run it to verify it fails**

Run: `python tools/tests/test_skills_hygiene.py`
Expected: FAIL. At least `test_no_machine_specific_repo_paths`, `test_no_codex_only_tool_instructions`, `test_authoring_skills_are_tracked`, `test_quest_skill_has_no_object_counter_contradiction` and `test_content_audit_reads_tracked_skills` fail.

- [ ] **Step 4: Move the untracked skills into the tracked source**

```bash
cp -r .claude/skills/terrain-author .agents/skills/terrain-author
cp -r .claude/skills/particle-author .agents/skills/particle-author
rm -rf .agents/skills/terrain-author/**/__pycache__ .agents/skills/particle-author/**/__pycache__
```
Check the diff between the copies once more (only `spell_catalog_lib.py` should differ, and the `.agents` copy is the newer one):
```bash
diff -rq --strip-trailing-cr -x __pycache__ .agents/skills .claude/skills
```
Expected: only `Only in .agents/skills: source-command-fix-crash` plus the `spell_catalog_lib.py` difference. The `.claude` copy of `spell_catalog_lib.py` is the stale one (it lacks aura types 38–40), so nothing needs to come back from `.claude`.

- [ ] **Step 5: Remove machine-specific paths**

Apply these rewrites in every file the test reported (use `grep -rn "F:[\\/]mmo" .agents/skills` to list them):
- `--project-root F:/mmo` and `--project-root F:\mmo` → delete the flag and its value (all npc/quest/item/spell scripts derive the root from their own location).
- `F:/mmo/generated/` → `generated/`; `F:\mmo\generated\` → `generated\`.
- In frontmatter descriptions and prose: `for F:/mmo` / `for F:\mmo` → `for this repository`; `in \`F:/mmo\`` → `in this repository`.

Replace the two hardcoded argparse defaults so the item scripts derive the root like the npc scripts do:
- `.agents/skills/mmo-item-designer/scripts/inspect_item_catalog.py:128` and `validate_item_json.py:136`:
```python
    parser.add_argument("--project-root", default=str(Path(__file__).resolve().parents[4]))
```
(Both files already import `Path`. If not, add `from pathlib import Path`.)

Fix `.agents/skills/terrain-author/references/example-layout-zone.py`. Replace the hardcoded scratchpad output path with a CLI argument:
```python
import argparse
parser = argparse.ArgumentParser()
parser.add_argument("--out-dir", required=True, help="where to write the generated layout (e.g. your scratchpad)")
OUT_DIR = Path(parser.parse_args().out_dir)
```
Keep the rest of the file unchanged, and use `OUT_DIR` wherever the old literal was used.

Fix `tools/particle_gen/README.md:34`: `(.claude/skills/particle-author/)` → `(.agents/skills/particle-author/)`.

- [ ] **Step 6: Make the Codex-only instructions tool-neutral**

- `mmo-item-designer/SKILL.md:23` replace the line with:
  `If \`python\` resolves to an unavailable Windows Store shim, run the scripts with \`py -3.14\` (or the workspace Python your harness provides) instead.`
- `mmo-item-designer/SKILL.md:35,84`, `references/icon-guidelines.md:32,57`, `mmo-spell-designer/SKILL.md:103,110`: replace "the built-in \`image_gen\` tool" with "the available image-generation tool (\`image_gen\` in Codex, the image-generation connector in Claude Code)". Replace other bare mentions of "\`image_gen\` output/result/export" with "image-generation output/result/export". Keep every rule about quality and not substituting placeholder art.
- `mmo-spell-designer/SKILL.md:78`: `C:\Users\RobinKlimonow\.codex\generated_images\<session>\<image>.png` → `<path to the generated source image>.png`.

- [ ] **Step 7: Fix the quest SKILL.md contradiction**

In `.agents/skills/mmo-quest-creator/SKILL.md` success criteria, replace:
`- The draft respects the current quest runtime constraints, especially the four-objective counter limit and the lack of native object-use counters.`
with:
`- The draft respects the current quest runtime constraints, especially the four-objective counter limit, and spell-cast-on-object requirements are credited only through the spell path.`

- [ ] **Step 8: Make the silent row cap visible**

In `.agents/skills/mmo-npc-designer/scripts/inspect_npc_catalog.py`, find where results are sliced to `args.limit` (default 20). Then:

1. Before slicing, keep the full count:
```python
total = len(rows)
rows = rows[: args.limit]
```
2. Next to the returned rows, put a notice into the JSON result:
```python
result["truncated"] = {"shown": len(rows), "total": total} if total > len(rows) else None
```
Use the actual local variable names in that function. Also print `f"NOTE: showing {len(rows)} of {total}; pass --limit {total} for all"` to stderr when the list is truncated.

- [ ] **Step 9: Point the content audit at the tracked source**

`tools/gate/content_audit.py:30`:
```python
SKILLS = REPO / ".agents" / "skills"
```

- [ ] **Step 10: Write the junction sync script**

Create `tools/sync_skills.ps1`:
```powershell
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
<#
.SYNOPSIS
	Makes every .claude/skills/<name> a directory junction to the tracked .agents/skills/<name>.
.DESCRIPTION
	.agents/skills is the single tracked skill source (Codex reads it directly). Claude Code reads
	.claude/skills, which is gitignored, so it used to be a hand-maintained copy that drifted.
	This script replaces each copy with a junction. It refuses to replace a copy that differs from
	the tracked source unless -Force is given, and it never touches skills that exist only in
	.claude/skills (user-local skills). Codex-migrated command skills (source-command-*) are skipped
	because Claude Code already has those as slash commands.
	-Check only reports problems and always exits 0 (used by tools/gate/verify.ps1).
#>
param(
	[switch]$Check,
	[switch]$Force
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repoRoot ".agents\skills"
$target = Join-Path $repoRoot ".claude\skills"

function Get-NormalizedFiles([string]$root)
{
	$result = @{}
	Get-ChildItem -Path $root -Recurse -File | Where-Object { $_.FullName -notmatch "\\__pycache__\\" } | ForEach-Object {
		$rel = $_.FullName.Substring($root.Length).TrimStart("\")
		$result[$rel] = ((Get-Content -Raw -LiteralPath $_.FullName) -replace "`r`n", "`n")
	}
	return $result
}

function Test-Junction([string]$path)
{
	$item = Get-Item -LiteralPath $path -Force
	return ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
}

$problems = @()
if (-not (Test-Path $target))
{
	if ($Check) { $problems += ".claude/skills is missing" } else { New-Item -ItemType Directory -Path $target | Out-Null }
}

foreach ($skill in Get-ChildItem -Path $source -Directory)
{
	if ($skill.Name -like "source-command-*") { continue }
	$link = Join-Path $target $skill.Name

	if (Test-Path $link)
	{
		if (Test-Junction $link)
		{
			$resolved = (Get-Item -LiteralPath $link -Force).Target
			if ($resolved -and ((Resolve-Path $resolved).Path -ne $skill.FullName))
			{
				$problems += "$($skill.Name): junction points to $resolved"
			}
			continue
		}

		if ($Check)
		{
			$problems += "$($skill.Name): is a copy, not a junction (run tools/sync_skills.ps1)"
			continue
		}

		$a = Get-NormalizedFiles $skill.FullName
		$b = Get-NormalizedFiles $link
		$diff = @($a.Keys + $b.Keys | Sort-Object -Unique | Where-Object { $a[$_] -ne $b[$_] })
		if ($diff.Count -gt 0 -and -not $Force)
		{
			Write-Host "REFUSING $($skill.Name): .claude copy differs from tracked source in:" -ForegroundColor Red
			$diff | ForEach-Object { Write-Host "  $_" }
			Write-Host "Merge those changes into .agents/skills/$($skill.Name) first, or re-run with -Force to discard them."
			exit 1
		}
		Remove-Item -LiteralPath $link -Recurse -Force
	}
	elseif ($Check)
	{
		$problems += "$($skill.Name): missing from .claude/skills (run tools/sync_skills.ps1)"
		continue
	}

	New-Item -ItemType Junction -Path $link -Target $skill.FullName | Out-Null
	Write-Host "linked $($skill.Name)"
}

if ($problems.Count -gt 0)
{
	$problems | ForEach-Object { Write-Host "WARNING: skills: $_" -ForegroundColor Yellow }
}
elseif ($Check)
{
	Write-Host "skills: .claude/skills junctions OK"
}
exit 0
```

- [ ] **Step 11: Hook the check into the gate (warning only)**

In `tools/gate/verify.ps1`, directly after the `tool_tests` `if ($ok) { ... }` block (currently ending at line 129), add:
```powershell
	# Warning only: a stale .claude/skills copy does not break the build, but it is why an
	# agent can follow instructions that the tracked .agents/skills no longer contains.
	& (Join-Path $repoRoot "tools\sync_skills.ps1") -Check
```

- [ ] **Step 12: Run the sync and the tests**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/sync_skills.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/sync_skills.ps1 -Check
python tools/tests/test_skills_hygiene.py
```
Expected:
- The first command prints `linked …` for each skill. If it prints `REFUSING mmo-spell-designer … spell_catalog_lib.py`, that difference is the stale `.claude` copy verified in Step 4, so re-run with `-Force`.
- `-Check` prints `skills: .claude/skills junctions OK`.
- The tests PASS.

Afterwards the `.claude/skills/terrain-author` and `.claude/skills/particle-author` directories have been replaced by junctions to the tracked copies.

- [ ] **Step 13: Commit**

```bash
git add .agents/skills tools/sync_skills.ps1 tools/tests/test_skills_hygiene.py tools/world/requirements.txt tools/gate/content_audit.py tools/gate/verify.ps1 tools/particle_gen/README.md
git commit -m "chore(skills): single tracked skill source, junction sync, drop F:\\mmo and Codex-only paths

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Chunk reader and terrain page parser

**Files:**
- Create: `tools/world/worldkit/__init__.py`, `tools/world/worldkit/constants.py`, `tools/world/worldkit/paths.py`, `tools/world/worldkit/formats/__init__.py`, `tools/world/worldkit/formats/chunks.py`, `tools/world/worldkit/formats/tile.py`, `tools/tests/world_fixtures.py`
- Test: `tools/tests/test_world_formats.py`

**Interfaces:**
- Produces `worldkit.constants`: `TILES_PER_PAGE=16`, `CELLS_PER_TILE=8`, `OUTER_PER_PAGE=129`, `INNER_PER_PAGE=128`, `LEGACY_OUTER_PER_PAGE=273`, `PIXELS_PER_PAGE=1009`, `WORLD_CENTER_PAGE=32`, `TILE_SIZE`, `PAGE_SIZE`, `CELL_SIZE`, `page_of(x: float, z: float) -> tuple[int, int]`, `page_origin(page_x: int, page_z: int) -> tuple[float, float]`.
- Produces `worldkit.paths`: `REPO: Path`, `world_root(directory: str, repo: Path = REPO) -> Path`, `terrain_dir(...)`, `entities_dir(...)`, `hwld_path(...)`, `atlas_path(map_id: int, repo=REPO) -> Path`, `cache_dir(directory: str, repo=REPO) -> Path`, `data_dir(repo=REPO) -> Path`, `skill_scripts(repo=REPO) -> Path`.
- Produces `worldkit.formats.chunks`: `class FormatError(ValueError)`, `iter_chunks(data: bytes, source: str) -> list[tuple[bytes, memoryview]]`, `class Cursor` with `u8() u16() u32() u64() f32() floats(n) -> np.ndarray str8() str16() remaining() done() -> bool`.
- Produces `worldkit.formats.tile`: `@dataclass TilePage` (fields below), `parse_tile(path: Path) -> TilePage`, `TilePage.hole_cells() -> np.ndarray[(128,128), bool]`, `TilePage.water_cells() -> np.ndarray[(128,128), bool]`, `expand_tile_bits(masks: np.ndarray[(16,16), uint64]) -> np.ndarray[(128,128), bool]`.
- Produces `tools/tests/world_fixtures.py`: `tile_bytes(**overrides) -> bytes`, `chunk(magic: bytes, payload: bytes) -> bytes`.

- [ ] **Step 1: Write the constants, paths and package markers**

`tools/world/worldkit/__init__.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""worldkit: read-only model of the MMO game world for content tooling.

Parses terrain pages, placed entities and the world header in pure Python, loads the
protobuf game data, and answers placement questions. See tools/world/README.md.
"""

__version__ = "1.0.0"
```

`tools/world/worldkit/formats/__init__.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Binary world file parsers. Each module is the only Python code that knows its format."""
```

`tools/world/worldkit/constants.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Terrain constants mirrored from src/shared/terrain/constants.h."""

from __future__ import annotations

import math
import struct

TILES_PER_PAGE = 16
CELLS_PER_TILE = 8
OUTER_PER_PAGE = 129          # (9 - 1) * 16 + 1 outer vertices per page side
INNER_PER_PAGE = 128          # one inner (centre) vertex per cell
LEGACY_OUTER_PER_PAGE = 273   # v1 pages
PIXELS_PER_PAGE = 1009        # (64 - 1) * 16 + 1 splat pixels per page side
WORLD_CENTER_PAGE = 32
MAX_PAGES = 64

# The engine declares `constexpr double TileSize = 33.33333f;` - a float literal widened to
# double. Use the same widened value so page and cell boundaries match the engine exactly.
TILE_SIZE = struct.unpack("<f", struct.pack("<f", 33.33333))[0]
PAGE_SIZE = TILE_SIZE * TILES_PER_PAGE
CELL_SIZE = PAGE_SIZE / INNER_PER_PAGE
PIXEL_SIZE = PAGE_SIZE / (PIXELS_PER_PAGE - 1)


def page_of(x: float, z: float) -> tuple[int, int]:
	"""Returns the (page_x, page_z) index of the page containing world point (x, z)."""
	return int(math.floor(x / PAGE_SIZE)) + WORLD_CENTER_PAGE, int(math.floor(z / PAGE_SIZE)) + WORLD_CENTER_PAGE


def page_origin(page_x: int, page_z: int) -> tuple[float, float]:
	"""Returns the world (x, z) of a page's minimum corner."""
	return (page_x - WORLD_CENTER_PAGE) * PAGE_SIZE, (page_z - WORLD_CENTER_PAGE) * PAGE_SIZE
```

`tools/world/worldkit/paths.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Repository-relative locations of every worldkit input and output."""

from __future__ import annotations

from pathlib import Path

# tools/world/worldkit/paths.py -> parents: worldkit, world, tools, <repo>
REPO = Path(__file__).resolve().parents[3]


def world_root(directory: str, repo: Path = REPO) -> Path:
	return repo / "data" / "client" / "Worlds" / directory / directory


def terrain_dir(directory: str, repo: Path = REPO) -> Path:
	return world_root(directory, repo) / "Terrain"


def entities_dir(directory: str, repo: Path = REPO) -> Path:
	return world_root(directory, repo) / "Entities"


def hwld_path(directory: str, repo: Path = REPO) -> Path:
	return repo / "data" / "client" / "Worlds" / directory / f"{directory}.hwld"


def atlas_path(map_id: int, repo: Path = REPO) -> Path:
	return repo / "data" / "world" / "atlas" / f"map_{map_id}.json"


def cache_dir(directory: str, repo: Path = REPO) -> Path:
	return repo / "generated" / "world" / directory


def data_dir(repo: Path = REPO) -> Path:
	return repo / "data" / "editor" / "data"


def skill_scripts(repo: Path = REPO) -> Path:
	"""Scripts dir whose proto_runtime.py compiles the .proto schemas (tracked copy)."""
	return repo / ".agents" / "skills" / "mmo-quest-creator" / "scripts"
```

- [ ] **Step 2: Write the chunk reader**

`tools/world/worldkit/formats/chunks.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Chunked binary container reading shared by all world formats.

A chunk is a 4-byte magic, a little-endian uint32 payload size, then the payload. Magics are
compared as the bytes found on disk (the C++ multi-char literals appear byte-reversed).
"""

from __future__ import annotations

import struct

import numpy as np


class FormatError(ValueError):
	"""Raised when a world file does not match the layout this parser knows. Never guessed around."""


def iter_chunks(data: bytes, source: str) -> list[tuple[bytes, memoryview]]:
	"""Splits a file into (magic, payload) pairs, validating every size against the file length."""
	chunks: list[tuple[bytes, memoryview]] = []
	view = memoryview(data)
	offset = 0
	while offset < len(data):
		if offset + 8 > len(data):
			raise FormatError(f"{source}: truncated chunk header at byte {offset}")
		magic = bytes(view[offset:offset + 4])
		size = struct.unpack_from("<I", data, offset + 4)[0]
		start = offset + 8
		end = start + size
		if end > len(data):
			raise FormatError(f"{source}: chunk {magic!r} at byte {offset} claims {size} bytes but only {len(data) - start} remain")
		chunks.append((magic, view[start:end]))
		offset = end
	return chunks


class Cursor:
	"""Sequential little-endian reader over one chunk payload."""

	def __init__(self, payload: memoryview, source: str, magic: bytes):
		self._data = payload
		self._pos = 0
		self._source = source
		self._magic = magic

	def _take(self, count: int) -> memoryview:
		if self._pos + count > len(self._data):
			raise FormatError(f"{self._source}: chunk {self._magic!r} ends early (need {count} bytes at offset {self._pos}, size {len(self._data)})")
		out = self._data[self._pos:self._pos + count]
		self._pos += count
		return out

	def u8(self) -> int:
		return self._take(1)[0]

	def u16(self) -> int:
		return struct.unpack("<H", self._take(2))[0]

	def u32(self) -> int:
		return struct.unpack("<I", self._take(4))[0]

	def u64(self) -> int:
		return struct.unpack("<Q", self._take(8))[0]

	def f32(self) -> float:
		return struct.unpack("<f", self._take(4))[0]

	def floats(self, count: int) -> np.ndarray:
		return np.frombuffer(self._take(count * 4), dtype="<f4").astype(np.float32)

	def str8(self) -> str:
		return bytes(self._take(self.u8())).decode("utf-8")

	def str16(self) -> str:
		return bytes(self._take(self.u16())).decode("utf-8")

	def remaining(self) -> int:
		return len(self._data) - self._pos

	def done(self) -> bool:
		return self._pos == len(self._data)
```

- [ ] **Step 3: Write the test fixtures (a synthetic .tile writer)**

`tools/tests/world_fixtures.py`:
```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Byte builders for synthetic world files used by the worldkit tests.

They mirror the writers in src/shared/terrain_io/page_io.cpp (SavePage) and
src/mmo_edit/editors/world_editor/world_editor_instance.cpp (.wobj save), so a parser test
exercises the same layout the engine writes. Not a production writer.
"""

import struct
import sys
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "world"))


def chunk(magic: bytes, payload: bytes) -> bytes:
	return magic + struct.pack("<I", len(payload)) + payload


def str16(text: str) -> bytes:
	raw = text.encode("utf-8")
	return struct.pack("<H", len(raw)) + raw


def str8(text: str) -> bytes:
	raw = text.encode("utf-8")
	return struct.pack("<B", len(raw)) + raw


def tile_bytes(
	outer=None, inner=None, areas=None, materials=None, layers=None,
	holes=None, water=None, water_heights=None, version=2, extra_chunks=b"",
) -> bytes:
	"""Builds a .tile file. Arrays default to a flat page at height 0.

	holes: dict {tile_index: uint64 mask}; water: dict {tile_index: (type, uint64 mask)}.
	"""
	parts = [chunk(b"MVER", struct.pack("<I", version))]
	names = materials if materials is not None else [""] * 256
	parts.append(chunk(b"MCMT", struct.pack("<H", len(names)) + b"".join(str16(n) for n in names)))
	if version == 2:
		o = np.zeros((129, 129), np.float32) if outer is None else np.asarray(outer, np.float32)
		i = np.zeros((128, 128), np.float32) if inner is None else np.asarray(inner, np.float32)
		parts.append(chunk(b"MCVT", o.astype("<f4").tobytes()))
		parts.append(chunk(b"MCVI", i.astype("<f4").tobytes()))
	else:
		o = np.zeros((273, 273), np.float32) if outer is None else np.asarray(outer, np.float32)
		parts.append(chunk(b"MCVT", o.astype("<f4").tobytes()))
	parts.append(chunk(b"MCNM", b"\x00\x7f\x00" * (129 * 129)))
	lay = np.full((1009, 1009), 0x000000FF, np.uint32) if layers is None else np.asarray(layers, np.uint32)
	parts.append(chunk(b"MCLY", lay.astype("<u4").tobytes()))
	if holes:
		payload = struct.pack("<H", len(holes)) + b"".join(struct.pack("<HQ", k, v) for k, v in sorted(holes.items()))
		parts.append(chunk(b"MHOL", payload))
	if water:
		wh = np.zeros((129, 129), np.float32) if water_heights is None else np.asarray(water_heights, np.float32)
		payload = struct.pack("<H", len(water))
		payload += b"".join(struct.pack("<HBQ", k, t, m) for k, (t, m) in sorted(water.items()))
		payload += wh.astype("<f4").tobytes() + str16("")
		parts.append(chunk(b"MCWQ", payload))
	a = np.zeros((16, 16), np.uint32) if areas is None else np.asarray(areas, np.uint32)
	parts.append(chunk(b"MCAR", a.astype("<u4").tobytes()))
	return b"".join(parts) + extra_chunks


def wobj_bytes(kind="mesh", version=3, unique_id=7, asset="Models/Test/Crate.hmsh",
			   position=(1.0, 2.0, 3.0), rotation=(1.0, 0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0),
			   name="Crate", category="Props", overrides=()) -> bytes:
	body = struct.pack("<Q", unique_id) + str16(asset)
	body += struct.pack("<3f", *position) + struct.pack("<4f", *rotation) + struct.pack("<3f", *scale)
	if kind == "mesh":
		body += struct.pack("<B", len(overrides)) + b"".join(struct.pack("<B", i) + str16(m) for i, m in overrides)
		if version > 1:
			body += str8(name) + str16(category)
		magic = b"HSMW"
	else:
		if version >= 3:
			body += str8(name) + str16(category)
		magic = b"OMWW"
	return chunk(b"REVW", struct.pack("<I", version)) + chunk(magic, body)


def hwld_bytes(version=3, has_terrain=True, default_material="Models/Terrain/Default.hmi", meshes=("Models/A.hmsh",)) -> bytes:
	parts = [chunk(b"REVM", struct.pack("<I", version))]
	parts.append(chunk(b"TERR", struct.pack("<B", 1 if has_terrain else 0) + str16(default_material)))
	parts.append(chunk(b"HSEM", b"".join(m.encode() + b"\x00" for m in meshes)))
	return b"".join(parts)
```

- [ ] **Step 4: Write the failing tile tests**

`tools/tests/test_world_formats.py`:
```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the worldkit binary format parsers (.tile, .wobj, .hwld).

Synthetic files come from world_fixtures.py, which mirrors the C++ writers. The final test class
parses every shipped world file, so a format change in the engine that the parsers do not know
about fails here instead of producing silently wrong heights.

	python tools/tests/test_world_formats.py
"""

import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402  (also puts tools/world on sys.path)

from worldkit.formats.chunks import FormatError  # noqa: E402
from worldkit.formats import tile  # noqa: E402


def write(tmp: Path, name: str, data: bytes) -> Path:
	path = tmp / name
	path.write_bytes(data)
	return path


class TileParserTests(unittest.TestCase):
	def setUp(self):
		self._tmp = tempfile.TemporaryDirectory()
		self.tmp = Path(self._tmp.name)

	def tearDown(self):
		self._tmp.cleanup()

	def test_v2_page_roundtrip(self):
		outer = np.arange(129 * 129, dtype=np.float32).reshape(129, 129)
		inner = np.full((128, 128), 5.0, np.float32)
		areas = np.zeros((16, 16), np.uint32)
		areas[2, 3] = 8  # tz=2, tx=3
		materials = [""] * 256
		materials[3 + 2 * 16] = "Models/Terrain/X.hmi"
		path = write(self.tmp, "33_31.tile", fx.tile_bytes(outer=outer, inner=inner, areas=areas, materials=materials))

		page = tile.parse_tile(path)

		self.assertEqual((page.page_x, page.page_z, page.version), (33, 31, 2))
		self.assertEqual(page.outer.shape, (129, 129))
		self.assertEqual(page.outer[1, 0], 129.0)  # [z, x]
		self.assertTrue(page.inner_from_file)
		self.assertEqual(page.inner[0, 0], 5.0)
		self.assertEqual(page.areas[2, 3], 8)
		self.assertEqual(page.materials[3 + 2 * 16], "Models/Terrain/X.hmi")
		self.assertEqual(page.layers.shape, (1009, 1009))

	def test_holes_and_water_expand_to_cells(self):
		# tile (tx=1, tz=0) index 1: hole bit for inner cell (ix=2, iz=3) -> cell (cx=10, cz=3)
		holes = {1: 1 << (2 + 3 * 8)}
		# tile (tx=0, tz=1) index 16: water quad (qx=0, qz=0) -> cell (cx=0, cz=8)
		water = {16: (1, 1)}
		path = write(self.tmp, "32_32.tile", fx.tile_bytes(holes=holes, water=water))
		page = tile.parse_tile(path)

		hole_cells = page.hole_cells()
		self.assertTrue(hole_cells[3, 10])
		self.assertEqual(int(hole_cells.sum()), 1)
		water_cells = page.water_cells()
		self.assertTrue(water_cells[8, 0])
		self.assertEqual(int(water_cells.sum()), 1)
		self.assertEqual(page.water_type[1, 0], 1)

	def test_v1_page_resamples_and_derives_inner(self):
		outer = np.zeros((273, 273), np.float32)
		outer[0, 272] = 10.0  # far x corner of row z=0
		path = write(self.tmp, "30_29.tile", fx.tile_bytes(outer=outer, version=1))
		page = tile.parse_tile(path)
		self.assertEqual(page.version, 1)
		self.assertEqual(page.outer.shape, (129, 129))
		self.assertEqual(page.outer[0, 128], 10.0)
		self.assertFalse(page.inner_from_file)
		# inner vertex of the last cell in row 0 averages its corners: (0 + 10 + 0 + 0) / 4
		self.assertAlmostEqual(float(page.inner[0, 127]), 2.5)

	def test_unknown_chunk_is_rejected(self):
		path = write(self.tmp, "32_32.tile", fx.tile_bytes(extra_chunks=fx.chunk(b"ZZZZ", b"")))
		with self.assertRaises(FormatError):
			tile.parse_tile(path)

	def test_wrong_height_chunk_size_is_rejected(self):
		data = fx.tile_bytes().replace(b"MCVT" + (129 * 129 * 4).to_bytes(4, "little"), b"MCVT" + (129 * 129 * 4 - 4).to_bytes(4, "little"), 1)
		path = write(self.tmp, "32_32.tile", data)
		with self.assertRaises(FormatError):
			tile.parse_tile(path)

	def test_unsupported_version_is_rejected(self):
		path = write(self.tmp, "32_32.tile", fx.tile_bytes(version=3))
		with self.assertRaises(FormatError):
			tile.parse_tile(path)


if __name__ == "__main__":
	unittest.main()
```

Note: the fixture with `version=3` writes legacy-sized arrays under a v3 version chunk. The parser must reject it on the version, before looking at any array.

- [ ] **Step 5: Run the tests to verify they fail**

Run: `python tools/tests/test_world_formats.py`
Expected: FAIL with `ImportError`/`ModuleNotFoundError: No module named 'worldkit.formats.tile'`.

- [ ] **Step 6: Write the tile parser**

`tools/world/worldkit/formats/tile.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Terrain page (.tile) parser, mirroring src/shared/terrain_io/page_io.cpp and the legacy v1
path in src/shared/terrain/page.cpp.

v2 pages store 129x129 outer heights plus 128x128 inner (cell centre) heights. v1 pages store
273x273 heights only; they are resampled with the engine's legacy index mapping, and the inner
vertex is derived by averaging the cell's four corners (what the engine does when no MCVI chunk
exists).
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np

from ..constants import INNER_PER_PAGE, LEGACY_OUTER_PER_PAGE, OUTER_PER_PAGE, PIXELS_PER_PAGE, TILES_PER_PAGE
from .chunks import Cursor, FormatError, iter_chunks

TILE_COUNT = TILES_PER_PAGE * TILES_PER_PAGE

# Chunks the engine writes. Rendering-only chunks (normals, vertex colours) are recognised and
# skipped. Anything else is an unknown format change and is rejected, never guessed around.
_IGNORED = {b"MCNM", b"MCNI", b"MCVS", b"MCSI"}
_KNOWN = {b"MVER", b"MCMT", b"MCVT", b"MCVI", b"MCLY", b"MCAR", b"MHOL", b"MCWQ"} | _IGNORED


@dataclass
class TilePage:
	"""One parsed terrain page. All grids are indexed [z, x]."""

	page_x: int
	page_z: int
	version: int
	path: Path
	outer: np.ndarray            # (129, 129) float32 outer vertex heights
	inner: np.ndarray            # (128, 128) float32 inner (cell centre) heights
	inner_from_file: bool        # False for v1 pages (inner derived from corners)
	areas: np.ndarray            # (16, 16) uint32 area/zone id per tile
	holes: np.ndarray            # (16, 16) uint64 per-tile hole bits (bit = ix + iz * 8)
	water_mask: np.ndarray       # (16, 16) uint64 per-tile water quad bits (bit = qx + qz * 8)
	water_type: np.ndarray       # (16, 16) uint8 terrain::WaterType per tile
	water_heights: np.ndarray    # (129, 129) float32 water surface per outer vertex
	materials: list[str]         # 256 per-tile material names; "" = the world's default material
	layers: np.ndarray           # (1009, 1009) uint32 splat pixels, layer 0 in the lowest byte

	def hole_cells(self) -> np.ndarray:
		"""(128, 128) bool, True where the cell is a terrain hole."""
		return expand_tile_bits(self.holes)

	def water_cells(self) -> np.ndarray:
		"""(128, 128) bool, True where the cell has a water quad."""
		return expand_tile_bits(self.water_mask)


def expand_tile_bits(masks: np.ndarray) -> np.ndarray:
	"""Expands per-tile 8x8 bit masks (bit = ix + iz * 8) to a (128, 128) per-cell bool grid [z, x]."""
	bits = np.arange(64, dtype=np.uint64)
	flags = ((masks[..., None] >> bits) & np.uint64(1)).astype(bool)   # [tz, tx, iz*8+ix]
	flags = flags.reshape(TILES_PER_PAGE, TILES_PER_PAGE, 8, 8)          # [tz, tx, iz, ix]
	return flags.transpose(0, 2, 1, 3).reshape(INNER_PER_PAGE, INNER_PER_PAGE)


def _legacy_index(new_idx: int, new_max: int, old_max: int) -> int:
	mapped = int(float(new_idx) / float(new_max) * float(old_max) + 0.5)
	return min(mapped, old_max)


def _resample_legacy(heights: np.ndarray) -> np.ndarray:
	idx = np.array([_legacy_index(i, OUTER_PER_PAGE - 1, LEGACY_OUTER_PER_PAGE - 1) for i in range(OUTER_PER_PAGE)])
	return heights[np.ix_(idx, idx)].astype(np.float32)


def _average_inner(outer: np.ndarray) -> np.ndarray:
	return ((outer[:-1, :-1] + outer[:-1, 1:] + outer[1:, :-1] + outer[1:, 1:]) * 0.25).astype(np.float32)


def _expect_size(source: str, magic: bytes, payload: memoryview, size: int) -> None:
	if len(payload) != size:
		raise FormatError(f"{source}: chunk {magic!r} has {len(payload)} bytes, expected {size}")


def parse_tile(path: Path) -> TilePage:
	"""Parses a .tile file named '<page_x>_<page_z>.tile'."""
	source = str(path)
	try:
		page_x, page_z = (int(part) for part in path.stem.split("_", 1))
	except ValueError as exc:
		raise FormatError(f"{source}: file name must be '<page_x>_<page_z>.tile'") from exc

	chunks = iter_chunks(path.read_bytes(), source)
	if not chunks or chunks[0][0] != b"MVER":
		raise FormatError(f"{source}: first chunk must be MVER")
	_expect_size(source, b"MVER", chunks[0][1], 4)
	version = Cursor(chunks[0][1], source, b"MVER").u32()
	if version not in (1, 2):
		raise FormatError(f"{source}: unsupported terrain page version {version} (known: 1, 2)")

	outer = None
	inner = None
	areas = np.zeros((TILES_PER_PAGE, TILES_PER_PAGE), np.uint32)
	holes = np.zeros((TILES_PER_PAGE, TILES_PER_PAGE), np.uint64)
	water_mask = np.zeros((TILES_PER_PAGE, TILES_PER_PAGE), np.uint64)
	water_type = np.zeros((TILES_PER_PAGE, TILES_PER_PAGE), np.uint8)
	water_heights = np.zeros((OUTER_PER_PAGE, OUTER_PER_PAGE), np.float32)
	materials = [""] * TILE_COUNT
	layers = None

	for magic, payload in chunks[1:]:
		if magic not in _KNOWN:
			raise FormatError(f"{source}: unknown chunk {magic!r} (legacy MLCW water or a new engine chunk?)")
		if magic in _IGNORED:
			continue
		cur = Cursor(payload, source, magic)
		if magic == b"MCVT":
			side = OUTER_PER_PAGE if version == 2 else LEGACY_OUTER_PER_PAGE
			_expect_size(source, magic, payload, side * side * 4)
			heights = cur.floats(side * side).reshape(side, side)
			outer = heights if version == 2 else _resample_legacy(heights)
		elif magic == b"MCVI":
			_expect_size(source, magic, payload, INNER_PER_PAGE * INNER_PER_PAGE * 4)
			inner = cur.floats(INNER_PER_PAGE * INNER_PER_PAGE).reshape(INNER_PER_PAGE, INNER_PER_PAGE)
		elif magic == b"MCAR":
			_expect_size(source, magic, payload, TILE_COUNT * 4)
			areas = np.frombuffer(payload, dtype="<u4").astype(np.uint32).reshape(TILES_PER_PAGE, TILES_PER_PAGE)
		elif magic == b"MCLY":
			_expect_size(source, magic, payload, PIXELS_PER_PAGE * PIXELS_PER_PAGE * 4)
			layers = np.frombuffer(payload, dtype="<u4").reshape(PIXELS_PER_PAGE, PIXELS_PER_PAGE)
		elif magic == b"MCMT":
			count = cur.u16()
			if count > TILE_COUNT:
				raise FormatError(f"{source}: MCMT lists {count} materials, more than {TILE_COUNT} tiles")
			for index in range(count):
				materials[index] = cur.str16()
			if not cur.done():
				raise FormatError(f"{source}: MCMT has {cur.remaining()} trailing bytes")
		elif magic == b"MHOL":
			count = cur.u16()
			_expect_size(source, magic, payload, 2 + count * 10)
			for _ in range(count):
				tile_index = cur.u16()
				mask = cur.u64()
				if tile_index >= TILE_COUNT:
					raise FormatError(f"{source}: MHOL tile index {tile_index} out of range")
				holes[tile_index // TILES_PER_PAGE, tile_index % TILES_PER_PAGE] = mask
		elif magic == b"MCWQ":
			count = cur.u16()
			for _ in range(count):
				tile_index = cur.u16()
				kind = cur.u8()
				mask = cur.u64()
				if tile_index >= TILE_COUNT:
					raise FormatError(f"{source}: MCWQ tile index {tile_index} out of range")
				water_mask[tile_index // TILES_PER_PAGE, tile_index % TILES_PER_PAGE] = mask
				water_type[tile_index // TILES_PER_PAGE, tile_index % TILES_PER_PAGE] = kind
			water_heights = cur.floats(OUTER_PER_PAGE * OUTER_PER_PAGE).reshape(OUTER_PER_PAGE, OUTER_PER_PAGE)
			if cur.remaining():
				cur.str16()  # optional water material name, unused by worldkit
			if not cur.done():
				raise FormatError(f"{source}: MCWQ has {cur.remaining()} trailing bytes")

	if outer is None:
		raise FormatError(f"{source}: missing MCVT height chunk")
	if layers is None:
		raise FormatError(f"{source}: missing MCLY layer chunk")
	inner_from_file = inner is not None
	if inner is None:
		inner = _average_inner(outer)

	return TilePage(page_x, page_z, version, path, outer, inner, inner_from_file, areas, holes,
		water_mask, water_type, water_heights, materials, layers)
```

- [ ] **Step 7: Run the tests to verify they pass**

Run: `python tools/tests/test_world_formats.py`
Expected: 6 tests PASS.

- [ ] **Step 8: Commit**

```bash
git add tools/world/worldkit tools/tests/world_fixtures.py tools/tests/test_world_formats.py
git commit -m "feat(worldkit): terrain page parser with versioned chunk validation

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Placed-entity and world-header parsers

**Files:**
- Create: `tools/world/worldkit/formats/wobj.py`, `tools/world/worldkit/formats/hwld.py`
- Test: `tools/tests/test_world_formats.py` (add classes)

**Interfaces:**
- Consumes: `Cursor`, `FormatError`, `iter_chunks` (Task 2).
- Produces `worldkit.formats.wobj`: `@dataclass(frozen=True) WorldEntity(unique_id: int, kind: str, asset: str, position: tuple[float,float,float], rotation: tuple[float,float,float,float], scale: tuple[float,float,float], name: str, category: str, material_overrides: tuple[tuple[int,str],...], path: str)` with `kind in {"mesh","wmo"}` and rotation ordered `(w, x, y, z)`; `parse_wobj(path: Path) -> WorldEntity`.
- Produces `worldkit.formats.hwld`: `@dataclass WorldHeader(version: int, has_terrain: bool, default_material: str, mesh_names: list[str])`; `parse_hwld(path: Path) -> WorldHeader`.

- [ ] **Step 1: Add the failing tests**

Append to `tools/tests/test_world_formats.py`, before the `if __name__` block:
```python
from worldkit.formats import hwld, wobj  # noqa: E402
from worldkit import paths  # noqa: E402


class WobjParserTests(unittest.TestCase):
	def setUp(self):
		self._tmp = tempfile.TemporaryDirectory()
		self.tmp = Path(self._tmp.name)

	def tearDown(self):
		self._tmp.cleanup()

	def test_mesh_v3(self):
		path = write(self.tmp, "7.wobj", fx.wobj_bytes(overrides=((1, "Models/M.hmi"),)))
		entity = wobj.parse_wobj(path)
		self.assertEqual(entity.kind, "mesh")
		self.assertEqual(entity.unique_id, 7)
		self.assertEqual(entity.asset, "Models/Test/Crate.hmsh")
		self.assertEqual(entity.position, (1.0, 2.0, 3.0))
		self.assertEqual(entity.rotation, (1.0, 0.0, 0.0, 0.0))
		self.assertEqual(entity.material_overrides, ((1, "Models/M.hmi"),))
		self.assertEqual((entity.name, entity.category), ("Crate", "Props"))

	def test_mesh_v1_has_no_name(self):
		entity = wobj.parse_wobj(write(self.tmp, "7.wobj", fx.wobj_bytes(version=1)))
		self.assertEqual((entity.name, entity.category), ("", ""))

	def test_wmo_v2_has_no_name_v3_has(self):
		self.assertEqual(wobj.parse_wobj(write(self.tmp, "a.wobj", fx.wobj_bytes(kind="wmo", version=2))).name, "")
		wmo = wobj.parse_wobj(write(self.tmp, "b.wobj", fx.wobj_bytes(kind="wmo", asset="Models/Haven_001.hwmo")))
		self.assertEqual((wmo.kind, wmo.asset, wmo.name), ("wmo", "Models/Haven_001.hwmo", "Crate"))

	def test_unknown_version_rejected(self):
		with self.assertRaises(FormatError):
			wobj.parse_wobj(write(self.tmp, "7.wobj", fx.wobj_bytes(version=4)))


class HwldParserTests(unittest.TestCase):
	def test_header(self):
		with tempfile.TemporaryDirectory() as tmp:
			path = write(Path(tmp), "W.hwld", fx.hwld_bytes(meshes=("Models/A.hmsh", "Models/B.hmsh")))
			header = hwld.parse_hwld(path)
		self.assertEqual(header.version, 3)
		self.assertTrue(header.has_terrain)
		self.assertEqual(header.default_material, "Models/Terrain/Default.hmi")
		self.assertEqual(header.mesh_names, ["Models/A.hmsh", "Models/B.hmsh"])


class ShippedWorldFilesTests(unittest.TestCase):
	"""Every shipped world file must parse. This is the drift alarm for engine format changes."""

	def test_every_tile_parses(self):
		tiles = sorted((paths.REPO / "data" / "client" / "Worlds").glob("*/*/Terrain/*.tile"))
		self.assertGreater(len(tiles), 100)
		for path in tiles:
			page = tile.parse_tile(path)
			self.assertTrue(np.isfinite(page.outer).all(), path)
			self.assertTrue(np.isfinite(page.inner).all(), path)

	def test_every_wobj_parses(self):
		files = sorted((paths.REPO / "data" / "client" / "Worlds").glob("*/*/Entities/*/*.wobj"))
		self.assertGreater(len(files), 100)
		for path in files:
			entity = wobj.parse_wobj(path)
			self.assertTrue(entity.asset, path)
			self.assertTrue((paths.REPO / "data" / "client" / entity.asset).is_file(), f"{path}: {entity.asset} missing")

	def test_every_hwld_parses(self):
		for path in sorted((paths.REPO / "data" / "client" / "Worlds").glob("*/*.hwld")):
			hwld.parse_hwld(path)
```

- [ ] **Step 2: Run to verify they fail**

Run: `python tools/tests/test_world_formats.py`
Expected: FAIL with `ImportError: cannot import name 'hwld'`.

- [ ] **Step 3: Write the parsers**

`tools/world/worldkit/formats/wobj.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Placed world entity (.wobj) parser, mirroring src/shared/game_common/world_entity_loader.cpp.

Layout: REVW (uint32 version 1..3), then exactly one of
  HSMW mesh:  u64 id, str16 mesh, 3f pos, 4f rot (w,x,y,z), 3f scale, u8 n + n*(u8 idx, str16 mat),
              version > 1: str8 name, str16 category
  OMWW world model: u64 id, str16 hwmo, 3f pos, 4f rot, 3f scale, version >= 3: str8 name, str16 category
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from .chunks import Cursor, FormatError, iter_chunks


@dataclass(frozen=True)
class WorldEntity:
	unique_id: int
	kind: str                                   # "mesh" | "wmo"
	asset: str                                  # .hmsh or .hwmo path relative to data/client
	position: tuple[float, float, float]
	rotation: tuple[float, float, float, float]  # (w, x, y, z)
	scale: tuple[float, float, float]
	name: str
	category: str
	material_overrides: tuple[tuple[int, str], ...]
	path: str


def parse_wobj(path: Path) -> WorldEntity:
	source = str(path)
	chunks = iter_chunks(path.read_bytes(), source)
	if len(chunks) != 2 or chunks[0][0] != b"REVW":
		raise FormatError(f"{source}: expected REVW followed by one entity chunk, found {[m for m, _ in chunks]}")
	version = Cursor(chunks[0][1], source, b"REVW").u32()
	if not 1 <= version <= 3:
		raise FormatError(f"{source}: unsupported world entity version {version} (known: 1..3)")

	magic, payload = chunks[1]
	if magic not in (b"HSMW", b"OMWW"):
		raise FormatError(f"{source}: unknown entity chunk {magic!r}")
	cur = Cursor(payload, source, magic)
	unique_id = cur.u64()
	asset = cur.str16()
	position = (cur.f32(), cur.f32(), cur.f32())
	rotation = (cur.f32(), cur.f32(), cur.f32(), cur.f32())
	scale = (cur.f32(), cur.f32(), cur.f32())
	overrides: list[tuple[int, str]] = []
	name = category = ""
	if magic == b"HSMW":
		for _ in range(cur.u8()):
			overrides.append((cur.u8(), cur.str16()))
		if version > 1:
			name, category = cur.str8(), cur.str16()
	elif version >= 3:
		name, category = cur.str8(), cur.str16()
	if not cur.done():
		raise FormatError(f"{source}: {cur.remaining()} trailing bytes in {magic!r}")

	return WorldEntity(unique_id, "mesh" if magic == b"HSMW" else "wmo", asset, position, rotation, scale,
		name, category, tuple(overrides), source)
```

`tools/world/worldkit/formats/hwld.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""World header (.hwld) parser, mirroring src/mmo_client/world_deserializer.cpp.

REVM: uint32 version. TERR: u8 has_terrain + str16 default terrain material. HSEM: NUL-terminated
mesh names. TNEM: legacy (v1/v2) inline entity records, superseded by .wobj files; recognised
and skipped.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from .chunks import Cursor, FormatError, iter_chunks


@dataclass
class WorldHeader:
	version: int
	has_terrain: bool = False
	default_material: str = ""
	mesh_names: list[str] = field(default_factory=list)


def parse_hwld(path: Path) -> WorldHeader:
	source = str(path)
	chunks = iter_chunks(path.read_bytes(), source)
	if not chunks or chunks[0][0] != b"REVM":
		raise FormatError(f"{source}: first chunk must be REVM")
	header = WorldHeader(version=Cursor(chunks[0][1], source, b"REVM").u32())
	if header.version < 1:
		raise FormatError(f"{source}: unsupported world version {header.version}")
	for magic, payload in chunks[1:]:
		if magic == b"TERR":
			cur = Cursor(payload, source, magic)
			header.has_terrain = cur.u8() != 0
			header.default_material = cur.str16()
		elif magic == b"HSEM":
			header.mesh_names = [name.decode("utf-8") for name in bytes(payload).split(b"\x00") if name]
		elif magic == b"TNEM":
			continue
		else:
			raise FormatError(f"{source}: unknown chunk {magic!r}")
	return header
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python tools/tests/test_world_formats.py`
Expected: all tests PASS, including `ShippedWorldFilesTests`. If `test_every_wobj_parses` reports a missing asset, that is a real broken reference in content. Record the path in the task report and change nothing in the parser. Then relax only that assertion to collect the missing assets into a list, print them, and still assert the parse succeeded.

- [ ] **Step 5: Commit**

```bash
git add tools/world/worldkit/formats tools/tests/test_world_formats.py
git commit -m "feat(worldkit): .wobj and .hwld parsers + shipped-file drift test

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 4: Exact surface sampling

**Files:**
- Create: `tools/world/worldkit/surface.py`
- Test: `tools/tests/test_world_surface.py`

**Interfaces:**
- Consumes: `CELL_SIZE`, `INNER_PER_PAGE` (Task 2).
- Produces: `sample(outer: np.ndarray, inner: np.ndarray, lx: float, lz: float) -> tuple[float, float]` (height in m, slope in degrees; `lx, lz` page-local metres, clamped to the page); `max_cell_slopes(outer, inner) -> np.ndarray[(128,128), float32]`.

The rendered terrain cell is a fan of four triangles around its *stored* inner vertex. The engine writes these triangles in `terrain_raycast.h`, and memory note *terrain-surface-reconstruction* explains why the corners must never be averaged. The four triangles are bottom `(0,0)(1,0)C`, right `(1,0)(1,1)C`, top `(1,1)(0,1)C` and left `(0,1)(0,0)C`, with `C = (0.5, 0.5)` in cell-local `(u, v)`. A point belongs to the triangle of the cell edge it is closest to.

- [ ] **Step 1: Write the failing tests**

`tools/tests/test_world_surface.py`:
```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for worldkit.surface: height/slope of the rendered terrain surface.

The key case is test_raised_inner_vertex_is_not_averaged: a sculpted inner vertex must show up in
the sampled height. Averaging the corners would put the spawn on a surface the renderer never draws.

	python tools/tests/test_world_surface.py
"""

import math
import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures  # noqa: F401,E402  (puts tools/world on sys.path)

from worldkit.constants import CELL_SIZE  # noqa: E402
from worldkit.surface import max_cell_slopes, sample  # noqa: E402


def tilted_page():
	"""Height equals world x (a 45 degree ramp rising along +x)."""
	xs = np.arange(129, dtype=np.float32) * CELL_SIZE
	outer = np.tile(xs, (129, 1))
	inner = np.tile((np.arange(128, dtype=np.float32) + 0.5) * CELL_SIZE, (128, 1))
	return outer, inner


class SurfaceTests(unittest.TestCase):
	def test_flat(self):
		outer = np.full((129, 129), 2.0, np.float32)
		inner = np.full((128, 128), 2.0, np.float32)
		height, slope = sample(outer, inner, 100.0, 200.0)
		self.assertAlmostEqual(height, 2.0, places=5)
		self.assertAlmostEqual(slope, 0.0, places=5)

	def test_tilted_plane(self):
		outer, inner = tilted_page()
		for lx in (0.0, 13.7, 250.1, 533.0):
			height, slope = sample(outer, inner, lx, 77.7)
			self.assertAlmostEqual(height, lx, places=2)
			self.assertAlmostEqual(slope, 45.0, places=2)

	def test_raised_inner_vertex_is_not_averaged(self):
		outer = np.zeros((129, 129), np.float32)
		inner = np.zeros((128, 128), np.float32)
		inner[0, 0] = 4.0
		centre, _ = sample(outer, inner, 0.5 * CELL_SIZE, 0.5 * CELL_SIZE)
		self.assertAlmostEqual(centre, 4.0, places=5)
		# (u=0.25, v=0.5) lies in the left triangle, where the plane rises 8 m per cell width in u
		left, slope = sample(outer, inner, 0.25 * CELL_SIZE, 0.5 * CELL_SIZE)
		self.assertAlmostEqual(left, 2.0, places=5)
		self.assertAlmostEqual(slope, math.degrees(math.atan(8.0 / CELL_SIZE)), places=4)

	def test_max_cell_slopes(self):
		outer, inner = tilted_page()
		slopes = max_cell_slopes(outer, inner)
		self.assertEqual(slopes.shape, (128, 128))
		self.assertTrue(np.allclose(slopes, 45.0, atol=0.01))

	def test_clamps_to_page(self):
		outer, inner = tilted_page()
		height, _ = sample(outer, inner, 9999.0, -5.0)
		self.assertAlmostEqual(height, 128 * CELL_SIZE, places=2)


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 2: Run to verify they fail**

Run: `python tools/tests/test_world_surface.py`
Expected: FAIL with `ModuleNotFoundError: No module named 'worldkit.surface'`.

- [ ] **Step 3: Implement**

`tools/world/worldkit/surface.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Height and slope of the rendered terrain surface.

A cell is four triangles fanned around its stored inner vertex (src/shared/terrain/terrain_raycast.h).
Reading the inner vertex instead of averaging the corners is mandatory: inner vertices are sculpted
independently, and an averaged surface is one the renderer never draws.
"""

from __future__ import annotations

import math

import numpy as np

from .constants import CELL_SIZE, INNER_PER_PAGE

_CENTRE = (0.5, 0.5)
# Triangle name -> its two cell-corner vertices in (u, v); the third vertex is the centre.
_TRIANGLES = {
	"bottom": ((0, 0), (1, 0)),
	"right": ((1, 0), (1, 1)),
	"top": ((1, 1), (0, 1)),
	"left": ((0, 1), (0, 0)),
}
_ORDER = ("bottom", "right", "top", "left")
# Plane h = a + b*u + c*v through the triangle's vertices: (a, b, c) = INV @ (h0, h1, h_centre).
_INV = {
	name: np.linalg.inv(np.array([[1.0, p0[0], p0[1]], [1.0, p1[0], p1[1]], [1.0, _CENTRE[0], _CENTRE[1]]]))
	for name, (p0, p1) in _TRIANGLES.items()
}


def _triangle_of(u: float, v: float) -> str:
	distances = (v, 1.0 - u, 1.0 - v, u)
	return _ORDER[distances.index(min(distances))]


def _slope_degrees(b, c):
	return np.degrees(np.arctan(np.hypot(b, c) / CELL_SIZE))


def sample(outer: np.ndarray, inner: np.ndarray, lx: float, lz: float) -> tuple[float, float]:
	"""Returns (height, slope in degrees) at page-local metres (lx, lz), clamped to the page."""
	gx = min(max(lx / CELL_SIZE, 0.0), float(INNER_PER_PAGE))
	gz = min(max(lz / CELL_SIZE, 0.0), float(INNER_PER_PAGE))
	cx = min(int(math.floor(gx)), INNER_PER_PAGE - 1)
	cz = min(int(math.floor(gz)), INNER_PER_PAGE - 1)
	u = gx - cx
	v = gz - cz

	name = _triangle_of(u, v)
	p0, p1 = _TRIANGLES[name]
	heights = np.array([
		outer[cz + p0[1], cx + p0[0]],
		outer[cz + p1[1], cx + p1[0]],
		inner[cz, cx],
	], dtype=np.float64)
	a, b, c = _INV[name] @ heights
	return float(a + b * u + c * v), float(_slope_degrees(b, c))


def max_cell_slopes(outer: np.ndarray, inner: np.ndarray) -> np.ndarray:
	"""(128, 128) float32: steepest of the four fan triangles of every cell, in degrees."""
	corners = {
		(0, 0): outer[:-1, :-1].astype(np.float64),
		(1, 0): outer[:-1, 1:].astype(np.float64),
		(0, 1): outer[1:, :-1].astype(np.float64),
		(1, 1): outer[1:, 1:].astype(np.float64),
	}
	centre = inner.astype(np.float64)
	best = np.zeros(inner.shape, np.float64)
	for name, (p0, p1) in _TRIANGLES.items():
		inv = _INV[name]
		h0 = corners[p0]
		h1 = corners[p1]
		b = inv[1, 0] * h0 + inv[1, 1] * h1 + inv[1, 2] * centre
		c = inv[2, 0] * h0 + inv[2, 1] * h1 + inv[2, 2] * centre
		best = np.maximum(best, _slope_degrees(b, c))
	return best.astype(np.float32)
```

- [ ] **Step 4: Run to verify they pass**

Run: `python tools/tests/test_world_surface.py`
Expected: 5 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/world/worldkit/surface.py tools/tests/test_world_surface.py
git commit -m "feat(worldkit): exact fan-surface height and slope sampling

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Entities and the cached WorldSnapshot

**Files:**
- Create: `tools/world/worldkit/entities.py`, `tools/world/worldkit/snapshot.py`
- Test: `tools/tests/test_world_snapshot.py`
- Modify: `tools/tests/world_fixtures.py` (add `make_world`)

**Interfaces:**
- Consumes: `parse_tile`, `parse_wobj`, `WorldEntity`, `parse_hwld`, `WorldHeader`, `max_cell_slopes`, `paths.*`, constants.
- Produces `worldkit.entities`: `load_entities(directory: str, repo: Path = REPO) -> list[WorldEntity]`, `footprint_radius(e: WorldEntity) -> float`, `structure_radius(e: WorldEntity) -> float`.
- Produces `worldkit.snapshot`: `class NoTerrainError(RuntimeError)`, `@dataclass WorldSnapshot` with fields `directory, page_min_x, page_min_z, pages_x, pages_z, page_slot (pages_z,pages_x) int32, page_versions (n,) uint8, outer (n,129,129), inner (n,128,128), height, slope, area, water_depth, hole, layer, material` (cell grids `(pages_z*128, pages_x*128)` indexed `[z,x]`), `materials: list[str]`, `default_material: str`, `entities: list[WorldEntity]`; methods `origin -> (x, z)`, `extent -> (x0, z0, x1, z1)`, `cell_index(x, z) -> tuple[int,int] | None` (returns `(cz, cx)`), `page_local(x, z) -> tuple[int, float, float] | None` (returns `(slot, lx, lz)`), `material_name(index: int) -> str`; functions `build_snapshot(directory, repo=REPO) -> WorldSnapshot`, `load_snapshot(directory, repo=REPO, use_cache=True) -> WorldSnapshot`, `dominant_layers(layers) -> np.ndarray[(128,128), uint8]`.
- Produces `world_fixtures.make_world(repo: Path, directory: str, pages: dict[tuple[int,int], dict], entities: list[dict] = (), default_material: str = "Models/Terrain/Default.hmi") -> None`. It writes a synthetic world under `<repo>/data/client/Worlds/...`; each `pages` value is `tile_bytes` kwargs and each `entities` item is `wobj_bytes` kwargs.

Grid semantics: `height` is the inner (cell-centre) height, NaN where there is no page. `slope` is the steepest fan triangle. `water_depth` is the mean water surface of the cell's quad minus the inner height, 0 where there is no water quad. `layer` is the dominant splat layer (0..3) at the cell-centre pixel. `material` is an index into `materials`, -1 meaning the world default.

- [ ] **Step 1: Add the world builder to the fixtures**

Append to `tools/tests/world_fixtures.py`:
```python
def make_world(repo: Path, directory: str, pages: dict, entities=(), default_material="Models/Terrain/Default.hmi") -> None:
	"""Writes a synthetic world under <repo>/data/client/Worlds/<directory>/.

	pages: {(page_x, page_z): tile_bytes kwargs}; entities: iterable of wobj_bytes kwargs.
	"""
	world = repo / "data" / "client" / "Worlds" / directory
	terrain = world / directory / "Terrain"
	terrain.mkdir(parents=True, exist_ok=True)
	(world / f"{directory}.hwld").write_bytes(hwld_bytes(default_material=default_material))
	for (page_x, page_z), kwargs in pages.items():
		(terrain / f"{page_x}_{page_z}.tile").write_bytes(tile_bytes(**kwargs))
	for index, kwargs in enumerate(entities):
		x, _, z = kwargs.get("position", (1.0, 2.0, 3.0))
		page_x = int(np.floor(x / (33.33333206176758 * 16))) + 32
		page_z = int(np.floor(z / (33.33333206176758 * 16))) + 32
		folder = world / directory / "Entities" / str((page_x << 8) | page_z)
		folder.mkdir(parents=True, exist_ok=True)
		(folder / f"{kwargs.get('unique_id', index + 1)}.wobj").write_bytes(wobj_bytes(**kwargs))
```

- [ ] **Step 2: Write the failing tests**

`tools/tests/test_world_snapshot.py`:
```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for worldkit.snapshot and worldkit.entities.

Synthetic worlds are written to a temp "repo" so the tests never touch data/client or generated/.
One test builds the real Development world without the cache as an end-to-end check.

	python tools/tests/test_world_snapshot.py
"""

import os
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit import paths  # noqa: E402
from worldkit.constants import CELL_SIZE, PAGE_SIZE  # noqa: E402
from worldkit.entities import footprint_radius, structure_radius  # noqa: E402
from worldkit.formats.wobj import WorldEntity  # noqa: E402
from worldkit.snapshot import NoTerrainError, build_snapshot, load_snapshot  # noqa: E402


def two_page_world(repo: Path):
	areas = np.zeros((16, 16), np.uint32)
	areas[:, :] = 8
	layers = np.full((1009, 1009), 0x000000FF, np.uint32)
	layers[:, :] = 0x0000FF00  # layer 1 dominant everywhere on page 33_32
	inner = np.zeros((128, 128), np.float32)
	water_heights = np.full((129, 129), 3.0, np.float32)
	fx.make_world(repo, "W", {
		(32, 32): dict(areas=areas, holes={0: 1}, water={1: (1, 1)}, water_heights=water_heights, inner=inner),
		(33, 32): dict(layers=layers, materials=["Models/Terrain/B.hmi"] * 256),
	}, entities=[dict(position=(10.0, 0.0, 10.0), unique_id=5)])


class SnapshotTests(unittest.TestCase):
	def setUp(self):
		self._tmp = tempfile.TemporaryDirectory()
		self.repo = Path(self._tmp.name)
		two_page_world(self.repo)

	def tearDown(self):
		self._tmp.cleanup()

	def test_grid_layout(self):
		snap = build_snapshot("W", repo=self.repo)
		self.assertEqual((snap.page_min_x, snap.page_min_z, snap.pages_x, snap.pages_z), (32, 32, 2, 1))
		self.assertEqual(snap.height.shape, (128, 256))
		self.assertEqual(snap.origin, (0.0, 0.0))
		self.assertAlmostEqual(snap.extent[2], 2 * PAGE_SIZE, places=3)
		self.assertEqual(int(snap.area[0, 0]), 8)
		self.assertEqual(int(snap.area[0, 200]), 0)
		self.assertTrue(snap.hole[0, 0])
		self.assertFalse(snap.hole[0, 1])
		self.assertAlmostEqual(float(snap.water_depth[0, 8]), 3.0)   # tile 1 -> cell cx 8
		self.assertEqual(float(snap.water_depth[0, 9]), 0.0)
		self.assertEqual(int(snap.layer[5, 5]), 0)
		self.assertEqual(int(snap.layer[5, 200]), 1)
		self.assertEqual(snap.material_name(int(snap.material[0, 0])), "Models/Terrain/Default.hmi")
		self.assertEqual(snap.material_name(int(snap.material[0, 200])), "Models/Terrain/B.hmi")
		self.assertEqual(len(snap.entities), 1)

	def test_lookups(self):
		snap = build_snapshot("W", repo=self.repo)
		self.assertEqual(snap.cell_index(CELL_SIZE * 3.5, CELL_SIZE * 2.5), (2, 3))
		self.assertIsNone(snap.cell_index(-1.0, 0.0))
		slot, lx, lz = snap.page_local(PAGE_SIZE + 5.0, 7.0)
		self.assertEqual(snap.page_slot[0, 1], slot)
		self.assertAlmostEqual(lx, 5.0, places=3)
		self.assertAlmostEqual(lz, 7.0, places=3)
		self.assertIsNone(snap.page_local(0.0, PAGE_SIZE + 1.0))

	def test_cache_roundtrip_and_invalidation(self):
		first = load_snapshot("W", repo=self.repo)
		meta = paths.cache_dir("W", self.repo) / "snapshot.json"
		self.assertTrue(meta.is_file())
		second = load_snapshot("W", repo=self.repo)
		self.assertTrue(np.array_equal(first.slope, second.slope))
		self.assertEqual(second.entities, first.entities)
		stamp = meta.stat().st_mtime_ns
		tile = paths.terrain_dir("W", self.repo) / "32_32.tile"
		os.utime(tile, ns=(tile.stat().st_atime_ns, tile.stat().st_mtime_ns + 10_000_000_000))
		load_snapshot("W", repo=self.repo)
		self.assertNotEqual(meta.stat().st_mtime_ns, stamp)

	def test_world_without_terrain(self):
		(self.repo / "data" / "client" / "Worlds" / "Empty" / "Empty" / "Terrain").mkdir(parents=True)
		with self.assertRaises(NoTerrainError):
			build_snapshot("Empty", repo=self.repo)


class FootprintTests(unittest.TestCase):
	def entity(self, asset, kind="mesh", scale=(1.0, 1.0, 1.0)):
		return WorldEntity(1, kind, asset, (0.0, 0.0, 0.0), (1.0, 0.0, 0.0, 0.0), scale, "", "", (), "x")

	def test_heuristics(self):
		self.assertEqual(footprint_radius(self.entity("Models/X/Haven_001.hwmo", kind="wmo")), 0.0)
		self.assertEqual(structure_radius(self.entity("Models/X/Haven_001.hwmo", kind="wmo")), 25.0)
		self.assertEqual(footprint_radius(self.entity("Models/FalwynPlains/Props/Camp/SM_hc_Camptent_B.hmsh")), 3.0)
		self.assertEqual(footprint_radius(self.entity("Models/FalwynPlains/Buildings/Fortress_Wall_01.hmsh", scale=(2.0, 1.0, 1.0))), 12.0)
		self.assertEqual(structure_radius(self.entity("Models/FalwynPlains/Buildings/Floor_01.hmsh")), 8.0)
		self.assertEqual(structure_radius(self.entity("Models/Trees/Tree_02.hmsh")), 0.0)


class RealWorldTests(unittest.TestCase):
	def test_development_world_builds(self):
		snap = build_snapshot("Development")
		self.assertGreaterEqual(len(snap.entities), 100)
		areas = set(np.unique(snap.area).tolist())
		self.assertTrue({1, 2, 8}.issubset(areas), areas)
		valid = ~np.isnan(snap.height)
		self.assertGreater(int(valid.sum()), 60 * 128 * 128)


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 3: Run to verify they fail**

Run: `python tools/tests/test_world_snapshot.py`
Expected: FAIL with `ModuleNotFoundError: No module named 'worldkit.entities'`.

- [ ] **Step 4: Implement `entities.py`**

`tools/world/worldkit/entities.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Placed world entities and their placement footprints.

Footprints are path heuristics (mesh bounds are not stored cheaply in .hmsh). World models (.hwmo)
have walkable interiors, so they never block a spawn. They do count as a *structure*: spawns
standing on a floor or inside a building legitimately sit above or below the terrain, so the
height rule is skipped there.
"""

from __future__ import annotations

from pathlib import Path

from .formats.wobj import WorldEntity, parse_wobj
from .paths import REPO, entities_dir

# (substring of the lower-cased asset path, footprint radius in metres at scale 1). First match wins.
_FOOTPRINTS = (
	("/buildings/", 6.0),
	("fortress_", 6.0),
	("tent", 3.0),
	("wagon", 2.5),
	("haystack", 1.5),
	("/trees/", 1.0),
	("fence", 0.5),
)
_DEFAULT_FOOTPRINT = 1.0
_WMO_STRUCTURE = 25.0
_BUILDING_STRUCTURE = 8.0


def load_entities(directory: str, repo: Path = REPO) -> list[WorldEntity]:
	root = entities_dir(directory, repo)
	if not root.is_dir():
		return []
	return [parse_wobj(path) for path in sorted(root.glob("*/*.wobj"))]


def _asset(entity: WorldEntity) -> str:
	return entity.asset.lower().replace("\\", "/")


def _planar_scale(entity: WorldEntity) -> float:
	return max(abs(entity.scale[0]), abs(entity.scale[2]))


def footprint_radius(entity: WorldEntity) -> float:
	"""Radius (m) around the entity origin in which a spawn would stand inside the mesh."""
	if entity.kind == "wmo":
		return 0.0
	asset = _asset(entity)
	for needle, radius in _FOOTPRINTS:
		if needle in asset:
			return radius * _planar_scale(entity)
	return _DEFAULT_FOOTPRINT * _planar_scale(entity)


def structure_radius(entity: WorldEntity) -> float:
	"""Radius (m) in which a spawn may legitimately stand on the structure instead of the terrain."""
	if entity.kind == "wmo":
		return _WMO_STRUCTURE * _planar_scale(entity)
	if "/buildings/" in _asset(entity):
		return _BUILDING_STRUCTURE * _planar_scale(entity)
	return 0.0
```

- [ ] **Step 5: Implement `snapshot.py`**

`tools/world/worldkit/snapshot.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Per-map world model: terrain grids at cell resolution plus placed entities, cached as npz.

The cache lives in generated/world/<Directory>/ (gitignored). It is keyed by a fingerprint of
every source file's path, size and mtime, so editing a page or moving a prop rebuilds it.
"""

from __future__ import annotations

import dataclasses
import hashlib
import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from .constants import CELL_SIZE, CELLS_PER_TILE, INNER_PER_PAGE, PAGE_SIZE, PIXELS_PER_PAGE, page_origin
from .entities import load_entities
from .formats.hwld import WorldHeader, parse_hwld
from .formats.tile import parse_tile
from .formats.wobj import WorldEntity
from .paths import REPO, cache_dir, entities_dir, hwld_path, terrain_dir
from .surface import max_cell_slopes

SNAPSHOT_VERSION = 1
_ARRAYS = ("page_slot", "page_versions", "outer", "inner", "height", "slope", "area", "water_depth", "hole", "layer", "material")
_CELL_PIXELS = np.rint((np.arange(INNER_PER_PAGE) + 0.5) * (PIXELS_PER_PAGE - 1) / INNER_PER_PAGE).astype(np.int64)


class NoTerrainError(RuntimeError):
	"""The world has no terrain pages (e.g. a pure world-model dungeon or an empty world)."""


@dataclass
class WorldSnapshot:
	directory: str
	page_min_x: int
	page_min_z: int
	pages_x: int
	pages_z: int
	page_slot: np.ndarray
	page_versions: np.ndarray
	outer: np.ndarray
	inner: np.ndarray
	height: np.ndarray
	slope: np.ndarray
	area: np.ndarray
	water_depth: np.ndarray
	hole: np.ndarray
	layer: np.ndarray
	material: np.ndarray
	materials: list[str]
	default_material: str
	entities: list[WorldEntity]

	@property
	def origin(self) -> tuple[float, float]:
		return page_origin(self.page_min_x, self.page_min_z)

	@property
	def extent(self) -> tuple[float, float, float, float]:
		x0, z0 = self.origin
		return x0, z0, x0 + self.pages_x * PAGE_SIZE, z0 + self.pages_z * PAGE_SIZE

	def cell_index(self, x: float, z: float) -> tuple[int, int] | None:
		x0, z0 = self.origin
		cx = int(np.floor((x - x0) / CELL_SIZE))
		cz = int(np.floor((z - z0) / CELL_SIZE))
		if 0 <= cx < self.height.shape[1] and 0 <= cz < self.height.shape[0]:
			return cz, cx
		return None

	def page_local(self, x: float, z: float) -> tuple[int, float, float] | None:
		x0, z0 = self.origin
		px = int(np.floor((x - x0) / PAGE_SIZE))
		pz = int(np.floor((z - z0) / PAGE_SIZE))
		if not (0 <= px < self.pages_x and 0 <= pz < self.pages_z):
			return None
		slot = int(self.page_slot[pz, px])
		if slot < 0:
			return None
		return slot, (x - x0) - px * PAGE_SIZE, (z - z0) - pz * PAGE_SIZE

	def material_name(self, index: int) -> str:
		return self.materials[index] if index >= 0 else self.default_material


def dominant_layers(layers: np.ndarray) -> np.ndarray:
	"""(128, 128) uint8: index (0..3) of the heaviest splat layer at each cell's centre pixel."""
	pixels = layers[np.ix_(_CELL_PIXELS, _CELL_PIXELS)]
	weights = np.stack([(pixels >> np.uint32(8 * i)) & np.uint32(0xFF) for i in range(4)], axis=-1)
	return weights.argmax(axis=-1).astype(np.uint8)


def _expand_tiles(values: np.ndarray) -> np.ndarray:
	return np.repeat(np.repeat(values, CELLS_PER_TILE, axis=0), CELLS_PER_TILE, axis=1)


def _page_files(directory: str, repo: Path) -> list[tuple[Path, int, int]]:
	files = []
	for path in sorted(terrain_dir(directory, repo).glob("*.tile")):
		page_x, page_z = (int(part) for part in path.stem.split("_", 1))
		files.append((path, page_x, page_z))
	return files


def build_snapshot(directory: str, repo: Path = REPO) -> WorldSnapshot:
	files = _page_files(directory, repo)
	if not files:
		raise NoTerrainError(f"world '{directory}' has no terrain pages in {terrain_dir(directory, repo)}")

	min_x = min(f[1] for f in files)
	min_z = min(f[2] for f in files)
	pages_x = max(f[1] for f in files) - min_x + 1
	pages_z = max(f[2] for f in files) - min_z + 1
	rows, cols = pages_z * INNER_PER_PAGE, pages_x * INNER_PER_PAGE

	page_slot = np.full((pages_z, pages_x), -1, np.int32)
	page_versions = np.zeros(len(files), np.uint8)
	outer = np.zeros((len(files), INNER_PER_PAGE + 1, INNER_PER_PAGE + 1), np.float32)
	inner = np.zeros((len(files), INNER_PER_PAGE, INNER_PER_PAGE), np.float32)
	height = np.full((rows, cols), np.nan, np.float32)
	slope = np.full((rows, cols), np.nan, np.float32)
	area = np.zeros((rows, cols), np.uint32)
	water_depth = np.zeros((rows, cols), np.float32)
	hole = np.zeros((rows, cols), bool)
	layer = np.zeros((rows, cols), np.uint8)
	material = np.full((rows, cols), -1, np.int16)
	materials: list[str] = []
	material_index: dict[str, int] = {}

	hwld = hwld_path(directory, repo)
	header = parse_hwld(hwld) if hwld.is_file() else WorldHeader(version=0)

	for slot, (path, page_x, page_z) in enumerate(files):
		page = parse_tile(path)
		page_slot[page_z - min_z, page_x - min_x] = slot
		page_versions[slot] = page.version
		outer[slot] = page.outer
		inner[slot] = page.inner

		r0 = (page_z - min_z) * INNER_PER_PAGE
		c0 = (page_x - min_x) * INNER_PER_PAGE
		cells = (slice(r0, r0 + INNER_PER_PAGE), slice(c0, c0 + INNER_PER_PAGE))
		height[cells] = page.inner
		slope[cells] = max_cell_slopes(page.outer, page.inner)
		area[cells] = _expand_tiles(page.areas)
		hole[cells] = page.hole_cells()
		wh = page.water_heights
		surface = (wh[:-1, :-1] + wh[:-1, 1:] + wh[1:, :-1] + wh[1:, 1:]) * 0.25
		water_depth[cells] = np.where(page.water_cells(), np.maximum(surface - page.inner, 0.0), 0.0)
		layer[cells] = dominant_layers(page.layers)

		tile_material = np.full((16, 16), -1, np.int16)
		for tile_index, name in enumerate(page.materials):
			if name:
				if name not in material_index:
					material_index[name] = len(materials)
					materials.append(name)
				tile_material[tile_index // 16, tile_index % 16] = material_index[name]
		material[cells] = _expand_tiles(tile_material)

	return WorldSnapshot(directory, min_x, min_z, pages_x, pages_z, page_slot, page_versions, outer, inner,
		height, slope, area, water_depth, hole, layer, material, materials, header.default_material,
		load_entities(directory, repo))


def _fingerprint(directory: str, repo: Path) -> str:
	digest = hashlib.sha1(f"worldkit-snapshot-v{SNAPSHOT_VERSION}".encode())
	sources = sorted(terrain_dir(directory, repo).glob("*.tile")) + sorted(entities_dir(directory, repo).glob("*/*.wobj"))
	hwld = hwld_path(directory, repo)
	if hwld.is_file():
		sources.append(hwld)
	for path in sources:
		stat = path.stat()
		digest.update(f"{path.relative_to(repo).as_posix()}|{stat.st_size}|{stat.st_mtime_ns}\n".encode())
	return digest.hexdigest()


def _save(snapshot: WorldSnapshot, fingerprint: str, repo: Path) -> None:
	folder = cache_dir(snapshot.directory, repo)
	folder.mkdir(parents=True, exist_ok=True)
	np.savez_compressed(folder / "snapshot.npz", **{name: getattr(snapshot, name) for name in _ARRAYS})
	meta = {
		"version": SNAPSHOT_VERSION,
		"fingerprint": fingerprint,
		"directory": snapshot.directory,
		"page_min_x": snapshot.page_min_x,
		"page_min_z": snapshot.page_min_z,
		"pages_x": snapshot.pages_x,
		"pages_z": snapshot.pages_z,
		"materials": snapshot.materials,
		"default_material": snapshot.default_material,
		"entities": [dataclasses.asdict(e) for e in snapshot.entities],
	}
	(folder / "snapshot.json").write_text(json.dumps(meta), encoding="utf-8")


def _entity_from_dict(data: dict) -> WorldEntity:
	return WorldEntity(
		data["unique_id"], data["kind"], data["asset"], tuple(data["position"]), tuple(data["rotation"]),
		tuple(data["scale"]), data["name"], data["category"],
		tuple((int(i), str(m)) for i, m in data["material_overrides"]), data["path"])


def _load_cached(directory: str, repo: Path, fingerprint: str) -> WorldSnapshot | None:
	folder = cache_dir(directory, repo)
	meta_path = folder / "snapshot.json"
	npz_path = folder / "snapshot.npz"
	if not meta_path.is_file() or not npz_path.is_file():
		return None
	meta = json.loads(meta_path.read_text(encoding="utf-8"))
	if meta.get("version") != SNAPSHOT_VERSION or meta.get("fingerprint") != fingerprint:
		return None
	with np.load(npz_path) as arrays:
		loaded = {name: arrays[name] for name in _ARRAYS}
	return WorldSnapshot(directory, meta["page_min_x"], meta["page_min_z"], meta["pages_x"], meta["pages_z"],
		materials=meta["materials"], default_material=meta["default_material"],
		entities=[_entity_from_dict(e) for e in meta["entities"]], **loaded)


def load_snapshot(directory: str, repo: Path = REPO, use_cache: bool = True) -> WorldSnapshot:
	"""Returns the snapshot, rebuilding and re-caching it when any source file changed."""
	fingerprint = _fingerprint(directory, repo)
	if use_cache:
		cached = _load_cached(directory, repo, fingerprint)
		if cached is not None:
			return cached
	snapshot = build_snapshot(directory, repo)
	if use_cache:
		_save(snapshot, fingerprint, repo)
	return snapshot
```

- [ ] **Step 6: Run to verify the tests pass**

Run: `python tools/tests/test_world_snapshot.py`
Expected: all tests PASS. `test_development_world_builds` takes a few seconds; it parses about 270 MB of layer data.

- [ ] **Step 7: Commit**

```bash
git add tools/world/worldkit/entities.py tools/world/worldkit/snapshot.py tools/tests/world_fixtures.py tools/tests/test_world_snapshot.py
git commit -m "feat(worldkit): cached per-map world snapshot with entity footprints

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Terrain kinds (which splat layer is a road)

**Files:**
- Create: `tools/world/worldkit/terrain_kinds.py`, `tools/world/worldkit/terrain_kinds.json`, `tools/world/worldkit/__main__.py`
- Test: `tools/tests/test_world_snapshot.py` (add class)

**Interfaces:**
- Consumes: `WorldSnapshot`, `load_snapshot`, `parse_tile`, `paths.terrain_dir`.
- Produces: `KINDS: tuple[str, ...]`, `ROAD_KINDS = frozenset({"path", "road"})`, `@dataclass TerrainKinds(table: dict[str, list[str]])` with `kind(material: str, layer: int) -> str` and `road_mask(snapshot) -> np.ndarray[bool]`, `load_kinds(path: Path = DEFAULT_KINDS_PATH) -> TerrainKinds`.
- Produces CLI: `python -m worldkit layers --world <Directory> --page <x>_<z> --out <png>` (run from `tools/world`, or with `tools/world` on `PYTHONPATH`).

- [ ] **Step 1: Write the failing test**

Append to `tools/tests/test_world_snapshot.py` (before `if __name__`):
```python
from worldkit.terrain_kinds import TerrainKinds, load_kinds  # noqa: E402


class TerrainKindTests(unittest.TestCase):
	def test_lookup_and_road_mask(self):
		with tempfile.TemporaryDirectory() as tmp:
			repo = Path(tmp)
			two_page_world(repo)
			snap = build_snapshot("W", repo=repo)
		kinds = TerrainKinds({"Models/Terrain/B.hmi": ["grass", "road", "rock", "dirt"]})
		self.assertEqual(kinds.kind("Models/Terrain/B.hmi", 1), "road")
		self.assertEqual(kinds.kind("Models/Terrain/Unknown.hmi", 1), "unknown")
		mask = kinds.road_mask(snap)
		self.assertTrue(mask[5, 200])   # page 33_32: material B, layer 1 dominant
		self.assertFalse(mask[5, 5])    # page 32_32: default material, not in the table

	def test_shipped_table_is_valid(self):
		kinds = load_kinds()
		for material, layers in kinds.table.items():
			self.assertEqual(len(layers), 4, material)
```

- [ ] **Step 2: Run to verify it fails**

Run: `python tools/tests/test_world_snapshot.py TerrainKindTests`
Expected: FAIL with `ModuleNotFoundError: No module named 'worldkit.terrain_kinds'`.

- [ ] **Step 3: Implement the lookup and an initial table**

`tools/world/worldkit/terrain_kinds.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Maps (terrain material, splat layer index) to a terrain kind such as road or grass.

The table is hand-authored in terrain_kinds.json after looking at layer previews
(`python -m worldkit layers`), because which layer a material uses for paths is an art decision
that no file states.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np

KINDS = ("grass", "dirt", "path", "road", "rock", "sand", "mud", "forest_floor", "snow", "unknown")
ROAD_KINDS = frozenset({"path", "road"})
DEFAULT_KINDS_PATH = Path(__file__).with_name("terrain_kinds.json")


@dataclass
class TerrainKinds:
	table: dict[str, list[str]]

	def kind(self, material: str, layer: int) -> str:
		layers = self.table.get(material)
		return layers[layer] if layers else "unknown"

	def road_mask(self, snapshot) -> np.ndarray:
		"""(Z, X) bool: cells whose dominant layer is a path or road."""
		mask = np.zeros(snapshot.layer.shape, bool)
		for index in [-1, *range(len(snapshot.materials))]:
			layers = self.table.get(snapshot.material_name(index))
			if not layers:
				continue
			in_material = snapshot.material == index
			for layer_index, kind in enumerate(layers):
				if kind in ROAD_KINDS:
					mask |= in_material & (snapshot.layer == layer_index)
		return mask


def load_kinds(path: Path = DEFAULT_KINDS_PATH) -> TerrainKinds:
	data = json.loads(path.read_text(encoding="utf-8"))
	table = data.get("materials", {})
	for material, layers in table.items():
		if len(layers) != 4 or any(kind not in KINDS for kind in layers):
			raise ValueError(f"{path}: '{material}' must list 4 kinds from {KINDS}, got {layers}")
	return TerrainKinds(table)
```

`tools/world/worldkit/terrain_kinds.json` (all `unknown` until Step 6):
```json
{
  "_comment": "Per terrain material: kind of splat layers 0..3 (layer 0 = lowest byte of MCLY). Filled by looking at `python -m worldkit layers` previews. Kinds: grass dirt path road rock sand mud forest_floor snow unknown.",
  "materials": {
    "Models/Terrain/Oakenshire_Boars_Enhanced.hmi": ["unknown", "unknown", "unknown", "unknown"]
  }
}
```

- [ ] **Step 4: Add the `layers` preview CLI**

`tools/world/worldkit/__main__.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""worldkit command line.

	python -m worldkit layers --world Development --page 32_32 --out generated/world/layers_32_32.png

(Run from tools/world, or put tools/world on PYTHONPATH.)
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from .formats.tile import parse_tile
from .paths import REPO, terrain_dir


def cmd_layers(args) -> int:
	page = parse_tile(terrain_dir(args.world, REPO) / f"{args.page}.tile")
	panels = []
	for index in range(4):
		weights = ((page.layers >> np.uint32(8 * index)) & np.uint32(0xFF)).astype(np.uint8)
		# Rows are +z; flip so +z points up like the map renderer.
		panel = Image.fromarray(weights[::-1, :]).convert("RGB").resize((504, 504))
		ImageDraw.Draw(panel).text((8, 8), f"layer {index}", fill=(255, 0, 0))
		panels.append(panel)
	sheet = Image.new("RGB", (1008, 1008))
	for index, panel in enumerate(panels):
		sheet.paste(panel, ((index % 2) * 504, (index // 2) * 504))
	out = Path(args.out)
	out.parent.mkdir(parents=True, exist_ok=True)
	sheet.save(out)
	names = sorted({name or "<world default>" for name in page.materials})
	print(f"wrote {out}; tile materials on this page: {', '.join(names)}")
	return 0


def cmd_query(args) -> int:
	from .cli_query import run_query
	return run_query(args)


def main(argv=None) -> int:
	parser = argparse.ArgumentParser(prog="worldkit")
	sub = parser.add_subparsers(dest="command", required=True)

	layers = sub.add_parser("layers", help="render the 4 splat layer weights of one terrain page")
	layers.add_argument("--world", required=True, help="world directory, e.g. Development")
	layers.add_argument("--page", required=True, help="page as <x>_<z>, e.g. 32_32")
	layers.add_argument("--out", required=True)
	layers.set_defaults(func=cmd_layers)

	query = sub.add_parser("query", help="describe a world point (height, slope, kind, POI, placeable)")
	query.add_argument("--map", type=int, required=True)
	query.add_argument("--at", type=float, nargs=2, metavar=("X", "Z"), required=True)
	query.add_argument("--no-cache", action="store_true")
	query.set_defaults(func=cmd_query)

	args = parser.parse_args(argv)
	return args.func(args)


if __name__ == "__main__":
	sys.exit(main())
```

(`cli_query` is created in Task 9. Until then only `layers` works.)

- [ ] **Step 5: Run the test to verify it passes**

Run: `python tools/tests/test_world_snapshot.py TerrainKindTests`
Expected: PASS.

- [ ] **Step 6: Fill in the kinds table by looking at the layers**

1. Render the Oakenshire pages, which have visible paths:
```powershell
cd tools/world
python -m worldkit layers --world Development --page 32_32 --out ../../generated/world/layers_32_32.png
python -m worldkit layers --world Development --page 33_32 --out ../../generated/world/layers_33_32.png
cd ../..
```
2. Open both PNGs with the Read tool and look at them. A path or road layer shows as thin, connected, winding bright lines. A slope or cliff layer follows ridges. A base layer is bright almost everywhere.
3. List the texture parameters of the material for context:
```powershell
python .agents/skills/mmo-material-editor/scripts/material_tool.py export-json data/client/Models/Terrain/Oakenshire_Boars_Enhanced.hmi -o generated/world/terrain_hmi.json
```
The known texture set is: grass (`Grass_*`), cliff (`Cliff_*`), churned dirt (`layer2_*`), dirt (`layer3_*`) and grass terrain (`layer4_*`).
4. Write the four kinds for `Models/Terrain/Oakenshire_Boars_Enhanced.hmi` into `terrain_kinds.json`. If Step 1 printed other material names, add a row for each.
5. If no layer clearly looks like paths, keep `unknown` for it. Put this question in the task report for the user: *"Which terrain paint layer do you use for roads/paths on Oakenshire_Boars_Enhanced?"* Do not guess.

- [ ] **Step 7: Re-run the test and commit**

Run: `python tools/tests/test_world_snapshot.py TerrainKindTests`
Expected: PASS.
```bash
git add tools/world/worldkit/terrain_kinds.py tools/world/worldkit/terrain_kinds.json tools/world/worldkit/__main__.py tools/tests/test_world_snapshot.py
git commit -m "feat(worldkit): terrain kind table + splat layer preview CLI

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Game data loader and spawn records

**Files:**
- Create: `tools/world/worldkit/data.py`, `tools/world/worldkit/spawns.py`
- Test: `tools/tests/test_world_lint.py` (created here with the spawn tests; lint tests are added in Task 10)

**Interfaces:**
- Consumes: `paths.skill_scripts`, `paths.data_dir`; `.agents/skills/mmo-quest-creator/scripts/proto_runtime.py::compile_proto_modules(project_root: Path) -> Path`.
- Produces `worldkit.data`: `load_proto_modules(repo=REPO) -> dict[str, module]` (keys `units maps quests zones objects items unit_loot object_loot`), `@dataclass GameData(units, maps, quests, zones, objects, items, unit_loot, object_loot: dict[int, message], modules: dict)`, with `map_by_directory(directory) -> message | None` and `unit_levels() -> dict[int, tuple[int, int]]`; `load_game_data(repo=REPO, data_root: Path | None = None) -> GameData`.
- Produces `worldkit.spawns`: `@dataclass(frozen=True) SpawnRecord(key, map_id, kind, entry, index, location, name, x, y, z, active, movement, respawn_delay_ms, waypoints)` with property `poi_prefix -> str | None`; `spawn_key(kind, map_id, entry, name, x, z) -> str`; `unit_spawn_records(map_entry) -> list[SpawnRecord]`; `object_spawn_records(map_entry) -> list[SpawnRecord]`; `records_from_npc_draft(doc: dict) -> list[SpawnRecord]`; `find_packs(records, link_distance=40.0) -> list[list[SpawnRecord]]`; `is_grid_pack(pack, min_size=5, max_cv=0.12) -> bool`.

- [ ] **Step 1: Write the failing tests**

`tools/tests/test_world_lint.py`:
```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for worldkit spawn flattening, pack detection and placement lint.

	python tools/tests/test_world_lint.py
"""

import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures  # noqa: F401,E402

from worldkit.data import load_game_data, load_proto_modules  # noqa: E402
from worldkit.spawns import SpawnRecord, find_packs, is_grid_pack, records_from_npc_draft, spawn_key, unit_spawn_records  # noqa: E402


def rec(x, z, entry=1, name="", y=0.0, kind="unit", active=True):
	return SpawnRecord(spawn_key(kind, 0, entry, name, x, z), 0, kind, entry, 0, 0, name, x, y, z, active, 0, 30000, ())


class SpawnRecordTests(unittest.TestCase):
	def test_key_and_prefix(self):
		record = rec(12.4, -7.6, name="Barrowfield - Barrow Skeleton 07")
		self.assertEqual(record.key, "unit:0:1:Barrowfield - Barrow Skeleton 07:12:-8")
		self.assertEqual(record.poi_prefix, "Barrowfield")
		self.assertIsNone(rec(0, 0, name="Cellar Rat").poi_prefix)

	def test_multi_location_spawn(self):
		mods = load_proto_modules()
		map_entry = mods["maps"].MapEntry(id=0, name="M", directory="M")
		spawn = map_entry.unitspawns.add(unitentry=9, positionx=1, positiony=2, positionz=3, name="A")
		spawn.locations.add(positionx=10, positiony=0, positionz=20)
		spawn.locations.add(positionx=30, positiony=0, positionz=40)
		records = unit_spawn_records(map_entry)
		self.assertEqual([(r.x, r.z, r.location) for r in records], [(10.0, 20.0, 0), (30.0, 40.0, 1)])

	def test_draft_records(self):
		doc = {"unit": {"id": 42, "minlevel": 5, "maxlevel": 6}, "spawns": [
			{"map_id": 0, "spawn": {"unitentry": 42, "name": "Mirewater - Bog Rat 01", "positionx": 1.0,
			 "positiony": 2.0, "positionz": 3.0, "movement": "RANDOM", "respawndelay": "45000"}}]}
		(record,) = records_from_npc_draft(doc)
		self.assertEqual((record.entry, record.movement, record.respawn_delay_ms, record.poi_prefix), (42, 1, 45000, "Mirewater"))

	def test_grid_pack_detection(self):
		grid = [rec(x * 14.0, z * 14.0) for x in range(3) for z in range(3)]
		rng = random.Random(7)
		scatter = [rec(rng.uniform(0, 60), rng.uniform(0, 60)) for _ in range(9)]
		(pack,) = find_packs(grid)
		self.assertTrue(is_grid_pack(pack))
		self.assertFalse(any(is_grid_pack(p) for p in find_packs(scatter)))
		self.assertFalse(is_grid_pack(grid[:4]))  # too small to call a grid

	def test_packs_split_by_entry_and_distance(self):
		packs = find_packs([rec(0, 0), rec(10, 0), rec(500, 0), rec(5, 0, entry=2)])
		self.assertEqual(sorted(len(p) for p in packs), [1, 1, 2])


class GameDataTests(unittest.TestCase):
	def test_loads_live_data(self):
		data = load_game_data()
		self.assertIn(0, data.maps)
		self.assertEqual(data.map_by_directory("Development").id, 0)
		self.assertTrue(data.units)
		levels = data.unit_levels()
		unit_id, (lo, hi) = next(iter(levels.items()))
		self.assertLessEqual(lo, hi)


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 2: Run to verify they fail**

Run: `python tools/tests/test_world_lint.py`
Expected: FAIL with `ModuleNotFoundError: No module named 'worldkit.data'`.

- [ ] **Step 3: Implement `data.py`**

`tools/world/worldkit/data.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Loads the protobuf game data (data/editor/data/*.data) for world tooling.

Schemas are compiled with the repository's protoc via the tracked quest skill's proto_runtime, so
worldkit always reads the same schema version as the authoring skills.
"""

from __future__ import annotations

import importlib
import sys
from dataclasses import dataclass, field
from pathlib import Path

from .paths import REPO, data_dir, skill_scripts

# key -> (python module, container message, data file)
_CATALOGS = {
	"units": ("units_pb2", "Units", "units.data"),
	"maps": ("maps_pb2", "Maps", "maps.data"),
	"quests": ("quests_pb2", "Quests", "quests.data"),
	"zones": ("zones_pb2", "Zones", "zones.data"),
	"objects": ("objects_pb2", "Objects", "objects.data"),
	"items": ("items_pb2", "Items", "items.data"),
	"unit_loot": ("unit_loot_pb2", "UnitLoot", "unit_loot.data"),
	"object_loot": ("object_loot_pb2", "ObjectLoot", "object_loot.data"),
}
_MODULES: dict[str, dict] = {}


def load_proto_modules(repo: Path = REPO) -> dict:
	key = str(repo)
	if key not in _MODULES:
		scripts = str(skill_scripts(repo))
		if scripts not in sys.path:
			sys.path.insert(0, scripts)
		from proto_runtime import compile_proto_modules
		compile_proto_modules(repo)
		_MODULES[key] = {name: importlib.import_module(module) for name, (module, _, _) in _CATALOGS.items()}
	return _MODULES[key]


@dataclass
class GameData:
	units: dict
	maps: dict
	quests: dict
	zones: dict
	objects: dict
	items: dict
	unit_loot: dict
	object_loot: dict
	modules: dict = field(repr=False, default_factory=dict)

	def map_by_directory(self, directory: str):
		for entry in self.maps.values():
			if entry.directory == directory:
				return entry
		return None

	def unit_levels(self) -> dict[int, tuple[int, int]]:
		return {unit_id: (unit.minlevel, max(unit.minlevel, unit.maxlevel)) for unit_id, unit in self.units.items()}


def load_game_data(repo: Path = REPO, data_root: Path | None = None) -> GameData:
	modules = load_proto_modules(repo)
	root = data_root or data_dir(repo)
	catalogs = {}
	for key, (_, container, file_name) in _CATALOGS.items():
		message = getattr(modules[key], container)()
		message.ParseFromString((root / file_name).read_bytes())
		catalogs[key] = {entry.id: entry for entry in message.entry}
	return GameData(modules=modules, **catalogs)
```

- [ ] **Step 4: Implement `spawns.py`**

`tools/world/worldkit/spawns.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Flat, position-resolved spawn records with stable identity keys.

A record is one placement: a unit spawn with several `locations` yields one record per location.
The key survives re-ordering of the spawn list (it does not use the list index) and changes when
the spawn moves, which is what the lint baseline needs: moving a buried spawn resolves it.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

_MOVEMENT_NAMES = {"STATIONARY": 0, "RANDOM": 1, "PATROL": 2}


@dataclass(frozen=True)
class SpawnRecord:
	key: str
	map_id: int
	kind: str                 # "unit" | "object"
	entry: int                # unit or object entry id
	index: int                # index in the map's spawn list
	location: int             # index in spawn.locations (0 for legacy single-position spawns)
	name: str
	x: float
	y: float
	z: float
	active: bool
	movement: int             # 0 stationary, 1 random, 2 patrol
	respawn_delay_ms: int
	waypoints: tuple

	@property
	def poi_prefix(self) -> str | None:
		"""'Barrowfield' for 'Barrowfield - Barrow Skeleton 07' (the spawn naming convention)."""
		if " - " not in self.name:
			return None
		return self.name.split(" - ", 1)[0].strip() or None


def spawn_key(kind: str, map_id: int, entry: int, name: str, x: float, z: float) -> str:
	return f"{kind}:{map_id}:{entry}:{name or '-'}:{int(round(x))}:{int(round(z))}"


def _positions(spawn) -> list[tuple[float, float, float]]:
	if len(spawn.locations):
		return [(loc.positionx, loc.positiony, loc.positionz) for loc in spawn.locations]
	return [(spawn.positionx, spawn.positiony, spawn.positionz)]


def unit_spawn_records(map_entry) -> list[SpawnRecord]:
	records = []
	for index, spawn in enumerate(map_entry.unitspawns):
		waypoints = tuple((w.positionx, w.positiony, w.positionz) for w in spawn.waypoints)
		for location, (x, y, z) in enumerate(_positions(spawn)):
			records.append(SpawnRecord(spawn_key("unit", map_entry.id, spawn.unitentry, spawn.name, x, z),
				map_entry.id, "unit", spawn.unitentry, index, location, spawn.name, x, y, z,
				spawn.isactive, int(spawn.movement), int(spawn.respawndelay), waypoints))
	return records


def object_spawn_records(map_entry) -> list[SpawnRecord]:
	records = []
	for index, spawn in enumerate(map_entry.objectspawns):
		loc = spawn.location
		x, y, z = loc.positionx, loc.positiony, loc.positionz
		records.append(SpawnRecord(spawn_key("object", map_entry.id, spawn.objectentry, spawn.name, x, z),
			map_entry.id, "object", spawn.objectentry, index, 0, spawn.name, x, y, z,
			spawn.isactive, 0, int(spawn.respawndelay), ()))
	return records


def records_from_npc_draft(doc: dict) -> list[SpawnRecord]:
	"""Spawn records from an mmo-npc-designer draft ({"unit": {...}, "spawns": [{"map_id", "spawn"}]})."""
	unit_id = int(doc.get("unit", {}).get("id", 0))
	records = []
	for index, wrapper in enumerate(doc.get("spawns", []) or []):
		spawn = wrapper.get("spawn", {})
		map_id = int(wrapper.get("map_id", 0))
		entry = int(spawn.get("unitentry", unit_id))
		name = spawn.get("name", "")
		movement = spawn.get("movement", 0)
		movement = _MOVEMENT_NAMES.get(movement, 0) if isinstance(movement, str) else int(movement)
		locations = spawn.get("locations") or [spawn]
		waypoints = tuple((w.get("positionx", 0.0), w.get("positiony", 0.0), w.get("positionz", 0.0)) for w in spawn.get("waypoints", []))
		for location, loc in enumerate(locations):
			x, y, z = float(loc.get("positionx", 0.0)), float(loc.get("positiony", 0.0)), float(loc.get("positionz", 0.0))
			records.append(SpawnRecord(spawn_key("unit", map_id, entry, name, x, z), map_id, "unit", entry, index,
				location, name, x, y, z, bool(spawn.get("isactive", True)), movement,
				int(spawn.get("respawndelay", 0) or 0), waypoints))
	return records


def find_packs(records: list[SpawnRecord], link_distance: float = 40.0) -> list[list[SpawnRecord]]:
	"""Single-linkage clusters of same-kind, same-entry spawns (planar distance <= link_distance)."""
	by_entry: dict[tuple[str, int, int], list[SpawnRecord]] = {}
	for record in records:
		by_entry.setdefault((record.kind, record.map_id, record.entry), []).append(record)
	packs = []
	for group in by_entry.values():
		unvisited = list(group)
		while unvisited:
			frontier = [unvisited.pop()]
			pack = []
			while frontier:
				current = frontier.pop()
				pack.append(current)
				near = [r for r in unvisited if math.hypot(r.x - current.x, r.z - current.z) <= link_distance]
				for r in near:
					unvisited.remove(r)
				frontier.extend(near)
			packs.append(sorted(pack, key=lambda r: r.key))
	return packs


def is_grid_pack(pack: list[SpawnRecord], min_size: int = 5, max_cv: float = 0.12) -> bool:
	"""True when nearest-neighbour spacing is suspiciously uniform (a machine-placed grid)."""
	if len(pack) < min_size:
		return False
	nearest = []
	for record in pack:
		nearest.append(min(math.hypot(o.x - record.x, o.z - record.z) for o in pack if o is not record))
	mean = sum(nearest) / len(nearest)
	if mean <= 0.0:
		return True
	variance = sum((d - mean) ** 2 for d in nearest) / len(nearest)
	return math.sqrt(variance) / mean < max_cv
```

- [ ] **Step 5: Run to verify the tests pass**

Run: `python tools/tests/test_world_lint.py`
Expected: all tests PASS.

- [ ] **Step 6: Commit**

```bash
git add tools/world/worldkit/data.py tools/world/worldkit/spawns.py tools/tests/test_world_lint.py
git commit -m "feat(worldkit): game data loader, spawn records, pack and grid detection

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: The atlas

**Files:**
- Create: `tools/world/worldkit/atlas.py`
- Test: `tools/tests/test_world_atlas.py`

**Interfaces:**
- Produces: constants `TOP_KEYS, ZONE_KEYS, POI_KEYS, ROAD_KEYS, STATUSES, POI_KINDS, SOURCES`; `class AtlasError(ValueError)`; `validate(doc: dict) -> list[str]`; `dumps(doc: dict) -> str`; `slugify(text: str) -> str`; `contains(poi: dict, x: float, z: float) -> bool`; `@dataclass Atlas(doc: dict, path: Path | None = None)` with properties `zones`, `pois`, `roads` (lists of dicts) and methods `zone(area_id) -> dict | None`, `poi(poi_id) -> dict | None`, `poi_by_name(name) -> dict | None`, `pois_at(x, z) -> list[dict]` (smallest first), `band_for(x, z, area_id: int | None) -> tuple[int, int, str] | None` (lo, hi, label), `unique_poi_id(name) -> str`, `add_zone(**fields) -> dict`, `add_poi(**fields) -> dict`, `add_road(**fields) -> dict`; `empty_atlas(map_id) -> Atlas`; `load_atlas(path) -> Atlas`; `load_atlas_or_none(path) -> Atlas | None`; `save_atlas(atlas, path=None) -> Path`.

- [ ] **Step 1: Write the failing tests**

`tools/tests/test_world_atlas.py`:
```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for worldkit.atlas: validation, stable formatting, and geometry lookups.

The atlas is edited both by agents (as text) and by mmo_edit (nlohmann json), so the byte-stable
round trip is part of the contract: loading and saving an untouched file must not change a byte.

	python tools/tests/test_world_atlas.py
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures  # noqa: F401,E402

from worldkit.atlas import AtlasError, contains, dumps, empty_atlas, load_atlas, save_atlas, validate  # noqa: E402


def sample_doc():
	return {
		"map": 0,
		"version": 1,
		"zones": [{"areaId": 8, "name": "Briarwatch March", "levelMin": 5, "levelMax": 10, "status": "placeholder", "ask": "Confirm band"}],
		"pois": [
			{"id": "kingsroad_waypost", "name": "Kingsroad Waypost", "kind": "hub", "center": [-262.0, 405.0], "radius": 25.0,
			 "levelMin": 7, "levelMax": 8, "status": "canon", "source": "user", "customField": {"keep": True}},
			{"id": "ridge_note", "name": "Kobold ridge", "kind": "note", "polygon": [[0.0, 0.0], [10.0, 0.0], [10.0, 10.0], [0.0, 10.0]],
			 "status": "note", "source": "user"},
		],
		"roads": [{"id": "westroad", "name": "Westroad", "points": [[0.0, 0.0], [100.0, 5.0]], "status": "placeholder", "ask": "Place it"}],
	}


class AtlasFormatTests(unittest.TestCase):
	def test_roundtrip_is_byte_stable(self):
		text = dumps(sample_doc())
		with tempfile.TemporaryDirectory() as tmp:
			path = Path(tmp) / "map_0.json"
			path.write_text(text, encoding="utf-8")
			atlas = load_atlas(path)
			save_atlas(atlas)
			self.assertEqual(path.read_text(encoding="utf-8"), text)

	def test_unknown_fields_and_order_preserved(self):
		text = dumps(sample_doc())
		self.assertIn('"customField"', text)
		keys = list(json.loads(text)["pois"][0].keys())
		self.assertEqual(keys[:3], ["id", "name", "kind"])
		self.assertEqual(keys[-1], "customField")

	def test_float_rounding_and_unicode(self):
		doc = sample_doc()
		doc["pois"][0]["center"] = [-262.04, 405.3333]
		doc["pois"][0]["name"] = "Thalric’s Camp"
		text = dumps(doc)
		self.assertIn("-262.0", text)
		self.assertIn("405.3", text)
		self.assertIn("Thalric’s Camp", text)
		self.assertTrue(text.endswith("}\n"))


class AtlasValidationTests(unittest.TestCase):
	def assertInvalid(self, doc, fragment):
		errors = validate(doc)
		self.assertTrue(any(fragment in e for e in errors), errors)

	def test_valid(self):
		self.assertEqual(validate(sample_doc()), [])

	def test_duplicate_poi_id(self):
		doc = sample_doc()
		doc["pois"][1]["id"] = "kingsroad_waypost"
		self.assertInvalid(doc, "duplicate id")

	def test_bad_kind(self):
		doc = sample_doc()
		doc["pois"][0]["kind"] = "camps"
		self.assertInvalid(doc, "pois[0].kind")

	def test_note_status_requires_note_kind(self):
		doc = sample_doc()
		doc["pois"][1]["kind"] = "camp"
		self.assertInvalid(doc, "note")

	def test_canon_must_not_keep_an_ask(self):
		doc = sample_doc()
		doc["pois"][0]["ask"] = "still open?"
		self.assertInvalid(doc, "pois[0].ask")

	def test_radius_xor_polygon(self):
		doc = sample_doc()
		doc["pois"][0]["polygon"] = [[0, 0], [1, 0], [1, 1]]
		self.assertInvalid(doc, "exactly one of radius or polygon")

	def test_level_order(self):
		doc = sample_doc()
		doc["zones"][0]["levelMin"] = 11
		self.assertInvalid(doc, "levelMin")

	def test_load_rejects_invalid(self):
		doc = sample_doc()
		doc["version"] = 2
		with tempfile.TemporaryDirectory() as tmp:
			path = Path(tmp) / "map_0.json"
			path.write_text(json.dumps(doc), encoding="utf-8")
			with self.assertRaises(AtlasError):
				load_atlas(path)


class AtlasLookupTests(unittest.TestCase):
	def setUp(self):
		with tempfile.TemporaryDirectory() as tmp:
			path = Path(tmp) / "map_0.json"
			path.write_text(dumps(sample_doc()), encoding="utf-8")
			self.atlas = load_atlas(path)

	def test_contains(self):
		self.assertTrue(contains(self.atlas.poi("kingsroad_waypost"), -250.0, 400.0))
		self.assertFalse(contains(self.atlas.poi("kingsroad_waypost"), -200.0, 400.0))
		self.assertTrue(contains(self.atlas.poi("ridge_note"), 5.0, 5.0))
		self.assertFalse(contains(self.atlas.poi("ridge_note"), 15.0, 5.0))

	def test_band_prefers_poi_over_zone(self):
		self.assertEqual(self.atlas.band_for(-262.0, 405.0, 8), (7, 8, "Kingsroad Waypost"))
		self.assertEqual(self.atlas.band_for(-100.0, 100.0, 8), (5, 10, "Briarwatch March"))
		self.assertIsNone(self.atlas.band_for(-100.0, 100.0, 99))

	def test_lookup_by_name_is_case_insensitive(self):
		self.assertEqual(self.atlas.poi_by_name("kingsroad waypost")["id"], "kingsroad_waypost")

	def test_add_poi_orders_keys_and_dedupes_ids(self):
		atlas = empty_atlas(0)
		first = atlas.add_poi(name="Old Mill", kind="ruin", center=[1.0, 2.0], radius=20.0, status="placeholder", source="agent")
		second = atlas.add_poi(name="Old Mill", kind="ruin", center=[50.0, 2.0], radius=20.0, status="placeholder", source="agent")
		self.assertEqual((first["id"], second["id"]), ("old_mill", "old_mill_2"))
		self.assertEqual(list(first.keys()), ["id", "name", "kind", "center", "radius", "status", "source"])

	def test_add_rejects_invalid(self):
		with self.assertRaises(AtlasError):
			empty_atlas(0).add_poi(name="X", kind="castle", center=[0.0, 0.0], radius=5.0, status="placeholder")


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 2: Run to verify they fail**

Run: `python tools/tests/test_world_atlas.py`
Expected: FAIL with `ModuleNotFoundError: No module named 'worldkit.atlas'`.

- [ ] **Step 3: Implement**

`tools/world/worldkit/atlas.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""The world atlas: named places, zone bands and roads for one map (data/world/atlas/map_<id>.json).

Tool-side only; never exported to the game. Agents edit it as text and mmo_edit edits it in the
Atlas edit mode, so the file format is part of the contract:
  - UTF-8, 2-space indent, non-ASCII kept as-is, trailing newline;
  - every float rounded to 0.1;
  - existing key order preserved (unknown keys kept), new objects written in schema order.
Status: placeholder (an agent's guess, may carry an `ask`), canon (confirmed by the user),
note (the user's design intent). Only the user promotes to canon.
"""

from __future__ import annotations

import json
import math
import re
from dataclasses import dataclass
from pathlib import Path

TOP_KEYS = ("map", "version", "zones", "pois", "roads")
ZONE_KEYS = ("areaId", "name", "levelMin", "levelMax", "theme", "description", "status", "ask", "source", "notes")
POI_KEYS = ("id", "name", "kind", "center", "radius", "polygon", "areaId", "levelMin", "levelMax", "faction",
	"description", "status", "ask", "source", "notes")
ROAD_KEYS = ("id", "name", "points", "status", "ask", "source", "notes")
STATUSES = ("placeholder", "canon", "note")
POI_KINDS = ("hub", "camp", "ruin", "lair", "landmark", "resource", "dungeon_entrance", "note")
SOURCES = ("agent-bootstrap", "agent", "user")
_ID = re.compile(r"^[a-z0-9_]+$")


class AtlasError(ValueError):
	pass


def _is_number(value) -> bool:
	return isinstance(value, (int, float)) and not isinstance(value, bool)


def _is_point(value) -> bool:
	return isinstance(value, list) and len(value) == 2 and all(_is_number(v) for v in value)


def _round_floats(value):
	if isinstance(value, float):
		return round(value, 1)
	if isinstance(value, list):
		return [_round_floats(v) for v in value]
	if isinstance(value, dict):
		return {k: _round_floats(v) for k, v in value.items()}
	return value


def dumps(doc: dict) -> str:
	return json.dumps(_round_floats(doc), indent=2, ensure_ascii=False) + "\n"


def slugify(text: str) -> str:
	slug = re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")
	return slug or "place"


def _ordered(fields: dict, keys: tuple[str, ...]) -> dict:
	out = {k: fields[k] for k in keys if k in fields and fields[k] is not None}
	out.update({k: v for k, v in fields.items() if k not in keys and v is not None})
	return out


def _check_levels(entry: dict, where: str, errors: list[str]) -> None:
	lo, hi = entry.get("levelMin"), entry.get("levelMax")
	for key, value in (("levelMin", lo), ("levelMax", hi)):
		if value is not None and (not isinstance(value, int) or isinstance(value, bool) or value < 1):
			errors.append(f"{where}.{key}: must be a positive integer")
	if isinstance(lo, int) and isinstance(hi, int) and lo > hi:
		errors.append(f"{where}.levelMin: {lo} is greater than levelMax {hi}")


def _check_common(entry: dict, where: str, allowed_status: tuple[str, ...], errors: list[str]) -> None:
	if not isinstance(entry.get("name"), str) or not entry.get("name"):
		errors.append(f"{where}.name: required non-empty string")
	status = entry.get("status")
	if status not in allowed_status:
		errors.append(f"{where}.status: {status!r} is not one of {allowed_status}")
	if "ask" in entry and not isinstance(entry["ask"], str):
		errors.append(f"{where}.ask: must be a string")
	if status == "canon" and entry.get("ask"):
		errors.append(f"{where}.ask: a canon entry must not keep an open ask (Confirm clears it)")
	if "source" in entry and entry["source"] not in SOURCES:
		errors.append(f"{where}.source: {entry['source']!r} is not one of {SOURCES}")


def validate(doc: dict) -> list[str]:
	errors: list[str] = []
	if not isinstance(doc, dict):
		return ["atlas: top level must be an object"]
	if not isinstance(doc.get("map"), int):
		errors.append("map: required integer map id")
	if doc.get("version") != 1:
		errors.append(f"version: {doc.get('version')!r} is not the supported version 1")
	for key in ("zones", "pois", "roads"):
		if not isinstance(doc.get(key), list):
			errors.append(f"{key}: required array")
	if errors:
		return errors

	seen_areas = set()
	for i, zone in enumerate(doc["zones"]):
		where = f"zones[{i}]"
		if not isinstance(zone, dict):
			errors.append(f"{where}: must be an object")
			continue
		area = zone.get("areaId")
		if not isinstance(area, int) or isinstance(area, bool) or area <= 0:
			errors.append(f"{where}.areaId: required positive integer")
		elif area in seen_areas:
			errors.append(f"{where}.areaId: duplicate id {area}")
		seen_areas.add(area)
		_check_common(zone, where, ("placeholder", "canon"), errors)
		_check_levels(zone, where, errors)

	seen_ids = set()
	for i, poi in enumerate(doc["pois"]):
		where = f"pois[{i}]"
		if not isinstance(poi, dict):
			errors.append(f"{where}: must be an object")
			continue
		poi_id = poi.get("id")
		if not isinstance(poi_id, str) or not _ID.match(poi_id):
			errors.append(f"{where}.id: required lower_snake_case string")
		elif poi_id in seen_ids:
			errors.append(f"{where}.id: duplicate id {poi_id!r}")
		seen_ids.add(poi_id)
		if poi.get("kind") not in POI_KINDS:
			errors.append(f"{where}.kind: {poi.get('kind')!r} is not one of {POI_KINDS}")
		if not _is_point(poi.get("center")):
			errors.append(f"{where}.center: required [x, z]")
		has_radius = "radius" in poi
		has_polygon = "polygon" in poi
		if has_radius == has_polygon:
			errors.append(f"{where}: needs exactly one of radius or polygon")
		elif has_radius and (not _is_number(poi["radius"]) or poi["radius"] <= 0):
			errors.append(f"{where}.radius: must be a positive number")
		elif has_polygon and (not isinstance(poi["polygon"], list) or len(poi["polygon"]) < 3 or not all(_is_point(p) for p in poi["polygon"])):
			errors.append(f"{where}.polygon: needs at least 3 [x, z] points")
		_check_common(poi, where, STATUSES, errors)
		_check_levels(poi, where, errors)
		if (poi.get("kind") == "note") != (poi.get("status") == "note"):
			errors.append(f"{where}: kind 'note' and status 'note' must go together")

	seen_roads = set()
	for i, road in enumerate(doc["roads"]):
		where = f"roads[{i}]"
		if not isinstance(road, dict):
			errors.append(f"{where}: must be an object")
			continue
		road_id = road.get("id")
		if not isinstance(road_id, str) or not _ID.match(road_id):
			errors.append(f"{where}.id: required lower_snake_case string")
		elif road_id in seen_roads:
			errors.append(f"{where}.id: duplicate id {road_id!r}")
		seen_roads.add(road_id)
		points = road.get("points")
		if not isinstance(points, list) or len(points) < 2 or not all(_is_point(p) for p in points):
			errors.append(f"{where}.points: needs at least 2 [x, z] points")
		_check_common(road, where, ("placeholder", "canon"), errors)
	return errors


def contains(poi: dict, x: float, z: float) -> bool:
	if "radius" in poi:
		cx, cz = poi["center"]
		return math.hypot(x - cx, z - cz) <= poi["radius"]
	inside = False
	points = poi["polygon"]
	for i in range(len(points)):
		x1, z1 = points[i]
		x2, z2 = points[(i + 1) % len(points)]
		if (z1 > z) != (z2 > z) and x < (x2 - x1) * (z - z1) / (z2 - z1) + x1:
			inside = not inside
	return inside


def _poi_area(poi: dict) -> float:
	if "radius" in poi:
		return math.pi * poi["radius"] ** 2
	pts = poi["polygon"]
	return abs(sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1] for i in range(len(pts)))) / 2.0


@dataclass
class Atlas:
	doc: dict
	path: Path | None = None

	@property
	def zones(self) -> list[dict]:
		return self.doc["zones"]

	@property
	def pois(self) -> list[dict]:
		return self.doc["pois"]

	@property
	def roads(self) -> list[dict]:
		return self.doc["roads"]

	def zone(self, area_id: int) -> dict | None:
		return next((z for z in self.zones if z.get("areaId") == area_id), None)

	def poi(self, poi_id: str) -> dict | None:
		return next((p for p in self.pois if p.get("id") == poi_id), None)

	def poi_by_name(self, name: str) -> dict | None:
		folded = name.casefold()
		return next((p for p in self.pois if str(p.get("name", "")).casefold() == folded), None)

	def pois_at(self, x: float, z: float) -> list[dict]:
		return sorted((p for p in self.pois if contains(p, x, z)), key=_poi_area)

	def band_for(self, x: float, z: float, area_id: int | None) -> tuple[int, int, str] | None:
		for poi in self.pois_at(x, z):
			if poi.get("levelMin") is not None and poi.get("levelMax") is not None:
				return poi["levelMin"], poi["levelMax"], poi["name"]
		zone = self.zone(area_id) if area_id else None
		if zone and zone.get("levelMin") is not None and zone.get("levelMax") is not None:
			return zone["levelMin"], zone["levelMax"], zone["name"]
		return None

	def unique_poi_id(self, name: str) -> str:
		base = slugify(name)
		taken = {p.get("id") for p in self.pois}
		if base not in taken:
			return base
		suffix = 2
		while f"{base}_{suffix}" in taken:
			suffix += 1
		return f"{base}_{suffix}"

	def _append(self, key: str, entry: dict) -> dict:
		self.doc[key].append(entry)
		errors = validate(self.doc)
		if errors:
			self.doc[key].pop()
			raise AtlasError("; ".join(errors))
		return entry

	def add_zone(self, **fields) -> dict:
		return self._append("zones", _ordered(fields, ZONE_KEYS))

	def add_poi(self, **fields) -> dict:
		fields.setdefault("id", self.unique_poi_id(fields.get("name", "place")))
		return self._append("pois", _ordered(fields, POI_KEYS))

	def add_road(self, **fields) -> dict:
		fields.setdefault("id", slugify(fields.get("name", "road")))
		return self._append("roads", _ordered(fields, ROAD_KEYS))


def empty_atlas(map_id: int) -> Atlas:
	return Atlas({"map": map_id, "version": 1, "zones": [], "pois": [], "roads": []})


def load_atlas(path: Path) -> Atlas:
	doc = json.loads(Path(path).read_text(encoding="utf-8"))
	errors = validate(doc)
	if errors:
		raise AtlasError(f"{path}: " + "; ".join(errors))
	return Atlas(doc, Path(path))


def load_atlas_or_none(path: Path) -> Atlas | None:
	return load_atlas(path) if Path(path).is_file() else None


def save_atlas(atlas: Atlas, path: Path | None = None) -> Path:
	target = Path(path or atlas.path)
	errors = validate(atlas.doc)
	if errors:
		raise AtlasError("; ".join(errors))
	target.parent.mkdir(parents=True, exist_ok=True)
	target.write_text(dumps(atlas.doc), encoding="utf-8", newline="\n")
	atlas.path = target
	return target
```

- [ ] **Step 4: Run to verify they pass**

Run: `python tools/tests/test_world_atlas.py`
Expected: all tests PASS.

- [ ] **Step 5: Keep the atlas LF-only and commit**

Append to `.gitattributes` (create the file if it does not exist):
```
data/world/atlas/*.json text eol=lf
```
```bash
git add tools/world/worldkit/atlas.py tools/tests/test_world_atlas.py .gitattributes
git commit -m "feat(worldkit): atlas schema, validation and byte-stable formatting

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: World queries, `query` CLI, and `inspect_terrain.py` on worldkit

**Files:**
- Create: `tools/world/worldkit/query.py`, `tools/world/worldkit/cli_query.py`
- Modify: `.agents/skills/mmo-npc-designer/scripts/inspect_terrain.py` (full rewrite, same CLI)
- Test: `tools/tests/test_world_query.py`

**Interfaces:**
- Consumes: `WorldSnapshot`, `surface.sample`, `entities.footprint_radius/structure_radius`, `Atlas`, `TerrainKinds`, `GameData`.
- Produces `worldkit.query`: constants `EDGE_MARGIN = 5.0`, `WATER_DEPTH_LIMIT = 1.0`, `SLOPE_LIMIT = 35.0`; `class WorldQuery(snapshot, atlas=None, kinds=None, zone_names=None)` with `has_terrain(x,z) -> bool`, `height_at(x,z) -> float | None`, `slope_at(x,z) -> float | None`, `area_at(x,z) -> int | None`, `water_depth_at(x,z) -> float`, `hole_at(x,z) -> bool`, `terrain_kind_at(x,z) -> str | None`, `edge_clear(x,z, margin=EDGE_MARGIN) -> bool`, `entities_near(x,z,r) -> list[tuple[float, WorldEntity]]`, `structure_at(x,z) -> WorldEntity | None`, `footprint_at(x,z) -> WorldEntity | None`, `placeable(x,z) -> tuple[bool, list[str]]`, `describe(x,z) -> dict`.
- Produces `worldkit.cli_query.run_query(args) -> int` (used by `python -m worldkit query`); `open_map(map_id: int, use_cache=True) -> tuple[GameData, map_entry, WorldQuery]` (reused by the CLIs in later tasks).

- [ ] **Step 1: Write the failing tests**

`tools/tests/test_world_query.py`:
```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for worldkit.query.WorldQuery on a synthetic world, plus a consistency check against the
old bilinear-corner height on the real Development world.

	python tools/tests/test_world_query.py
"""

import math
import random
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit.atlas import empty_atlas  # noqa: E402
from worldkit.constants import CELL_SIZE, OUTER_PER_PAGE, PAGE_SIZE, page_of, page_origin  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402


def steep_outer():
	outer = np.zeros((129, 129), np.float32)
	outer[:, 64:] = np.arange(65, dtype=np.float32) * CELL_SIZE * 2.0   # 63 degree ramp in the east half
	return outer


class QueryTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls._tmp = tempfile.TemporaryDirectory()
		repo = Path(cls._tmp.name)
		outer = steep_outer()
		inner = ((outer[:-1, :-1] + outer[:-1, 1:] + outer[1:, :-1] + outer[1:, 1:]) * 0.25).astype(np.float32)
		water_heights = np.full((129, 129), 2.0, np.float32)
		fx.make_world(repo, "W", {
			(32, 32): dict(outer=outer, inner=inner, holes={2: 0xFFFFFFFFFFFFFFFF}, water={1: (1, 0xFFFFFFFFFFFFFFFF)}, water_heights=water_heights),
		}, entities=[
			dict(position=(100.0, 0.0, 100.0), asset="Models/FalwynPlains/Props/Camp/SM_hc_Camptent_B.hmsh", unique_id=1),
			dict(kind="wmo", position=(200.0, 0.0, 30.0), asset="Models/Haven_001.hwmo", unique_id=2),
		])
		cls.snapshot = build_snapshot("W", repo=repo)
		atlas = empty_atlas(0)
		atlas.add_poi(name="Camp", kind="camp", center=[100.0, 100.0], radius=30.0, status="placeholder", source="agent")
		cls.query = WorldQuery(cls.snapshot, atlas=atlas)

	@classmethod
	def tearDownClass(cls):
		cls._tmp.cleanup()

	def test_placeable_flat_ground(self):
		ok, reasons = self.query.placeable(150.0, 150.0)
		self.assertTrue(ok, reasons)

	def test_no_terrain(self):
		ok, reasons = self.query.placeable(-10.0, 10.0)
		self.assertEqual((ok, reasons), (False, ["no terrain"]))
		self.assertIsNone(self.query.height_at(-10.0, 10.0))

	def test_hole_water_slope_edge_footprint(self):
		self.assertIn("terrain hole", self.query.placeable(70.0, 5.0)[1])       # tile 2 = x 66..100, z 0..33
		self.assertTrue(any(r.startswith("water") for r in self.query.placeable(40.0, 5.0)[1]))  # tile 1
		self.assertTrue(any(r.startswith("slope") for r in self.query.placeable(400.0, 200.0)[1]))
		self.assertIn("within 5 m of the terrain edge", self.query.placeable(2.0, 200.0)[1])
		self.assertTrue(any("inside footprint" in r for r in self.query.placeable(101.0, 100.0)[1]))

	def test_structure_and_describe(self):
		self.assertEqual(self.query.structure_at(210.0, 30.0).asset, "Models/Haven_001.hwmo")
		self.assertIsNone(self.query.footprint_at(210.0, 30.0))
		info = self.query.describe(100.0, 100.0)
		self.assertEqual(info["pois"][0]["name"], "Camp")
		self.assertEqual(info["entities"][0]["asset"], "Models/FalwynPlains/Props/Camp/SM_hc_Camptent_B.hmsh")


class DevelopmentConsistencyTests(unittest.TestCase):
	"""The fan surface differs from the old bilinear-corner height only where inner vertices were
	sculpted, so on the real world the two agree closely in the median."""

	def test_median_difference_is_small(self):
		snap = build_snapshot("Development")
		query = WorldQuery(snap)
		rng = random.Random(1)
		x0, z0, x1, z1 = snap.extent
		diffs = []
		while len(diffs) < 300:
			x, z = rng.uniform(x0, x1), rng.uniform(z0, z1)
			located = snap.page_local(x, z)
			if located is None:
				continue
			slot, lx, lz = located
			outer = snap.outer[slot]
			scale = PAGE_SIZE / (OUTER_PER_PAGE - 1)
			gx, gz = lx / scale, lz / scale
			xi, zi = min(int(gx), 127), min(int(gz), 127)
			u, v = gx - xi, gz - zi
			bilinear = (outer[zi, xi] * (1 - u) * (1 - v) + outer[zi, xi + 1] * u * (1 - v)
						+ outer[zi + 1, xi] * (1 - u) * v + outer[zi + 1, xi + 1] * u * v)
			diffs.append(abs(query.height_at(x, z) - bilinear))
		self.assertLess(float(np.median(diffs)), 0.1)


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 2: Run to verify they fail**

Run: `python tools/tests/test_world_query.py`
Expected: FAIL with `ModuleNotFoundError: No module named 'worldkit.query'`.

- [ ] **Step 3: Implement `query.py`**

`tools/world/worldkit/query.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Point queries against a WorldSnapshot: exact height/slope, terrain facts, nearby props, POIs."""

from __future__ import annotations

import math

from .atlas import Atlas
from .entities import footprint_radius, structure_radius
from .snapshot import WorldSnapshot
from .surface import sample
from .terrain_kinds import TerrainKinds

EDGE_MARGIN = 5.0
WATER_DEPTH_LIMIT = 1.0
SLOPE_LIMIT = 35.0
_EDGE_DIRECTIONS = [(math.cos(a), math.sin(a)) for a in (i * math.pi / 4.0 for i in range(8))]


class WorldQuery:
	def __init__(self, snapshot: WorldSnapshot, atlas: Atlas | None = None, kinds: TerrainKinds | None = None,
				 zone_names: dict[int, str] | None = None):
		self.snapshot = snapshot
		self.atlas = atlas
		self.kinds = kinds
		self.zone_names = zone_names or {}

	def has_terrain(self, x: float, z: float) -> bool:
		return self.snapshot.page_local(x, z) is not None

	def _surface(self, x: float, z: float) -> tuple[float, float] | None:
		located = self.snapshot.page_local(x, z)
		if located is None:
			return None
		slot, lx, lz = located
		return sample(self.snapshot.outer[slot], self.snapshot.inner[slot], lx, lz)

	def height_at(self, x: float, z: float) -> float | None:
		surface = self._surface(x, z)
		return surface[0] if surface else None

	def slope_at(self, x: float, z: float) -> float | None:
		surface = self._surface(x, z)
		return surface[1] if surface else None

	def _cell(self, x: float, z: float):
		return self.snapshot.cell_index(x, z) if self.has_terrain(x, z) else None

	def area_at(self, x: float, z: float) -> int | None:
		cell = self._cell(x, z)
		return int(self.snapshot.area[cell]) if cell else None

	def water_depth_at(self, x: float, z: float) -> float:
		cell = self._cell(x, z)
		return float(self.snapshot.water_depth[cell]) if cell else 0.0

	def hole_at(self, x: float, z: float) -> bool:
		cell = self._cell(x, z)
		return bool(self.snapshot.hole[cell]) if cell else False

	def terrain_kind_at(self, x: float, z: float) -> str | None:
		cell = self._cell(x, z)
		if not cell or not self.kinds:
			return None
		material = self.snapshot.material_name(int(self.snapshot.material[cell]))
		return self.kinds.kind(material, int(self.snapshot.layer[cell]))

	def edge_clear(self, x: float, z: float, margin: float = EDGE_MARGIN) -> bool:
		return all(self.has_terrain(x + dx * margin, z + dz * margin) for dx, dz in _EDGE_DIRECTIONS)

	def entities_near(self, x: float, z: float, radius: float):
		found = []
		for entity in self.snapshot.entities:
			distance = math.hypot(entity.position[0] - x, entity.position[2] - z)
			if distance <= radius:
				found.append((distance, entity))
		return sorted(found, key=lambda item: item[0])

	def structure_at(self, x: float, z: float):
		for distance, entity in self.entities_near(x, z, 100.0):
			if distance <= structure_radius(entity):
				return entity
		return None

	def footprint_at(self, x: float, z: float):
		for distance, entity in self.entities_near(x, z, 50.0):
			if distance <= footprint_radius(entity):
				return entity
		return None

	def placeable(self, x: float, z: float) -> tuple[bool, list[str]]:
		if not self.has_terrain(x, z):
			return False, ["no terrain"]
		reasons = []
		if self.hole_at(x, z):
			reasons.append("terrain hole")
		depth = self.water_depth_at(x, z)
		if depth > WATER_DEPTH_LIMIT:
			reasons.append(f"water {depth:.1f} m deep")
		slope = self.slope_at(x, z)
		if slope is not None and slope > SLOPE_LIMIT:
			reasons.append(f"slope {slope:.0f}° (> {SLOPE_LIMIT:.0f}°)")
		if not self.edge_clear(x, z):
			reasons.append(f"within {EDGE_MARGIN:.0f} m of the terrain edge")
		blocker = self.footprint_at(x, z)
		if blocker is not None:
			reasons.append(f"inside footprint of {blocker.asset}")
		return not reasons, reasons

	def describe(self, x: float, z: float) -> dict:
		area = self.area_at(x, z)
		ok, reasons = self.placeable(x, z)
		pois = self.atlas.pois_at(x, z) if self.atlas else []
		return {
			"x": x,
			"z": z,
			"height": self.height_at(x, z),
			"slope_deg": self.slope_at(x, z),
			"area_id": area,
			"area_name": self.zone_names.get(area) if area else None,
			"terrain_kind": self.terrain_kind_at(x, z),
			"water_depth": self.water_depth_at(x, z),
			"hole": self.hole_at(x, z),
			"placeable": {"ok": ok, "reasons": reasons},
			"pois": [{"id": p["id"], "name": p["name"], "kind": p["kind"], "status": p["status"]} for p in pois],
			"entities": [{"asset": e.asset, "distance": round(d, 1)} for d, e in self.entities_near(x, z, 30.0)[:5]],
		}
```

- [ ] **Step 4: Implement `cli_query.py`**

`tools/world/worldkit/cli_query.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Shared map opening for the worldkit CLIs, and the `python -m worldkit query` command."""

from __future__ import annotations

import json

from .atlas import load_atlas_or_none
from .data import GameData, load_game_data
from .paths import atlas_path
from .query import WorldQuery
from .snapshot import load_snapshot
from .terrain_kinds import load_kinds


def open_map(map_id: int, use_cache: bool = True):
	"""Returns (game_data, map_entry, query) for a map id; raises SystemExit for an unknown map."""
	data: GameData = load_game_data()
	map_entry = data.maps.get(map_id)
	if map_entry is None:
		raise SystemExit(f"unknown map id {map_id}")
	snapshot = load_snapshot(map_entry.directory, use_cache=use_cache)
	zone_names = {zone_id: zone.name for zone_id, zone in data.zones.items()}
	query = WorldQuery(snapshot, load_atlas_or_none(atlas_path(map_id)), load_kinds(), zone_names)
	return data, map_entry, query


def run_query(args) -> int:
	_, _, query = open_map(args.map, use_cache=not args.no_cache)
	print(json.dumps(query.describe(args.at[0], args.at[1]), indent=2, ensure_ascii=False))
	return 0
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `python tools/tests/test_world_query.py`
Expected: all tests PASS.

Then smoke-test the CLI:
```powershell
cd tools/world; python -m worldkit query --map 0 --at -262 405; cd ../..
```
Expected: JSON with a finite `height`, an `area_id`, and `placeable`.

- [ ] **Step 6: Rewrite `inspect_terrain.py` on worldkit (same CLI and keys)**

Replace the whole of `.agents/skills/mmo-npc-designer/scripts/inspect_terrain.py` with:
```python
#!/usr/bin/env python3
"""Inspect terrain pages, zone bindings, and terrain height for spawn placement.

Thin CLI over tools/world/worldkit, the single terrain parser in this repository. Heights come from
the rendered surface (the four-triangle fan around each cell's stored inner vertex), so they can
differ from older bilinear results where inner vertices were sculpted. Also reports slope, water,
holes, terrain kind and whether the spot is placeable.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from npc_catalog_lib import find_project_root, load_catalog_bundle

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools" / "world"))

from worldkit.atlas import load_atlas_or_none  # noqa: E402
from worldkit.constants import MAX_PAGES, PAGE_SIZE, TILE_SIZE, TILES_PER_PAGE, CELLS_PER_TILE, page_of, page_origin  # noqa: E402
from worldkit.paths import atlas_path, terrain_dir  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import load_snapshot  # noqa: E402
from worldkit.terrain_kinds import load_kinds  # noqa: E402

WORLD_HALF_SIZE = MAX_PAGES * PAGE_SIZE * 0.5


def tile_bounds_from_global(global_tile_x: int, global_tile_z: int) -> dict[str, float]:
	min_x = global_tile_x * TILE_SIZE - WORLD_HALF_SIZE
	min_z = global_tile_z * TILE_SIZE - WORLD_HALF_SIZE
	return {"min_x": min_x, "max_x": min_x + TILE_SIZE, "min_z": min_z, "max_z": min_z + TILE_SIZE,
		"center_x": min_x + TILE_SIZE * 0.5, "center_z": min_z + TILE_SIZE * 0.5}


def map_block(map_entry) -> dict:
	return {"id": map_entry.id, "name": map_entry.name, "directory": map_entry.directory}


def inspect_world_position(project_root: Path, map_entry, zone_index, world_x: float, world_z: float) -> dict:
	snapshot = load_snapshot(map_entry.directory, repo=project_root)
	query = WorldQuery(snapshot, load_atlas_or_none(atlas_path(map_entry.id, project_root)), load_kinds(),
		{zone_id: zone.name for zone_id, zone in zone_index.items()})
	page_x, page_z = page_of(world_x, world_z)
	located = snapshot.page_local(world_x, world_z)
	if located is None:
		raise SystemExit(f"Terrain page does not exist: {terrain_dir(map_entry.directory, project_root) / f'{page_x}_{page_z}.tile'}")
	slot = located[0]
	height = query.height_at(world_x, world_z)
	ok, reasons = query.placeable(world_x, world_z)
	global_tile_x = int((world_x + WORLD_HALF_SIZE) / TILE_SIZE)
	global_tile_z = int((world_z + WORLD_HALF_SIZE) / TILE_SIZE)
	area_id = query.area_at(world_x, world_z) or 0
	zone_entry = zone_index.get(area_id)
	origin_x, origin_z = page_origin(page_x, page_z)
	return {
		"map": map_block(map_entry),
		"query": {"world_x": world_x, "world_z": world_z},
		"terrain_page": {"page_x": page_x, "page_z": page_z, "origin_x": origin_x, "origin_z": origin_z,
			"path": str(terrain_dir(map_entry.directory, project_root) / f"{page_x}_{page_z}.tile"),
			"version": int(snapshot.page_versions[slot])},
		"terrain_sample": {"height_y": height, "suggested_spawn_y": height,
			"slope_deg": query.slope_at(world_x, world_z), "water_depth": query.water_depth_at(world_x, world_z),
			"hole": query.hole_at(world_x, world_z), "terrain_kind": query.terrain_kind_at(world_x, world_z)},
		"placement": {"ok": ok, "reasons": reasons,
			"pois": [p["name"] for p in (query.atlas.pois_at(world_x, world_z) if query.atlas else [])]},
		"tile": {"global_tile_x": global_tile_x, "global_tile_z": global_tile_z,
			"local_tile_x": global_tile_x % TILES_PER_PAGE, "local_tile_z": global_tile_z % TILES_PER_PAGE,
			"bounds": tile_bounds_from_global(global_tile_x, global_tile_z)},
		"area": {"id": area_id, "name": zone_entry.name if zone_entry else None,
			"parentzone": zone_entry.parentzone if zone_entry and zone_entry.HasField("parentzone") else None},
	}


def resolve_zone_ids(indexes, map_id: int, zone_id: int | None, zone_name: str | None) -> list[int]:
	zones = indexes["zones"]
	if zone_id is not None:
		return [zone_id] if zone_id in zones else []
	if not zone_name:
		return []
	folded = zone_name.casefold()
	candidates = [e for e in zones.values() if not e.HasField("map") or e.map == map_id]
	exact = sorted(e.id for e in candidates if e.name.casefold() == folded)
	return exact or sorted(e.id for e in candidates if folded in e.name.casefold())


def inspect_zone_coverage(project_root: Path, map_entry, indexes, target_zone_ids: list[int]) -> dict:
	result = {"map": map_block(map_entry), "matched_zone_ids": target_zone_ids,
		"matched_zone_names": [indexes["zones"][z].name for z in target_zone_ids],
		"tile_count": 0, "pages": [], "world_bounds": None, "sample_tile_centers": []}
	if not target_zone_ids:
		return result
	snapshot = load_snapshot(map_entry.directory, repo=project_root)
	pages = set()
	global_tiles = []
	for pz in range(snapshot.pages_z):
		for px in range(snapshot.pages_x):
			if snapshot.page_slot[pz, px] < 0:
				continue
			page_x, page_z = snapshot.page_min_x + px, snapshot.page_min_z + pz
			rows = slice(pz * 128, pz * 128 + 128, CELLS_PER_TILE)
			cols = slice(px * 128, px * 128 + 128, CELLS_PER_TILE)
			tile_areas = snapshot.area[rows, cols]
			for local_z in range(TILES_PER_PAGE):
				for local_x in range(TILES_PER_PAGE):
					if int(tile_areas[local_z, local_x]) in target_zone_ids:
						pages.add((page_x, page_z))
						global_tiles.append((page_x, page_z, local_x, local_z, page_x * TILES_PER_PAGE + local_x, page_z * TILES_PER_PAGE + local_z))
	result["tile_count"] = len(global_tiles)
	result["pages"] = [{"page_x": x, "page_z": z} for x, z in sorted(pages)]
	if global_tiles:
		lo = tile_bounds_from_global(min(t[4] for t in global_tiles), min(t[5] for t in global_tiles))
		hi = tile_bounds_from_global(max(t[4] for t in global_tiles), max(t[5] for t in global_tiles))
		result["world_bounds"] = {"min_x": lo["min_x"], "max_x": hi["max_x"], "min_z": lo["min_z"], "max_z": hi["max_z"]}
		for page_x, page_z, local_x, local_z, gx, gz in global_tiles[:12]:
			bounds = tile_bounds_from_global(gx, gz)
			result["sample_tile_centers"].append({"page_x": page_x, "page_z": page_z, "local_tile_x": local_x,
				"local_tile_z": local_z, "world_x": bounds["center_x"], "world_z": bounds["center_z"]})
	return result


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--project-root", default=None)
	parser.add_argument("--map-id", type=int, required=True)
	parser.add_argument("--world-x", type=float)
	parser.add_argument("--world-z", type=float)
	parser.add_argument("--zone-id", type=int)
	parser.add_argument("--zone-name")
	parser.add_argument("--pretty", action="store_true")
	args = parser.parse_args()

	if (args.world_x is None) != (args.world_z is None):
		parser.error("--world-x and --world-z must be provided together")
	if args.world_x is None and args.zone_id is None and not args.zone_name:
		parser.error("provide either --world-x/--world-z, --zone-id, or --zone-name")

	project_root = find_project_root(args.project_root)
	_, _, indexes = load_catalog_bundle(project_root)
	map_entry = indexes["maps"].get(args.map_id)
	if not map_entry:
		raise SystemExit(f"Unknown map id {args.map_id}")

	result = {}
	if args.world_x is not None:
		result["world_position"] = inspect_world_position(project_root, map_entry, indexes["zones"], args.world_x, args.world_z)
	if args.zone_id is not None or args.zone_name:
		result["zone_coverage"] = inspect_zone_coverage(project_root, map_entry, indexes,
			resolve_zone_ids(indexes, args.map_id, args.zone_id, args.zone_name))
	print(json.dumps(result, indent=2 if args.pretty else None))
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
```

(This script keeps the original file's 4-space-indent-free style. It has no copyright header today; leave that as it was.)

- [ ] **Step 7: Smoke-test the rewritten script**

```powershell
python .agents/skills/mmo-npc-designer/scripts/inspect_terrain.py --map-id 0 --world-x 150 --world-z 450 --pretty
python .agents/skills/mmo-npc-designer/scripts/inspect_terrain.py --map-id 0 --zone-name Oakenshire --pretty
```
Expected:
- The first command returns `terrain_sample.height_y` (finite), `slope_deg`, `placement.ok`, and `area.name`.
- The second command returns `tile_count` 121 and the bounding box x 100..467, z 400..833 (the numbers from the survey).

- [ ] **Step 8: Commit**

```bash
git add tools/world/worldkit/query.py tools/world/worldkit/cli_query.py tools/tests/test_world_query.py .agents/skills/mmo-npc-designer/scripts/inspect_terrain.py
git commit -m "feat(worldkit): point queries + query CLI; inspect_terrain now uses worldkit

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 10: Placement lint, baseline, `lint.py` CLI

**Files:**
- Create: `tools/world/worldkit/lint.py`, `tools/world/worldkit/baseline.py`, `tools/world/lint.py`
- Test: `tools/tests/test_world_lint.py` (add classes)

**Interfaces:**
- Consumes: `SpawnRecord`, `find_packs`, `is_grid_pack`, `unit_spawn_records`, `object_spawn_records`, `records_from_npc_draft` (Task 7); `WorldQuery` + limits (Task 9); `contains` (Task 8); `open_map` (Task 9).
- Produces `worldkit.lint`: `HEIGHT_WARNING = 0.5`, `HEIGHT_ERROR = 2.0`; `@dataclass(frozen=True) Violation(rule, severity, subject, message, x=None, z=None)` with `key -> "rule|subject"` and `to_dict()`; `lint_placement(record, query) -> list[Violation]`; `lint_packs(records) -> list[Violation]`; `lint_levels(records, query, unit_levels) -> list[Violation]`; `naming_violations(records) -> list[Violation]`; `lint_records(records, query, unit_levels) -> list[Violation]` (placement + packs + levels, sorted); `lint_map(game_data, map_entry, query) -> tuple[list[SpawnRecord], list[Violation]]`.
- Produces `worldkit.baseline`: `BASELINE_PATH`, `load_baseline(section: str, path=BASELINE_PATH) -> dict[str, str]`, `save_baseline(section: str, map_id: int, items: list, path=BASELINE_PATH) -> int` (replaces only that section's entries for that map; items need `.key`, `.message`), `split(items, baseline) -> tuple[list, list]`.
- Produces CLI `python tools/world/lint.py --map <id> [--draft <npc.json>] [--update-baseline] [--json <out>] [--show-known] [--no-cache]`. Exit code 0 when there are no *new* errors, 1 when there are. New warnings are printed but do not fail (warnings are advice; the baseline only protects against new *errors*).

Rule names (stable, used in baseline keys): `no_terrain`, `in_hole`, `in_water`, `steep_slope`, `terrain_edge`, `height_delta`, `inside_footprint`, `grid_pattern`, `outside_poi`, `level_band`, `spawn_name`.

- [ ] **Step 1: Write the failing tests**

Append to `tools/tests/test_world_lint.py` (before `if __name__`):
```python
import json  # noqa: E402
import tempfile  # noqa: E402

import numpy as np  # noqa: E402

from worldkit.atlas import empty_atlas  # noqa: E402
from worldkit.baseline import load_baseline, save_baseline, split  # noqa: E402
from worldkit.constants import CELL_SIZE  # noqa: E402
from worldkit.lint import lint_records, naming_violations  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402


def lint_world(repo: Path):
	outer = np.zeros((129, 129), np.float32)
	outer[:, 100:] = (np.arange(29, dtype=np.float32) * CELL_SIZE * 2.0)[None, :]  # steep far-east strip
	inner = ((outer[:-1, :-1] + outer[:-1, 1:] + outer[1:, :-1] + outer[1:, 1:]) * 0.25).astype(np.float32)
	world_fixtures.make_world(repo, "L", {(32, 32): dict(outer=outer, inner=inner, areas=np.full((16, 16), 8, np.uint32))})
	return build_snapshot("L", repo=repo)


class LintTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls._tmp = tempfile.TemporaryDirectory()
		snapshot = lint_world(Path(cls._tmp.name))
		atlas = empty_atlas(0)
		atlas.add_zone(areaId=8, name="Briarwatch March", levelMin=5, levelMax=10, status="placeholder")
		atlas.add_poi(name="Barrowfield", kind="camp", center=[100.0, 100.0], radius=40.0, levelMin=8, levelMax=9, status="placeholder", source="agent")
		cls.query = WorldQuery(snapshot, atlas=atlas)

	@classmethod
	def tearDownClass(cls):
		cls._tmp.cleanup()

	def rules(self, records, levels=None):
		return {(v.rule, v.severity) for v in lint_records(records, self.query, levels or {})}

	def test_clean_spawn(self):
		self.assertEqual(self.rules([rec(200.0, 200.0, y=0.2)]), set())

	def test_height_delta(self):
		self.assertEqual(self.rules([rec(200.0, 200.0, y=-3.0)]), {("height_delta", "error")})
		self.assertEqual(self.rules([rec(200.0, 200.0, y=1.0)]), {("height_delta", "warning")})

	def test_slope_edge_and_no_terrain(self):
		self.assertIn(("steep_slope", "error"), self.rules([rec(480.0, 200.0, y=self.query.height_at(480.0, 200.0))]))
		self.assertIn(("terrain_edge", "error"), self.rules([rec(2.0, 200.0)]))
		self.assertEqual(self.rules([rec(-50.0, 200.0)]), {("no_terrain", "error")})

	def test_grid_pack(self):
		grid = [rec(150 + x * 14.0, 150 + z * 14.0) for x in range(3) for z in range(3)]
		self.assertIn(("grid_pattern", "warning"), self.rules(grid))

	def test_poi_and_level_band(self):
		inside = rec(100.0, 100.0, entry=7, name="Barrowfield - Barrow Skeleton 01")
		outside = rec(300.0, 300.0, entry=7, name="Barrowfield - Barrow Skeleton 02")
		self.assertEqual(self.rules([inside], {7: (8, 9)}), set())
		self.assertIn(("outside_poi", "warning"), self.rules([outside], {7: (8, 9)}))
		self.assertIn(("level_band", "warning"), self.rules([inside], {7: (2, 3)}))
		self.assertIn(("level_band", "warning"), self.rules([outside], {7: (20, 21)}))  # zone band 5-10

	def test_naming(self):
		self.assertEqual([v.rule for v in naming_violations([rec(0, 0, name="Bog Rat")])], ["spawn_name"])
		self.assertEqual(naming_violations([rec(0, 0, name="Mirewater - Bog Rat 01")]), [])


class BaselineTests(unittest.TestCase):
	def test_sections_and_maps_are_independent(self):
		class Item:
			def __init__(self, key, message="m"):
				self.key, self.message = key, message
		with tempfile.TemporaryDirectory() as tmp:
			path = Path(tmp) / "baseline.json"
			save_baseline("placement", 0, [Item("a|unit:0:1"), Item("b|unit:0:2")], path)
			save_baseline("placement", 2, [Item("a|unit:2:1")], path)
			save_baseline("reachability", 0, [Item("no_turn_in|quest:5")], path)
			save_baseline("placement", 0, [Item("a|unit:0:1")], path)   # map 0 re-baselined, one fixed
			placement = load_baseline("placement", path)
			self.assertEqual(set(placement), {"a|unit:0:1", "a|unit:2:1"})
			self.assertEqual(set(load_baseline("reachability", path)), {"no_turn_in|quest:5"})
			new, known = split([Item("a|unit:0:1"), Item("c|unit:0:9")], placement)
			self.assertEqual(([i.key for i in new], [i.key for i in known]), (["c|unit:0:9"], ["a|unit:0:1"]))
			self.assertEqual(json.loads(path.read_text())["version"], 1)
```

- [ ] **Step 2: Run to verify they fail**

Run: `python tools/tests/test_world_lint.py`
Expected: FAIL with `ModuleNotFoundError: No module named 'worldkit.baseline'`.

- [ ] **Step 3: Implement `worldkit/lint.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Placement lint for unit and object spawns.

Errors are placements a player would notice as broken (buried, floating, in a lake, on a cliff, at
the world edge). Warnings are design smells (machine-made grids, spawns outside the place they are
named for, levels outside the band of the area).
"""

from __future__ import annotations

from dataclasses import asdict, dataclass

from .atlas import contains
from .query import EDGE_MARGIN, SLOPE_LIMIT, WATER_DEPTH_LIMIT, WorldQuery
from .spawns import SpawnRecord, find_packs, is_grid_pack, object_spawn_records, unit_spawn_records

HEIGHT_WARNING = 0.5
HEIGHT_ERROR = 2.0
_SEVERITY_ORDER = {"error": 0, "warning": 1}


@dataclass(frozen=True)
class Violation:
    rule: str
    severity: str        # "error" | "warning"
    subject: str         # spawn key, or "pack:<first spawn key>"
    message: str
    x: float | None = None
    z: float | None = None

    @property
    def key(self) -> str:
        return f"{self.rule}|{self.subject}"

    def to_dict(self) -> dict:
        return {"key": self.key, **asdict(self)}


def _label(record: SpawnRecord) -> str:
    return record.name or f"{record.kind} {record.entry}"


def lint_placement(record: SpawnRecord, query: WorldQuery) -> list[Violation]:
    found: list[Violation] = []
    x, z = record.x, record.z

    def add(rule: str, severity: str, text: str) -> None:
        found.append(Violation(rule, severity, record.key, f"{_label(record)} at ({x:.0f}, {z:.0f}): {text}", x, z))

    if not query.has_terrain(x, z):
        add("no_terrain", "error", "no terrain under the spawn")
        return found
    if query.hole_at(x, z):
        add("in_hole", "error", "stands in a terrain hole")
    depth = query.water_depth_at(x, z)
    if depth > WATER_DEPTH_LIMIT:
        add("in_water", "error", f"stands in {depth:.1f} m deep water")
    slope = query.slope_at(x, z)
    if slope is not None and slope > SLOPE_LIMIT:
        add("steep_slope", "error", f"slope {slope:.0f}° exceeds {SLOPE_LIMIT:.0f}°")
    if not query.edge_clear(x, z):
        add("terrain_edge", "error", f"within {EDGE_MARGIN:.0f} m of the terrain edge")
    if query.structure_at(x, z) is None:
        delta = record.y - query.height_at(x, z)
        if abs(delta) > HEIGHT_WARNING:
            severity = "error" if abs(delta) > HEIGHT_ERROR else "warning"
            add("height_delta", severity, f"{abs(delta):.1f} m {'above' if delta > 0 else 'below'} the ground")
    blocker = query.footprint_at(x, z)
    if blocker is not None:
        add("inside_footprint", "warning", f"inside the footprint of {blocker.asset}")
    return found


def lint_packs(records: list[SpawnRecord]) -> list[Violation]:
    found = []
    for pack in find_packs(records):
        if is_grid_pack(pack):
            first = pack[0]
            cx = sum(r.x for r in pack) / len(pack)
            cz = sum(r.z for r in pack) / len(pack)
            found.append(Violation("grid_pattern", "warning", f"pack:{first.key}",
                f"{len(pack)} x {_label(first)} around ({cx:.0f}, {cz:.0f}) sit on a regular grid; scatter them irregularly",
                cx, cz))
    return found


def lint_levels(records: list[SpawnRecord], query: WorldQuery, unit_levels: dict[int, tuple[int, int]]) -> list[Violation]:
    atlas = query.atlas
    if atlas is None:
        return []
    found = []
    for record in records:
        if record.kind != "unit":
            continue
        x, z = record.x, record.z
        prefix = record.poi_prefix
        if prefix:
            poi = atlas.poi_by_name(prefix)
            if poi is not None and not contains(poi, x, z):
                found.append(Violation("outside_poi", "warning", record.key,
                    f"{_label(record)} at ({x:.0f}, {z:.0f}) is named for '{poi['name']}' but stands outside it", x, z))
        levels = unit_levels.get(record.entry)
        band = atlas.band_for(x, z, query.area_at(x, z)) if levels else None
        if band and (levels[1] < band[0] or levels[0] > band[1]):
            found.append(Violation("level_band", "warning", record.key,
                f"{_label(record)} at ({x:.0f}, {z:.0f}) is level {levels[0]}-{levels[1]}, outside the {band[2]} band {band[0]}-{band[1]}", x, z))
    return found


def naming_violations(records: list[SpawnRecord]) -> list[Violation]:
    """Spawn naming convention '<Place> - <Unit name>[ NN]' (applied to drafts, not to legacy data)."""
    found = []
    for record in records:
        if record.name and (" - " not in record.name or record.name.isupper()):
            found.append(Violation("spawn_name", "warning", record.key,
                f"spawn name '{record.name}' should follow '<Place> - <Unit name>' (see docs/world/bible.md naming guide)",
                record.x, record.z))
    return found


def _sorted(violations: list[Violation]) -> list[Violation]:
    return sorted(violations, key=lambda v: (_SEVERITY_ORDER[v.severity], v.rule, v.subject))


def lint_records(records: list[SpawnRecord], query: WorldQuery, unit_levels: dict[int, tuple[int, int]]) -> list[Violation]:
    found: list[Violation] = []
    for record in records:
        found.extend(lint_placement(record, query))
    found.extend(lint_packs(records))
    found.extend(lint_levels(records, query, unit_levels))
    return _sorted(found)


def lint_map(game_data, map_entry, query: WorldQuery) -> tuple[list[SpawnRecord], list[Violation]]:
    records = unit_spawn_records(map_entry) + object_spawn_records(map_entry)
    return records, lint_records(records, query, game_data.unit_levels())
```

(Non-test Python under `tools/world` and `.agents` uses 4-space indentation, like the existing `tools/*.py`. Tests under `tools/tests` use tabs, like the existing tests. Convert the plan's tab-indented listings accordingly when writing files.)

- [ ] **Step 4: Implement `worldkit/baseline.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Known-violation baseline (tools/world/lint_baseline.json).

Sections ("placement", "reachability") and maps are updated independently, so re-baselining one
map after fixing it never forgets another map's known issues. Fixing a violation changes or removes
its key, so it simply drops out on the next --update-baseline.
"""

from __future__ import annotations

import json
from pathlib import Path

from .paths import REPO

BASELINE_PATH = REPO / "tools" / "world" / "lint_baseline.json"


def _read(path: Path) -> dict:
    if not Path(path).is_file():
        return {"version": 1}
    return json.loads(Path(path).read_text(encoding="utf-8"))


def load_baseline(section: str, path: Path = BASELINE_PATH) -> dict[str, str]:
    return {entry["key"]: entry["message"] for entry in _read(path).get(section, [])}


def save_baseline(section: str, map_id: int, items, path: Path = BASELINE_PATH) -> int:
    doc = _read(path)
    kept = [entry for entry in doc.get(section, []) if entry.get("map") != map_id]
    fresh = [{"map": map_id, "key": item.key, "message": item.message} for item in items]
    doc[section] = sorted(kept + fresh, key=lambda entry: (entry["map"], entry["key"]))
    doc["version"] = 1
    Path(path).write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    return len(fresh)


def split(items, baseline: dict[str, str]) -> tuple[list, list]:
    new = [item for item in items if item.key not in baseline]
    known = [item for item in items if item.key in baseline]
    return new, known
```

- [ ] **Step 5: Implement the CLI `tools/world/lint.py`**

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Placement lint for a map's spawns (or for the spawns of an NPC draft).

    python tools/world/lint.py --map 0                      # whole map, compared to the baseline
    python tools/world/lint.py --map 0 --draft generated/npcs/bog_rat.json
    python tools/world/lint.py --map 0 --update-baseline    # accept today's violations as known

Exit code 1 when there are new errors (not in tools/world/lint_baseline.json). New warnings are
printed but do not fail.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from worldkit.baseline import BASELINE_PATH, load_baseline, save_baseline, split
from worldkit.cli_query import open_map
from worldkit.lint import lint_map, lint_records, naming_violations
from worldkit.snapshot import NoTerrainError
from worldkit.spawns import records_from_npc_draft


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--map", type=int, default=0)
    parser.add_argument("--draft", type=Path, help="mmo-npc-designer draft JSON to lint instead of the live map")
    parser.add_argument("--baseline", type=Path, default=BASELINE_PATH)
    parser.add_argument("--update-baseline", action="store_true")
    parser.add_argument("--json", type=Path, help="write the full result as JSON")
    parser.add_argument("--show-known", action="store_true")
    parser.add_argument("--no-cache", action="store_true")
    args = parser.parse_args(argv)

    try:
        data, map_entry, query = open_map(args.map, use_cache=not args.no_cache)
    except NoTerrainError as exc:
        print(f"skipped map {args.map}: {exc}")
        return 0

    if args.draft:
        if args.update_baseline:
            parser.error("--update-baseline cannot be combined with --draft")
        doc = json.loads(args.draft.read_text(encoding="utf-8"))
        records = [r for r in records_from_npc_draft(doc) if r.map_id == args.map]
        levels = data.unit_levels()
        unit = doc.get("unit", {})
        if "id" in unit and "minlevel" in unit:
            levels[int(unit["id"])] = (int(unit["minlevel"]), max(int(unit["minlevel"]), int(unit.get("maxlevel", unit["minlevel"]))))
        violations = lint_records(records, query, levels) + naming_violations(records)
    else:
        records, violations = lint_map(data, map_entry, query)

    if args.update_baseline:
        count = save_baseline("placement", args.map, violations, args.baseline)
        print(f"baselined {count} placement violations for map {args.map} in {args.baseline}")
        return 0

    new, known = split(violations, load_baseline("placement", args.baseline))
    new_errors = [v for v in new if v.severity == "error"]
    new_warnings = [v for v in new if v.severity == "warning"]
    print(f"map {args.map} ({map_entry.name}): checked {len(records)} spawns, "
          f"{len(new_errors)} new errors, {len(new_warnings)} new warnings, {len(known)} known")
    for v in new_errors:
        print(f"  ERROR   {v.rule}: {v.message}")
    for v in new_warnings:
        print(f"  WARNING {v.rule}: {v.message}")
    if args.show_known:
        for v in known:
            print(f"  known   {v.rule}: {v.message}")
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps({
            "map": args.map, "checked": len(records),
            "new_errors": [v.to_dict() for v in new_errors],
            "new_warnings": [v.to_dict() for v in new_warnings],
            "known": len(known),
        }, indent=2, ensure_ascii=False), encoding="utf-8")
    return 1 if new_errors else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 6: Run the tests, then the CLI on map 0**

Run: `python tools/tests/test_world_lint.py`
Expected: all PASS.

Run: `python tools/world/lint.py --map 0`
Expected: exit 1, because there is no baseline yet. The output lists the known problems from the survey: about 15 `height_delta` errors (buried up to 6.7 m), `steep_slope` for Cellar Rat (265, 609) and Duskfang Stalker (-148, 408), and `grid_pattern` warnings for Bog Rat, Risen Laborer and Barrow Skeleton. Read the output and check that these names appear. If a known case is missing, or there are wildly more errors than listed, investigate before continuing. For example, hundreds of `height_delta` errors would mean a coordinate or height bug.

- [ ] **Step 7: Commit**

```bash
git add tools/world/worldkit/lint.py tools/world/worldkit/baseline.py tools/world/lint.py tools/tests/test_world_lint.py
git commit -m "feat(worldkit): placement lint with sectioned baseline and CLI

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: Content report (reachability and health)

**Files:**
- Create: `tools/world/worldkit/report.py`, `tools/world/report.py`
- Test: `tools/tests/test_world_report.py`

**Interfaces:**
- Consumes: `GameData`, `load_proto_modules`, `unit_spawn_records`, `object_spawn_records`, `WorldQuery`, baseline functions, `open_map`.
- Produces `worldkit.report`: `AUTO_REWARDED = 0x20`, `DEFAULT_MAX_DISTANCE = 250.0`; `@dataclass(frozen=True) Finding(rule, severity, subject, message)` with `key` and `to_dict()`; `quest_givers(data, map_entry) -> dict[int, list[tuple[float,float]]]`; `objective_sources(data, map_entry, quest, requirement) -> list[tuple[float,float]]`; `check_reachability(data, map_entry, max_distance=DEFAULT_MAX_DISTANCE) -> list[Finding]`; `quest_links(data, map_entry) -> list[tuple[int, tuple[float,float], tuple[float,float]]]`; `summarize(data, map_entry, query) -> dict`; `to_markdown(map_entry, new, known, summary) -> str`.
- Rules: `no_active_source` (error), `source_far` (warning), `auto_rewarded` (error), `no_turn_in` (error). Subjects: `quest:<id>:req<n>` or `quest:<id>`.

- [ ] **Step 1: Write the failing tests**

`tools/tests/test_world_report.py`:
```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for worldkit.report on hand-built protobuf game data.

	python tools/tests/test_world_report.py
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures  # noqa: F401,E402

from worldkit.data import GameData, load_proto_modules  # noqa: E402
from worldkit.report import check_reachability, quest_links  # noqa: E402


def build(target_pos=(100.0, 0.0), target_active=True, flags=0, with_ender=True, requirement="creature"):
	m = load_proto_modules()
	giver = m["units"].UnitEntry(id=1, name="Farmer Haldor", minlevel=5, maxlevel=5)
	giver.quests.append(10)
	if with_ender:
		giver.end_quests.append(10)
	boar = m["units"].UnitEntry(id=2, name="Young Forest Boar", minlevel=2, maxlevel=3, unitlootentry=50)
	loot = m["unit_loot"].UnitLoot().entry.add(id=50, name="Boar")
	loot.groups.add().definitions.add(item=900, dropchance=50.0)
	quest = m["quests"].QuestEntry(id=10, name="The Boar Problem", flags=flags)
	if requirement == "creature":
		quest.requirements.add(creatureid=2, creaturecount=10)
	else:
		quest.requirements.add(itemid=900, itemcount=5)
	map_entry = m["maps"].MapEntry(id=0, name="Dev", directory="Development")
	map_entry.unitspawns.add(unitentry=1, positionx=0, positiony=0, positionz=0)
	map_entry.unitspawns.add(unitentry=2, positionx=target_pos[0], positiony=0, positionz=target_pos[1], isactive=target_active)
	item = m["items"].ItemEntry(id=900, name="Boar Tusk")
	return GameData(units={1: giver, 2: boar}, maps={0: map_entry}, quests={10: quest}, zones={}, objects={},
		items={900: item}, unit_loot={50: loot}, object_loot={}, modules=m), map_entry


class ReachabilityTests(unittest.TestCase):
	def rules(self, **kwargs):
		data, map_entry = build(**kwargs)
		return {(f.rule, f.severity) for f in check_reachability(data, map_entry)}

	def test_reachable(self):
		self.assertEqual(self.rules(), set())

	def test_inactive_target(self):
		self.assertEqual(self.rules(target_active=False), {("no_active_source", "error")})

	def test_far_target(self):
		self.assertEqual(self.rules(target_pos=(1000.0, 0.0)), {("source_far", "warning")})

	def test_item_from_loot(self):
		self.assertEqual(self.rules(requirement="item"), set())

	def test_turn_in_rules(self):
		self.assertIn(("auto_rewarded", "error"), self.rules(flags=0x20))
		self.assertIn(("no_turn_in", "error"), self.rules(with_ender=False))

	def test_quest_links(self):
		data, map_entry = build()
		self.assertEqual(quest_links(data, map_entry), [(10, (0.0, 0.0), (100.0, 0.0))])


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 2: Run to verify they fail**

Run: `python tools/tests/test_world_report.py`
Expected: FAIL with `ModuleNotFoundError: No module named 'worldkit.report'`.

- [ ] **Step 3: Implement `worldkit/report.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Content health of a map: can every quest objective actually be done near its quest giver, does
every quest end with a turn-in, and how are spawns and levels distributed over zones and places."""

from __future__ import annotations

import math
from dataclasses import asdict, dataclass

import numpy as np

from .spawns import object_spawn_records, unit_spawn_records

AUTO_REWARDED = 0x20
DEFAULT_MAX_DISTANCE = 250.0
_SEVERITY_ORDER = {"error": 0, "warning": 1}


@dataclass(frozen=True)
class Finding:
    rule: str
    severity: str
    subject: str
    message: str

    @property
    def key(self) -> str:
        return f"{self.rule}|{self.subject}"

    def to_dict(self) -> dict:
        return {"key": self.key, **asdict(self)}


def _active_positions(records) -> dict[int, list[tuple[float, float]]]:
    positions: dict[int, list[tuple[float, float]]] = {}
    for record in records:
        if record.active:
            positions.setdefault(record.entry, []).append((record.x, record.z))
    return positions


def _loot_ids_with_item(loot: dict, item_id: int) -> set[int]:
    ids = set()
    for loot_id, entry in loot.items():
        for group in entry.groups:
            if any(d.item == item_id and d.isactive for d in group.definitions):
                ids.add(loot_id)
    return ids


def _object_loot_ids(obj) -> set[int]:
    ids = set(getattr(obj, "objectlootentries", []))
    legacy = getattr(obj, "objectlootentry", 0)
    if legacy:
        ids.add(legacy)
    return ids


def quest_givers(data, map_entry) -> dict[int, list[tuple[float, float]]]:
    givers: dict[int, list[tuple[float, float]]] = {}
    for entry, points in _active_positions(unit_spawn_records(map_entry)).items():
        unit = data.units.get(entry)
        for quest_id in (unit.quests if unit else []):
            givers.setdefault(quest_id, []).extend(points)
    for entry, points in _active_positions(object_spawn_records(map_entry)).items():
        obj = data.objects.get(entry)
        for quest_id in (obj.quests if obj else []):
            givers.setdefault(quest_id, []).extend(points)
    return givers


def _quest_enders(data) -> set[int]:
    enders = set()
    for unit in data.units.values():
        enders.update(unit.end_quests)
    for obj in data.objects.values():
        enders.update(obj.end_quests)
    return enders


def objective_sources(data, map_entry, quest, requirement) -> list[tuple[float, float]]:
    units = _active_positions(unit_spawn_records(map_entry))
    objects = _active_positions(object_spawn_records(map_entry))
    if requirement.creatureid:
        return list(units.get(requirement.creatureid, []))
    if requirement.objectid:
        return list(objects.get(requirement.objectid, []))
    if requirement.itemid:
        sources: list[tuple[float, float]] = []
        unit_loot_ids = _loot_ids_with_item(data.unit_loot, requirement.itemid)
        for unit_id, points in units.items():
            unit = data.units.get(unit_id)
            if unit is not None and unit.unitlootentry in unit_loot_ids:
                sources.extend(points)
        object_loot_ids = _loot_ids_with_item(data.object_loot, requirement.itemid)
        for index, spawn in enumerate(map_entry.objectspawns):
            if not spawn.isactive:
                continue
            obj = data.objects.get(spawn.objectentry)
            loot_ids = {spawn.loot_entry} if spawn.loot_entry else (_object_loot_ids(obj) if obj else set())
            if loot_ids & object_loot_ids:
                sources.append((spawn.location.positionx, spawn.location.positionz))
        return sources
    return []


def _requirement_label(data, requirement) -> str:
    if requirement.creatureid:
        unit = data.units.get(requirement.creatureid)
        return f"kill {unit.name if unit else requirement.creatureid}"
    if requirement.objectid:
        obj = data.objects.get(requirement.objectid)
        return f"use {obj.name if obj else requirement.objectid}"
    item = data.items.get(requirement.itemid)
    return f"collect {item.name if item else requirement.itemid}"


def _nearest(givers, sources) -> tuple[float, tuple[float, float], tuple[float, float]]:
    return min(((math.hypot(g[0] - s[0], g[1] - s[1]), g, s) for g in givers for s in sources), key=lambda t: t[0])


def check_reachability(data, map_entry, max_distance: float = DEFAULT_MAX_DISTANCE) -> list[Finding]:
    findings: list[Finding] = []
    givers = quest_givers(data, map_entry)
    enders = _quest_enders(data)
    for quest_id in sorted(givers):
        quest = data.quests.get(quest_id)
        if quest is None:
            continue
        title = f"Quest {quest_id} '{quest.name}'"
        if quest.flags & AUTO_REWARDED:
            findings.append(Finding("auto_rewarded", "error", f"quest:{quest_id}",
                f"{title} is AutoRewarded; every quest must end with a turn-in at an NPC or object"))
        if quest_id not in enders:
            findings.append(Finding("no_turn_in", "error", f"quest:{quest_id}", f"{title} has no NPC or object that accepts the turn-in"))
        for index, requirement in enumerate(quest.requirements):
            if requirement.itemid and requirement.itemid == quest.srcitemid:
                continue
            if not (requirement.creatureid or requirement.objectid or requirement.itemid):
                continue
            sources = objective_sources(data, map_entry, quest, requirement)
            subject = f"quest:{quest_id}:req{index}"
            label = _requirement_label(data, requirement)
            if not sources:
                findings.append(Finding("no_active_source", "error", subject, f"{title}: '{label}' has no active source on map {map_entry.id}"))
                continue
            distance, _, _ = _nearest(givers[quest_id], sources)
            if distance > max_distance:
                findings.append(Finding("source_far", "warning", subject,
                    f"{title}: nearest '{label}' source is {distance:.0f} m from the quest giver (> {max_distance:.0f} m)"))
    return sorted(findings, key=lambda f: (_SEVERITY_ORDER[f.severity], f.rule, f.subject))


def quest_links(data, map_entry) -> list[tuple[int, tuple[float, float], tuple[float, float]]]:
    """(quest id, giver position, nearest objective source) per quest objective, for map arrows."""
    links = []
    givers = quest_givers(data, map_entry)
    for quest_id in sorted(givers):
        quest = data.quests.get(quest_id)
        if quest is None:
            continue
        for requirement in quest.requirements:
            sources = objective_sources(data, map_entry, quest, requirement)
            if sources:
                _, giver, source = _nearest(givers[quest_id], sources)
                link = (quest_id, (float(giver[0]), float(giver[1])), (float(source[0]), float(source[1])))
                if link not in links:
                    links.append(link)
    return links


def _band(levels: list[tuple[int, int]]) -> list[int] | None:
    return [min(l[0] for l in levels), max(l[1] for l in levels)] if levels else None


def summarize(data, map_entry, query) -> dict:
    unit_levels = data.unit_levels()
    zones: dict[int, list] = {}
    places: dict[str, list] = {}
    givers_outside = []
    giver_units = {unit_id for unit_id, unit in data.units.items() if len(unit.quests)}
    for record in unit_spawn_records(map_entry):
        levels = unit_levels.get(record.entry, (0, 0))
        area = query.area_at(record.x, record.z) or 0
        zones.setdefault(area, []).append(levels)
        pois = query.atlas.pois_at(record.x, record.z) if query.atlas else []
        if pois:
            places.setdefault(pois[0]["name"], []).append(levels)
        elif query.atlas and record.entry in giver_units:
            givers_outside.append(f"{data.units[record.entry].name} at ({record.x:.0f}, {record.z:.0f})")
    present_areas = set(np.unique(query.snapshot.area).tolist())
    no_terrain = sorted(zone.name for zone_id, zone in data.zones.items()
                        if zone_id not in present_areas and (not zone.HasField("map") or zone.map == map_entry.id))
    open_asks = []
    if query.atlas:
        for key in ("zones", "pois", "roads"):
            for entry in query.atlas.doc[key]:
                if entry.get("status") == "placeholder" and entry.get("ask"):
                    open_asks.append(f"{entry['name']}: {entry['ask']}")
    return {
        "zones": {data.zones[a].name if a in data.zones else f"area {a}": {"spawns": len(l), "levels": _band(l)} for a, l in sorted(zones.items())},
        "places": {name: {"spawns": len(l), "levels": _band(l)} for name, l in sorted(places.items())},
        "quest_givers_outside_places": givers_outside,
        "zones_without_terrain": no_terrain,
        "open_asks": open_asks,
    }


def to_markdown(map_entry, new: list[Finding], known: list[Finding], summary: dict) -> str:
    lines = [f"# Content report: map {map_entry.id} ({map_entry.name})", ""]
    lines.append(f"**New findings:** {len(new)} (errors: {sum(f.severity == 'error' for f in new)}), known: {len(known)}")
    lines.append("")
    for finding in new:
        lines.append(f"- **{finding.severity.upper()}** `{finding.rule}` {finding.message}")
    lines += ["", "## Spawns per zone", "", "| Zone | Spawns | Levels |", "|---|---|---|"]
    for name, row in summary["zones"].items():
        lines.append(f"| {name} | {row['spawns']} | {row['levels']} |")
    if summary["places"]:
        lines += ["", "## Spawns per place", "", "| Place | Spawns | Levels |", "|---|---|---|"]
        for name, row in summary["places"].items():
            lines.append(f"| {name} | {row['spawns']} | {row['levels']} |")
    for title, key in (("Quest givers outside any place", "quest_givers_outside_places"),
                       ("Zones without terrain", "zones_without_terrain"),
                       ("Open questions in the atlas", "open_asks")):
        if summary[key]:
            lines += ["", f"## {title}", ""] + [f"- {item}" for item in summary[key]]
    return "\n".join(lines) + "\n"
```

- [ ] **Step 4: Implement the CLI `tools/world/report.py`**

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Content health report for a map: quest reachability, turn-in rule, spawn/level distribution.

    python tools/world/report.py --map 0 --markdown generated/world/report_map_0.md
    python tools/world/report.py --map 0 --update-baseline

Exit code 1 when there are new errors (not in the reachability section of the baseline).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from worldkit.baseline import BASELINE_PATH, load_baseline, save_baseline, split
from worldkit.cli_query import open_map
from worldkit.report import DEFAULT_MAX_DISTANCE, check_reachability, summarize, to_markdown
from worldkit.snapshot import NoTerrainError


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--map", type=int, default=0)
    parser.add_argument("--max-distance", type=float, default=DEFAULT_MAX_DISTANCE)
    parser.add_argument("--baseline", type=Path, default=BASELINE_PATH)
    parser.add_argument("--update-baseline", action="store_true")
    parser.add_argument("--json", type=Path)
    parser.add_argument("--markdown", type=Path)
    args = parser.parse_args(argv)

    try:
        data, map_entry, query = open_map(args.map)
    except NoTerrainError as exc:
        print(f"skipped map {args.map}: {exc}")
        return 0

    findings = check_reachability(data, map_entry, args.max_distance)
    if args.update_baseline:
        count = save_baseline("reachability", args.map, findings, args.baseline)
        print(f"baselined {count} reachability findings for map {args.map} in {args.baseline}")
        return 0

    new, known = split(findings, load_baseline("reachability", args.baseline))
    summary = summarize(data, map_entry, query)
    new_errors = [f for f in new if f.severity == "error"]
    print(f"map {args.map} ({map_entry.name}): {len(new_errors)} new errors, {len(new) - len(new_errors)} new warnings, {len(known)} known")
    for finding in new:
        print(f"  {finding.severity.upper():7} {finding.rule}: {finding.message}")
    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        args.markdown.write_text(to_markdown(map_entry, new, known, summary), encoding="utf-8")
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps({"map": args.map, "new_errors": [f.to_dict() for f in new_errors],
            "new_warnings": [f.to_dict() for f in new if f.severity == "warning"], "known": len(known),
            "summary": summary}, indent=2, ensure_ascii=False), encoding="utf-8")
    return 1 if new_errors else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 5: Run the tests and the CLI**

Run: `python tools/tests/test_world_report.py`
Expected: all PASS.

Run: `python tools/world/report.py --map 0 --markdown generated/world/report_map_0.md`
Expected:
- exit 1 (no baseline yet);
- the findings include `no_active_source` for quest 30 "Broken Ground" (its Marshland Ambusher and Hexer spawns are inactive);
- `zones_without_terrain` in the markdown lists Haven, Market, Mage Tower and Greystone Pass.

Read the markdown to confirm.

- [ ] **Step 6: Commit**

```bash
git add tools/world/worldkit/report.py tools/world/report.py tools/tests/test_world_report.py
git commit -m "feat(worldkit): content report - quest reachability, turn-in rule, spawn distribution

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 12: Map renderer

**Files:**
- Create: `tools/world/worldkit/render.py`, `tools/world/render_map.py`
- Test: `tools/tests/test_world_render.py`

**Interfaces:**
- Consumes: `WorldSnapshot`, `TerrainKinds.road_mask`, `Atlas`, `SpawnRecord`, `quest_links`, `open_map`.
- Produces `worldkit.render`: `ALL_LAYERS = ("relief", "water", "roads", "entities", "zones", "pois", "spawns", "quests")`; `LEGEND_WIDTH = 240`; `@dataclass RenderOptions(bbox=None, px_per_m=None, max_side=2048, layers=frozenset(ALL_LAYERS), color_by="level", title="")`; `render_map(snapshot, *, spawns, unit_levels, unit_factions, atlas, kinds, quest_arrows, options, diff_state=None) -> PIL.Image.Image`; `dump_state(snapshot, spawns) -> dict`; `world_to_pixel(bbox, scale, x, z) -> tuple[float, float]`.
- Produces CLI `python tools/world/render_map.py --map <id> [--zone <name|areaId>] [--poi <id>] [--bbox X0 Z0 X1 Z1] [--margin M] [--px-per-m S] [--layers a,b,c] [--color-by level|faction] [--diff state.json] [--dump-state out.json] --out <png>`.

Orientation: image right is +X, image up is +Z. The legend says so.

- [ ] **Step 1: Write the failing tests**

`tools/tests/test_world_render.py`:
```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Smoke and pixel tests for worldkit.render on a synthetic world.

	python tools/tests/test_world_render.py
"""

import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402
from test_world_lint import rec  # noqa: E402

from worldkit.atlas import empty_atlas  # noqa: E402
from worldkit.render import LEGEND_WIDTH, RenderOptions, dump_state, render_map, world_to_pixel  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402
from worldkit.terrain_kinds import TerrainKinds  # noqa: E402


class RenderTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls._tmp = tempfile.TemporaryDirectory()
		repo = Path(cls._tmp.name)
		fx.make_world(repo, "R", {(32, 32): dict(water={0: (1, 0xFFFFFFFFFFFFFFFF)}, water_heights=np.full((129, 129), 3.0, np.float32))},
			entities=[dict(position=(300.0, 0.0, 300.0))])
		cls.snapshot = build_snapshot("R", repo=repo)

	@classmethod
	def tearDownClass(cls):
		cls._tmp.cleanup()

	def render(self, **kwargs):
		atlas = empty_atlas(0)
		atlas.add_poi(name="Camp", kind="camp", center=[200.0, 200.0], radius=20.0, status="placeholder", source="agent", ask="Name it")
		options = RenderOptions(bbox=(0.0, 0.0, 400.0, 400.0), px_per_m=1.0)
		return render_map(self.snapshot, spawns=kwargs.get("spawns", [rec(250.0, 150.0, entry=1)]), unit_levels={1: (5, 5)},
			unit_factions={1: 3}, atlas=atlas, kinds=TerrainKinds({}), quest_arrows=[(10, (200.0, 200.0), (250.0, 150.0))],
			options=options, diff_state=kwargs.get("diff_state"))

	def test_size_includes_legend(self):
		image = self.render()
		self.assertEqual(image.size, (400 + LEGEND_WIDTH, 400))

	def test_water_is_blue(self):
		image = self.render(spawns=[])
		px, py = world_to_pixel((0.0, 0.0, 400.0, 400.0), 1.0, 15.0, 15.0)   # tile 0 is water
		r, g, b = image.getpixel((int(px), int(py)))[:3]
		self.assertGreater(b, r + 40)

	def test_spawn_dot_drawn(self):
		plain = np.asarray(self.render(spawns=[]))
		with_spawn = np.asarray(self.render())
		px, py = world_to_pixel((0.0, 0.0, 400.0, 400.0), 1.0, 250.0, 150.0)
		self.assertFalse(np.array_equal(plain[int(py), int(px)], with_spawn[int(py), int(px)]))

	def test_diff_state_roundtrip(self):
		before = dump_state(self.snapshot, [rec(10.0, 10.0)])
		image = self.render(diff_state=before)
		self.assertEqual(image.size, (400 + LEGEND_WIDTH, 400))


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 2: Run to verify they fail**

Run: `python tools/tests/test_world_render.py`
Expected: FAIL with `ModuleNotFoundError: No module named 'worldkit.render'`.

- [ ] **Step 3: Implement `worldkit/render.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Top-down map rendering for review packets: relief, water, roads, props, zones, named places,
spawns, quest links, plus an optional before/after diff. Image right = +X, image up = +Z."""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from .atlas import Atlas
from .constants import CELL_SIZE

ALL_LAYERS = ("relief", "water", "roads", "entities", "zones", "pois", "spawns", "quests")
LEGEND_WIDTH = 240
_NO_TERRAIN = (32, 32, 32)
_ZONE_TINTS = [(118, 160, 92), (160, 150, 96), (100, 140, 150), (150, 110, 140), (170, 130, 90), (110, 150, 120),
               (140, 140, 100), (120, 120, 160), (160, 120, 110), (100, 160, 140), (150, 150, 150), (130, 110, 90)]
_UNZONED = (140, 140, 140)
_LEVEL_COLORS = [(46, 204, 113), (163, 203, 56), (241, 196, 15), (230, 126, 34), (231, 76, 60), (192, 57, 43), (142, 68, 173)]
_STATUS_COLORS = {"placeholder": (255, 165, 0), "canon": (255, 255, 255), "note": (255, 64, 255)}
_ROAD_COLOR = (181, 140, 90)
_WATER_COLOR = (40, 90, 200)


@dataclass
class RenderOptions:
    bbox: tuple[float, float, float, float] | None = None   # x0, z0, x1, z1
    px_per_m: float | None = None
    max_side: int = 2048
    layers: frozenset = field(default_factory=lambda: frozenset(ALL_LAYERS))
    color_by: str = "level"
    title: str = ""


def _font(size: int):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


def world_to_pixel(bbox, scale: float, x: float, z: float) -> tuple[float, float]:
    x0, _, _, z1 = bbox
    return (x - x0) * scale, (z1 - z) * scale


def level_color(level: int) -> tuple[int, int, int]:
    return _LEVEL_COLORS[min(max((level - 1) // 3, 0), len(_LEVEL_COLORS) - 1)]


def faction_color(faction: int) -> tuple[int, int, int]:
    return _ZONE_TINTS[faction % len(_ZONE_TINTS)]


def _hillshade(height: np.ndarray) -> np.ndarray:
    filled = np.where(np.isnan(height), np.nanmean(height) if np.isfinite(height).any() else 0.0, height)
    grad_z, grad_x = np.gradient(filled, CELL_SIZE)
    # Rows are +z; the light comes from the image's upper left, i.e. from -x/+z.
    normal = np.stack([-grad_x, np.ones_like(filled), -grad_z], axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    light = np.array([-1.0, 1.4, 1.0])
    light /= np.linalg.norm(light)
    return np.clip(normal @ light, 0.0, 1.0)


def _label(draw: ImageDraw.ImageDraw, xy, text: str, color, font) -> None:
    draw.text(xy, text, fill=color, font=font, stroke_width=2, stroke_fill=(0, 0, 0))


def _raster(snapshot, bbox, scale, width, height, options, kinds) -> tuple[np.ndarray, np.ndarray]:
    x0, _, _, z1 = bbox
    ox, oz = snapshot.origin
    rows, cols = snapshot.height.shape
    xs = x0 + (np.arange(width) + 0.5) / scale
    zs = z1 - (np.arange(height) + 0.5) / scale
    cx = np.floor((xs - ox) / CELL_SIZE).astype(np.int64)
    cz = np.floor((zs - oz) / CELL_SIZE).astype(np.int64)
    in_x = (cx >= 0) & (cx < cols)
    in_z = (cz >= 0) & (cz < rows)
    CZ, CX = np.meshgrid(np.clip(cz, 0, rows - 1), np.clip(cx, 0, cols - 1), indexing="ij")
    valid = in_z[:, None] & in_x[None, :] & ~np.isnan(snapshot.height[CZ, CX])

    area = snapshot.area[CZ, CX]
    tints = np.array(_ZONE_TINTS, np.float64)
    base = np.where((area > 0)[..., None], tints[area % len(_ZONE_TINTS)], np.array(_UNZONED, np.float64))
    if "relief" in options.layers:
        shade = _hillshade(snapshot.height)[CZ, CX]
        base = base * (0.35 + 0.65 * shade[..., None])
    if "water" in options.layers:
        depth = snapshot.water_depth[CZ, CX]
        alpha = np.where(depth > 0, np.minimum(1.0, 0.45 + depth / 4.0), 0.0)[..., None]
        base = base * (1 - alpha) + np.array(_WATER_COLOR) * alpha
    if "roads" in options.layers and kinds is not None:
        road = kinds.road_mask(snapshot)[CZ, CX]
        base = np.where(road[..., None], base * 0.3 + np.array(_ROAD_COLOR) * 0.7, base)
    base = np.where(snapshot.hole[CZ, CX][..., None], 0.0, base)
    if "zones" in options.layers:
        border = np.zeros(area.shape, bool)
        border[1:, :] |= area[1:, :] != area[:-1, :]
        border[:, 1:] |= area[:, 1:] != area[:, :-1]
        base = np.where(border[..., None], np.array((20, 20, 20)), base)
    base = np.where(valid[..., None], base, np.array(_NO_TERRAIN))
    return base.astype(np.uint8), area


def _legend(image: Image.Image, map_height: int, scale: float, options: RenderOptions, bbox) -> None:
    draw = ImageDraw.Draw(image)
    left = image.width - LEGEND_WIDTH
    draw.rectangle([left, 0, image.width, image.height], fill=(24, 24, 24))
    font, small = _font(14), _font(11)
    y = 8
    if options.title:
        draw.text((left + 10, y), options.title, fill=(255, 255, 255), font=font)
        y += 22
    draw.text((left + 10, y), "right = +X, up = +Z", fill=(200, 200, 200), font=small)
    y += 16
    draw.text((left + 10, y), f"x {bbox[0]:.0f}..{bbox[2]:.0f}  z {bbox[1]:.0f}..{bbox[3]:.0f}", fill=(200, 200, 200), font=small)
    y += 22
    nice = [10, 25, 50, 100, 250, 500, 1000, 2500]
    metres = max([n for n in nice if n * scale <= LEGEND_WIDTH - 40] or [nice[0]])
    draw.line([left + 10, y, left + 10 + metres * scale, y], fill=(255, 255, 255), width=3)
    draw.text((left + 10, y + 4), f"{metres} m", fill=(255, 255, 255), font=small)
    y += 26
    if options.color_by == "level":
        draw.text((left + 10, y), "spawn level", fill=(255, 255, 255), font=small)
        y += 16
        for index, color in enumerate(_LEVEL_COLORS):
            draw.ellipse([left + 12, y + 2, left + 20, y + 10], fill=color)
            label = f"{index * 3 + 1}-{index * 3 + 3}" if index < len(_LEVEL_COLORS) - 1 else f"{index * 3 + 1}+"
            draw.text((left + 26, y), label, fill=(220, 220, 220), font=small)
            y += 14
    else:
        draw.text((left + 10, y), "spawn colour = faction template", fill=(255, 255, 255), font=small)
        y += 16
    draw.text((left + 10, y), "hollow dot = inactive spawn", fill=(220, 220, 220), font=small)
    y += 20
    for status, color in _STATUS_COLORS.items():
        draw.ellipse([left + 12, y + 2, left + 20, y + 10], outline=color, width=2)
        draw.text((left + 26, y), f"place: {status}", fill=(220, 220, 220), font=small)
        y += 14
    for text, color in (("road / path", _ROAD_COLOR), ("water", _WATER_COLOR), ("quest giver -> objective", (255, 255, 120)),
                        ("added (diff)", (60, 220, 60)), ("removed (diff)", (230, 40, 40)), ("prop / building", (90, 90, 90))):
        draw.rectangle([left + 12, y + 3, left + 20, y + 9], fill=color)
        draw.text((left + 26, y), text, fill=(220, 220, 220), font=small)
        y += 14


def dump_state(snapshot, spawns) -> dict:
    """Positions of spawns and entities, for a later --diff render."""
    return {
        "spawns": {s.key: [round(s.x, 1), round(s.z, 1)] for s in spawns},
        "entities": {str(e.unique_id): [round(e.position[0], 1), round(e.position[2], 1)] for e in snapshot.entities},
    }


def render_map(snapshot, *, spawns, unit_levels, unit_factions, atlas: Atlas | None, kinds, quest_arrows,
               options: RenderOptions, diff_state: dict | None = None) -> Image.Image:
    bbox = options.bbox or snapshot.extent
    width_m, height_m = bbox[2] - bbox[0], bbox[3] - bbox[1]
    scale = options.px_per_m or min(options.max_side / width_m, options.max_side / height_m)
    width, height = max(1, int(math.ceil(width_m * scale))), max(1, int(math.ceil(height_m * scale)))

    pixels, _ = _raster(snapshot, bbox, scale, width, height, options, kinds)
    image = Image.new("RGB", (width + LEGEND_WIDTH, height), (24, 24, 24))
    image.paste(Image.fromarray(pixels, "RGB"), (0, 0))
    draw = ImageDraw.Draw(image)
    font, small = _font(13), _font(11)

    def to_px(x, z):
        return world_to_pixel(bbox, scale, x, z)

    if "entities" in options.layers:
        for entity in snapshot.entities:
            px, py = to_px(entity.position[0], entity.position[2])
            size = 4 if entity.kind == "wmo" else 1.5
            if entity.kind == "wmo":
                draw.rectangle([px - size, py - size, px + size, py + size], outline=(60, 40, 20), width=2)
            else:
                draw.rectangle([px - size, py - size, px + size, py + size], fill=(90, 90, 90))

    if atlas is not None and "roads" in options.layers:
        for road in atlas.roads:
            points = [to_px(x, z) for x, z in road["points"]]
            color = _STATUS_COLORS.get(road.get("status"), (255, 255, 255))
            draw.line(points, fill=color, width=3)
            _label(draw, (points[0][0] + 4, points[0][1] + 4), road["name"], color, small)

    if atlas is not None and "pois" in options.layers:
        for poi in atlas.pois:
            color = _STATUS_COLORS.get(poi.get("status"), (255, 255, 255))
            cx, cz = poi["center"]
            px, py = to_px(cx, cz)
            if "radius" in poi:
                r = max(4.0, poi["radius"] * scale)
                draw.ellipse([px - r, py - r, px + r, py + r], outline=color, width=2)
            else:
                draw.polygon([to_px(x, z) for x, z in poi["polygon"]], outline=color, width=2)
            text = poi["name"] + (" ?" if poi.get("ask") else "")
            _label(draw, (px + 6, py - 16), text, color, font)

    if "spawns" in options.layers:
        for spawn in spawns:
            if spawn.waypoints:
                draw.line([to_px(w[0], w[2]) for w in spawn.waypoints], fill=(200, 200, 255), width=1)
            px, py = to_px(spawn.x, spawn.z)
            if spawn.kind == "object":
                color = (240, 240, 240)
            elif options.color_by == "faction":
                color = faction_color(unit_factions.get(spawn.entry, 0))
            else:
                color = level_color(unit_levels.get(spawn.entry, (1, 1))[1])
            box = [px - 3, py - 3, px + 3, py + 3]
            if spawn.active:
                draw.ellipse(box, fill=color, outline=(0, 0, 0))
            else:
                draw.ellipse(box, outline=color, width=2)

    if "quests" in options.layers:
        for quest_id, giver, source in quest_arrows:
            gx, gy = to_px(*giver)
            sx, sy = to_px(*source)
            draw.line([gx, gy, sx, sy], fill=(255, 255, 120), width=2)
            angle = math.atan2(sy - gy, sx - gx)
            head = [(sx, sy), (sx - 9 * math.cos(angle - 0.4), sy - 9 * math.sin(angle - 0.4)),
                    (sx - 9 * math.cos(angle + 0.4), sy - 9 * math.sin(angle + 0.4))]
            draw.polygon(head, fill=(255, 255, 120))
            _label(draw, ((gx + sx) / 2, (gy + sy) / 2), f"Q{quest_id}", (255, 255, 120), small)

    if diff_state is not None:
        current = dump_state(snapshot, spawns)
        for key, (x, z) in current["spawns"].items():
            if key not in diff_state.get("spawns", {}):
                px, py = to_px(x, z)
                draw.ellipse([px - 7, py - 7, px + 7, py + 7], outline=(60, 220, 60), width=2)
        for key, (x, z) in diff_state.get("spawns", {}).items():
            if key not in current["spawns"]:
                px, py = to_px(x, z)
                draw.line([px - 5, py - 5, px + 5, py + 5], fill=(230, 40, 40), width=2)
                draw.line([px - 5, py + 5, px + 5, py - 5], fill=(230, 40, 40), width=2)
        before = diff_state.get("entities", {})
        for key, (x, z) in current["entities"].items():
            if key not in before:
                px, py = to_px(x, z)
                draw.rectangle([px - 6, py - 6, px + 6, py + 6], outline=(60, 220, 60), width=2)
            elif math.hypot(before[key][0] - x, before[key][1] - z) > 0.5:
                draw.line([*to_px(*before[key]), *to_px(x, z)], fill=(255, 140, 0), width=2)
        for key, (x, z) in before.items():
            if key not in current["entities"]:
                px, py = to_px(x, z)
                draw.line([px - 5, py - 5, px + 5, py + 5], fill=(230, 40, 40), width=2)
                draw.line([px - 5, py + 5, px + 5, py - 5], fill=(230, 40, 40), width=2)

    _legend(image, height, scale, options, bbox)
    return image
```

- [ ] **Step 4: Implement the CLI `tools/world/render_map.py`**

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Render a top-down review map of a world (or of one zone / place).

    python tools/world/render_map.py --map 0 --out generated/world/review/map_0.png
    python tools/world/render_map.py --map 0 --zone "Briarwatch March" --out generated/world/review/briarwatch.png
    python tools/world/render_map.py --map 0 --poi barrowfield --margin 150 --dump-state generated/world/review/before.json --out before.png
    python tools/world/render_map.py --map 0 --poi barrowfield --margin 150 --diff generated/world/review/before.json --out after.png
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

from worldkit.cli_query import open_map
from worldkit.constants import CELL_SIZE
from worldkit.render import ALL_LAYERS, RenderOptions, dump_state, render_map
from worldkit.report import quest_links
from worldkit.spawns import object_spawn_records, unit_spawn_records


def zone_bbox(query, data, zone: str, margin: float):
    area_id = int(zone) if zone.isdigit() else next((z.id for z in data.zones.values() if z.name.casefold() == zone.casefold()), None)
    if area_id is None:
        raise SystemExit(f"unknown zone '{zone}'")
    rows, cols = np.nonzero(query.snapshot.area == area_id)
    if rows.size == 0:
        raise SystemExit(f"zone '{zone}' (area {area_id}) has no terrain on this map")
    ox, oz = query.snapshot.origin
    return (ox + cols.min() * CELL_SIZE - margin, oz + rows.min() * CELL_SIZE - margin,
            ox + (cols.max() + 1) * CELL_SIZE + margin, oz + (rows.max() + 1) * CELL_SIZE + margin)


def poi_bbox(query, poi_id: str, margin: float):
    poi = query.atlas.poi(poi_id) if query.atlas else None
    if poi is None:
        raise SystemExit(f"unknown place '{poi_id}' (is data/world/atlas/map_<id>.json present?)")
    if "radius" in poi:
        (cx, cz), r = poi["center"], poi["radius"]
        return cx - r - margin, cz - r - margin, cx + r + margin, cz + r + margin
    xs = [p[0] for p in poi["polygon"]]
    zs = [p[1] for p in poi["polygon"]]
    return min(xs) - margin, min(zs) - margin, max(xs) + margin, max(zs) + margin


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--map", type=int, default=0)
    area = parser.add_mutually_exclusive_group()
    area.add_argument("--zone", help="zone name or area id")
    area.add_argument("--poi", help="atlas place id")
    area.add_argument("--bbox", type=float, nargs=4, metavar=("X0", "Z0", "X1", "Z1"))
    parser.add_argument("--margin", type=float, default=50.0)
    parser.add_argument("--px-per-m", type=float)
    parser.add_argument("--layers", default=",".join(ALL_LAYERS))
    parser.add_argument("--color-by", choices=("level", "faction"), default="level")
    parser.add_argument("--diff", type=Path, help="state JSON from an earlier --dump-state")
    parser.add_argument("--dump-state", type=Path, help="write spawn/entity positions for a later --diff")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)

    data, map_entry, query = open_map(args.map)
    bbox = tuple(args.bbox) if args.bbox else None
    if args.zone:
        bbox = zone_bbox(query, data, args.zone, args.margin)
    elif args.poi:
        bbox = poi_bbox(query, args.poi, args.margin)
    layers = frozenset(l.strip() for l in args.layers.split(",") if l.strip())
    unknown = layers - set(ALL_LAYERS)
    if unknown:
        parser.error(f"unknown layers {sorted(unknown)}; choose from {ALL_LAYERS}")

    spawns = unit_spawn_records(map_entry) + object_spawn_records(map_entry)
    title = f"{map_entry.name}" + (f" - {args.zone or args.poi}" if (args.zone or args.poi) else "")
    image = render_map(query.snapshot, spawns=spawns, unit_levels=data.unit_levels(),
        unit_factions={uid: u.factionTemplate for uid, u in data.units.items()}, atlas=query.atlas, kinds=query.kinds,
        quest_arrows=quest_links(data, map_entry),
        options=RenderOptions(bbox=bbox, px_per_m=args.px_per_m, layers=layers, color_by=args.color_by, title=title),
        diff_state=json.loads(args.diff.read_text(encoding="utf-8")) if args.diff else None)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.out)
    print(f"wrote {args.out} ({image.width}x{image.height})")
    if args.dump_state:
        args.dump_state.parent.mkdir(parents=True, exist_ok=True)
        args.dump_state.write_text(json.dumps(dump_state(query.snapshot, spawns)), encoding="utf-8")
        print(f"wrote {args.dump_state}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 5: Run the tests, then render and look at the real map**

Run: `python tools/tests/test_world_render.py`
Expected: all PASS.

Run:
```powershell
python tools/world/render_map.py --map 0 --out generated/world/review/map_0.png
python tools/world/render_map.py --map 0 --zone "Briarwatch March" --out generated/world/review/briarwatch.png
```
Open both PNGs with the Read tool and check that:
- relief is visible;
- zones are tinted with borders;
- the Oakenshire props show as grey squares or brown WMO outlines;
- spawn dots cluster where the survey said (Oakenshire around x 100..467, z 400..833);
- quest arrows point from givers to targets;
- the legend is readable;
- +Z is up. The Oakenshire cluster (z ≈ 400..833) must sit in the *upper* part of the full map.

Fix anything that looks wrong before committing.

- [ ] **Step 6: Commit**

```bash
git add tools/world/worldkit/render.py tools/world/render_map.py tools/tests/test_world_render.py
git commit -m "feat(worldkit): top-down review map renderer with diff overlay

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 13: Content-audit integration and the initial baseline

**Files:**
- Modify: `tools/gate/content_audit.py`
- Create: `tools/world/lint_baseline.json` (generated)

**Interfaces:**
- Consumes: `tools/world/lint.py` and `tools/world/report.py` CLIs (JSON output `new_errors`, `new_warnings`, `known`).
- Produces audit domains `placement` (GLOBAL maps) and `reachability` (all maps). Each result is `{"checked": <maps>, "failures": [new errors], "warnings": [new warnings], "known": n, "maps": {...}}`, and it fails only when `failures` is non-empty.

- [ ] **Step 1: Add the domains**

In `tools/gate/content_audit.py`:

1. Extend the module docstring's first paragraph with: `It also runs the world placement lint (placement domain, GLOBAL maps) and the quest reachability report (reachability domain) from tools/world, both compared against tools/world/lint_baseline.json.`
2. Below `DATA = ...` add:
```python
WORLD_TOOLS = REPO / "tools" / "world"
MAP_GLOBAL = 0
```
3. Add after `audit_quest_chains`:
```python
def audit_world(tool: str, name: str, tmp_dir: Path) -> dict:
    """Runs tools/world/<tool>.py per map; only new errors (not in the baseline) are failures."""
    msg = proto_modules()["maps"].Maps()
    msg.ParseFromString((DATA / "maps.data").read_bytes())
    maps = [m for m in msg.entry if name != "placement" or m.instancetype == MAP_GLOBAL]
    result = {"checked": 0, "failures": [], "warnings": [], "known": 0, "maps": {}}
    for map_entry in maps:
        out = tmp_dir / f"{name}_{map_entry.id}.json"
        proc = subprocess.run([sys.executable, str(WORLD_TOOLS / f"{tool}.py"), "--map", str(map_entry.id), "--json", str(out)],
                              capture_output=True, text=True, cwd=str(WORLD_TOOLS))
        text = (proc.stdout + proc.stderr).strip()
        if not out.is_file():
            if "skipped map" in text:
                result["maps"][map_entry.id] = "skipped (no terrain)"
                continue
            result["failures"].append({"id": map_entry.id, "stage": tool, "output": text[-2000:]})
            continue
        data = json.loads(out.read_text(encoding="utf-8"))
        result["checked"] += 1
        result["known"] += data["known"]
        result["failures"] += [{"id": map_entry.id, **f} for f in data["new_errors"]]
        result["warnings"] += [{"id": map_entry.id, **w} for w in data["new_warnings"]]
        result["maps"][map_entry.id] = {"new_errors": len(data["new_errors"]), "new_warnings": len(data["new_warnings"]), "known": data["known"]}
    print(f"[{name}] maps {result['checked']}, new errors {len(result['failures'])}, new warnings {len(result['warnings'])}, known {result['known']}")
    return result
```
4. In `main()`: change the `--domain` choices and the default `selected` list to `[*DOMAINS, "questchain", "placement", "reachability", "xp"]`. Update the help text to say `default: all domains`. In the loop, add before the final `else`:
```python
            elif name == "placement":
                result = audit_world("lint", "placement", tmp_dir)
            elif name == "reachability":
                result = audit_world("report", "reachability", tmp_dir)
```

- [ ] **Step 2: Generate the baseline**

```powershell
python tools/world/lint.py --map 0 --update-baseline
python tools/world/lint.py --map 2 --update-baseline
python tools/world/report.py --map 0 --update-baseline
python tools/world/report.py --map 1 --update-baseline
python tools/world/report.py --map 2 --update-baseline
```
Expected: a line such as `baselined N placement violations for map 0` for each map (`skipped` for a map without terrain is fine). Then:
```powershell
python tools/world/lint.py --map 0; python tools/world/report.py --map 0
```
Expected: both exit 0 and report `0 new errors`.

Open `tools/world/lint_baseline.json` and skim it. Every entry must be a real, pre-existing problem, with no parser artefacts. Keep the counts per rule for the final report to the user.

- [ ] **Step 3: Run the two new audit domains**

Run: `python tools/gate/content_audit.py --domain placement --domain reachability`
Expected: `[placement] … new errors 0`, `[reachability] … new errors 0`, `Result: GREEN`.

- [ ] **Step 4: Commit**

```bash
git add tools/gate/content_audit.py tools/world/lint_baseline.json
git commit -m "feat(gate): content audit runs world placement lint and quest reachability

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 14: Atlas bootstrap for map 0 and the first review packet

**Files:**
- Create: `tools/world/worldkit/bootstrap.py`, `tools/world/bootstrap_atlas.py`
- Create (generated, then committed): `data/world/atlas/map_0.json`
- Test: `tools/tests/test_world_bootstrap.py`

**Interfaces:**
- Consumes: `GameData`, `WorldQuery`, `Atlas`/`empty_atlas`/`save_atlas`, spawn records, `quest_givers`.
- Produces `worldkit.bootstrap`: `KNOWN_PLACES`, `KNOWN_ROADS`, `cluster(points: list[tuple[float, float, object]], link: float) -> list[list[object]]`, `bootstrap_atlas(data, map_entry, query) -> Atlas`.
- Produces CLI `python tools/world/bootstrap_atlas.py --map <id> [--force]`. It refuses to overwrite an existing atlas without `--force`, because the user's edits live in that file.

The bootstrap is deterministic (sorted inputs, fixed thresholds). Everything it writes has `status: placeholder` and `source: agent-bootstrap`, plus an `ask`.

- [ ] **Step 1: Write the failing tests**

`tools/tests/test_world_bootstrap.py`:
```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the atlas bootstrap (placeholder places derived from spawns, givers and props).

	python tools/tests/test_world_bootstrap.py
"""

import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402

from worldkit.atlas import validate  # noqa: E402
from worldkit.bootstrap import bootstrap_atlas, cluster  # noqa: E402
from worldkit.data import GameData, load_proto_modules  # noqa: E402
from worldkit.query import WorldQuery  # noqa: E402
from worldkit.snapshot import build_snapshot  # noqa: E402


class BootstrapTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls._tmp = tempfile.TemporaryDirectory()
		repo = Path(cls._tmp.name)
		fx.make_world(repo, "B", {(32, 32): dict(areas=np.full((16, 16), 8, np.uint32))},
			entities=[dict(position=(400.0 + i * 3, 0.0, 400.0), unique_id=i + 1) for i in range(6)])
		snapshot = build_snapshot("B", repo=repo)
		m = load_proto_modules()
		zone = m["zones"].ZoneEntry(id=8, name="Briarwatch March")
		haldor = m["units"].UnitEntry(id=1, name="Farmer Haldor", minlevel=5, maxlevel=5)
		haldor.quests.append(10)
		skeleton = m["units"].UnitEntry(id=2, name="Barrow Skeleton", minlevel=8, maxlevel=9)
		quest = m["quests"].QuestEntry(id=10, name="Q", questlevel=6)
		map_entry = m["maps"].MapEntry(id=0, name="Dev", directory="B")
		map_entry.unitspawns.add(unitentry=1, positionx=100, positiony=0, positionz=100)
		for i in range(4):
			map_entry.unitspawns.add(unitentry=2, name=f"Barrowfield - Barrow Skeleton 0{i}", positionx=250 + i * 10, positiony=0, positionz=250)
		cls.data = GameData(units={1: haldor, 2: skeleton}, maps={0: map_entry}, quests={10: quest}, zones={8: zone},
			objects={}, items={}, unit_loot={}, object_loot={}, modules=m)
		cls.atlas = bootstrap_atlas(cls.data, map_entry, WorldQuery(snapshot))

	@classmethod
	def tearDownClass(cls):
		cls._tmp.cleanup()

	def test_valid_and_all_placeholders(self):
		self.assertEqual(validate(self.atlas.doc), [])
		for key in ("zones", "pois", "roads"):
			for entry in self.atlas.doc[key]:
				self.assertEqual(entry["status"], "placeholder")
				self.assertTrue(entry.get("ask"))

	def test_zone_band_from_quests(self):
		zone = self.atlas.zone(8)
		self.assertEqual((zone["name"], zone["levelMin"], zone["levelMax"]), ("Briarwatch March", 6, 6))

	def test_named_group_becomes_place(self):
		poi = self.atlas.poi_by_name("Barrowfield")
		self.assertEqual(poi["kind"], "camp")
		self.assertEqual(poi["center"], [265.0, 250.0])
		self.assertEqual((poi["levelMin"], poi["levelMax"]), (8, 9))

	def test_giver_hub_props_and_known_places(self):
		kinds = {p["kind"] for p in self.atlas.pois}
		self.assertIn("hub", kinds)
		self.assertIn("landmark", kinds)
		self.assertIsNotNone(self.atlas.poi_by_name("Forest Camp"))   # anchored on Farmer Haldor
		self.assertEqual(self.atlas.poi_by_name("Forest Camp")["center"], [100.0, 100.0])
		self.assertIsNotNone(self.atlas.poi_by_name("Haven"))
		self.assertEqual({r["name"] for r in self.atlas.roads}, {"Westroad", "Northroad"})

	def test_cluster(self):
		groups = cluster([(0, 0, "a"), (10, 0, "b"), (100, 0, "c")], 20.0)
		self.assertEqual(sorted(sorted(g) for g in groups), [["a", "b"], ["c"]])


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 2: Run to verify they fail**

Run: `python tools/tests/test_world_bootstrap.py`
Expected: FAIL with `ModuleNotFoundError: No module named 'worldkit.bootstrap'`.

- [ ] **Step 3: Implement `worldkit/bootstrap.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""One-shot atlas bootstrap: derives placeholder places from what the world already contains.

1. zones from area ids on the terrain (level band from the quests given there);
2. named places from spawn-name prefixes ("Barrowfield - Barrow Skeleton 07");
3. hubs from quest givers that are not inside a place yet;
4. unnamed creature clusters;
5. prop clusters (placed entities);
6. places and roads the user named in docs/world/setting-notes-2026-09-27.md, anchored on their NPCs
   when those exist and parked near the map centre otherwise.
Everything is a placeholder with an `ask`; the user names, moves and confirms it in mmo_edit.
"""

from __future__ import annotations

import math

from .atlas import Atlas, contains, empty_atlas
from .report import quest_givers
from .spawns import object_spawn_records, unit_spawn_records

DEFAULT_ASK = "Name this place, check its kind (hub/camp/ruin/lair/landmark/resource), level band and radius, then confirm."
KNOWN_PLACES = (
    {"name": "Forest Camp", "kind": "hub", "anchors": ("Farmer Haldor", "Healer Mirenna", "Guard Emrik"),
     "ask": "The camp where the human story begins (formal name not settled). Check position and radius, rename if you like, confirm."},
    {"name": "Haven", "kind": "hub", "anchors": ("Baldric Steelheart", "Armsmaster Theobald Kerrin"),
     "ask": "Haven, the walled town (~level 10 hub). Drag this pin to where Haven is or should be built and confirm; the road from Oakenshire is meant to be 500-1000 m long."},
    {"name": "Dungeon Entrance", "kind": "dungeon_entrance", "anchors": (),
     "ask": "Where is the group dungeon beyond the forest line entered? Drag and confirm, or delete the pin if still undecided."},
)
KNOWN_ROADS = (
    {"name": "Westroad", "direction": (-1.0, 0.0), "ask": "Drag the start and end of the Westroad, add points where it bends, then confirm."},
    {"name": "Northroad", "direction": (0.0, 1.0), "ask": "Drag the start and end of the Northroad, add points where it bends, then confirm."},
)
NAMED_MIN_RADIUS = 20.0
LINK_GIVERS = 60.0
LINK_CREATURES = 40.0
LINK_PROPS = 30.0
MIN_CREATURE_CLUSTER = 4
MIN_PROP_CLUSTER = 5


def cluster(points, link: float) -> list[list]:
    """Single-linkage clusters of (x, z, payload) triples; returns lists of payloads."""
    remaining = list(points)
    groups = []
    while remaining:
        frontier = [remaining.pop(0)]
        group = []
        while frontier:
            current = frontier.pop()
            group.append(current)
            near = [p for p in remaining if math.hypot(p[0] - current[0], p[1] - current[1]) <= link]
            for p in near:
                remaining.remove(p)
            frontier.extend(near)
        groups.append([p[2] for p in group])
    return groups


def _centre(xs, zs) -> list[float]:
    return [round(sum(xs) / len(xs), 1), round(sum(zs) / len(zs), 1)]


def _radius(centre, xs, zs, minimum: float) -> float:
    far = max(math.hypot(x - centre[0], z - centre[1]) for x, z in zip(xs, zs))
    return float(max(minimum, math.ceil((far + 10.0) / 5.0) * 5.0))


def _inside_any(atlas: Atlas, x: float, z: float) -> bool:
    return any(contains(p, x, z) for p in atlas.pois)


def bootstrap_atlas(data, map_entry, query) -> Atlas:
    atlas = empty_atlas(map_entry.id)
    units = unit_spawn_records(map_entry)
    objects = object_spawn_records(map_entry)
    levels = data.unit_levels()
    givers = quest_givers(data, map_entry)
    giver_units = {uid for uid, unit in data.units.items() if len(unit.quests)}

    # 1. zones
    quest_levels_by_area: dict[int, list[int]] = {}
    for quest_id, points in givers.items():
        quest = data.quests.get(quest_id)
        level = quest.questlevel if quest is not None else 0
        for x, z in points:
            area = query.area_at(x, z)
            if area and level > 0:
                quest_levels_by_area.setdefault(area, []).append(level)
    areas = sorted({int(a) for a in set(query.snapshot.area.flatten().tolist()) if a})
    for area in areas:
        zone = data.zones.get(area)
        fields = {"areaId": area, "name": zone.name if zone else f"Area {area}", "status": "placeholder",
                  "ask": "Confirm the level band and write a one-line theme and description.", "source": "agent-bootstrap"}
        band = quest_levels_by_area.get(area)
        if band:
            fields["levelMin"], fields["levelMax"] = min(band), max(band)
        atlas.add_zone(**fields)

    # 2. named groups
    by_prefix: dict[str, list] = {}
    for record in units + objects:
        if record.poi_prefix:
            by_prefix.setdefault(record.poi_prefix.casefold(), []).append(record)
    for key in sorted(by_prefix):
        group = by_prefix[key]
        xs, zs = [r.x for r in group], [r.z for r in group]
        centre = _centre(xs, zs)
        has_giver = any(r.kind == "unit" and r.entry in giver_units for r in group)
        unit_bands = [levels[r.entry] for r in group if r.kind == "unit" and r.entry in levels]
        fields = {"name": group[0].poi_prefix, "kind": "hub" if has_giver else "camp", "center": centre,
                  "radius": _radius(centre, xs, zs, NAMED_MIN_RADIUS), "status": "placeholder", "ask": DEFAULT_ASK,
                  "source": "agent-bootstrap"}
        area = query.area_at(*centre)
        if area:
            fields["areaId"] = area
        if unit_bands and not has_giver:
            fields["levelMin"] = min(b[0] for b in unit_bands)
            fields["levelMax"] = max(b[1] for b in unit_bands)
        atlas.add_poi(**fields)

    # 6a. known places anchored on their NPCs (before generic hubs, so they win the name)
    unit_ids_by_name = {unit.name: uid for uid, unit in data.units.items()}
    parked = 0
    x0, z0, x1, z1 = query.snapshot.extent
    for place in KNOWN_PLACES:
        if atlas.poi_by_name(place["name"]):
            continue
        anchor_ids = {unit_ids_by_name[n] for n in place["anchors"] if n in unit_ids_by_name}
        points = [(r.x, r.z) for r in units if r.entry in anchor_ids]
        if points:
            centre = _centre([p[0] for p in points], [p[1] for p in points])
            radius = _radius(centre, [p[0] for p in points], [p[1] for p in points], 30.0)
            ask = place["ask"]
        else:
            centre = [round((x0 + x1) / 2 + parked * 40.0, 1), round((z0 + z1) / 2, 1)]
            radius = 30.0
            parked += 1
            ask = "PARKED (position unknown). " + place["ask"]
        atlas.add_poi(name=place["name"], kind=place["kind"], center=centre, radius=radius, status="placeholder",
                      ask=ask, source="agent-bootstrap")

    # 3. quest-giver hubs
    loose_givers = [(r.x, r.z, r) for r in units if r.entry in giver_units and r.active and not _inside_any(atlas, r.x, r.z)]
    for group in cluster(sorted(loose_givers, key=lambda p: p[2].key), LINK_GIVERS):
        xs, zs = [r.x for r in group], [r.z for r in group]
        centre = _centre(xs, zs)
        area = query.area_at(*centre)
        zone = data.zones.get(area) if area else None
        first = data.units[group[0].entry].name
        name = f"{zone.name} hub" if zone and not atlas.poi_by_name(f"{zone.name} hub") else f"Hub near {first}"
        atlas.add_poi(name=name, kind="hub", center=centre, radius=_radius(centre, xs, zs, 25.0), status="placeholder",
                      ask=DEFAULT_ASK, source="agent-bootstrap")

    # 4. unnamed creature clusters
    loose = [(r.x, r.z, r) for r in units if r.active and r.entry not in giver_units and not _inside_any(atlas, r.x, r.z)]
    for group in cluster(sorted(loose, key=lambda p: p[2].key), LINK_CREATURES):
        if len(group) < MIN_CREATURE_CLUSTER:
            continue
        xs, zs = [r.x for r in group], [r.z for r in group]
        centre = _centre(xs, zs)
        counts: dict[int, int] = {}
        for r in group:
            counts[r.entry] = counts.get(r.entry, 0) + 1
        dominant = data.units.get(max(counts, key=counts.get))
        bands = [levels[r.entry] for r in group if r.entry in levels]
        atlas.add_poi(name=f"{dominant.name if dominant else 'Creature'} grounds", kind="camp", center=centre,
                      radius=_radius(centre, xs, zs, NAMED_MIN_RADIUS), levelMin=min(b[0] for b in bands) if bands else None,
                      levelMax=max(b[1] for b in bands) if bands else None, status="placeholder", ask=DEFAULT_ASK,
                      source="agent-bootstrap")

    # 5. prop clusters
    props = [(e.position[0], e.position[2], e) for e in query.snapshot.entities if not _inside_any(atlas, e.position[0], e.position[2])]
    for group in cluster(sorted(props, key=lambda p: p[2].unique_id), LINK_PROPS):
        if len(group) < MIN_PROP_CLUSTER:
            continue
        xs, zs = [e.position[0] for e in group], [e.position[2] for e in group]
        centre = _centre(xs, zs)
        atlas.add_poi(name=f"Props near ({centre[0]:.0f}, {centre[1]:.0f})", kind="landmark", center=centre,
                      radius=_radius(centre, xs, zs, 15.0), status="placeholder",
                      ask="What is this place? Name it, pick its kind and confirm.", source="agent-bootstrap")

    # 6b. named roads, starting at Oakenshire when it exists
    start = atlas.poi_by_name("Oakenshire")
    sx, sz = start["center"] if start else [round((x0 + x1) / 2, 1), round((z0 + z1) / 2, 1)]
    for road in KNOWN_ROADS:
        dx, dz = road["direction"]
        atlas.add_road(name=road["name"], points=[[sx, sz], [round(sx + dx * 300.0, 1), round(sz + dz * 300.0, 1)]],
                       status="placeholder", ask=road["ask"], source="agent-bootstrap")
    return atlas
```

Note: `add_poi(levelMin=None, …)` is fine because `_ordered` drops `None` values.

- [ ] **Step 4: Implement the CLI `tools/world/bootstrap_atlas.py`**

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Create the first atlas for a map from existing spawns, quest givers and props (all placeholders).

    python tools/world/bootstrap_atlas.py --map 0

Refuses to overwrite an existing atlas (it holds the user's names and confirmations) unless --force.
"""

from __future__ import annotations

import argparse
import sys

from worldkit.atlas import save_atlas
from worldkit.bootstrap import bootstrap_atlas
from worldkit.cli_query import open_map
from worldkit.paths import atlas_path


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--map", type=int, default=0)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args(argv)

    target = atlas_path(args.map)
    if target.exists() and not args.force:
        print(f"refusing to overwrite {target}; it may contain confirmed places. Use --force to replace it.")
        return 1
    data, map_entry, query = open_map(args.map)
    atlas = bootstrap_atlas(data, map_entry, query)
    save_atlas(atlas, target)
    print(f"wrote {target}: {len(atlas.zones)} zones, {len(atlas.pois)} places, {len(atlas.roads)} roads (all placeholders)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 5: Run the tests**

Run: `python tools/tests/test_world_bootstrap.py`
Expected: all PASS.

- [ ] **Step 6: Bootstrap map 0 and build the review packet**

```powershell
python tools/world/bootstrap_atlas.py --map 0
python tools/world/render_map.py --map 0 --out generated/world/review/atlas-bootstrap/map_0.png
python tools/world/render_map.py --map 0 --zone "Briarwatch March" --out generated/world/review/atlas-bootstrap/briarwatch.png
python tools/world/render_map.py --map 0 --zone "Oakenshire" --out generated/world/review/atlas-bootstrap/oakenshire.png
python tools/world/render_map.py --map 0 --zone "Falwyn Forest" --out generated/world/review/atlas-bootstrap/falwyn.png
python tools/world/report.py --map 0 --markdown generated/world/review/atlas-bootstrap/report.md
```
Look at every image. The places must sit on the clusters they came from, labels must be readable, and parked pins must say "PARKED". If a generic name is clearly wrong, fix it in `map_0.json` by hand; it stays a placeholder. Example: a hub named after a trainer where the spawns say "Kingsroad Waypost".

Then write `generated/world/review/atlas-bootstrap/README.md`, the review packet the user reads:
```markdown
# Atlas bootstrap review - map 0 (Development)

What this is: the first draft of `data/world/atlas/map_0.json`, generated from existing spawns, quest
givers and props. Every entry is a *placeholder* (orange in the images); nothing is canon until you
confirm it in mmo_edit's Atlas mode (World editor -> mode "Atlas" -> "Needs your input").

Images: map_0.png (whole map), falwyn.png, oakenshire.png, briarwatch.png.

## Places (<N>)
| id | name | kind | centre | radius | levels | question |
|---|---|---|---|---|---|---|
<one row per POI from map_0.json>

## Roads
<Westroad / Northroad rows, both parked as 300 m stubs from Oakenshire>

## Zones
<rows: areaId, name, levels, question>

## What I could not place
<list of PARKED entries with their ask>

## Findings worth your attention
<copy the "Zones without terrain" and top findings from report.md>
```
Fill every `<…>` from the actual data. Generate the tables with a short Python snippet that reads `map_0.json`; do not type them by hand.

- [ ] **Step 7: Validate and commit the atlas**

Run: `python -c "import sys; sys.path.insert(0,'tools/world'); from worldkit.atlas import load_atlas; a=load_atlas('data/world/atlas/map_0.json'); print(len(a.pois), 'places OK')"`
Expected: `<N> places OK`.
```bash
git add tools/world/worldkit/bootstrap.py tools/world/bootstrap_atlas.py tools/tests/test_world_bootstrap.py data/world/atlas/map_0.json
git commit -m "feat(world): bootstrap map 0 atlas (placeholders for user review)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 15: World bible v1

**Files:**
- Create: `tools/world/extract_canon.py`, `docs/world/bible.md`
- Test: `tools/tests/test_world_bootstrap.py` (add one smoke test)

**Interfaces:**
- Consumes: `load_game_data`, `quest_givers`, `unit_spawn_records`, `open_map`, the atlas.
- Produces `tools/world/extract_canon.py` with `build_canon_markdown(data, map_entry, query) -> str` and CLI `python tools/world/extract_canon.py --map 0 --out generated/world/canon_map_0.md`.
- Produces `docs/world/bible.md` (hand-written from sources, sections below).

- [ ] **Step 1: Write the failing smoke test**

Append to `tools/tests/test_world_bootstrap.py` (before `if __name__`):
```python
class CanonExtractTests(unittest.TestCase):
	def test_real_canon_mentions_first_quest(self):
		sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "world"))
		from extract_canon import build_canon_markdown
		from worldkit.cli_query import open_map
		data, map_entry, query = open_map(0)
		text = build_canon_markdown(data, map_entry, query)
		self.assertIn("The Boar Problem", text)
		self.assertIn("## Quests", text)
		self.assertIn("## Characters", text)
```

- [ ] **Step 2: Run to verify it fails**

Run: `python tools/tests/test_world_bootstrap.py CanonExtractTests`
Expected: FAIL with `ModuleNotFoundError: No module named 'extract_canon'`.

- [ ] **Step 3: Implement the extractor**

`tools/world/extract_canon.py`:
```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Dump the narrative content already in the game (quests, characters, places) as Markdown, grouped by
place, as source material for docs/world/bible.md.

    python tools/world/extract_canon.py --map 0 --out generated/world/canon_map_0.md
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from worldkit.cli_query import open_map
from worldkit.report import quest_givers
from worldkit.spawns import unit_spawn_records


def _where(query, x: float, z: float) -> str:
    pois = query.atlas.pois_at(x, z) if query.atlas else []
    area = query.area_at(x, z)
    zone = query.zone_names.get(area) if area else None
    return " / ".join(n for n in (zone, pois[0]["name"] if pois else None) if n) or f"({x:.0f}, {z:.0f})"


def _text(value: str) -> str:
    return " ".join((value or "").replace("$B", " ").split())


def build_canon_markdown(data, map_entry, query) -> str:
    lines = [f"# In-game canon: map {map_entry.id} ({map_entry.name})", "",
             "Generated by tools/world/extract_canon.py. Source material, not the bible.", ""]
    givers = quest_givers(data, map_entry)
    giver_names = {}
    for unit_id, unit in data.units.items():
        for quest_id in unit.quests:
            giver_names.setdefault(quest_id, []).append(unit.name)
    ender_names = {}
    for unit_id, unit in data.units.items():
        for quest_id in unit.end_quests:
            ender_names.setdefault(quest_id, []).append(unit.name)

    lines += ["## Quests", ""]
    for quest_id in sorted(data.quests):
        quest = data.quests[quest_id]
        where = _where(query, *givers[quest_id][0]) if quest_id in givers else "not given on this map"
        lines.append(f"### {quest_id}. {quest.name} (level {quest.questlevel})")
        lines.append(f"- Giver: {', '.join(giver_names.get(quest_id, ['-']))} - {where}")
        lines.append(f"- Turn in: {', '.join(ender_names.get(quest_id, ['-']))}")
        if quest.prevquestid or quest.nextquestid:
            lines.append(f"- Chain: prev {quest.prevquestid or '-'}, next {quest.nextquestid or '-'}")
        for label, value in (("Details", quest.detailstext), ("Objectives", quest.objectivestext),
                             ("Request", quest.requestitemstext), ("Reward text", quest.offerrewardtext)):
            if value:
                lines.append(f"- {label}: {_text(value)}")
        lines.append("")

    lines += ["## Characters", "", "| Unit | Name | Subname | Level | Where | Gives | Ends |", "|---|---|---|---|---|---|---|"]
    spawns_by_unit = {}
    for record in unit_spawn_records(map_entry):
        spawns_by_unit.setdefault(record.entry, []).append(record)
    for unit_id in sorted(data.units):
        unit = data.units[unit_id]
        records = spawns_by_unit.get(unit_id, [])
        where = _where(query, records[0].x, records[0].z) if records else "not spawned"
        if len(records) > 1:
            where += f" (+{len(records) - 1} more)"
        subname = getattr(unit, "subname", "")
        lines.append(f"| {unit_id} | {unit.name} | {subname} | {unit.minlevel}-{unit.maxlevel} | {where} | "
                     f"{', '.join(map(str, unit.quests)) or '-'} | {', '.join(map(str, unit.end_quests)) or '-'} |")

    lines += ["", "## Quest items", ""]
    quest_items = sorted({r.itemid for q in data.quests.values() for r in q.requirements if r.itemid})
    for item_id in quest_items:
        item = data.items.get(item_id)
        lines.append(f"- {item_id}: {item.name if item else '?'}" + (f" - {_text(item.description)}" if item is not None and getattr(item, "description", "") else ""))

    lines += ["", "## Zones", ""]
    for zone_id in sorted(data.zones):
        zone = data.zones[zone_id]
        parent = data.zones.get(zone.parentzone).name if zone.parentzone in data.zones else "-"
        lines.append(f"- {zone_id}: {zone.name} (parent: {parent})")
    return "\n".join(lines) + "\n"


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--map", type=int, default=0)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)
    data, map_entry, query = open_map(args.map)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(build_canon_markdown(data, map_entry, query), encoding="utf-8")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

If `UnitEntry` has no `subname` field, the `getattr` default covers it. The same goes for `ItemEntry.description`. Check `units.proto`/`items.proto` for the real field names (for example `subname` or `description`) and use them.

- [ ] **Step 4: Run the test and produce the canon dump**

Run: `python tools/tests/test_world_bootstrap.py CanonExtractTests`
Expected: PASS.

Run: `python tools/world/extract_canon.py --map 0 --out generated/world/canon_map_0.md`
Read the whole output file.

- [ ] **Step 5: Write `docs/world/bible.md`**

Sources: `docs/world/setting-notes-2026-09-27.md` (layer E), `generated/world/canon_map_0.md` (layer C), `data/world/atlas/map_0.json` (place ids). Tag every factual statement at its end with its layer: **[E]** established (user notes), **[C]** in-game canon (existing data), **[?]** open question. Use exactly these sections:

1. **How to use this bible.** The three layers. Only the user promotes to [E]. Agents may add [C] entries when they ship content (with quest/unit ids). Contradictions go to section 12.
2. **Identity and tone.** Genre, grounded lived-in feel, the stylized hand-painted look, the sense of scale and travel. [E]
3. **Geography.** One subsection per zone and per atlas place with content. Each gives its atlas id, level band, what it is, who lives there, and what threatens it. Mark places that exist only in notes, or only as placeholders, as [?].
4. **Peoples and factions.** Humans of Falwyn, guards and militia, bandits, kobolds, the restless dead, wildlife. Only mechanics-backed facts are [C].
5. **Named characters.** A table with columns name, role, location (atlas id), unit id, quests given/ended, and layer. Include every quest giver from the canon dump.
6. **Creatures and threats.** Per creature family: where it lives, level range, and what it means in the story.
7. **Classes in the fiction.** Warrior, Mage, Cleric, Scout, Shadowmancer and their trainers. [E]/[?]
8. **Objects and motifs.** Silverleaf, letters, crates, carts, and so on.
9. **Naming guide.** Derive it from the existing names and state it as rules with examples:
   - person names (given name + family name or epithet, and role prefixes such as "Farmer", "Healer", "Guard");
   - place names (English compound names such as Oakenshire, Briarwatch, Barrowfield, Mirewater);
   - item names;
   - spawn names `<Place> - <Unit name> NN`;
   - what to avoid: modern words, puns, real-world names, digits in unit names.
10. **Quest-writing guide.** Cover:
    - voice (plain, grounded, second person, short sentences);
    - text lengths (details 2–5 sentences, objectives 1 sentence, reward text 1–3 sentences), with 2 good examples quoted from existing quests (≤ 15 words each);
    - the arc "local problem → road → Haven";
    - **every quest ends with a turn-in at an NPC or object (never AutoRewarded)** [E];
    - quest rewards and collect objectives are never grey items;
    - design against the atlas: every objective sits in a named place within about 250 m of its hub.
11. **Travel and scale.** [E]/[?]
12. **Open questions and contradictions.** The user's gap table. Also:
    - every conflict between notes and data, for example a quest text calling a place something else, or an NPC in the notes that does not exist in the data;
    - world facts from the report: zones without terrain (Haven, Market, Mage Tower, Greystone Pass), the 95% of map 0 without a zone.

Acceptance check before committing:
- No sentence without a layer tag.
- Every character in the table has a unit id or is marked [?] "not in data".
- Section 12 lists at least every "Major gaps" row from the notes.
- Quotes from quest text are 15 words or fewer.

- [ ] **Step 6: Commit**

```bash
git add tools/world/extract_canon.py tools/tests/test_world_bootstrap.py docs/world/bible.md
git commit -m "docs(world): world bible v1 from setting notes and in-game canon

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 16: World-aware content skills

**Files:**
- Create: `tools/world/worldkit/skill_checks.py`, `tools/world/README.md`
- Modify: `.agents/skills/mmo-npc-designer/scripts/validate_npc_json.py` (`main`), `.agents/skills/mmo-quest-creator/scripts/validate_quest_json.py` (`validate_document` + `main`), `.agents/skills/mmo-quest-creator/SKILL.md`, `.agents/skills/mmo-npc-designer/SKILL.md`, `.agents/skills/mmo-npc-designer/workflows/apply-and-spawn-npc.md`, `.agents/skills/mmo-quest-creator/workflows/build-or-extend-questline.md`, `.agents/skills/mmo-quest-creator/references/questline-design-guide.md` (header note)
- Test: `tools/tests/test_world_lint.py` (add class)

**Interfaces:**
- Consumes: `records_from_npc_draft`, `lint_records`, `naming_violations`, `load_baseline`, `open_map`, `unit_spawn_records`, `NoTerrainError`.
- Produces `worldkit.skill_checks`: `npc_draft_findings(doc: dict) -> tuple[list[str], list[str]]` (errors, warnings); `quest_draft_warnings(doc: dict) -> list[str]`.

- [ ] **Step 1: Write the failing test**

Append to `tools/tests/test_world_lint.py` (before `if __name__`):
```python
from worldkit.skill_checks import npc_draft_findings, quest_draft_warnings  # noqa: E402


class SkillCheckTests(unittest.TestCase):
	def test_npc_draft_on_real_map(self):
		doc = {"unit": {"id": 999001, "minlevel": 5, "maxlevel": 6}, "spawns": [
			{"map_id": 0, "spawn": {"unitentry": 999001, "name": "Bog Rat", "positionx": -330.0, "positiony": -500.0, "positionz": 230.0}}]}
		errors, warnings = npc_draft_findings(doc)
		self.assertTrue(any("below the ground" in e for e in errors), errors)
		self.assertTrue(any("spawn name" in w for w in warnings), warnings)

	def test_quest_draft_without_level_is_quiet(self):
		self.assertEqual(quest_draft_warnings({"quest": {"id": 1}}), [])
```

- [ ] **Step 2: Run to verify it fails**

Run: `python tools/tests/test_world_lint.py SkillCheckTests`
Expected: FAIL with `ModuleNotFoundError: No module named 'worldkit.skill_checks'`.

- [ ] **Step 3: Implement `worldkit/skill_checks.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""World checks used by the content skills' validators (validate_npc_json.py, validate_quest_json.py).

Placement errors that are already in the lint baseline are downgraded to warnings, so exporting and
re-validating existing content (the weekly content audit does this for every NPC) never fails on
known legacy problems, while a new draft cannot introduce new ones.
"""

from __future__ import annotations

from .baseline import load_baseline
from .cli_query import open_map
from .lint import lint_records, naming_violations
from .snapshot import NoTerrainError
from .spawns import records_from_npc_draft, unit_spawn_records

_MAPS: dict[int, tuple] = {}


def _map(map_id: int):
    if map_id not in _MAPS:
        _MAPS[map_id] = open_map(map_id)
    return _MAPS[map_id]


def npc_draft_findings(doc: dict) -> tuple[list[str], list[str]]:
    records = records_from_npc_draft(doc)
    errors: list[str] = []
    warnings: list[str] = []
    if not records:
        return errors, warnings
    baseline = load_baseline("placement")
    unit = doc.get("unit", {})
    for map_id in sorted({r.map_id for r in records}):
        try:
            data, _, query = _map(map_id)
        except (NoTerrainError, SystemExit) as exc:
            warnings.append(f"world placement checks skipped for map {map_id}: {exc}")
            continue
        levels = data.unit_levels()
        if "id" in unit and "minlevel" in unit:
            lo = int(unit["minlevel"])
            levels[int(unit["id"])] = (lo, max(lo, int(unit.get("maxlevel", lo))))
        map_records = [r for r in records if r.map_id == map_id]
        for violation in lint_records(map_records, query, levels) + naming_violations(map_records):
            if violation.key in baseline:
                warnings.append(f"{violation.message} (known issue, baselined)")
            elif violation.severity == "error":
                errors.append(f"placement: {violation.message}")
            else:
                warnings.append(f"placement: {violation.message}")
        if query.atlas is None:
            warnings.append(f"map {map_id} has no atlas (data/world/atlas/map_{map_id}.json); place/level checks skipped")
    return errors, warnings


def quest_draft_warnings(doc: dict) -> list[str]:
    quest = doc.get("quest", {})
    level = quest.get("questlevel") or quest.get("minlevel")
    provider_ids = set((doc.get("providers") or {}).get("unit_ids") or [])
    if not level or not provider_ids:
        return []
    warnings: list[str] = []
    data, _, _ = _map(0)
    for map_id, map_entry in sorted(data.maps.items()):
        spawns = [r for r in unit_spawn_records(map_entry) if r.entry in provider_ids]
        if not spawns:
            continue
        try:
            _, _, query = _map(map_id)
        except (NoTerrainError, SystemExit):
            continue
        if query.atlas is None:
            continue
        for record in spawns:
            band = query.atlas.band_for(record.x, record.z, query.area_at(record.x, record.z))
            if band and not band[0] <= int(level) <= band[1]:
                warnings.append(f"quest level {level} is outside the {band[2]} band {band[0]}-{band[1]} where provider "
                                f"{data.units[record.entry].name} stands (map {map_id}, {record.x:.0f}, {record.z:.0f})")
    return sorted(set(warnings))
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `python tools/tests/test_world_lint.py SkillCheckTests`
Expected: PASS. The Mirewater camp at (-330, 230) is on terrain, and y = -500 is far below ground.

- [ ] **Step 5: Call the checks from the validators**

In `.agents/skills/mmo-npc-designer/scripts/validate_npc_json.py`, add below the imports:
```python
def world_checks(doc: dict) -> tuple[list[str], list[str]]:
    """Terrain/atlas placement checks from tools/world (never blocks validation if the tooling itself fails)."""
    try:
        sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools" / "world"))
        from worldkit.skill_checks import npc_draft_findings
        return npc_draft_findings(doc)
    except Exception as exc:  # noqa: BLE001 - tooling failure must surface as a warning, not a crash
        return [], [f"world placement checks skipped: {exc}"]
```
and in `main()` replace:
```python
    errors = validate_document(doc, project_root)
    if errors:
```
with:
```python
    errors = validate_document(doc, project_root)
    world_errors, world_warnings = world_checks(doc)
    errors.extend(world_errors)
    for message in world_warnings:
        print(f"WARNING: {message}")
    if errors:
```

In `.agents/skills/mmo-quest-creator/scripts/validate_quest_json.py`:
- In `validate_document`, right after the `quest.type` check (around line 85), add:
```python
    if quest_message.flags & 0x0020:
        add_error(errors, "quest.flags sets AutoRewarded (0x20); every quest must end with a turn-in at an NPC or object (docs/world/bible.md, quest-writing guide)")
```
- Add below the imports:
```python
def world_warnings(doc: dict) -> list[str]:
    try:
        sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools" / "world"))
        from worldkit.skill_checks import quest_draft_warnings
        return quest_draft_warnings(doc)
    except Exception as exc:  # noqa: BLE001
        return [f"world level-band checks skipped: {exc}"]
```
- In `main()`, change `errors, warnings = validate_document(doc, project_root)` to:
```python
    errors, warnings = validate_document(doc, project_root)
    warnings.extend(world_warnings(doc))
```

Check that both files already import `sys` and `Path`. If not, add `import sys` and `from pathlib import Path`.

- [ ] **Step 6: Teach the skills to design against the world**

Add this section to `.agents/skills/mmo-quest-creator/SKILL.md`, directly after `</essential_principles>`:
```markdown
<world_aware_design>
Design against the real world, never in a vacuum. Before designing new quest content:

1. Read `docs/world/bible.md`: identity and tone, the geography section of the target zone, named
   characters, the naming guide and the quest-writing guide. Reuse established characters and places.
2. Look up the target zone and places in `data/world/atlas/map_<id>.json`. Do not invent a location
   that is not in the atlas. If the content needs a new place, add it to the atlas as a
   `placeholder` POI with an `ask` for the user (source `agent`) and say so in the review packet.
   Only the user promotes anything to `canon`.
3. Render the area and look at it before choosing positions:
   `python tools/world/render_map.py --map 0 --zone "<Zone>" --dump-state generated/world/review/<slug>/before.json --out generated/world/review/<slug>/before.png`
   Use `cd tools/world; python -m worldkit query --map 0 --at X Z` for any single point.
4. Every objective sits in a named place within ~250 m of its hub; every quest ends with a turn-in
   at an NPC or object (the validator rejects `AutoRewarded`).
5. After applying, run `python tools/world/report.py --map 0` (no new errors allowed) and render the
   after-map with `--diff generated/world/review/<slug>/before.json`. Hand the user a review packet:
   `generated/world/review/<slug>/README.md` with the brief, the before/after images and the report.
</world_aware_design>
```

Add the same section to `.agents/skills/mmo-npc-designer/SKILL.md` after `</essential_principles>`, with step 4 replaced by:
```markdown
4. Place spawns like a level designer: packs of 3-6 scattered irregularly around a place's features
   (never on a grid), mixed stationary/random movement, a few patrols along roads, varied respawn
   delays, names `<Place> - <Unit name> NN`. Check every draft with
   `python tools/world/lint.py --map 0 --draft <draft.json>`; the validator fails on new placement
   errors (buried, floating, water, holes, cliffs, terrain edge).
```

In `.agents/skills/mmo-npc-designer/workflows/apply-and-spawn-npc.md`, replace the step that says to inspect terrain height with `inspect_terrain.py` with:
```markdown
- Choose spawn positions from the rendered area map and the atlas place (see SKILL.md
  <world_aware_design>). Use `inspect_terrain.py` or `python -m worldkit query` for exact heights;
  both report slope, water, holes and whether the point is placeable.
- Run `python tools/world/lint.py --map <id> --draft <draft.json>` and fix every error before apply.
```
In `.agents/skills/mmo-quest-creator/workflows/build-or-extend-questline.md`, add at the top of the steps:
```markdown
0. Follow SKILL.md <world_aware_design>: bible, atlas, before-render. Plan the questline on the map
   (hub, objective places, flow arrows) and include that plan in the review packet before applying.
```
At the top of `references/questline-design-guide.md`, add:
`> Structural background only (WoW TBC case study). For Alestia's tone, names and rules, docs/world/bible.md wins.`

- [ ] **Step 7: Write `tools/world/README.md`**

````markdown
# worldkit - world knowledge for content tooling

Read-only model of the game world for agents and scripts: terrain (height, slope, water, holes,
road layers), placed props, spawns, quest links, the atlas of named places, and lint/report checks.

Python: `python` (the gate's interpreter). If the Windows Store `python` alias does not launch
(common inside Claude Code), use `py -3.14` with `pip install --user -r tools/world/requirements.txt`.

| Command | What it does |
|---|---|
| `cd tools/world; python -m worldkit query --map 0 --at X Z` | everything about one point, incl. placeable + reasons |
| `cd tools/world; python -m worldkit layers --world Development --page 32_32 --out x.png` | splat layer preview (for terrain_kinds.json) |
| `python tools/world/render_map.py --map 0 [--zone Z | --poi ID | --bbox ...] --out x.png` | review map (`--dump-state` / `--diff` for before/after) |
| `python tools/world/lint.py --map 0 [--draft npc.json]` | placement lint vs baseline |
| `python tools/world/report.py --map 0 --markdown r.md` | quest reachability, turn-in rule, spawn/level distribution |
| `python tools/world/bootstrap_atlas.py --map N` | first atlas for a new map (refuses to overwrite) |
| `python tools/world/extract_canon.py --map 0 --out c.md` | quest/NPC text dump for the bible |

Files: atlas `data/world/atlas/map_<id>.json` (edit as text or in mmo_edit's Atlas mode), bible
`docs/world/bible.md`, known-issue baseline `tools/world/lint_baseline.json`, terrain kinds
`tools/world/worldkit/terrain_kinds.json`, snapshot cache `generated/world/<World>/`.

Review packets live in `generated/world/review/<slug>/`: `README.md` (brief), before/after PNGs,
`report.md`.

Tests: `python -m unittest discover -s tools/tests -p "test_world_*.py"`.
````

- [ ] **Step 8: Verify end to end and commit**

```powershell
python -m unittest discover -s tools/tests -p "test_world_*.py"
python tools/tests/test_skills_hygiene.py
python .agents/skills/mmo-npc-designer/scripts/export_npc_json.py --unit-id 10 --output generated/npcs/audit_10.json
python .agents/skills/mmo-npc-designer/scripts/validate_npc_json.py generated/npcs/audit_10.json
python .agents/skills/mmo-quest-creator/scripts/export_quest_json.py --quest-id 22 --output generated/quests/audit_22.json
python .agents/skills/mmo-quest-creator/scripts/validate_quest_json.py generated/quests/audit_22.json
python tools/gate/content_audit.py --domain npc --domain quest --limit 5
```
Expected:
- The tests pass.
- Both validators print `OK:`, possibly with `WARNING:` lines.
- The audit smoke run is `GREEN` for npc and quest.
```bash
git add tools/world/worldkit/skill_checks.py tools/world/README.md tools/tests/test_world_lint.py .agents/skills
git commit -m "feat(skills): quest/NPC skills design against atlas, bible and placement lint

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 17: mmo_edit Atlas edit mode

**Files:**
- Create: `src/mmo_edit/editors/world_editor/edit_modes/atlas_edit_mode.h`, `src/mmo_edit/editors/world_editor/edit_modes/atlas_edit_mode.cpp`
- Modify: `src/mmo_edit/editors/world_editor/edit_modes/world_edit_mode.h` (add `FocusWorldPosition` to `IWorldEditor`), `src/mmo_edit/editors/world_editor/world_editor_instance.h` (member + override), `src/mmo_edit/editors/world_editor/world_editor_instance.cpp` (construction near line 380, both `availableModes` arrays that list `m_fogVolumeEditMode.get()`, the camera-drag condition near line 877, `FocusWorldPosition` next to `FocusSelection` at line 1787)

**Interfaces:**
- Consumes: `IWorldEditor::GetCamera()`, `GetTerrain()`, `HasTerrain()`; `terrain::Terrain::RayIntersects(const Ray&)`, `TryGetSmoothHeightAt(float, float, float&)`; `Camera::GetCameraToViewportRay(float, float, float)`, `GetProjectionMatrix()`, `GetViewMatrix()`; `proto::Project::getLastPath()` (= `../../data/editor/data` relative to the exe); nlohmann `ordered_json`; `imgui_stdlib` `ImGui::InputText(const char*, std::string*)`.
- Produces `class AtlasEditMode final : public WorldEditMode` with `IsDragging() const -> bool`; `IWorldEditor::FocusWorldPosition(const Vector3&)`.

The atlas path is `std::filesystem::path(projectPath) / ".." / ".." / "world" / "atlas" / ("map_" + id + ".json")`, which resolves to `data/world/atlas/map_<id>.json`. The JSON is edited in place as an `ordered_json`, so unknown fields and key order survive. Floats are written rounded to 0.1 (`std::round(v * 10.0) / 10.0`) and the output is `dump(2) + "\n"`. That matches `worldkit.atlas.dumps` byte for byte for values written by either side.

- [ ] **Step 1: Add `FocusWorldPosition` to `IWorldEditor`**

In `world_edit_mode.h`, after `virtual void FocusSelection() = 0;` add:
```cpp
		/// @brief Moves the camera to focus on a world position (e.g. a place picked from a list).
		/// @param position The world position to focus.
		virtual void FocusWorldPosition(const Vector3& position) = 0;
```
In `world_editor_instance.h` declare `void FocusWorldPosition(const Vector3& position) override;` next to `FocusSelection`. In `world_editor_instance.cpp`, after `FocusSelection()` (line 1798), add:
```cpp
	void WorldEditorInstance::FocusWorldPosition(const Vector3& position)
	{
		m_cameraAnchor->SetPosition(position);
		m_cameraVelocity = Vector3::Zero;
	}
```

- [ ] **Step 2: Write the header**

`src/mmo_edit/editors/world_editor/edit_modes/atlas_edit_mode.h`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_edit_mode.h"

#include "nlohmann/json.hpp"

#include <filesystem>
#include <functional>
#include <limits>
#include <vector>

namespace mmo
{
	namespace proto
	{
		class MapEntry;
	}

	/// @brief World editor mode for the tool-side world atlas (data/world/atlas/map_<id>.json).
	/// @details Draws the atlas' named places (pins with radius rings or outlines) and roads as a viewport
	///          overlay. Places and road points can be dragged across the terrain; places can be added,
	///          edited, confirmed (placeholder -> canon) and deleted. The file is saved explicitly and is
	///          editor-only data: it is never exported to the client, the servers or the ClientDB. Unknown
	///          JSON fields are preserved so fields written by the Python tooling survive a round trip.
	class AtlasEditMode final : public WorldEditMode
	{
	public:
		/// @brief Creates the atlas edit mode.
		/// @param worldEditor The owning world editor.
		/// @param projectPath The editor project data path (proto::Project::getLastPath(), i.e. data/editor/data).
		/// @param mapEntryProvider Returns the map currently being edited, or nullptr if none is selected.
		AtlasEditMode(IWorldEditor& worldEditor, std::filesystem::path projectPath, std::function<const proto::MapEntry*()> mapEntryProvider);

		~AtlasEditMode() override = default;

	public:
		/// @copydoc WorldEditMode::GetName
		const char* GetName() const override;

		/// @copydoc WorldEditMode::DrawDetails
		void DrawDetails() override;

		/// @brief Loads the atlas of the current map.
		void OnActivate() override;

		/// @brief Places a new pin (when placing) or starts dragging the handle under the cursor.
		void OnMouseDown(float x, float y) override;

		/// @brief Moves the dragged handle to the terrain point under the cursor.
		void OnMouseMoved(float x, float y) override;

		/// @brief Ends a drag.
		void OnMouseUp(float x, float y) override;

		/// @copydoc WorldEditMode::DrawViewportOverlay
		void DrawViewportOverlay(ImDrawList* drawList, const ImVec2& viewportMin, const ImVec2& viewportSize) override;

		/// @brief Whether a place or road point is being dragged (the camera must not rotate meanwhile).
		[[nodiscard]] bool IsDragging() const { return m_drag.type != HandleType::None; }

	private:
		enum class HandleType
		{
			None,
			Poi,
			RoadPoint
		};

		struct Handle
		{
			HandleType type = HandleType::None;
			int index = -1;
			int pointIndex = -1;
		};

		struct ScreenHandle
		{
			Handle handle;
			float x = 0.0f;
			float y = 0.0f;
		};

		/// @brief (Re)loads the atlas file of the current map. Returns false and sets m_loadError on failure.
		bool Load();

		/// @brief Writes the atlas file. Returns false if nothing was loaded or the write failed.
		bool Save();

		/// @brief Path of the current map's atlas file.
		std::filesystem::path GetAtlasPath(uint32 mapId) const;

		/// @brief Intersects the view ray through a normalized viewport position with the terrain.
		bool RaycastTerrain(float viewportX, float viewportY, Vector3& outPosition) const;

		/// @brief Terrain height at a world position, or 0 when no terrain is loaded there.
		float GroundHeight(float worldX, float worldZ) const;

		/// @brief The handle drawn closest to a normalized viewport position (within 10 px), if any.
		Handle PickHandle(float viewportX, float viewportY) const;

		/// @brief Moves a place (with its polygon) or a road point to a world position.
		void MoveHandle(const Handle& handle, float worldX, float worldZ);

		/// @brief Appends a new user-authored place at a world position and selects it.
		void AddPoi(float worldX, float worldZ);

		/// @brief Returns a lower_snake_case id derived from name that no place uses yet.
		String UniquePoiId(const String& name) const;

		void DrawPoiProperties(nlohmann::ordered_json& poi);
		void DrawRoadProperties(nlohmann::ordered_json& road);
		void DrawNeedsInput();

	private:
		std::filesystem::path m_projectPath;
		std::function<const proto::MapEntry*()> m_mapEntryProvider;
		nlohmann::ordered_json m_atlas;
		uint32 m_loadedMapId = std::numeric_limits<uint32>::max();
		String m_loadError;
		bool m_dirty = false;
		bool m_placing = false;
		int m_placeKind = 0;
		Handle m_selected;
		Handle m_drag;
		std::vector<ScreenHandle> m_screenHandles;
		float m_viewportMinX = 0.0f;
		float m_viewportMinY = 0.0f;
		float m_viewportWidth = 1.0f;
		float m_viewportHeight = 1.0f;
	};
}
```

- [ ] **Step 3: Write the implementation**

`src/mmo_edit/editors/world_editor/edit_modes/atlas_edit_mode.cpp`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "atlas_edit_mode.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include "log/default_log_levels.h"
#include "math/matrix4.h"
#include "math/ray.h"
#include "math/vector3.h"
#include "math/vector4.h"
#include "proto_data/maps.pb.h"
#include "scene_graph/camera.h"
#include "terrain/terrain.h"

namespace mmo
{
	namespace
	{
		using Json = nlohmann::ordered_json;

		constexpr const char* s_poiKinds[] = { "hub", "camp", "ruin", "lair", "landmark", "resource", "dungeon_entrance", "note" };
		constexpr float s_pickRadiusPixels = 10.0f;
		constexpr int s_ringSegments = 48;

		double Round1(const double value)
		{
			return std::round(value * 10.0) / 10.0;
		}

		ImU32 StatusColor(const Json& entry)
		{
			const String status = entry.value("status", String());
			if (status == "canon")
			{
				return IM_COL32(255, 255, 255, 255);
			}
			if (status == "note")
			{
				return IM_COL32(255, 64, 255, 255);
			}
			return IM_COL32(255, 165, 0, 255);
		}

		bool ReadPoint(const Json& value, float& outX, float& outZ)
		{
			if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number())
			{
				return false;
			}
			outX = value[0].get<float>();
			outZ = value[1].get<float>();
			return true;
		}

		Json MakePoint(const float x, const float z)
		{
			return Json::array({ Round1(x), Round1(z) });
		}

		bool EditText(Json& entry, const char* key, const char* label, const bool multiline, const bool required)
		{
			String value = entry.contains(key) && entry[key].is_string() ? entry[key].get<String>() : String();
			const bool changed = multiline ? ImGui::InputTextMultiline(label, &value, ImVec2(-1.0f, 60.0f)) : ImGui::InputText(label, &value);
			if (!changed)
			{
				return false;
			}

			if (value.empty() && !required)
			{
				entry.erase(key);
			}
			else if (!value.empty())
			{
				entry[key] = value;
			}
			return true;
		}

		bool EditLevels(Json& entry)
		{
			bool hasBand = entry.contains("levelMin") && entry.contains("levelMax");
			bool changed = false;
			if (ImGui::Checkbox("Level band", &hasBand))
			{
				changed = true;
				if (hasBand)
				{
					entry["levelMin"] = 1;
					entry["levelMax"] = 1;
				}
				else
				{
					entry.erase("levelMin");
					entry.erase("levelMax");
				}
			}

			if (hasBand)
			{
				int band[2] = { entry["levelMin"].get<int>(), entry["levelMax"].get<int>() };
				if (ImGui::InputInt2("Levels (min, max)", band))
				{
					band[0] = std::max(1, band[0]);
					band[1] = std::max(band[0], band[1]);
					entry["levelMin"] = band[0];
					entry["levelMax"] = band[1];
					changed = true;
				}
			}
			return changed;
		}

		bool DrawConfirmAndStatus(Json& entry)
		{
			const String status = entry.value("status", String());
			ImGui::Text("Status: %s", status.c_str());
			if (entry.contains("ask") && entry["ask"].is_string())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 165, 0, 255));
				ImGui::TextWrapped("Question: %s", entry["ask"].get<String>().c_str());
				ImGui::PopStyleColor();
			}

			if (status == "placeholder")
			{
				if (ImGui::Button("Confirm (make canon)"))
				{
					entry["status"] = "canon";
					entry.erase("ask");
					return true;
				}
				ImGui::SameLine();
				ImGui::TextDisabled("You checked name, position and facts.");
			}
			return false;
		}
	}

	AtlasEditMode::AtlasEditMode(IWorldEditor& worldEditor, std::filesystem::path projectPath, std::function<const proto::MapEntry*()> mapEntryProvider)
		: WorldEditMode(worldEditor)
		, m_projectPath(std::move(projectPath))
		, m_mapEntryProvider(std::move(mapEntryProvider))
	{
	}

	const char* AtlasEditMode::GetName() const
	{
		return "Atlas";
	}

	void AtlasEditMode::OnActivate()
	{
		WorldEditMode::OnActivate();
		if (!m_dirty)
		{
			Load();
		}
	}

	std::filesystem::path AtlasEditMode::GetAtlasPath(const uint32 mapId) const
	{
		return (m_projectPath / ".." / ".." / "world" / "atlas" / ("map_" + std::to_string(mapId) + ".json")).lexically_normal();
	}

	bool AtlasEditMode::Load()
	{
		m_atlas = Json();
		m_loadError.clear();
		m_dirty = false;
		m_selected = Handle();
		m_drag = Handle();

		const proto::MapEntry* mapEntry = m_mapEntryProvider ? m_mapEntryProvider() : nullptr;
		if (!mapEntry)
		{
			m_loadError = "Select the map in the World Settings panel first.";
			return false;
		}

		m_loadedMapId = mapEntry->id();
		const std::filesystem::path path = GetAtlasPath(m_loadedMapId);
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			// No atlas yet: start an empty one, written on the first save.
			m_atlas = Json::object();
			m_atlas["map"] = m_loadedMapId;
			m_atlas["version"] = 1;
			m_atlas["zones"] = Json::array();
			m_atlas["pois"] = Json::array();
			m_atlas["roads"] = Json::array();
			return true;
		}

		const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		m_atlas = Json::parse(text, nullptr, false);
		if (m_atlas.is_discarded() || !m_atlas.is_object() || !m_atlas.contains("pois") || !m_atlas["pois"].is_array() ||
			!m_atlas.contains("roads") || !m_atlas["roads"].is_array())
		{
			m_atlas = Json();
			m_loadError = "Could not read " + path.string() + ". Fix the file by hand; the editor will not overwrite it.";
			ELOG(m_loadError);
			return false;
		}

		return true;
	}

	bool AtlasEditMode::Save()
	{
		if (!m_loadError.empty() || !m_atlas.is_object())
		{
			return false;
		}

		const std::filesystem::path path = GetAtlasPath(m_loadedMapId);
		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			ELOG("Failed to write atlas file " << path.string());
			return false;
		}

		file << m_atlas.dump(2) << "\n";
		m_dirty = false;
		ILOG("Saved atlas " << path.string());
		return true;
	}

	bool AtlasEditMode::RaycastTerrain(const float viewportX, const float viewportY, Vector3& outPosition) const
	{
		terrain::Terrain* terrain = m_worldEditor.GetTerrain();
		if (!m_worldEditor.HasTerrain() || !terrain)
		{
			return false;
		}

		// Long enough to span the whole world from any camera distance (see terrain_raycast.h).
		const Ray ray = m_worldEditor.GetCamera().GetCameraToViewportRay(viewportX, viewportY, 100000.0f);
		const auto hit = terrain->RayIntersects(ray);
		if (!hit.first)
		{
			return false;
		}

		outPosition = hit.second.position;
		return true;
	}

	float AtlasEditMode::GroundHeight(const float worldX, const float worldZ) const
	{
		terrain::Terrain* terrain = m_worldEditor.GetTerrain();
		float height = 0.0f;
		if (m_worldEditor.HasTerrain() && terrain && terrain->TryGetSmoothHeightAt(worldX, worldZ, height))
		{
			return height;
		}
		return 0.0f;
	}

	AtlasEditMode::Handle AtlasEditMode::PickHandle(const float viewportX, const float viewportY) const
	{
		const float screenX = m_viewportMinX + viewportX * m_viewportWidth;
		const float screenY = m_viewportMinY + viewportY * m_viewportHeight;

		Handle best;
		float bestDistance = s_pickRadiusPixels;
		for (const ScreenHandle& candidate : m_screenHandles)
		{
			const float distance = std::hypot(candidate.x - screenX, candidate.y - screenY);
			if (distance <= bestDistance)
			{
				bestDistance = distance;
				best = candidate.handle;
			}
		}
		return best;
	}

	void AtlasEditMode::MoveHandle(const Handle& handle, const float worldX, const float worldZ)
	{
		if (handle.type == HandleType::Poi)
		{
			Json& poi = m_atlas["pois"][handle.index];
			float oldX = 0.0f;
			float oldZ = 0.0f;
			if (ReadPoint(poi["center"], oldX, oldZ) && poi.contains("polygon") && poi["polygon"].is_array())
			{
				for (Json& point : poi["polygon"])
				{
					float px = 0.0f;
					float pz = 0.0f;
					if (ReadPoint(point, px, pz))
					{
						point = MakePoint(px + (worldX - oldX), pz + (worldZ - oldZ));
					}
				}
			}
			poi["center"] = MakePoint(worldX, worldZ);
			poi["source"] = "user";
		}
		else if (handle.type == HandleType::RoadPoint)
		{
			Json& road = m_atlas["roads"][handle.index];
			road["points"][handle.pointIndex] = MakePoint(worldX, worldZ);
			road["source"] = "user";
		}
		m_dirty = true;
	}

	String AtlasEditMode::UniquePoiId(const String& name) const
	{
		String base;
		for (const char c : name)
		{
			if (std::isalnum(static_cast<unsigned char>(c)))
			{
				base += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			else if (!base.empty() && base.back() != '_')
			{
				base += '_';
			}
		}
		while (!base.empty() && base.back() == '_')
		{
			base.pop_back();
		}
		if (base.empty())
		{
			base = "place";
		}

		auto taken = [this](const String& id)
		{
			return std::any_of(m_atlas["pois"].begin(), m_atlas["pois"].end(), [&id](const Json& poi)
			{
				return poi.value("id", String()) == id;
			});
		};

		String candidate = base;
		for (int suffix = 2; taken(candidate); ++suffix)
		{
			candidate = base + "_" + std::to_string(suffix);
		}
		return candidate;
	}

	void AtlasEditMode::AddPoi(const float worldX, const float worldZ)
	{
		const String kind = s_poiKinds[m_placeKind];
		const bool isNote = kind == "note";
		const String name = isNote ? "Note" : "New place";

		Json poi = Json::object();
		poi["id"] = UniquePoiId(name);
		poi["name"] = name;
		poi["kind"] = kind;
		poi["center"] = MakePoint(worldX, worldZ);
		poi["radius"] = isNote ? 10.0 : 25.0;
		// A place the user drops is their own decision, so it is canon (or a note) right away.
		poi["status"] = isNote ? "note" : "canon";
		poi["source"] = "user";

		m_atlas["pois"].push_back(std::move(poi));
		m_selected = Handle{ HandleType::Poi, static_cast<int>(m_atlas["pois"].size()) - 1, -1 };
		m_dirty = true;
	}

	void AtlasEditMode::OnMouseDown(const float x, const float y)
	{
		if (!m_atlas.is_object())
		{
			return;
		}

		if (m_placing)
		{
			Vector3 hit;
			if (RaycastTerrain(x, y, hit))
			{
				AddPoi(hit.x, hit.z);
			}
			m_placing = false;
			return;
		}

		const Handle picked = PickHandle(x, y);
		m_selected = picked;
		m_drag = picked;
	}

	void AtlasEditMode::OnMouseMoved(const float x, const float y)
	{
		if (m_drag.type == HandleType::None)
		{
			return;
		}

		Vector3 hit;
		if (RaycastTerrain(x, y, hit))
		{
			MoveHandle(m_drag, hit.x, hit.z);
		}
	}

	void AtlasEditMode::OnMouseUp(float, float)
	{
		m_drag = Handle();
	}

	void AtlasEditMode::DrawViewportOverlay(ImDrawList* drawList, const ImVec2& viewportMin, const ImVec2& viewportSize)
	{
		m_viewportMinX = viewportMin.x;
		m_viewportMinY = viewportMin.y;
		m_viewportWidth = std::max(1.0f, viewportSize.x);
		m_viewportHeight = std::max(1.0f, viewportSize.y);
		m_screenHandles.clear();

		if (!drawList || !m_atlas.is_object())
		{
			return;
		}

		Camera& camera = m_worldEditor.GetCamera();
		const Matrix4 viewProj = camera.GetProjectionMatrix() * camera.GetViewMatrix();
		auto project = [&](const float x, const float y, const float z, ImVec2& out) -> bool
		{
			const Vector4 clip = viewProj * Vector4(Vector3(x, y, z), 1.0f);
			if (clip.w <= 0.0f)
			{
				return false;
			}
			out.x = viewportMin.x + (clip.x / clip.w * 0.5f + 0.5f) * viewportSize.x;
			out.y = viewportMin.y + (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * viewportSize.y;
			return true;
		};
		auto label = [&](const ImVec2& at, const ImU32 color, const String& text)
		{
			drawList->AddText(ImVec2(at.x + 9.0f, at.y - 7.0f), IM_COL32(0, 0, 0, 255), text.c_str());
			drawList->AddText(ImVec2(at.x + 8.0f, at.y - 8.0f), color, text.c_str());
		};

		drawList->PushClipRect(viewportMin, ImVec2(viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y), true);

		Json& roads = m_atlas["roads"];
		for (size_t roadIndex = 0; roadIndex < roads.size(); ++roadIndex)
		{
			const Json& road = roads[roadIndex];
			const bool selected = m_selected.type == HandleType::RoadPoint && m_selected.index == static_cast<int>(roadIndex);
			const ImU32 color = selected ? IM_COL32(255, 255, 0, 255) : StatusColor(road);
			const Json& points = road.contains("points") ? road["points"] : Json::array();
			ImVec2 previous;
			bool hasPrevious = false;
			for (size_t pointIndex = 0; pointIndex < points.size(); ++pointIndex)
			{
				float x = 0.0f;
				float z = 0.0f;
				ImVec2 screen;
				if (!ReadPoint(points[pointIndex], x, z) || !project(x, GroundHeight(x, z) + 0.5f, z, screen))
				{
					hasPrevious = false;
					continue;
				}
				if (hasPrevious)
				{
					drawList->AddLine(previous, screen, color, 3.0f);
				}
				drawList->AddCircleFilled(screen, 5.0f, color);
				if (pointIndex == 0)
				{
					label(screen, color, road.value("name", String("road")));
				}
				m_screenHandles.push_back({ Handle{ HandleType::RoadPoint, static_cast<int>(roadIndex), static_cast<int>(pointIndex) }, screen.x, screen.y });
				previous = screen;
				hasPrevious = true;
			}
		}

		Json& pois = m_atlas["pois"];
		for (size_t poiIndex = 0; poiIndex < pois.size(); ++poiIndex)
		{
			const Json& poi = pois[poiIndex];
			float cx = 0.0f;
			float cz = 0.0f;
			if (!ReadPoint(poi.contains("center") ? poi["center"] : Json(), cx, cz))
			{
				continue;
			}

			const bool selected = m_selected.type == HandleType::Poi && m_selected.index == static_cast<int>(poiIndex);
			const ImU32 color = selected ? IM_COL32(255, 255, 0, 255) : StatusColor(poi);

			// Outline on the ground: a sampled circle for radius places, the polygon otherwise.
			std::vector<ImVec2> outline;
			if (poi.contains("radius") && poi["radius"].is_number())
			{
				const float radius = poi["radius"].get<float>();
				for (int i = 0; i <= s_ringSegments; ++i)
				{
					const float angle = static_cast<float>(i) / s_ringSegments * 6.2831853f;
					const float x = cx + std::cos(angle) * radius;
					const float z = cz + std::sin(angle) * radius;
					ImVec2 screen;
					if (project(x, GroundHeight(x, z) + 0.3f, z, screen))
					{
						outline.push_back(screen);
					}
				}
			}
			else if (poi.contains("polygon") && poi["polygon"].is_array())
			{
				for (const Json& point : poi["polygon"])
				{
					float x = 0.0f;
					float z = 0.0f;
					ImVec2 screen;
					if (ReadPoint(point, x, z) && project(x, GroundHeight(x, z) + 0.3f, z, screen))
					{
						outline.push_back(screen);
					}
				}
				if (!outline.empty())
				{
					outline.push_back(outline.front());
				}
			}
			if (outline.size() > 1)
			{
				drawList->AddPolyline(outline.data(), static_cast<int>(outline.size()), color, ImDrawFlags_None, selected ? 3.0f : 2.0f);
			}

			ImVec2 pin;
			if (project(cx, GroundHeight(cx, cz) + 2.0f, cz, pin))
			{
				drawList->AddCircleFilled(pin, selected ? 8.0f : 6.0f, color);
				drawList->AddCircle(pin, selected ? 8.0f : 6.0f, IM_COL32(0, 0, 0, 255), 0, 1.5f);
				String text = poi.value("name", String("?"));
				if (poi.contains("ask"))
				{
					text += " (?)";
				}
				label(pin, color, text);
				m_screenHandles.push_back({ Handle{ HandleType::Poi, static_cast<int>(poiIndex), -1 }, pin.x, pin.y });
			}
		}

		drawList->PopClipRect();
	}

	void AtlasEditMode::DrawPoiProperties(Json& poi)
	{
		bool changed = false;
		ImGui::TextDisabled("id: %s", poi.value("id", String()).c_str());
		changed |= EditText(poi, "name", "Name", false, true);

		const String kind = poi.value("kind", String());
		int kindIndex = 0;
		for (int i = 0; i < static_cast<int>(std::size(s_poiKinds)); ++i)
		{
			if (kind == s_poiKinds[i])
			{
				kindIndex = i;
			}
		}
		if (ImGui::Combo("Kind", &kindIndex, s_poiKinds, static_cast<int>(std::size(s_poiKinds))))
		{
			const bool wasNote = kind == "note";
			const bool isNote = String(s_poiKinds[kindIndex]) == "note";
			poi["kind"] = s_poiKinds[kindIndex];
			// kind 'note' and status 'note' always go together (worldkit/atlas.py validates this).
			if (isNote)
			{
				poi["status"] = "note";
				poi.erase("ask");
			}
			else if (wasNote)
			{
				poi["status"] = "canon";
			}
			changed = true;
		}

		if (poi.contains("radius") && poi["radius"].is_number())
		{
			float radius = poi["radius"].get<float>();
			if (ImGui::DragFloat("Radius (m)", &radius, 0.5f, 1.0f, 2000.0f, "%.1f"))
			{
				poi["radius"] = Round1(radius);
				changed = true;
			}
		}

		changed |= EditLevels(poi);
		changed |= EditText(poi, "faction", "Faction", false, false);
		changed |= EditText(poi, "description", "Description", true, false);
		changed |= EditText(poi, "notes", "Notes", true, false);
		changed |= DrawConfirmAndStatus(poi);

		if (ImGui::Button("Delete place"))
		{
			m_atlas["pois"].erase(static_cast<size_t>(m_selected.index));
			m_selected = Handle();
			changed = true;
		}

		m_dirty |= changed;
	}

	void AtlasEditMode::DrawRoadProperties(Json& road)
	{
		bool changed = false;
		ImGui::TextDisabled("id: %s", road.value("id", String()).c_str());
		changed |= EditText(road, "name", "Name", false, true);
		Json& points = road["points"];
		ImGui::Text("Points: %d (drag them in the viewport)", static_cast<int>(points.size()));

		if (ImGui::Button("Add point at end") && !points.empty())
		{
			float x = 0.0f;
			float z = 0.0f;
			float px = 0.0f;
			float pz = 0.0f;
			ReadPoint(points.back(), x, z);
			if (points.size() > 1 && ReadPoint(points[points.size() - 2], px, pz))
			{
				points.push_back(MakePoint(x + (x - px) * 0.5f, z + (z - pz) * 0.5f));
			}
			else
			{
				points.push_back(MakePoint(x + 20.0f, z));
			}
			changed = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Remove last point") && points.size() > 2)
		{
			points.erase(points.size() - 1);
			changed = true;
		}

		changed |= EditText(road, "notes", "Notes", true, false);
		changed |= DrawConfirmAndStatus(road);

		if (ImGui::Button("Delete road"))
		{
			m_atlas["roads"].erase(static_cast<size_t>(m_selected.index));
			m_selected = Handle();
			changed = true;
		}

		m_dirty |= changed;
	}

	void AtlasEditMode::DrawNeedsInput()
	{
		int open = 0;
		for (const char* section : { "pois", "roads", "zones" })
		{
			if (!m_atlas.contains(section) || !m_atlas[section].is_array())
			{
				continue;
			}

			Json& entries = m_atlas[section];
			for (size_t i = 0; i < entries.size(); ++i)
			{
				const Json& entry = entries[i];
				if (entry.value("status", String()) != "placeholder" || !entry.contains("ask"))
				{
					continue;
				}

				++open;
				ImGui::PushID(section);
				ImGui::PushID(static_cast<int>(i));
				const String caption = entry.value("name", String("?")) + "##needs";
				if (ImGui::Selectable(caption.c_str()))
				{
					float x = 0.0f;
					float z = 0.0f;
					const String sectionName = section;
					if (sectionName == "pois" && ReadPoint(entry["center"], x, z))
					{
						m_selected = Handle{ HandleType::Poi, static_cast<int>(i), -1 };
						m_worldEditor.FocusWorldPosition(Vector3(x, GroundHeight(x, z), z));
					}
					else if (sectionName == "roads" && entry.contains("points") && !entry["points"].empty() && ReadPoint(entry["points"][0], x, z))
					{
						m_selected = Handle{ HandleType::RoadPoint, static_cast<int>(i), 0 };
						m_worldEditor.FocusWorldPosition(Vector3(x, GroundHeight(x, z), z));
					}
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", entry["ask"].is_string() ? entry["ask"].get<String>().c_str() : "");
				}
				ImGui::PopID();
				ImGui::PopID();
			}
		}

		if (open == 0)
		{
			ImGui::TextDisabled("Nothing open - every placeholder has been answered.");
		}
	}

	void AtlasEditMode::DrawDetails()
	{
		ImGui::PushID("AtlasEditMode");

		const proto::MapEntry* mapEntry = m_mapEntryProvider ? m_mapEntryProvider() : nullptr;
		if (mapEntry && mapEntry->id() != m_loadedMapId && !m_dirty)
		{
			Load();
		}

		if (!m_loadError.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 80, 80, 255));
			ImGui::TextWrapped("%s", m_loadError.c_str());
			ImGui::PopStyleColor();
			if (ImGui::Button("Reload"))
			{
				Load();
			}
			ImGui::PopID();
			return;
		}

		ImGui::TextDisabled("%s", GetAtlasPath(m_loadedMapId).string().c_str());
		ImGui::TextDisabled("Editor-only data; never exported to the game.");
		if (ImGui::Button(m_dirty ? "Save atlas *" : "Save atlas"))
		{
			Save();
		}
		ImGui::SameLine();
		if (ImGui::Button("Reload"))
		{
			Load();
		}

		if (ImGui::CollapsingHeader("Needs your input", ImGuiTreeNodeFlags_DefaultOpen))
		{
			DrawNeedsInput();
		}

		if (ImGui::CollapsingHeader("Add place", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Combo("Kind##add", &m_placeKind, s_poiKinds, static_cast<int>(std::size(s_poiKinds)));
			if (ImGui::Button(m_placing ? "Click the terrain..." : "Place on terrain"))
			{
				m_placing = !m_placing;
			}
			ImGui::TextDisabled("Pick 'note' to leave a design note for the agent.");
		}

		if (ImGui::CollapsingHeader("Places"))
		{
			Json& pois = m_atlas["pois"];
			for (size_t i = 0; i < pois.size(); ++i)
			{
				ImGui::PushID(static_cast<int>(i));
				const bool selected = m_selected.type == HandleType::Poi && m_selected.index == static_cast<int>(i);
				if (ImGui::Selectable(pois[i].value("name", String("?")).c_str(), selected))
				{
					m_selected = Handle{ HandleType::Poi, static_cast<int>(i), -1 };
				}
				ImGui::PopID();
			}
		}

		if (ImGui::CollapsingHeader("Roads"))
		{
			Json& roads = m_atlas["roads"];
			for (size_t i = 0; i < roads.size(); ++i)
			{
				ImGui::PushID(1000 + static_cast<int>(i));
				const bool selected = m_selected.type == HandleType::RoadPoint && m_selected.index == static_cast<int>(i);
				if (ImGui::Selectable(roads[i].value("name", String("?")).c_str(), selected))
				{
					m_selected = Handle{ HandleType::RoadPoint, static_cast<int>(i), 0 };
				}
				ImGui::PopID();
			}
		}

		if (m_selected.type == HandleType::Poi && m_selected.index >= 0 && m_selected.index < static_cast<int>(m_atlas["pois"].size()))
		{
			if (ImGui::CollapsingHeader("Selected place", ImGuiTreeNodeFlags_DefaultOpen))
			{
				DrawPoiProperties(m_atlas["pois"][m_selected.index]);
			}
		}
		else if (m_selected.type == HandleType::RoadPoint && m_selected.index >= 0 && m_selected.index < static_cast<int>(m_atlas["roads"].size()))
		{
			if (ImGui::CollapsingHeader("Selected road", ImGuiTreeNodeFlags_DefaultOpen))
			{
				DrawRoadProperties(m_atlas["roads"][m_selected.index]);
			}
		}

		ImGui::PopID();
	}
}
```

Before building, verify each include and API name against the headers. These can differ:
- the `Vector4` header name (`math/vector4.h`?) and its constructor `Vector4(const Vector3&, float)` (water_edit_mode.cpp uses the same expression, so copy its includes);
- `Matrix4` operator `*` with `Vector4`;
- the `maps.pb.h` include path used elsewhere in world_editor (copy from `spawn_edit_mode.cpp`);
- `Terrain::TryGetSmoothHeightAt(float, float, float&)`;
- `ImDrawFlags_None` (older ImGui: pass `0`).

Fix names to match the code base; do not change behaviour.

- [ ] **Step 4: Register the mode**

In `world_editor_instance.h`:
- add `#include "edit_modes/atlas_edit_mode.h"` next to the other edit-mode includes;
- add `std::unique_ptr<AtlasEditMode> m_atlasEditMode;` after `m_fogVolumeEditMode`.

In `world_editor_instance.cpp`:
- after `m_fogVolumeEditMode = std::make_unique<FogVolumeEditMode>(*this);` (line ~380) add:
```cpp
		m_atlasEditMode = std::make_unique<AtlasEditMode>(*this, std::filesystem::path(m_editor.GetProject().getLastPath()),
			[this]() -> const proto::MapEntry* { return m_mapEntry; });
```
- in **every** `WorldEditMode *availableModes[] = { … }` array that lists `m_fogVolumeEditMode.get(),` (find them with grep), add `m_atlasEditMode.get(),` directly after it;
- extend the camera-rotation guard (line ~877):
```cpp
			const bool isDraggingWaypoint = (m_editMode == m_spawnEditMode.get() && m_spawnEditMode->IsDraggingWaypoint()) ||
				(m_editMode == m_atlasEditMode.get() && m_atlasEditMode->IsDragging());
```
- if `SetEditMode` or a destructor resets the modes explicitly (grep `m_fogVolumeEditMode.reset`), mirror it for `m_atlasEditMode`.

- [ ] **Step 5: Build**

Run: `cmake --build build --config Debug -t mmo_edit`
Expected: builds without errors or new warnings in the touched files. The editor CMake uses `add_gui_exe_recurse`, which picks up the new files. If it does not, re-run CMake configure: `cmake -S . -B build`.

- [ ] **Step 6: Check the JSON contract headlessly**

The editor cannot be driven by automation (memory note: computer-use can't drive mmo_edit), so pin the format contract instead. Write a throwaway check in the scratchpad: a tiny C++ file compiled against `deps/json` that loads `data/world/atlas/map_0.json` as `ordered_json`, dumps it with `dump(2) + "\n"`, and compares bytes. The result must be identical. If building a throwaway exe is impractical, do the equivalent with Python: confirm that `json.dumps(json.loads(t), indent=2, ensure_ascii=False) + "\n" == t` for the committed atlas. That is the same formatting rule nlohmann follows for this data (shortest round-trip floats, 2-space indent, `": "` separators, raw UTF-8). Record in the task report which check was run.

- [ ] **Step 7: Commit**

```bash
git add src/mmo_edit/editors/world_editor
git commit -m "feat(mmo_edit): Atlas edit mode - pins, roads, needs-your-input panel, confirm

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

Manual checks left for the user (list them in the final report):
- open Development in the world editor and switch to the Atlas mode;
- the placeholders appear orange with labels, and "Needs your input" lists them;
- clicking an entry moves the camera there;
- dragging a pin follows the terrain and the camera does not rotate while dragging;
- Confirm turns the pin white;
- Save writes `data/world/atlas/map_0.json`, and `git diff` shows only the intended changes;
- `python -c "..load_atlas.."` still validates the saved file.

---

### Task 18: Final verification

**Files:** none new.

- [ ] **Step 1: Full tool test sweep**

Run: `python -m unittest discover -s tools/tests -p "test_*.py"`
Expected: all tests PASS. This is the same command the gate uses.

- [ ] **Step 2: Full content audit**

Run: `python tools/gate/content_audit.py`
Expected: `Result: GREEN`, except for the spell 247 failure that existed before this work.

That failure came from the stale `.claude` spell library, which Task 1 replaced with a junction. It should now be green too. If it is not, look into it and report the cause; do not change spell data.

- [ ] **Step 3: The quality gate**

Run the `/gate` skill. It runs `tools/gate/verify.ps1`: Debug build, unit tests and E2E, then a code review of the branch diff. Fix every real finding from the review, re-run the gate until it is green, and commit the fixes.

- [ ] **Step 4: Report to the user**

The final message must include:
- what shipped;
- the baseline counts per rule (the known world problems, now listed);
- the path of the review packet (`generated/world/review/atlas-bootstrap/`);
- the open questions (terrain kinds if unresolved; all placeholder `ask`s);
- the manual editor checks from Task 17;
- a statement that the branch is gated but not merged or pushed.

Offer to `/ship` once the user has looked.

---

## Self-review notes (plan author)

- Spec coverage:
  - §5.1 parsers → Tasks 2–3;
  - §5.2 snapshot/query/CLI → Tasks 5, 9;
  - terrain kinds → Task 6;
  - §5.3 atlas → Task 8;
  - §5.4 bible → Task 15;
  - §5.5 renderer → Task 12;
  - §5.6 lint → Task 10;
  - §5.7 report → Task 11;
  - §5.8 baseline → Tasks 10, 13;
  - §6 integration → Tasks 13, 16;
  - §7 hygiene → Task 1;
  - §8 bootstrap → Task 14;
  - §9 editor mode → Task 17;
  - §10 error handling → the parsers' `FormatError`, `AtlasError`, and the editor's refusal to overwrite unreadable files;
  - §11 testing → a test file per module plus the shipped-file drift tests.
- Deviations from the spec are listed at the top under "Deliberate narrowings":
  - named roads only;
  - no separate schema file;
  - footprint heuristics;
  - tolerance-based height consistency test (the fan surface is the correct surface; exact parity with the old bilinear output is impossible where inner vertices are sculpted);
  - new warnings do not fail the audit, only new errors do;
  - baseline file has sections per tool.

---

## Implementation notes (2026-09-27, recorded after execution)

Where the implementation deliberately differs from the listings above, and why:

- **North is −Z.** The client minimap (`src/mmo_client/ui/minimap.cpp`) puts smaller world Z at the top. Renders and the layer preview are therefore north-up with −Z at the top. The Task 12 listing's "image up = +Z" was wrong, and the Northroad stub points to −Z. Checking against the map showed that quests 29, 30, 44, 55 and 56 have north and south swapped in their text (recorded in the bible, section 12).
- **Loot sources follow the server.** Creatures use `unitlootentries` plus the legacy `unitlootentry`. World objects draw from `unit_loot`: `object_loot.data` is empty and unused (`game_world_object_s.cpp`). Spawners switched on by `SetSpawnState` (action 4) and creatures summoned by `SummonCreature` (action 25) count as objective sources. Objectives on another map (dungeons) are accepted without a distance check.
- **Lint refinements from real data:**
  - A spawn over a terrain hole is the warning `over_hole`, and slope and height are skipped there: the hole's floor mesh is the ground (the Oakenshire Inn cellar).
  - Building pieces (`/buildings/`) are structures, not blocking footprints.
  - Level bands exempt service NPCs (quest givers and enders, trainers, vendors, gossip) through `GameData.service_units()`.
  - Unnamed spawns are labelled with their unit or object name.
- **Bootstrap refinements:**
  - Spawn-name prefixes equal to a zone name are skipped.
  - Scattered prefix groups are split into clusters.
  - Known places anchor on the largest cluster of their NPCs, and the question names any anchors found elsewhere.
  - Creature clusters are split until they fit a camp (≤ 90 m).
  - Parked pins sit in the middle of the content.
- **Renderer default framing** is the content extent (zoned terrain, props, spawns, places); `--full` shows every page.
- **Terrain kinds:** layer 1 of `Oakenshire_Boars_Enhanced.hmi` is the path paint. It also covers the long western coastal strip, so "path" means trodden dirt. The user has not confirmed this yet.
- **Baseline:** the Ossuar findings (q58, q61) are deliberately not baselined. The Ossuar spawn is inactive only in the uncommitted `data/editor` working copy.
