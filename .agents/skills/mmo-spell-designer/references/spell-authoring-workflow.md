<overview>
Use this workflow when creating or editing a spell for this project. It is optimized for AI-assisted iteration against the live data files instead of ad hoc reasoning.
</overview>

<workflow>
1. Identify the gameplay pattern first.
   Examples: passive stat aura, periodic DOT, triggered proc aura, item-on-equip buff, class spell rank, reagent-based utility spell.

2. Find the closest existing spell.
   Use `scripts/inspect_spell_catalog.py` with `--name-contains` or a known `--spell-id`.

3. Export the existing spell to JSON.
   Use `scripts/export_spell_json.py` and treat the result as the editable draft.

4. Edit the minimal surface.
   Usually this means changing only:
   - name or description
   - cost, cooldown, levels, restrictions
   - one or more effect payload fields
   - proc fields
   - visualization or category references

5. Validate the JSON draft.
   Use `scripts/validate_spell_json.py`.

6. Apply the draft only after validation passes.
   Use `scripts/apply_spell_json.py --backup`.

7. If the spell should be trainer-taught, inspect the target trainer first and then update `trainers.data`.
   Use `scripts/inspect_spell_catalog.py --section trainers`, then `scripts/upsert_trainer_spell.py` or `scripts/remove_trainer_spell.py`.

8. If the spell should be talent-granted, inspect the target tree first and then update `talents.data`.
   Use `scripts/inspect_spell_catalog.py --section talents` or `--section talent_tabs`, then `scripts/upsert_talent_entry.py` or `scripts/edit_talent_ranks.py`.

9. Re-inspect the applied spell and its linked references.
   Confirm class or race masks, trigger spell wiring, reagent items, categories, auras, item references, trainer references, and talent references if applicable.
</workflow>

<heuristics>
Prefer these design shortcuts:

- For a new rank, start from the previous rank and adjust numeric tuning plus rank linkage.
- For a new proc aura, start from an existing proc aura and change only the trigger spell or proc gates.
- For a new periodic effect, copy an existing spell with the same tick rhythm and aura family.
- For an equipment-driven spell, inspect item `OnEquip` references that already do something similar.
- For a trainer-taught spell, inspect an existing trainer spell row for the same class or profession and copy its cost or requirement pattern.
- For a talent spell, inspect an existing neighboring talent in the same tree and copy its placement and rank-count pattern before changing spell IDs.
</heuristics>

<verification>
A spell draft is ready when:

- every referenced ID exists
- every effect has a meaningful runtime handler
- aura or proc logic has been checked against runtime semantics
- the draft validates cleanly
- the applied result can be re-inspected from live data
</verification>
