# Human Racial Abilities — Design

Date: 2026-10-03
Branch: `feature/channel-end-notify` (current branch, by user request). Submodule data commits
(`data/client`, `data/editor`) land directly on the submodules' `master`.

## Goal

Give each playable people a small racial kit — one or two passives and one active ability — so
that peoples feel distinct once more than one is playable. Humans are the only playable people
today, so this round builds the grant mechanism and the human kit.

Tone (from `docs/world/bible.md`): the humans of Falwyn are practical, dry and humane; heroism
is duty and competence rather than glory. The Watch guards the roads, the Crown owns them.

## The human kit

| Kind | Name (deDE / enUS) | Effect |
|---|---|---|
| Passive | **Vielseitigkeit / Versatility** | +2% Strength, Agility, Stamina, Intellect and Spirit |
| Passive | **Harte Arbeit gewohnt / Used to Hard Work** | +10% health regeneration |
| Active | **Ruf der Wache / Call of the Watch** | Instant. You and party members within 30 yd gain +5% damage dealt and −5% damage taken for 10 s. 2 min cooldown. |

French and Russian names and texts are added too: every spell row in the data carries all four
locales (deDE, enUS, frFR, ruRU), and new rows follow suit.

### Spell data

All three spells:
- `racemask = 0x80000000` (Human, race id 0 — the mask encoding is `1 << (id - 1)`, which the
  existing data already relies on for id 0).
- `classmask = 0`, so the multi-class system treats them as persistent across class switches.
- No power cost, no reagents, no level requirement (`spelllevel`/`baselevel` 1).

Passives:
- Attribute `Passive`, not `Ability`; they show in the spellbook's General tab.
- *Versatility*: five `ApplyAura` effects, `ModStatPct` (aura 31), basepoints 2, `miscvaluea`
  0–4, target caster, infinite duration.
- *Used to Hard Work*: one `ApplyAura`, `ModHealthRegenPercent` (aura 38), basepoints 10.
- Both carry `HiddenAura`, like talent passives, so two permanent buffs do not clutter the
  buff bar. They are read from the spellbook (icon + description).
- Icons (unused so far):
  - *Versatility* `T_Icon_Gold_17` (scales)
  - *Used to Hard Work* `T_Icon_Gold_07` (hammer)
  - *Call of the Watch* `T_Icon_Gold_84` (winged crest)

Active (*Call of the Watch*):
- Attribute `Ability`, so it is auto-placed on the action bar of new characters and lands in the
  Abilities tab.
- Two `ApplyAreaAura` effects targeting the caster: `ModDamageDonePct` (aura 23) +5 and
  `ModDamageTakenPct` (aura 24) −5. `rangetype = 5` (30 yd) drives the party propagation radius
  in `AuraContainer::HandleAreaAuraTick`. `dmgclass` stays None so the damage-taken reduction
  covers all damage.
- Duration 10 000 ms, cooldown 120 000 ms, triggers the global cooldown, `positive`.
- Solo the buff applies to the caster only; in a group it propagates to party members in range
  and is removed when they leave range (existing area-aura behaviour).

## Grant mechanism (new code)

New field in `src/shared/proto_data/races.proto`:

```proto
// Spells every character of this race knows regardless of class (racial abilities).
repeated uint32 racialSpells = 22;
```

`initialSpells` (keyed by class) is deliberately not reused: racials would have to be listed
five times, and class legality falls back to inferring allowed classes from it.

Honoured in three places:
1. **Character creation** — `src/realm_server/player.cpp` (around the class-spell loop at
   line ~672): append the race's `racialSpells` to `spellIds` (deduplicated), and let them take
   part in action-bar auto-placement under the same rule as class spells (non-passive, not
   hidden, `Ability`).
2. **Login backfill** — `src/realm_server/player.cpp` (~3542): after the class-spell backfill,
   add any missing racial spell. Existing human characters thereby learn the kit on next login.
   No action-bar change for existing characters.
3. **Editor** — the race editor window in `mmo_edit` gets a list editor for `racialSpells`
   with spell pickers, following the pattern of the existing list fields there.

If the world server has its own "missing class spells" pass (`GamePlayerS::GrantClassSpells`)
that could strip or miss racials, it is checked and extended so racials survive class switches.

No wire format changes: spells reach the world server through the existing character spell
list. No `ProtocolVersion` bump. `src/shared/client_data/races.proto` is not touched — the client
does not need the racial list.

## Area-aura duration fix

`AuraContainer::HandleAreaAuraTick` copies the caster's aura to party members with the full
`m_duration` (confirmed in code). A member who leaves range and returns therefore gets a
fresh 10 s, even when the caster's aura is about to expire.

The fix: propagated copies start with the caster aura's remaining time, via the existing
`SetInitialRemainingTime`. A timed aura with no time left is not propagated at all, because
an initial remaining time of 0 would fall back to the full duration.

## Visualization of *Call of the Watch*

New, built for this spell (the passives get none).

Colour language: warm gold with a steel-blue accent — the Watch and the Crown, not holy light
(keep it distinct from the cleric's palette).

- **Caster, on cast:**
  - A shout or battle-cry animation from the human rigs if one exists; clip lengths are
    measured from the `.skel` rather than guessed. Leave `duration_ms` unset.
  - An onset flash at chest height.
  - A gold ground ring expanding outwards. Its size and alpha curves must hold alpha through
    the growth phase.
  - Rising gold/steel motes, like banner sparks.
- **Each buffed unit (aura applied):** a short, low-alpha upward burst. Optionally a very subtle
  loop at the feet while the aura lasts; drop the loop if it reads as clutter.
- **Particles** are authored with the particle-author skill and `tools/particle_gen`:
  - Recipe → build → preview → look → tune.
  - Use the `Particle_*.hmi` alpha materials, never `Additive.hmat`.
  - Volumetric layers at peak alpha 0.2–0.45; sparks and the onset flash may run bright.
- **Sound:** a new effect generated via ElevenLabs with `tools/sfx_gen`.
  - The prompt is layered: a short brass war-horn call, a shield/steel ring, a low drum hit and
    a short tail, at `prompt_influence` ~0.75.
  - Prompts stay under 450 characters.
  - Postprocessing is mandatory.
  - Generate four takes, pre-select them on measurements, then send the user an audition reel
    for the final pick.
  - The sound is routed through the `SoundEntry` catalog (`SpellKit.sound_ids`) with a new
    SoundEntry.
  - It plays on cast only, never on aura events, because aura events replay when units come
    into view.
- Visualization data is written to both editor data and ClientDB (the dual-write rule).

## Testing

- **Unit tests** (`game_server_tests`):
  - The racial-spell helper merges the list without duplicates and skips unknown spell ids.
  - The action-bar predicate.
  - The area-aura propagation-time function.
  - `realm_server/player.cpp` itself is not compiled into any test suite, so the logic lives
    in `game_server`.
- **E2E scenario** `e2e/scenarios/human_racials.lua`:
  - A fresh human character knows all three spells.
  - Casting *Call of the Watch* applies its aura and puts the spell on cooldown.
  - After switching to another class, all three racials are still known and castable.
  - The passive auras are hidden and cannot be observed from the client. Passive application
    uses the same path as every other passive (Dodge, talents).
- **Data tests:** the new spells carry all four locales, racemask Human and classmask 0.
- Gate: `/gate` must be green before `/ship`.

## Balance framework for further peoples

Numbers checked against the class data (2026-10-03): base stats at the current level cap 10 are
17–35, so *Versatility* is worth roughly 0–1 point per stat today. At ~300 in a main stat it
would give about +6 to the main stat and +6 Stamina. That is less than a specialised
"+5% one stat" racial (+15) for the classes that want that stat, but it helps every class.

This is the intended trade for humans, who can switch freely between classes. *Used to Hard
Work* is deliberately modest, since it acts mostly out of combat.

Rule for future peoples: each people gets one **broad** and one **narrow** passive. Humans
take the broadest option. Other peoples should each lean towards a role. Candidates that the
server already supports:

| Archetype | Example | Aura type |
|---|---|---|
| Specialist | +5% to one stat (Stamina for a hardy people, Intellect for a learned one) | `ModStatPct` |
| Magic ward | +25 resistance to one or all schools, or −2% magic damage taken | `ModResistance`, `ModDamageTakenPct` + dmgclass |
| Nimble | +1% dodge | `ModDodgeChance` |
| Toughness | −2% chance to be critically hit | `ModCritChanceTaken` |
| Scholarly | +10% mana regeneration | `ModPowerRegenPercent` |
| Fleet | +3% run speed | `ModSpeedAlways` |
| Instinct | chance on hit to gain a short buff | `ProcTriggerSpell` |

With new code, later options are an XP bonus, crit chance and weapon skill. Revisit
*Versatility* when the level cap rises or when the second people becomes playable.

## Out of scope

- Racials for Orc / Undead Human (disabled races) — the mechanism supports them, content later.
- New aura types (XP gain, threat, max-health %, crit chance), dispel-by-mechanic.
- Icons beyond picking from the existing icon set.
