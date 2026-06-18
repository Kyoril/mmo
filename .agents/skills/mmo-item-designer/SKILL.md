---
name: mmo-item-designer
description: Design balanced MMO game items, generate ready-to-import JSON files for the F:\mmo item editor, and create a matching 128x128 PNG icon proposal for each item. Use whenever the user asks to create, design, or generate any item, weapon, armor piece, shield, accessory, bag, consumable, potion, food, quest item, crafting material, ammunition, or other in-game equipment, including terse requests such as "make me a sword" or "I need a potion".
---

# MMO Item Designer

Create complete item concepts, editor-compatible JSON files, and matching item icon proposals.

## Workflow

1. Read [references/item-format.md](references/item-format.md).
   For weapons, armor, shields, accessories, or stat-bearing consumables, also read [references/balance-guidelines.md](references/balance-guidelines.md).
   Read [references/icon-guidelines.md](references/icon-guidelines.md) before generating icons.
2. Inspect the live project catalogs before selecting class, subclass, display, proficiency, or spell IDs:

```powershell
python scripts/inspect_item_catalog.py --project-root F:\mmo --pretty
```

Use `--section items`, `classes`, `subclasses`, `displays`, or `spells` to narrow output. Treat the repository data as authoritative because IDs may change.

If `python` resolves to an unavailable Windows Store shim, call `codex_app.load_workspace_dependencies` and run the scripts with the returned Python executable.

3. Infer reasonable design details from a terse request. Ask a question only when a missing choice would materially change the requested identity or power budget.
4. Match nearby existing items by type and level when balancing stats, armor, damage, delay, durability, prices, and stack size. Use the balance reference to fill gaps, but prefer live project data when it conflicts with a heuristic.
5. Use only catalog IDs confirmed by the inspection script. If no suitable display or spell exists, use `displayid: 0` or omit the spell rather than inventing an ID.
6. Write one JSON file per item under `F:\mmo\generated\items\` unless the user gives another path. Use a lowercase snake-case filename ending in `.json`.
7. Use the built-in `image_gen` tool to create one square icon proposal per item. Use the bundled reference icons as style references, not edit targets. The final shipped icon must come from `image_gen` output, not from a hand-drawn fallback, procedural placeholder, copied project icon, or any other substitute. Generate at the tool's supported square size, locate the emitted file under the default Codex generated-images directory when needed, then save and normalize it beside the JSON as `<item_name>_icon.png`:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/normalize_item_icon.ps1 -InputPath <generated-square-image> -OutputPath F:\mmo\generated\items\item_name_icon.png
```

8. Validate every JSON file:

```powershell
python scripts/validate_item_json.py F:\mmo\generated\items\item_name.json --project-root F:\mmo
```

9. Verify every final icon is a PNG with exactly `128x128` pixels. Also visually inspect the final icon against the generated draft and reject it if it looks materially flatter, more schematic, or lower fidelity than the `image_gen` result.
10. Report both output paths, briefly summarize the item's gameplay role and important assumptions, render the icon inline, and include a compact tooltip-style preview for equippable items.

## Output Rules

- Emit the editor wrapper with `"format": "mmo-item"` and `"version": 1`.
- Do not include or promise an item ID. Import preserves the currently selected editor entry's ID.
- Always provide a non-empty `item.name`.
- Always set `maxstack` to at least `1`; zero can cause division by zero in inventory validation.
- Use numeric enum values from the reference.
- Use `inventorytype: 0` for non-equippable items.
- Use `damage` and `delay` for weapons. Keep `mindmg <= maxdmg`, non-negative damage, and positive delay.
- Keep `stats`, `spells`, `sockets`, `name_loc`, and `description_loc` as arrays, even when empty.
- Use `allowedclasses: -1` and `allowedraces: -1` unless intentionally restricted.
- Use `bonding` values `0` to `3`.
- Keep consumable effects in `spells`; never fabricate a spell ID.
- Prefer omitted or zero optional references over invalid references.
- Produce strict JSON with no comments or trailing commas.
- Generate multiple requested items as separate JSON files. Never replace the singular `item` object with an `items` array.
- Generate exactly one distinct icon proposal for each requested item unless the user asks for variants.
- Keep JSON and icon basenames paired: `item_name.json` and `item_name_icon.png`.
- Do not reuse an existing project icon as the generated proposal.
- Do not substitute locally drawn, procedurally generated, or placeholder artwork when `image_gen` persistence is inconvenient. If the `image_gen` result cannot be located or saved correctly, stop and report the blocker instead of fabricating an inferior icon.

## Design Defaults

- Common mundane gear: quality `1`.
- Distinctive quest or crafted gear: quality `2` when justified.
- Rare or stronger qualities require an explicit user request or clear content context.
- Equipment normally stacks to `1`; ordinary consumables and materials may stack to `20`.
- Set `requiredlevel <= itemlevel`.
- Make buy price greater than or equal to sell price when both are nonzero.
- Omit `description` for ordinary drops and routine crafted items. Use concise flavor text for legendary, story-unique, named boss, NPC, or lore-linked items.
