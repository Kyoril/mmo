<overview>
Quest authoring in this repository spans multiple catalogs. `quests.data` stores the quest row, but availability, turn-in, gated objects, and scripted completion all live elsewhere too.
</overview>

<authoring_sources>
- `data/editor/data/quests.data`: `QuestEntry` rows and quest-local fields.
- `data/editor/data/units.data`: unit quest providers in `quests`, unit quest enders in `end_quests`, and quest-accept or scripted behavior through `triggers`.
- `data/editor/data/objects.data`: object quest providers or enders, plus `requiredquest` for quest-gated world objects such as herb nodes or crates.
- `data/editor/data/triggers.data`: trigger rows that can react to quest acceptance or complete a quest through `QuestEventOrExploration`.
- `data/editor/data/area_triggers.data`: volume triggers that can execute trigger rows when the player enters a region.
- `data/editor/data/maps.data`: object spawns and optional per-spawn trigger overrides for world objects.
- `src/shared/proto_data/quests.proto`: the authoritative quest schema.
</authoring_sources>

<quest_entry_fields>
Important `QuestEntry` fields in `quests.proto`:

- identity and text: `id`, `name`, `internalname`, `detailstext`, `objectivestext`, `offerrewardtext`, `requestitemstext`, `endtext`
- level and gates: `minlevel`, `maxlevel`, `questlevel`, `requiredraces`, `requiredclasses`, `requiredskill`, `requiredskillval`
- chain structure: `prevquestid`, `nextquestid`, `exclusivegroup`, `nextchainquestid`
- objectives: repeated `requirements`
- rewards: `rewarditemschoice`, `rewarditems`, `rewardmoney`, `rewardspell`, `rewardspellcast`, `rewardxp`
- quest startup and flow helpers: `srcitemid`, `srcitemcount`, `srcspell`, `timelimit`, `flags`, `type`
- trigger references: `starttriggers`, `failtriggers`, `rewardtriggers`
</quest_entry_fields>

<objective_runtime_limit>
Quest progress uses the four packed `QuestField.counters` bytes in `src/shared/game/quest.h`. Treat four requirements as the real hard limit for counter-based objectives, even though protobuf itself would allow more rows.
</objective_runtime_limit>

<quest_requirement_shapes>
Each `QuestRequirement` can model one of these patterns:

- item collection: `itemid + itemcount`
- hidden item drop dependency: `sourceid + sourcecount`
- creature kill credit: `creatureid + creaturecount`
- spell cast on object: `objectid + objectcount + spellcast`
- scripted or exploration display text: `text`

The current editor UI only exposes item, creature, and freeform text objectives. `objectid`, `objectcount`, `spellcast`, and some other advanced fields exist in the schema and runtime, but are not surfaced by the quest editor window.
</quest_requirement_shapes>

<provider_and_ender_wiring>
Questgivers and turn-in targets are not stored in `QuestEntry`. They are discovered by scanning:

- `UnitEntry.quests`
- `UnitEntry.end_quests`
- `ObjectEntry.quests`
- `ObjectEntry.end_quests`

If these link fields are missing, the quest row exists but players cannot accept or turn it in through normal gameplay.
</provider_and_ender_wiring>

<gated_world_objects>
World objects can be visible or usable only while a quest is active through `ObjectEntry.requiredquest`. Live examples:

- object `1` `Silverleaf Sprig` requires quest `2`
- object `2` `Lina's Crate` requires quest `9`

This is how the current repository gates quest-only interactables such as collection nodes and recovery crates.
</gated_world_objects>

<live_examples>
Representative live quest examples in the current repository:

- quest `1` `The Boar Problem`: simple kill quest
- quest `2` `Gathering Healing Herbs`: item collection gated through quest-only world objects
- quest `3`, `4`, `7`, `21`: handoff or trainer notification quests with no counter objective
- quest `8` `Hands of the Light`: scripted quest completed through a trigger flow after healing an injured guard
- quest `19` `Foundations of Progress`: two-item collection turn-in
- quest `20` `Discover Haven`: area-trigger completion
- quest `22` `Lessons in Steel`: scripted arena quest with exploration-style completion and trigger-driven combat flow
</live_examples>

<area_trigger_examples>
Relevant live area triggers:

- area trigger `2` `New Area Trigger` uses trigger `12`, which completes quest `20`
- area trigger `5` `Warrior Lv 10 Quest Start Trigger` uses trigger `16`, which starts the scripted arena flow for quest `22`
</area_trigger_examples>
