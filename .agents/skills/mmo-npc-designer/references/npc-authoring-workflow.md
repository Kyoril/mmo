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

<anti_patterns>
- Do not invent a faction template when an existing one already matches the intended diplomacy.
- Do not create a vendor or trainer by filling `npcflags` without linked rows.
- Do not put unrelated loot outcomes into one giant loot group if the intended behavior is mutually exclusive rolls.
- Do not use unknown `script_name` values unless the corresponding C++ combat script actually exists.
- Do not append unnamed spawns repeatedly when the real intent is to update one existing placement. Use `replace_by_map_and_index` for cloned unnamed spawns or add stable spawn names.
</anti_patterns>
