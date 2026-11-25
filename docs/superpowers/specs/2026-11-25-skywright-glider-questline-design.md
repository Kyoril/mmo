# The Skywright of Haven — glider questline (design)

Status: approved by the user 2026-11-25. Builds on the traversal pillar
(`docs/superpowers/specs/2026-11-21-traversal-pillar-design.md`) and the glider visuals
(`docs/superpowers/specs/2026-11-22-glider-visuals-design.md`, branch `feature/glider-visuals`).

## Intent

The glider (spell 1436) and Dash (spell 1437) work, but only a GM can learn them. They should
reach players through a short questline in and around Haven, available from level 10, so that
gliding becomes something players earn and remember about Alestia rather than a hidden tool.

Level 10 is where today's content ends (Haven Road 78–94, Hollow Choir 57–65); the questline is the
first thing to do after it.

## Story (proposed by Claude, accepted by the user)

In Haven's harbour lives an eccentric inventor, **Master Elowen Marsh, "the Skywright"**. For years
she has studied the flight of gulls and bats and built wings of ash wood and sailcloth. The Thorn
smugglers had an eye on her plans: with gliders, any town wall can be crossed. Now that the ring has
been broken (q94), she needs someone to help her finish the glider and be the first to fly it.

Tone matches the Haven Road content: grounded, a little humorous, no magic involved in the glider
itself — it is craft and courage.

## The quests (all level 10, min level 10)

| # | Working title | Giver → ender | What the player does | Reward |
|---|---|---|---|---|
| 1 | Wings over the Basin | Watch-Captain Corwin Ashby (u98, Haven north gate) → Elowen Marsh | Breadcrumb: Ashby mentions "the mad woman with the wings" in the harbour; visit her workshop. Ashby's quest text links to the end of the Thorn story; the Thorn chain is **not** a prerequisite. | char XP, class XP, small money |
| 2 | Ash and Sailcloth | Elowen → Elowen | Gather seasoned ash staves (world objects) in the river valley; collect waxed sailcloth from a sailmaker NPC in the harbour (talk/deliver). | char XP, class XP, money |
| 3 | The Stolen Folio | Elowen → Elowen | Surviving Thornhand smugglers stole her notebook; recover it at Thornhand Landing (kill + loot, quest item). | char XP, class XP, **Dash (1437)** — "so you outrun the thieves next time" |
| 4 | First Flight | Elowen → Elowen | Elowen hands over the prototype (**Glider 1436 is learned on accepting this quest**). Jump from the Keep wall and glide through three ribbon markers down into the lower court (exploration objectives, airborne). Teaches open, close and Vigor. | char XP, class XP |
| 5 | On Warm Air | Elowen → Elowen | Use new updrafts around the harbour (above the smithy chimneys, at Gate Island) to climb to the top of Haven Keep and raise her flag there (object use). | char XP, class XP, a cosmetic/keepsake item (q≥2, see quest item rule) |

Quest 4 makes the glider the player's for good. A temporary "prototype" glider was rejected as
much more engineering for little gain (Claude's call, accepted by the user).

## Goals

- Players reach Glider and Dash by playing, from level 10.
- Every quest rewards **character XP and class XP** (`rewardxp` and `rewardclassxp`; user rule).
- The questline teaches the mechanics in order: Vigor via Dash (q3), opening/closing/landing (q4),
  updrafts (q5).
- Updrafts and markers become part of Haven's world, visible with the updraft particle columns.
- All texts in every locale (enUS, deDE at least; deDE addresses the player with Ihr/Euch/Euer).

## Non-goals

- Mount categories, mount abilities, free climbing (pillar spec: later).
- New zones or terrain reshaping around Haven; only placed NPCs, objects and area triggers.
- A glider skin or crafting system.

## Anti-goals (would make this a failure even if it works)

- Gliding unlocked for players while the known anti-cheat gaps are open (see Constraints): a
  speed/fly-hack surface on the live realm.
- A quest that can be completed without gliding (e.g. markers reachable on foot) — the line must
  teach the glider, not skip it.
- A quest that strands the player (on the Keep roof, on Gate Island) without a way down.

## Constraints

- **Ungating for players** requires the open items from the traversal test field, in the same
  branch or before it merges:
  - scale the glide rise tolerance by elapsed time (server glide validation);
  - clamp client timestamps in the glide speed check;
  - creature AI against airborne players (no permanent evade/leash loops, no unreachable-target exploits);
  - an E2E scenario covering the glide path (open, glide, updraft, close, landing without fall damage).
- Glider/Dash spells stay as they are (1436/1437); the quests grant them (`srcspell` or reward spell
  on accept for q4, `rewardspell` for q3), no GM path removed.
- Quest data follows the mmo-quest-creator skill patterns (`.agents/skills/mmo-quest-creator/`):
  editor data and ClientDB copies byte-identical where both exist, ids appended.
- Next free ids at the time of writing: quests from 95 (verify on develop at branch time); units,
  objects, items, area triggers, sound entries likewise appended (sounds 450–453 are taken by the
  glider visuals).
- Quest rewards and collect objectives never use grey items (quest item quality rule).
- Feature branch `feature/skywright-quests`, stacked on `feature/glider-visuals` (not yet shipped;
  shipping that branch first stays the user's call); fast gate before /ship; data submodules on their own branches, landed on master before
  develop is pushed.
- World placement: Haven town WMO at (−689, 1025), north gate z ≈ 950, lower court z ≈ 976, Keep
  z ≈ 1060+; Gate Island between the two bridges before the north gate; Thornhand Landing on the
  west bank above the harbour (bible §3.3, §3.13).

## Approaches considered

- **Glider as the final reward only** (q5): the player would have to glide in q4 without owning
  the glider → needs a temporary aura/item-based glider. Rejected: more engine work for no player gain.
- **Glider learned on accepting q4 (chosen):** one data field, the tutorial uses the real glider.
- **Prerequisite on the Thorn chain (q94):** rejected; level 10 is the only gate, so players who
  came via the Hollow Choir are not locked out.

## Mechanics settled (builder check, 2026-11-25)

- **Updrafts are data:** area triggers with `updraft_strength` (field 14, both protos; client DB is a
  byte copy), sphere or yaw-rotated box. Strength is the upward speed; a unit inside rises as long as
  it stays inside, so a column as tall as the climb lifts the player all the way (several stacked
  volumes are also fine). The client shows each one as a particle column (UpdraftVisuals). Code is on
  develop since 0959b8ea.
- **Order:** strict chain q1 → q2 → q3 → q4 → q5 (prev/next quest ids). Dash (q3) comes before the
  first flight (q4) so Vigor is known. *(Claude's call)*
- **Glider on accept of q4 is permanent;** abandoning q4 does not take it away, and q4 can be
  re-accepted. *(Claude's call: the spell is the player's from then on)*
- **q4 markers count only from the air** by placement: the three marker area triggers float in
  mid-air, far enough out from the Keep wall and high enough above the court that a jump or fall
  without the glider cannot pass through them, and no walkable surface touches them. No new engine
  condition. *(Claude's call)*
- **Safe way down:** the player owns the glider by q4/q5, so gliding down is the way back; the Keep
  top used in q5 must also have a walkable way down (stairs) or the flag is placed where one exists.
  The builder checks the Keep's walkable collision before placing. *(Claude's call)*

## Decided by the user

- Questline in or around Haven, from level 10 (2026-11-25).
- Every quest gives class XP as well as character XP (2026-11-25).
- Story as above (2026-11-25).
- The ungating work (glide rise tolerance, timestamp clamp, creature AI vs airborne players, glide
  E2E) is built in the same branch as the questline, so both go live together (2026-11-25).

## Left to the builder

- Exact NPC/object/marker/updraft positions and heights (needs a look at the Keep's walkable roof and
  the harbour terrain; the user signs off placements visually).
- XP and money amounts (keep `tools/xp_audit.py` coverage ≥ 120%; class XP sized like the other
  level-10 quests).
- Quest texts in enUS/deDE, NPC names besides Elowen Marsh, the sailmaker's name.
- Whether the flag in q5 is a world object state change or a spawned object.
- The keepsake item for q5.
