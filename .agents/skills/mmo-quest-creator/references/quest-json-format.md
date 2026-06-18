<overview>
The quest tooling in this skill reads and writes one JSON document per quest. The document is authoritative for the quest row and for quest-specific provider, ender, object-gating, trigger, and area-trigger wiring.
</overview>

<document_shape>
```json
{
  "format": "mmo-quest",
  "version": 1,
  "quest": {
    "...": "QuestEntry fields"
  },
  "providers": {
    "unit_ids": [15],
    "object_ids": []
  },
  "enders": {
    "unit_ids": [15],
    "object_ids": []
  },
  "gated_object_ids": [1],
  "attached_triggers": [
    {
      "...": "TriggerEntry fields"
    }
  ],
  "attached_area_triggers": [
    {
      "...": "AreaTriggerEntry fields"
    }
  ]
}
```
</document_shape>

<field_semantics>
- `quest`: required. Parsed as `QuestEntry`.
- `providers.unit_ids`: units whose `quests` list should contain this quest ID after apply.
- `providers.object_ids`: objects whose `quests` list should contain this quest ID after apply.
- `enders.unit_ids`: units whose `end_quests` list should contain this quest ID after apply.
- `enders.object_ids`: objects whose `end_quests` list should contain this quest ID after apply.
- `gated_object_ids`: objects whose `requiredquest` should equal this quest ID after apply.
- `attached_triggers`: optional trigger rows to upsert into `triggers.data`.
- `attached_area_triggers`: optional area trigger rows to upsert into `area_triggers.data`.
</field_semantics>

<authoritative_wiring>
Apply treats the provider and ender sections as authoritative for this quest ID:

- it removes the quest ID from every other unit or object provider list before adding it back to the listed providers
- it removes the quest ID from every other unit or object ender list before adding it back to the listed enders
- it clears `requiredquest == quest.id` from objects not listed in `gated_object_ids`

This makes exported drafts safe to edit and reapply without having to manually clean up old wiring.
</authoritative_wiring>

<practical_guidance>
- Export a live quest first whenever possible.
- Keep only one quest per file.
- Store drafts under `F:/mmo/generated/quests/` unless the user asked for another location.
- Use lowercase snake-case filenames ending in `.json`.
- Prefer attached trigger or area-trigger rows only when the quest change also owns those rows. If a trigger is shared by many content rows, inspect it first before editing it in a quest draft.
</practical_guidance>
