# Requirements: MMO Project

**Defined:** 2026-03-31
**Core Value:** A fully functional, self-hosted MMO server/client stack where players can authenticate, enter a 3D world, fight creatures, cast spells, manage inventory, and group with other players.

## v1 Requirements

Requirements for the next milestone: **Game Systems Polish & Playable Loop**. Each maps to roadmap phases.

### Triggers & Scripting

- [ ] **TRIG-01**: All trigger actions in trigger_handler are implemented (SetWorldObjectState, SetVirtualEquipmentSlot, SetPhase, Dismount, SetMount, SetSpawnState for ObjectSpawner, SetCombatMovement)
- [ ] **TRIG-02**: Trigger system supports world object state changes (open/close doors, activate/deactivate objects)
- [ ] **TRIG-03**: Lua scripting covers quest events, NPC dialog customization, and world object interaction hooks

### Quests

- [ ] **QUEST-01**: Player can discover quest givers via visual indicators (!, ?) above NPC heads on the client
- [ ] **QUEST-02**: Player can read quest description, objectives, and rewards before accepting
- [ ] **QUEST-03**: Player can track multiple active quests in a quest log UI
- [ ] **QUEST-04**: Quest objectives update in real-time (kill counts, item collection progress)
- [ ] **QUEST-05**: Player can turn in completed quests and receive rewards (items, XP, gold, spells)
- [ ] **QUEST-06**: Quest chains work end-to-end (completing one quest unlocks the next)

### NPC Interaction

- [x] **NPC-01**: Player can interact with vendor NPCs to buy and sell items
- [x] **NPC-02**: Player can interact with trainer NPCs to learn abilities
- [x] **NPC-03**: Gossip menus support multi-page dialog trees
- [x] **NPC-04**: NPC interaction enforces range and line-of-sight checks

### Chat

- [ ] **CHAT-01**: Player can send and receive Say messages (nearby players)
- [ ] **CHAT-02**: Player can send and receive Yell messages (wider radius)
- [ ] **CHAT-03**: Player can send and receive Group/Raid chat messages
- [ ] **CHAT-04**: Player can send and receive Guild chat messages
- [ ] **CHAT-05**: Player can send whisper messages to other players by name

### Groups & Social

- [ ] **GRP-01**: Player can invite others to a group and accept/decline invitations
- [ ] **GRP-02**: Group members share XP from creature kills
- [ ] **GRP-03**: Group loot rules work (round-robin, master loot, or free-for-all)
- [ ] **GRP-04**: Guild roster displays online status and member ranks
- [ ] **GRP-05**: Guild leader can promote, demote, invite, and remove members

### Character Progression

- [ ] **PROG-01**: Player gains XP from killing creatures and completing quests
- [ ] **PROG-02**: Player levels up when XP threshold is reached, with stat increases
- [ ] **PROG-03**: Player can learn new spells/abilities at level-up milestones or from trainers
- [ ] **PROG-04**: Level-appropriate content is enforced (quest level requirements, creature difficulty scaling)

### Loot & Economy

- [ ] **LOOT-01**: Defeated creatures drop loot based on configured loot tables
- [ ] **LOOT-02**: Player can loot items and gold from creature corpses
- [ ] **LOOT-03**: World objects (chests, gathering nodes) provide loot when used
- [ ] **LOOT-04**: Group loot distribution follows configured rules (need/greed, round-robin)
- [ ] **LOOT-05**: Vendor buy/sell prices work for basic economy

### World Objects

- [x] **WOBJ-01**: World objects respond to player interaction (doors open, chests give loot)
- [ ] **WOBJ-02**: Quest-required world objects are only usable when the quest is active
- [ ] **WOBJ-03**: World objects can be enabled/disabled by triggers and scripts

### Editor Workflows

- [ ] **EDIT-01**: Editor supports placing and configuring creature spawns with patrol paths
- [ ] **EDIT-02**: Editor supports placing and configuring world object spawns
- [ ] **EDIT-03**: Editor supports editing trigger chains with visual feedback
- [ ] **EDIT-04**: Editor supports quest creation and linking (objectives, rewards, chains)

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
| TRIG-01 | Phase 1 | Pending |
| TRIG-02 | Phase 1 | Pending |
| TRIG-03 | Phase 1 | Pending |
| QUEST-01 | Phase 2 | Pending |
| QUEST-02 | Phase 2 | Pending |
| QUEST-03 | Phase 2 | Pending |
| QUEST-04 | Phase 2 | Pending |
| QUEST-05 | Phase 2 | Pending |
| QUEST-06 | Phase 2 | Pending |
| NPC-01 | Phase 3 | Complete |
| NPC-02 | Phase 3 | Complete |
| NPC-03 | Phase 3 | Complete |
| NPC-04 | Phase 3 | Complete |
| CHAT-01 | Phase 4 | Pending |
| CHAT-02 | Phase 4 | Pending |
| CHAT-03 | Phase 4 | Pending |
| CHAT-04 | Phase 4 | Pending |
| CHAT-05 | Phase 4 | Pending |
| GRP-01 | Phase 5 | Pending |
| GRP-02 | Phase 5 | Pending |
| GRP-03 | Phase 5 | Pending |
| GRP-04 | Phase 5 | Pending |
| GRP-05 | Phase 5 | Pending |
| PROG-01 | Phase 6 | Pending |
| PROG-02 | Phase 6 | Pending |
| PROG-03 | Phase 6 | Pending |
| PROG-04 | Phase 6 | Pending |
| LOOT-01 | Phase 7 | Pending |
| LOOT-02 | Phase 7 | Pending |
| LOOT-03 | Phase 7 | Pending |
| LOOT-04 | Phase 7 | Pending |
| LOOT-05 | Phase 7 | Pending |
| WOBJ-01 | Phase 3 | Complete |
| WOBJ-02 | Phase 3 | Pending |
| WOBJ-03 | Phase 3 | Pending |
| EDIT-01 | Phase 8 | Pending |
| EDIT-02 | Phase 8 | Pending |
| EDIT-03 | Phase 8 | Pending |
| EDIT-04 | Phase 8 | Pending |

**Coverage:**
- v1 requirements: 37 total
- Mapped to phases: 37
- Unmapped: 0

---
*Requirements defined: 2026-03-31*
*Last updated: 2026-03-31 after initialization*
