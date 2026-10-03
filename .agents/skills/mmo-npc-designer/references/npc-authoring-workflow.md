<overview>
The safest creature workflow in this repository is inspect -> clone -> edit -> validate -> apply -> re-inspect.
</overview>

<process>
1. Inspect the live unit and its dependencies first.
2. Clone the closest matching unit to JSON when editing an existing family of NPCs.
3. Edit the smallest set of fields necessary.
4. Validate against live catalogs before any write.
5. Apply base unit and linked tables.
6. Apply spawns only when placement changes are requested.
7. Re-inspect the final live data and compare it against the requested behavior.
</process>

<cross_skill_coordination>
Use the supporting MMO skills instead of bloating NPC drafts with invented IDs:

- Need a new drop or vendor item: use `mmo-item-designer`.
- Need a new creature spell, passive, aura, or trainer spell: use `mmo-spell-designer`.

Bring the confirmed IDs back into the NPC draft only after those workflows finish successfully.
</cross_skill_coordination>

<loot_design>
Loot tables are reusable modules: a creature lists several tables in `unitlootentries`, and each one
is rolled independently (see `npc-runtime-semantics.md` for how chances inside a group behave).

- **Family base table.** Every creature family has one shared base table that all of its members
  link, whatever their level: e.g. `Boar Base Loot` (id 0: boar meat, then tusk / bristles / tooth),
  `Wolf Base Loot` (id 17). It holds the family's meat and body-part junk (fangs, teeth, fur, hide).
  New members of a family link the existing table; never fork a per-creature copy.
- **Level-band equipment tables.** Shared tables of low-value gear by level band, e.g.
  `Trash Loot - Level 1+` (20, grey cloaks/robes/weapons), `Low Level Greens - Level 3+` (1),
  `Open World Greens - Level 6+` (18). Link the bands matching the creature's level range in
  addition to the family base table.
- **Drop rate target.** An ordinary mob should drop something on nearly every kill (about 95%).
  Reach that with the base table (e.g. meat group 85%, parts group 65%), not by inflating gear.
- **Keep entry-level loot boring.** Level 1-5 creatures drop grey/white junk and food; greens stay
  rare (single-digit % per kill).
- **Animals never drop money.** Beasts and other animals (boars, wolves, bears, spiders, ...) must
  only link tables with `minmoney = maxmoney = 0`, and the unit's `minlootgold`/`maxlootgold` stay 0.
  Money comes from humanoids. Do not put money on a family base table for animals, and do not link a
  money-bearing table to an animal. (Unit `type` is not populated in the data, so this cannot be
  checked automatically; check it by hand.)
- **Shared tables change many creatures.** Before editing a shared table, list every unit that links
  it and make sure the change is right for all of them.
- Quest collect objectives and rewards must never use grey items; give quests their own white
  quest item instead of a family junk item.
</loot_design>

<anti_patterns>
- Do not invent a faction template when an existing one already matches the intended diplomacy.
- Do not create a vendor or trainer by filling `npcflags` without linked rows.
- Do not put unrelated loot outcomes into one giant loot group if the intended behavior is mutually exclusive rolls.
- Do not use unknown `script_name` values unless the corresponding C++ combat script actually exists.
- Do not append unnamed spawns repeatedly when the real intent is to update one existing placement. Use `replace_by_map_and_index` for cloned unnamed spawns or add stable spawn names.
</anti_patterns>
