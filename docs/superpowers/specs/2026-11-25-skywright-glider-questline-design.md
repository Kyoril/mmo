# Gliders as equipment, glide stats and the Skywright questline (design)

Status: revision 2, waiting for the user's approval (revision 1, the questline alone, was approved
2026-11-25; the user then turned the glider into equipment and added glide stats).
Builds on the traversal pillar (`docs/superpowers/specs/2026-11-21-traversal-pillar-design.md`) and
the glider visuals (`docs/superpowers/specs/2026-11-22-glider-visuals-design.md`, branch
`feature/glider-visuals`, not yet shipped).

## Intent

The glider works, but only a GM can learn it, and every glider looks the same. Players should earn
a glider in a questline from level 10, and later find or buy other gliders that look different and
fly differently. Gliders become **equipment**: a new slot in the character window, a new item type,
the glider's look and its glide stats come from the equipped item. This also lays the ground for
the planned mount categories (one item per category, collectible skins with their own abilities).

## Decided by the user (2026-11-25)

- Questline in or around Haven, from level 10; the story below.
- Every quest gives class XP as well as character XP (`rewardxp` and `rewardclassxp`).
- The glider is an item in a **new equipment slot**; new gliders change the glider's look.
- The glider spell (1436) becomes a **base spell** every character knows, **hidden in the spellbook**,
  and it only works with a glider item equipped.
- The questline gives the **first glider item**, not the spell.
- Gliders may carry normal item stats, and **separate glide stats** such as glide speed and agility.
- The slot must be **embedded properly in the UI**: the character window is reworked for it, and a
  new item type is added.
- The ungating work (anti-cheat, creature AI, E2E) is in the same branch as the questline.

## The four parts (built in this order, one branch, a green gate after each)

### Part 1 — Glider item and equipment slot

- New `inventory_type::Glider` (appended before `Count_`; string tables in game, editor and Lua
  `INVENTORY_TYPE_GLIDER` updated).
- New equipment slot **86** (`player_glider_slots::Slot`, the first unused subslot after the buyback range) instead
  of slot 19. *(Claude's call: inserting at 19 would shift every bag, pack, bank and buyback slot,
  renumber `character_items.slot` in the realm database and break the additive-migration rule, since
  the previous build could no longer read the inventory.)*
  - Treated as an equipment slot everywhere equipment is handled: equip validation (only
    `inventory_type::Glider` fits), auto-equip, unequip, stats and OnEquip spells, durability as for
    other equipment.
  - Not part of `CharacterView` (character select shows no glider), so the character list payload
    stays the same.
  - DB: no migration for existing rows; a glider row is a new slot value (`0xFF56`). The previous
    build must ignore an unknown slot without deleting the item (verify; if it would delete it, the
    plan guards that path first).
- Replication: one new visible item block for the glider slot, **appended at the end of the player
  fields**, so other clients know which glider to draw. Game protocol bump and fingerprint update.
- Spell 1436 is granted to every character as a base spell (also existing characters on login),
  flagged hidden so the spellbook does not list it.
  - Server gate `CanUseTraversal(Glider)` becomes "knows 1436 **and** has a glider equipped";
    refusal stays `NoGlider` (a violation only when the client claims to glide without one).
  - Client gate `IsGliderUsable` checks the glider slot the same way; without a glider the jump key
    shows "You have no glider equipped." / deDE "Ihr habt keinen Gleiter angelegt."
  - Max Vigor follows "can glide or knows Dash" instead of "knows 1436".
- The glider's look: `GliderVisual` takes mesh, material and attach transform from the equipped
  glider's item display (`ItemDisplayEntry` variant with mesh + bone attachment, as weapons do)
  instead of the fixed `Models/Gear/Glider/Glider.hmsh`. The current mesh becomes the item display
  of "Elowen's Prototype". Remote players use their replicated glider.
- GM path: adding the prototype item replaces `.learn 1436` for testing.

**Character window (UI):** the paperdoll gets a **travel row** below the weapon row: the glider slot
with its own wing-silhouette slot icon and a label ("Glider" / deDE "Gleiter"), laid out so a mount
slot can join it later. The window is resized as needed so nothing overlaps; checked with `ui_test`
screenshots (all locales) and sent to the user for sign-off. *(Claude's call on the placement; the
user signs off the screenshots.)*

### Part 2 — Glide stats

- Four new item stat types for gliders (appended item stat ids, shown in the item tooltip):

  | Stat | enUS / deDE | Effect (scales the base value in `TraversalSettings`) |
  |---|---|---|
  | Glide speed | Glide Speed / Gleitgeschwindigkeit | `glideForwardSpeed` |
  | Agility | Agility in Flight / Wendigkeit | new turn rate: how fast the glide direction follows input (today it turns instantly) |
  | Lift | Lift / Tragkraft | lower `glideSinkSpeed` |
  | Endurance | Flight Endurance / Flugausdauer | lower `glideDrainPerSecond` |

  Values are percentages (e.g. +10 %), with caps so data cannot make a glider rise on its own or
  outrun the validator.
- One function builds a player's effective `TraversalSettings` from base settings + equipped glider
  stats; server physics, the server glide validator, the own client's physics and the remote movement
  renderer all use it. Remote clients get the glider's item entry through the visible slot.
- The prototype from the questline has no glide stats (base values); later gliders add them.

### Part 3 — Ungating for players

From the test field's open items, with what the code does today:
- Glide rise tolerance: `IsGlideSpeedValid` adds a fixed 0.5 m plus carried rise per checked interval
  with no upper bound on the interval; scale the allowance with elapsed time and bound the interval.
- Client timestamps: the glide validator takes `dt` from converted client timestamps without a clamp;
  clamp them against the server receive time.
- Creature AI vs airborne players: range checks are flat, so a creature chases under a glider and may
  hit it overhead. No melee hits on targets more than a melee height above; evade as for unreachable
  targets; ranged attacks allowed.
- E2E: the bot cannot jump, fall or glide today (it lands at once). Add bot fall/glide physics and Lua
  `Jump`, `OpenGlider`, `CloseGlider`, `IsGliding`, `IsFalling`; scenario: equip the prototype,
  worldport onto a height, jump, glide through an updraft, land without fall damage, plus a refusal
  case without a glider.

### Part 4 — The Skywright questline

**Story (proposed by Claude, accepted by the user).** In Haven's harbour lives an eccentric inventor,
**Master Elowen Marsh, "the Skywright"**. For years she has studied the flight of gulls and bats and
built wings of ash wood and sailcloth. The Thorn smugglers had an eye on her plans: with gliders, any
town wall can be crossed. Now that the ring has been broken (q94), she needs someone to help her
finish the glider and fly it first. Grounded and a little humorous, no magic in the glider itself.

| # | Working title | Giver → ender | What the player does | Reward |
|---|---|---|---|---|
| 1 | Wings over the Basin | Watch-Captain Corwin Ashby (u98) → Elowen Marsh | Breadcrumb to her harbour workshop; texts link to the end of the Thorn story, which is **not** a prerequisite. | XP, class XP, money |
| 2 | Ash and Sailcloth | Elowen → Elowen | Gather seasoned ash staves (world objects) in the river valley; collect waxed sailcloth from a harbour sailmaker. | XP, class XP, money |
| 3 | The Stolen Folio | Elowen → Elowen | Thornhand survivors stole her notebook; recover it at Thornhand Landing (kill + loot). | XP, class XP, **Dash (1437)** |
| 4 | First Flight | Elowen → Elowen | On accept she hands over **"Elowen's Prototype"** (glider item, `srcitemid`); equip it, jump from the Keep wall and glide through three ribbon markers into the lower court. | XP, class XP |
| 5 | On Warm Air | Elowen → Elowen | Ride new updrafts at the harbour (smithy chimneys, Gate Island) to the top of Haven Keep and raise her flag. | XP, class XP, keepsake item (q ≥ 2) |

Mechanics settled:
- Strict chain q1 → q5 (prev/next quest ids). Dash before the first flight, so Vigor is known.
- The prototype is the player's to keep; abandoning q4 keeps it, q4 can be re-accepted. A lost or
  destroyed prototype can be asked from Elowen again (gossip option while the player has none and
  q4 is accepted or done). *(Claude's call)*
- Updrafts are area triggers with `updraft_strength`; a column as tall as the climb lifts the player
  all the way, shown as a particle column. New area triggers need the ClientDB copy.
- q4's markers are area triggers floating in mid-air where no jump or fall from the wall passes
  through them; credit via `on_enter_trigger` → `QuestExplorationCredit` with a `QuestStatus`
  condition, as in the Haven Road quests.
- Safe way down: gliding; the flag in q5 sits where a walkable way down also exists.
- The hand-over uses `srcitemid`; `starttriggers` is not used (it never runs).

## Goals

- Players reach the glider and Dash by playing, from level 10; later gliders change look and flight.
- The character window shows the glider slot cleanly in every locale.
- Server and client agree on glide physics for every glider (no rubber-banding, no false kicks).
- All texts in every locale (enUS, deDE at least; deDE uses Ihr/Euch/Euer).

## Non-goals

- Mount slot, mount categories, mount abilities (the travel row only leaves room for them).
- Glider crafting, glider vendors, more glider items than the prototype (data can add them later).
- Free climbing; terrain reshaping around Haven.

## Anti-goals

- Gliding for players while the anti-cheat gaps are open.
- A slot change that renumbers existing inventory or breaks a rollback to the previous build.
- A quest completable without gliding; a quest that strands the player.
- Glide stats that let data make a glider climb without updrafts or exceed the validator.

## Constraints

- Feature branch `feature/skywright-quests`, stacked on `feature/glider-visuals`; data submodules on
  their own branches, landed on master before develop is pushed.
- Game protocol bump with `tools/protocol_version_check.py --update` and `docs/protocol_versions.md`.
- Realm DB migrations additive only.
- Next free ids (data master 1fe20f8, re-check on develop): quests 95, units 151, objects 24, object
  displays 16, items 181, item displays 240, area triggers 14, triggers 144, gossip menus 33,
  conditions 27, unit loot 37, spells 2009; sound entries 454 (450–453 taken by the glider visuals).
- Quest data via the mmo-quest-creator / npc / item skills; `tools/world/lint.py` and `report.py`
  after placing; `tools/xp_audit.py` coverage ≥ 120 % (it skips exploration and object quests).
- Quest rewards and collect objectives never grey.

## Left to the builder

Exact positions and heights (Keep walkable collision checked first), XP/money/class XP amounts, stat
caps and percentages, quest texts, NPC names besides Elowen Marsh, the slot icon art, the keepsake.
