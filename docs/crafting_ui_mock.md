# Crafting UI preview

Open the in-game window with `/craft`, `/crafting`, `/berufe` or `/professions`.
The window participates in the normal UI panel handling, including Escape and the
close button. Select a profession to browse recipes, search by localized name,
collapse categories, filter by materials, skill gain or favorites, and inspect
reagent counts. The `+` marker and orange text identify skill-gaining recipes.
The recipe list reuses nine visible slots. Mouse wheel, scrollbar arrows and thumb
dragging change the first displayed entry, without creating additional recipe frames.
Category headers are separate non-checkable controls; recipe highlighting comes only
from the selected recipe ID. Opaque content surfaces distinguish the list and details
from the outer wood frame. Actions use the red game-menu buttons.

The preview contains ten professions and thirty recipes. Crafting takes 1.2 seconds
per recipe, consumes sample materials and increases the displayed sample skill.
Choose a quantity or create all available copies. During crafting, the create button
becomes an enabled Cancel button; cancelling or finishing restores its original label.
Cancel, close the window or switch
professions to stop the batch; completed copies remain consumed. Enchanting requires
selecting the sample bracers first. Refill samples restores material counts. One
cooking recipe deliberately starts with insufficient spices to demonstrate shortages.

Crafting state is local to the current UI session. No spells are cast, inventory items
are created or consumed, chat links are sent, or server data is changed. French and
Russian localization entries currently use English placeholders.
Reagent slots use the real item icon and tooltip for sample item ID 1 (Chunk of Boar
Meat), independently of the displayed mock material name. Item metadata is requested
through the normal cache; hovering waits for the asynchronous response if necessary.
Replace the shared sample ID with each reagent's actual item ID during integration.

## Files and later integration

- `data/client/Interface/GameUI/Crafting.xml`: native Alestia panel, controls and states.
- `Crafting.lua`: navigation, filtering and mock crafting behavior.
- `CraftingMockData.lua`: isolated sample profession/recipe/material provider.

Recipes have stable IDs independent of their visible row or scroll offset. To integrate
real professions, replace the sample provider and the local availability/start/update
logic with inventory and server-backed crafting. Keep recipe IDs stable, refresh from
game events, and let the server validate materials and completion. The sample target
button must become a real eligible-item selector. Favorites and preview state are
intentionally not persisted.

## Verification

Run `python tools/tests/test_crafting_mock.py`. The layout and localization checks
use the Python standard library; install `lupa` to also execute the Lua behavior
checks. These tests cover virtual scrolling, category collapse, exclusive selection,
declared image bindings, filtering, shortages, enchanting,
batch consumption, cancellation, and profession/window transitions with frame stubs.
They do not replace a visual check in the running game. Build the native client with
`cmake --build build --config Release -t mmo_client`.
