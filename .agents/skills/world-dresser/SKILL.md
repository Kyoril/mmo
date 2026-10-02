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
3. Pick or write a template in `tools/world/templates/` (see the template guide below). Keep away from neighbours with `--keep-away <poi>:<metres>`: it keeps items out of a circle of that place's RADIUS PLUS the metres, around its centre. To keep a gap measured from the neighbour's edge, use `:0` or a small value.
4. Run `python tools/world/dress.py plan ...`. Pin centres are often on steep ground, so choose `--anchor <x> <z>` on suitable ground; every pilot site needed one. Then open the previews in `generated/world/passes/<id>/`. North is -Z; the previews name the direction they look in.
5. Fix what looks wrong by editing `draft.json`: move, rotate, swap or delete an item. Then run `dress.py check <id>` and look again. `apply` refuses a draft that was edited after its last `check`.
6. Run `dress.py apply <id>`. It leaves the pass `applied-unchecked`; the walkability check that follows promotes it to `applied`. Every `apply` changes the world fingerprint, so the next draft must be re-checked before it can be applied. With several passes the sequence is: `dress.py check <id>`, then `dress.py apply <id> --skip-nav`, repeated per pass, then one `dress.py check --nav <id> <id> ...` at the end.
7. Hand the user the packet: `dress.py packet-index --name <batch> <ids...>`. List the gaps, any "awaiting water" notes, and how to undo.
</world_aware_design>

<template_guide>
- A template file has: `name`, `description`, `entry` (`anchor` or a role name), `clear` (radii kept empty around the anchor), `notes`, and `roles`.
- Each role has `name`, `query` (`tags`, `exclude`, `size`: small < 1.5 m, medium < 4 m, large; or `assets`), `count` [min, max], `spacing`, `store` (`wobj` or `hfol`), `rule`, and optionally `scale`, `yaw` and `sink`. `sink` (metres, >= 0) replaces the asset tag's sink for placement depth only; use it when a small scale makes the tag's sink (calibrated for big instances) bury the asset. The lint still judges with the tag's own sink.
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
- Anything the user changed since is kept and listed, unless `--force`. That includes a prop the user dragged onto another page.
- A pass left `applying` (the process was killed mid-write) is undone the same way; items that were never written are listed as already gone.
- Before writing, `apply` backs up every page `.hfol` it changes to `generated/world/passes/<id>/backup/`.
</undo>
