# Requirements: MMO Project

**Defined:** 2026-03-31
**Core Value:** A fully functional, self-hosted MMO server/client stack where players can authenticate, enter a 3D world, fight creatures, cast spells, manage inventory, and group with other players.

## v1 Requirements

Requirements for the next milestone: **Game Systems Polish & Playable Loop**. Each maps to roadmap phases.

### Triggers & Scripting

- [x] **TRIG-01**: All trigger actions in trigger_handler are implemented (SetWorldObjectState, SetVirtualEquipmentSlot, SetPhase, Dismount, SetMount, SetSpawnState for ObjectSpawner, SetCombatMovement)
- [x] **TRIG-02**: Trigger system supports world object state changes (open/close doors, activate/deactivate objects)
- [x] **TRIG-03**: Lua scripting covers quest events, NPC dialog customization, and world object interaction hooks

### Quests

- [x] **QUEST-01**: Player can discover quest givers via visual indicators (!, ?) above NPC heads on the client
- [x] **QUEST-02**: Player can read quest description, objectives, and rewards before accepting
- [x] **QUEST-03**: Player can track multiple active quests in a quest log UI
- [x] **QUEST-04**: Quest objectives update in real-time (kill counts, item collection progress)
- [x] **QUEST-05**: Player can turn in completed quests and receive rewards (items, XP, gold, spells)
- [x] **QUEST-06**: Quest chains work end-to-end (completing one quest unlocks the next)

### NPC Interaction

- [x] **NPC-01**: Player can interact with vendor NPCs to buy and sell items
- [x] **NPC-02**: Player can interact with trainer NPCs to learn abilities
- [x] **NPC-03**: Gossip menus support multi-page dialog trees
- [x] **NPC-04**: NPC interaction enforces range and line-of-sight checks

### Chat

- [x] **CHAT-01**: Player can send and receive Say messages (nearby players)
- [x] **CHAT-02**: Player can send and receive Yell messages (wider radius)
- [x] **CHAT-03**: Player can send and receive Group/Raid chat messages
- [x] **CHAT-04**: Player can send and receive Guild chat messages
- [x] **CHAT-05**: Player can send whisper messages to other players by name

### Groups & Social

- [x] **GRP-01**: Player can invite others to a group and accept/decline invitations
- [x] **GRP-02**: Group members share XP from creature kills
- [x] **GRP-03**: Group loot rules work (round-robin, master loot, or free-for-all) *(partial: creatures only — chest loot fix in Phase 9)*
- [x] **GRP-04**: Guild roster displays online status and member ranks
- [x] **GRP-05**: Guild leader can promote, demote, invite, and remove members

### Character Progression

- [x] **PROG-01**: Player gains XP from killing creatures and completing quests
- [x] **PROG-02**: Player levels up when XP threshold is reached, with stat increases
- [x] **PROG-03**: Player can learn new spells/abilities at level-up milestones or from trainers
- [x] **PROG-04**: Level-appropriate content is enforced (quest level requirements, creature difficulty scaling) *(partial: quest/item level gates implemented; creature con-color and difficulty scaling not yet done)*

### Loot & Economy

- [x] **LOOT-01**: Defeated creatures drop loot based on configured loot tables
- [x] **LOOT-02**: Player can loot items and gold from creature corpses
- [x] **LOOT-03**: World objects (chests, gathering nodes) provide loot when used
- [ ] **LOOT-04**: Group loot distribution follows configured rules (need/greed, round-robin) *(partial: creatures only — chest loot fix in Phase 9)*
- [x] **LOOT-05**: Vendor buy/sell prices work for basic economy

### World Objects

- [x] **WOBJ-01**: World objects respond to player interaction (doors open, chests give loot)
- [x] **WOBJ-02**: Quest-required world objects are only usable when the quest is active
- [x] **WOBJ-03**: World objects can be enabled/disabled by triggers and scripts *(partial: state triggers wired; per-spawn trigger_id fire on use in Phase 9)*

### Editor Workflows

- [x] **EDIT-01**: Editor supports placing and configuring creature spawns with patrol paths
- [ ] **EDIT-02**: Editor supports placing and configuring world object spawns *(partial: loot_entry wired; trigger_id runtime wiring in Phase 9)*
- [x] **EDIT-03**: Editor supports editing trigger chains with visual feedback
- [ ] **EDIT-04**: Editor supports quest creation and linking (objectives, rewards, chains) *(not built — Phase 10)*

## v2 Requirements

Deferred to future milestone. Tracked but not in current roadmap.

### PvP & Competitive

- **PVP-01**: Player vs player combat with damage, CC, and death
- **PVP-02**: Battleground/arena instanced PvP

### Economy & Trading

- **ECON-01**: Auction house for player-to-player item trading
- **ECON-02**: Direct player-to-player trade window
- **ECON-03**: Mail system with item attachments

### Social

- **SOC-01**: Friends list with online status tracking
- **SOC-02**: Ignore list to block chat from specific players
- **SOC-03**: Achievement system

### Polish

- **POL-01**: Talent/skill tree system
- **POL-02**: Crafting professions
- **POL-03**: Mount system (visual mount display, speed increase)

## Out of Scope

Explicitly excluded. Documented to prevent scope creep.

| Feature | Reason |
|---------|--------|
| Voice chat | Not planned for this project |
| Mobile client | Desktop only (Windows/macOS) |
| Microtransaction store | Not applicable |
| Cross-server gameplay | Single-realm focus first |
| Linux client | Only server-side Linux via Docker |
| World PvP zones | PvE focus for v1 |
| LFG/matchmaking queue | Manual group formation first |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| TRIG-01 | Phase 1 | Complete |
| TRIG-02 | Phase 1 | Complete |
| TRIG-03 | Phase 1 | Complete |
| QUEST-01 | Phase 2 | Complete |
| QUEST-02 | Phase 2 | Complete |
| QUEST-03 | Phase 2 | Complete |
| QUEST-04 | Phase 2 | Complete |
| QUEST-05 | Phase 2 | Complete |
| QUEST-06 | Phase 2 | Complete |
| NPC-01 | Phase 3 | Complete |
| NPC-02 | Phase 3 | Complete |
| NPC-03 | Phase 3 | Complete |
| NPC-04 | Phase 3 | Complete (partial — LOS pending) |
| CHAT-01 | Phase 4 | Complete |
| CHAT-02 | Phase 4 | Complete |
| CHAT-03 | Phase 4 | Complete |
| CHAT-04 | Phase 4 | Complete |
| CHAT-05 | Phase 4 | Complete |
| GRP-01 | Phase 5 | Complete |
| GRP-02 | Phase 5 | Complete |
| GRP-03 | Phase 5 → Phase 9 | Partial (chest loot in Phase 9) |
| GRP-04 | Phase 5 | Complete |
| GRP-05 | Phase 5 | Complete |
| PROG-01 | Phase 6 | Complete |
| PROG-02 | Phase 6 | Complete |
| PROG-03 | Phase 6 | Complete |
| PROG-04 | Phase 6 | Complete (partial — difficulty scaling pending) |
| LOOT-01 | Phase 7 | Complete |
| LOOT-02 | Phase 7 | Complete |
| LOOT-03 | Phase 7 | Complete |
| LOOT-04 | Phase 7 → Phase 9 | Pending (Phase 9) |
| LOOT-05 | Phase 7 | Complete |
| WOBJ-01 | Phase 3 | Complete |
| WOBJ-02 | Phase 3 | Complete |
| WOBJ-03 | Phase 3 → Phase 9 | Partial (trigger_id runtime in Phase 9) |
| EDIT-01 | Phase 8 | Complete |
| EDIT-02 | Phase 8 → Phase 9 | Partial (trigger_id runtime in Phase 9) |
| EDIT-03 | Phase 8 | Complete |
| EDIT-04 | Phase 10 | Pending (Phase 10) |

**Coverage:**
- v1 requirements: 37 total
- Mapped to phases: 37
- Complete: 30
- Partial: 5 (NPC-04, GRP-03, PROG-04, WOBJ-03, EDIT-02)
- Pending gap closure: 2 (LOOT-04 → Phase 9, EDIT-04 → Phase 10)
- Unmapped: 0

---
*Requirements defined: 2026-03-31*
*Last updated: 2026-03-31 after initialization*
