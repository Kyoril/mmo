<required_reading>
Read these reference files now:

1. `references/npc-data-surface.md`
2. `references/npc-runtime-semantics.md`
3. `references/npc-json-format.md`
4. `references/npc-authoring-workflow.md`
</required_reading>

<process>
1. Decide the NPC's interaction surface: questgiver, quest finisher, vendor, trainer, binder, gossip-only, or a combination.
2. Choose a faction template that makes the NPC friendly, neutral, or selectively available to the intended player races.
3. Use a confirmed model ID. If no bespoke model exists, use a safe default such as the live human male or female NPC model IDs already present in `model_data.data`.
4. Wire service behavior through linked data:
   - Vendor service: set `unit.vendorentry` and author or reuse `vendor_entry`.
   - Trainer service: set `unit.trainerentry` and author or reuse `trainer_entry`.
   - Gossip service: populate `unit.gossip_menus` and author or reuse the linked menu rows.
   - Quest service: populate `unit.quests` and `unit.end_quests`.
5. If a gossip option should branch into trainer or vendor UI, use the gossip action types from the live project instead of inventing behavior.
6. If a gossip option should fire scripted behavior, confirm the unit has the relevant trigger rows and that the trigger event data matches the menu and option IDs.
7. Validate the draft with `scripts/validate_npc_json.py`.
</process>

<success_criteria>
- The NPC's service behavior is driven by linked trainer, vendor, gossip, and quest data.
- The faction template and model IDs were confirmed from live data.
- Triggered or conditioned gossip behavior references real condition and trigger rows.
- The JSON validates cleanly before apply.
</success_criteria>
