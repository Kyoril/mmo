# Mage spell presentation

Date: 2026-10-02. Status: particles, sounds, visualization kits and two engine fixes installed;
in-game art/audio acceptance still required.

Follows the warrior pass ([warrior_spell_visuals.md](warrior_spell_visuals.md)) and keeps its
rules: no kit sets `duration_ms`, kits use `sound_ids` (catalog entries) rather than raw paths,
stylized-fantasy particles built from density at low alpha because the renderer has no additive
blending and no bloom.

## What was there before

Class 0 (Mage) has 14 active abilities across 17 spell entries (three Frostbolt ranks, the Fire
Barrage projectile, and the Chilled slow from Frost Armor). Before this pass:

- Frostburn, Arcane Intellect, Frost Nova, Arcane Disruption and Arcane Pulse used the shared
  "Default Spell Visualization" (9): a cast animation and nothing else.
- Fireball used "Default Projectile Spell Vis" (8): an untextured sphere mesh.
- Sleep, Conjure Lesser Manastone, Fire Barrage and Chilled had no visualization at all.
- Frostbolt and Ice Lance shared one entry (3) whose trail was `Particles/Testing.hpar` and whose
  impact used `FrostImpact.hpar`, which renders as opaque rectangles (`Additive.hmat` is Unlit).
- Every sound was a legacy file path; none went through the sound catalog.

## Art direction

One identity per school, visually and sonically, so spells are distinguishable at a glance and
with eyes closed:

| School | Particles | Audio |
|---|---|---|
| Frost | white-hot core, pale cyan, deep azure; snowflake stars, stretched shards, low mist | glassy chimes, cracking ice, cold wind, bell-like tails |
| Fire | yellow-white core, orange, crimson; roiling flame puffs, embers, sparse smoke | ignition whoosh, crackle, deep explosive body |
| Arcane | pink-white core, violet, magenta; orbiting motes, four-point stars, clean rings | resonant tonal chimes, choral-synth pad, glitter |

Recipes: `tools/particle_gen/recipes/mage_{common,frost,fire,arcane}.py`, output in
`data/client/Particles/Mage/`. Sounds: `tools/sfx_gen/recipes/mage.py`, output in
`data/client/Sound/Spells/Mage/`.

## Installed presentation

Visualizations, sound entries and spell links are written by
`tools/mage_visuals/author_mage_visuals.py` (validate by default, `--apply` writes the editor
and ClientDB copies of `sounds.data`, `spell_visualizations.data` and `spells.data`). Unlike the
warrior script it writes `spells.data`, but only `visualization_id`: most mage spells pointed at
shared default entries that clerics and creatures also use. The superseded mage-only entries 1,
3, 10 and 24 are left in place, unreferenced.

| Spell (ids) | Cast | Release / impact | Aura |
|---|---|---|---|
| Frostbolt (3, 18, 158) | CastLoop + FrostHandChannel + frost channel loop | Frostbolt projectile trail; FrostboltImpact at `spine_03` + shuffled impact pair + flash light | cold tint while slowed |
| Ice Lance (187) | -- | IceLanceTrail projectile; IceLanceImpact | -- |
| Frostburn (5) | frost channel | ChilledBurst + sound on the primary target | FrostburnVeil loop |
| Frost Nova (68) | -- | FrostNova ground burst + sound + light | icy tint + FrozenRoot loop |
| Frost Armor (6) | -- | FrostArmorBurst + sound + light | FrostArmorAura loop (30 min, deliberately sparse) |
| Chilled (22) | -- | -- | ChilledBurst + sound + tint on apply |
| Fireball (4) | CastLoop + FireHandChannel + fire channel loop | FireballTrail projectile; FireballImpact + shuffled impact pair + light | FireballBurn loop during the DoT |
| Fire Blast (7) | -- | FireBlastHand at `hand_r`; FireBlastImpact eruption at the target's feet + sound + light | -- |
| Fire Barrage (150) | CHANNELING: `Channel` loop pose + FireHandChannel + fire channel loop, held until the channel ends | -- | none (see below) |
| Fire Barrage Projectile (152) | -- | per wave: FireBlastHand flare at `hand_r` + shot sound, three arcing FireBarrageTrail comets; FireBarrageImpact + quiet impact sound | -- |
| Arcane Intellect (11) | -- | ArcaneIntellectBurst + sound on the target | -- |
| Sleep (72) | CastLoop + ArcaneHandChannel + arcane channel loop | SleepApply at `head` + sound | SleepAura loop above the head |
| Arcane Disruption (75) | -- | ArcaneDisruptionCast at `hand_r` + sound; ArcaneDisruptionImpact at `head` + light | -- |
| Arcane Pulse (198) | -- | ArcanePulse ground burst + sound + light | -- |
| Conjure Lesser Manastone (124) | arcane channel | ManastoneConjure + sound + light | -- |

Every instant also plays `CastRelease`. Orc rigs have neither `CastLoop` nor `CastRelease`; the
service skips the animation and still plays particles, light and sound.

### Rules the data follows, and why

- **No sound on AURA_APPLIED or AURA_IDLE** (validated). AURA_APPLIED fires again whenever a unit
  carrying the aura comes into view, so a buff sound there would replay for every buffed player
  who walks past. Buff feedback with sound sits on CAST_SUCCEEDED or IMPACT. Chilled is the one
  exception by design: it lasts 5 s and is the only feedback the attacker gets.
- **Only CASTING kits loop.** The channel kit carries CastLoop, the hand particles, a hand light
  and a looping catalog sound; CAST_SUCCEEDED / CANCEL_CAST tear all of it down. Looping
  *particles* elsewhere (trails, aura states) need no kit flag -- the `.hpar` loops and the
  engine destroys it on projectile impact or aura removal.
- **Fire Barrage uses CHANNELING, not CASTING.** It is channeled: the server sends SpellStart,
  ChannelStart and SpellGo in the same tick, so a CASTING kit would be spawned and destroyed by
  the SpellGo in the same frame. CHANNELING kits start on ChannelStart and are held until
  ChannelUpdate(0), which every channel end sends (expiry, movement, interrupt, target loss).
  Every triggered 152 wave additionally flares the casting hand.
- **Aura kits are TARGET-scoped** (validated). Aura events pass the aura holder as the target; a
  CASTER-scoped aura kit would be keyed on the caster and never torn down.
- **Fire Barrage has no AURA_IDLE kit.** It applies spell 150 auras to both the caster and the
  target, and an aura-scoped kit plays on whoever holds the aura -- the target would get the
  caster's hand effect.
- **Fire Barrage impacts are quiet.** The projectile manager raises one IMPACT per projectile,
  so a wave produces three impact sounds and particles at once.

## Engine changes

1. **Aura visuals survive the cast ending.** CAST_SUCCEEDED and CANCEL_CAST used to clean up
   every effect the caster had for that spell, including AURA_IDLE effects, so recasting Frost
   Armor stripped its idle visual for the remaining 30 minutes. This branch first fixed it with an
   `auraBound` flag; the cleric branch, merged to develop first, fixed the same bug more generally
   by splitting effects into cast phase and non-cast phase (`spell_visual_rules.h`,
   `IsCastPhaseEvent`), which also protects self-impact effects. The merge keeps only the cleric
   mechanism.
2. **Projectile trails fade instead of popping** (`projectile_manager`). The trail particle
   system was destroyed in the frame of impact, deleting every world-space particle it had left
   behind. On impact the trail now stops emitting and stays at the impact point until its
   particles have died (3 s safety limit).
3. **Visualization state is reset on world leave** (`SpellVisualizationService::Reset`, called
   from `WorldState`). The service is a process-lifetime singleton holding raw pointers into the
   world scene; a record that outlived the scene was dereferenced as soon as the same
   character's guid resolved again after re-login. This predates the branch, but a long-lived
   aura record (Frost Armor) that now survives recasts made it the common path.

4. **Projectiles launch from the hand and fly at the torso** (`projectile_manager`). The client
   ignored `ProjectileVisual.spawn_bone` and started every projectile at the caster's feet,
   aimed at the target's feet; only the editor preview resolved the bone. Projectiles now start
   at the spawn bone's world position as posed at launch (spawning is already delayed to the
   cast animation's release) and aim at the target's `spine_03`, with height fallbacks (1.2 /
   1.0) for models without those bones.
5. **CHANNELING visualization event** (`SpellVisualEvent` 9, both proto copies). Started by
   `SpellVisualizationService::BeginChannel` on ChannelStart, ended by `EndChannel` on
   ChannelUpdate(0). Channel effects are keyed by `spell_visual::ChannelKey` so neither the
   spell's cast-end nor its aura removal touches them, and while a caster channels, terminating
   events (the channel spell's own SpellGo, and every SpellGo of the spells it triggers) leave
   its locked loop animation and loop sound alone. Triggers cannot play it: the trigger editor
   and the world server stop at AURA_IDLE.

None of these touch the wire: visualization contents and event values never cross it; no
`ProtocolVersion` bump.

## Sounds

See `tools/sfx_gen/recipes/mage.py`. 23 sounds, generated with ElevenLabs
`eleven_text_to_sound_v2`, four takes each, preselected on measurements (attack time, peak
position, loop seam). The three channel sounds are generated with the model's `loop` option and
fetched with `fetch.py --loop` (`postprocess.process_loop`): no trimming or one-shot fades, only
a 40 ms *linear* seam crossfade and normalisation. Linear because the two ends of a near-seamless
loop are strongly correlated, and an equal-power blend of correlated signals swells by up to
3 dB mid-fade.

## Remaining work

- In-game acceptance of every effect, especially ring and star sprites (the preview draws every
  sprite as a soft blob) and effects placed by height tricks from a feet origin (SleepAura,
  FireballBurn, ManastoneConjure).
- Frost Nova and Arcane Pulse play their ground effect from CAST_SUCCEEDED on the caster; there
  is no per-target impact for AoE spells (the same confirmed-hit-list limitation as Cleave).
- Projectile ribbon trails and projectile lights still pop on impact; only particle trails fade.
  The editor's preview uses its own projectile manager and does not show the fade.
- One looped sound per actor: an aura expiring on a casting mage can still affect its channel
  loop, depending on whether the aura's visualization owns the loop.
- Orc rigs need cast animations.
