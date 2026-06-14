<required_reading>
Read these reference files now:
1. `references/quest-data-surface.md`
2. `references/quest-runtime-semantics.md`
3. `references/quest-authoring-patterns.md`
4. `references/quest-json-format.md`
</required_reading>

<process>
1. Map the chain before editing:
   - entry quest
   - follow-up quests
   - branch points or class splits
   - provider and turn-in transitions
   - reward pacing by level
2. Inspect the adjacent live quests and export every quest that will be touched. Do not edit only the new quest if existing prerequisite or follow-up rows also need chain-field changes.
3. Use the quest fields intentionally:
   - `prevquestid` for hard prerequisite completion
   - `nextchainquestid` for immediate handoff after reward when the same questgiver also offers the next step
   - `nextquestid` only when the runtime or UI path actually expects it
   - `exclusivegroup` only for mutually exclusive branches
4. Keep the chain readable to players:
   - use delivery or trainer handoff quests to move the player to a new hub
   - escalate reward XP and item value with level and friction
   - avoid repeated objective texture without changing context, target mix, geography, or scripted beats
5. Validate every touched quest draft individually, then apply them in prerequisite order when the changes interact.
6. Re-inspect the chain after apply and verify the prerequisite graph and provider wiring on every touched step.
</process>

<success_criteria>
This workflow is complete when:

- Every touched quest in the chain has consistent prerequisite and follow-up fields.
- Provider and turn-in transitions work across the whole chain, not just inside each row.
- Reward pacing and objective escalation are coherent for the targeted level band.
</success_criteria>
