# Warrior spell presentation audit and implementation

Date: 2026-09-06. Status: first playable presentation pass installed; in-game art/audio acceptance still required.

## What was missing

The audit followed Warrior class 1, class grants, warrior trainers, warrior talent tabs/ranks, class masks and triggered-spell dependencies in the live editor catalogs. It found 14 active combat abilities / 15 spell entries including both Strike ranks. Of those entries, **11 had no visualization reference, all 15 had no particle kits, and all 15 had no usable sound kits**. Three entries had animation kits: Cleave reused Wounding Strike, while Provoke and Demoralizing Shout reused generic caster visualization 9. Bloodrush used visualization 4, containing an empty sound path and a red aura tint.

Cleave's old target animation was also attached to CAST_SUCCEEDED, but the client dispatches that event with an empty target list, so that target kit could not run. Auto-attack already has a separate animation controller; an absent visualization on Attack 153 is not evidence that auto-attacks lack animation.

Passive talents, armor/weapon proficiencies, Dodge and Block should remain silent when learned or restored on login. Open, Open Door and class-change utility spells are not warrior combat presentation and were left alone. Empty Deep Wound visualization entries 5 and 6 are not referenced by this audited warrior ability set; their existence alone does not make them active warrior skills.

## Installed presentation

The class identity is physical: steel sparks, brief earthy dust, subdued dark-red aggression and muted gold/steel rally cues. No fireballs, lingering spell fog, constant lights, or large idle auras. All particle files are finite bursts (12–42 particles per invocation), with zero continuous emission and no looping sound. Particles fade within about 0.7 seconds. Effects are feedback, not radius telegraphs.

Every ability has a dedicated new visualization. All existing shared visualization entries remain intact. Only `visualization_id` changed on spell records; gameplay, localization, ranks, training and talent placement were preserved. Both `data/editor/data/` and `data/client/ClientDB/` were updated using their respective protobuf schemas.

| Ability (spell IDs → visualization) | Before | Installed now | Best next art improvement |
|---|---|---|---|
| Strike (8, 71 → 26) | No link | Steel spark burst at target torso; short metallic thump on IMPACT. Existing next-swing attack animation remains responsible for the swing. | A restrained weapon-edge flash at the actual contact notify; weapon-specific 1H/2H swings. |
| Battlecry (9 → 27) | No link | Gold rally burst and low air/thump cue on caster; brief burst on each aura recipient. CastRelease is a temporary gesture. | Chest-forward shout, clenched fist or raised weapon; expanding amber pressure ring, no permanent glow. |
| Rend (19 → 28) | No link | Attack_1H_01; small dark-red target torso burst and dry tearing/noise impact, delayed 100 ms. | Curved red slash and a few droplets; tiny silent bleed ticks only after a real aura-tick event is available. |
| Charge (48 → 29) | No link | Departure dust and accelerating-air-style cue on successful cast. Movement animation is left to the existing movement controller. | Forward-lean charge animation, short ground trail and an arrival-only shoulder impact. |
| Execute (50 → 30) | No link | Attack_1H_02 over 650 ms; larger steel/amber impact with weightier thump, delayed 160 ms. | Distinct overhead finishing blow and tight red cut accent at contact. |
| Bloodrush (62 → 31) | Tint + empty sound | Brief red activation burst and low pulse. No overlay animation to interrupt attacking. | Clenched-fist exertion, restrained red breath/veins; a quiet heartbeat layer if duration-based audio is added. |
| Crippling Strike (69 → 32) | No link | Attack_1H_01; red impact attached to target foot_l, sharing Rend's sound. | Dedicated low leg sweep; brief ankle slash, no magical chains. |
| Skullbash (70 → 33) | No link | UnarmedAttack01 over 400 ms; sharp metallic/blunt sound and head-attached sparks, delayed 70 ms. | Actual headbutt/elbow animation and concise interrupt snap. |
| Shockwave (122 → 34) | No link | UnarmedAttack01 gesture; caster-centered dust burst with low earth impact, delayed 120 ms. Works without an explicit unit target. | Dedicated stomp and expanding ground ring with stone chips. Current dust is local feedback, not the 10 m damage boundary. |
| Shield Slam (142 → 35) | No link | UnarmedAttack01 over 500 ms; heavy target impact with resonant metallic thump, delayed 100 ms. | Left-arm shield bash with shield-rim sparks. Current animation is a fallback punch. |
| Last Stand (205 → 36) | No link | Steel-blue activation burst and low resonant cue; no attack-disrupting gesture. | Brief brace/plant-feet animation and restrained shield-shaped flash. |
| Cleave (209 → 37) | Reused attack animation | Attack_1H_02 over 600 ms; warm burst from hand_r with broad whoosh at 80 ms; primary target sparks at 130 ms. | Proper sweeping weapon arc and impact on every confirmed hit target. |
| Provoke (216 → 38) | Generic cast | Short CastRelease fallback, dark earthy caster/target bursts and sharp pressure cue. | Point/challenge animation, single aggressive bark, small target marker. |
| Demoralizing Shout (217 → 39) | Generic cast | CastRelease fallback, darker outward caster burst and deeper pressure cue; aura recipients get a short silent burst. | Harsh intimidating roar and desaturated expanding pressure ring. |

Humanoid impact sockets `spine_03`, `head`, and `foot_l` were checked against all four player skeletons. The existing service falls back to the actor root when a creature lacks a socket; that is functional but may place an impact near its feet. Cleave uses the verified `hand_r` socket.

The short delays above are an initial feel adjustment, not synchronization to a verified animation contact frame. They must be tuned while playing. The implementation does not change when damage occurs.

## Original sounds supplied

Thirteen original, deterministic synthesized WAVs are installed in `data/client/Sound/Spells/Warrior/`. These contain filtered noise, short resonant transients and low impact bodies. They use no downloaded recordings, sampled commercial effects or cloned voices. They are **stylized first-pass sound design**, not recordings of real steel, flesh or a person shouting. The shout filenames currently contain non-vocal emphasis layers; actual voices are still missing.

Format: mono PCM16, 44.1 kHz; 0.28–0.85 seconds; peak amplitudes 0.29–0.46, leaving mixing headroom. All are one-shots. Crippling Strike deliberately shares Rend; Strike ranks share Strike. No sound plays on every bleed tick or aura recipient.

Listen to `generated/warrior_visuals/sound_preview.wav`. Playback order, with half-second gaps: Strike, Rend, Execute, Skullbash, Shield Slam, Cleave, Charge, Shockwave, Battlecry, Bloodrush, Provoke, Demoralizing Shout, Last Stand.

`tools/warrior_visuals/build_assets.py` regenerates all ten HPAR files, thirteen WAVs and the listening reel. It requires NumPy and has no dependency on the separate, pre-existing `tools/particle_gen/` work. The HPAR writer matches the current version-2 engine serializer. Existing particle materials are reused.

## Sound acquisition and generation plan

My priority would be to replace the common Strike/weapon-contact layer first, then add real vocal barks, then finish shield/earth detail. A frequently repeated convincing hit contributes more than a long spectacular cooldown sound.

| Cue | Desired final sound | Ready-to-use generation brief |
|---|---|---|
| Strike / Cleave | Fast blade air, compact steel contact, leather/armor body; Cleave has a broader air sweep | Single fantasy melee sword strike, fast dry whoosh and compact metal-on-armor impact, close isolated foley, 0.4 seconds, no music, no voice, no ambience, minimal reverb. |
| Rend / Crippling | Dry cloth/flesh tear with a small blade scrape, restrained gore | One short slicing impact through leather and flesh, dry scrape and subtle wet detail, 0.35 seconds, isolated game foley, no scream, no music, no tail. |
| Execute | Heavy blade contact with low body and short brittle crack | One powerful two-handed overhead weapon impact, heavy organic thump and brief steel bite, 0.6 seconds, tight cinematic game foley, no explosion, no music, no voice. |
| Skullbash / Shield Slam | Blunt snap versus heavier hollow shield impact, tiny metal rattle | Single heavy shield bash, solid wooden body with steel rim rattle, short close blunt impact, 0.45 seconds, isolated, no voice, no music, no long ringing. |
| Charge | Armor/leather movement, brief air rush; separate arrival hit | Short armored warrior rushing forward, fast cloth and chain movement with an air rush, 0.6 seconds, dry isolated game foley, no footsteps loop, no impact, no voice. |
| Shockwave | Boot/stone strike and short gravel pressure burst; no thunder | One heavy boot stomping stone, low physical thump and outward grit scatter, 0.8 seconds, dry isolated fantasy game effect, no explosion, no magical chime, no voice. |
| Battlecry | Confident nonverbal rally shout; short chest resonance | One adult warrior's forceful rallying shout, nonverbal, determined rather than hurt, 0.8 seconds, dry close studio recording, no crowd, no music, no reverb. Generate separate masculine and feminine performances. |
| Provoke | Clipped, contemptuous challenge bark | One short aggressive nonverbal warrior challenge bark, 0.35 seconds, close dry recording, no words, no pain, no crowd, no music. |
| Demoralizing Shout | Rough intimidating roar, deeper and longer than Provoke | One adult warrior's intimidating battle roar, 0.9 seconds, powerful breath and rasp, human rather than monster, isolated dry studio sound, no crowd, no music. |
| Bloodrush / Last Stand | Controlled exertion, breath and leather strain; different emotional intent | One short determined exertion grunt with armored clothing tension, controlled effort rather than pain, 0.5 seconds, dry isolated recording, no words, no music. |

Verified source suggestions (2026-09-06):

- **Paid weapon foley:** [BOOM Medieval Weapons](https://www.boomlibrary.com/sound-effects/medieval-weapons/). Its [official sound list](https://www.boomlibrary.com/metadata/00_Medieval_Weapons_DS_Metadata.pdf) contains broadsword hits, metal shield hits, wooden shield hits and armor impacts. These are specific starting points for Strike, Shield Slam and Execute. Audition the product's demos before choosing a pack. [Medieval Melee](https://www.boomlibrary.com/sound-effects/medieval-melee/) is another focused combat library.
- **Free search pool:** [Sonniss GameAudioGDC archive](https://sonniss.com/gameaudiogdc/). Search downloaded metadata for sword, whoosh, cloth, impact, metal, grunt and gravel. It is a broad archive, not a promise that every requested warrior cue is included. Keep its [bundle license](https://sonniss.com/gdc-bundle-license/) with chosen assets; the license grants royalty-free use subject to its terms.
- **Generate from the briefs:** [ElevenLabs Sound Effects](https://elevenlabs.io/sound-effects). Its [documentation](https://help.elevenlabs.io/hc/en-us/articles/25735182995985-What-is-Sound-Effects) supports text prompting and duration control. Use an appropriate commercially licensed plan for shipped game content; check the [publication rules](https://help.elevenlabs.io/hc/en-us/articles/13313564601361-Can-I-publish-the-content-I-generate-on-the-platform) for the actual plan/service. No connected generative-audio tool was available here, so the supplied WAVs were synthesized locally instead.
- **Vocal performances:** A short voice-actor recording session with the nonverbal briefs above is my preferred option for consistent race/sex sets. [BOOM Crowds — War & Battle](https://www.boomlibrary.com/sound-effects/crowds-war-battle/) offers battle vocal material, but it is a crowd-oriented collection; audition for isolated usability rather than assuming it provides individual player barks.

Record or generate 3–5 variations per frequent cue, at a consistent microphone distance and intensity. Export trimmed mono WAVs with short anti-click fades, retain natural attack transients, and avoid baking in stereo positioning or a long room tail. New recordings can replace the current filenames after auditioning. Keep source/license metadata alongside any acquired audio.

## Remaining engine and animation work

1. **Animation coverage is the largest visual asset gap.** Human male/female both have Attack_1H_01, Attack_1H_02, UnarmedAttack01 and CastRelease. Only human female has EmoteRoar. Orc male has UnarmedAttack01 but no armed attacks or CastRelease; orc female has only Idle. The service skips missing animations with a warning, while particles/audio still work. Import/retarget the missing base combat set for the orcs, plus shout, stomp, left-arm shield bash, low sweep and 2H attacks across all playable rigs. Do not relabel a punch as a finished shield-slam animation.
2. **Select animation by weapon/rig.** SpellKit currently holds one literal animation name. The 1H attack fallback is therefore not an appropriate finished 2H presentation. A semantic animation action with per-rig/weapon resolution would avoid copying kits per equipment type. Current animation overlays and movement blending need live QA.
3. **Charge arrival timing.** For instant spells the client dispatches IMPACT directly from SpellGo, not from physical arrival. No fake arrival burst was added. Bind a dedicated arrival event to charge movement completion before adding collision audio or target dust.
4. **Confirmed hits and multiple targets.** Instant IMPACT uses the explicit unit target, not the complete resolved hit list. Cleave cannot reliably show its secondary impact; Shockwave's caster burst and Demoralizing Shout's aura events avoid depending on that list. It also means presentation is not authoritative evidence of a successful hit versus miss/avoidance. Add confirmed hit-result target data and contact-notify timing before claiming exact synchronization.
5. **Target attachment fallback.** Standard humanoid sockets were verified; animals may use different names. A normalized impact-height/socket resolver would make torso and leg effects reliable on every creature.
6. **No sustained visual aura state in this pass.** Brief activation feedback plus the existing aura UI avoids introducing persistent tint cleanup/order problems. Bloodrush's former full-body red tint is replaced by a short pulse. Last Stand has no persistent shield dome; Rend has no repeating blood cloud.
7. **Sound variation and voice identity need support.** Repeated SpellKit sounds are played together, not randomly selected. Do not put five variations in that list. Add a sound-entry/random selection reference and race/sex-aware voice dispatch before wiring recorded variants. Current non-vocal layers are intentionally character-neutral.
8. **Final texture art.** Current sparks/smoke reuse existing materials. Purpose-made slash, dust-ring and droplet textures, with a brief mesh/ribbon arc for Cleave, will be more recognizable than these generic bursts.

Two supporting runtime changes are included in `spell_visualization_service.cpp`: finished one-shot emitters and their owned nodes are retired during Update, including when audio is unavailable; one-shot audio starts at authored volume instead of losing its transient to a 0.33-second fade-in. The latter affects all spell one-shots, so listen to existing caster spells during QA as well. Looping audio still fades normally. These changes have been compiled, not exercised in a live combat session here.

## Validation and handoff

- All 15 spell JSON drafts passed the skill validator before apply.
- Editor/client schemas parse the result and all 14 new visualization definitions match across catalogs.
- Only visualization references changed on the 15 spell entries; all other spell fields and unrelated spell entries were compared to pre-apply backups.
- Sound and particle paths resolve; sounds are finite mono PCM WAVs without clipping; particle files are bounded non-looping bursts and were parsed with an independent existing HPAR reader.
- Approximate offline preview sheets were inspected for burst size and decay. These are not game renders and do not validate material appearance, bone attachment, animation timing or the combat audio mix.
- Debug `game_client` and full `mmo_client` builds succeeded after the final runtime change. The initial build encountered a temporary compiler output lock; the successful rebuild supersedes it.
- No live in-game visual or listening acceptance was performed. The generated sound reel is provided for listening review.

Reopen the editor before saving catalogs so an already-open editor does not write stale data over the changes. Restart the newly built client to reload ClientDB. The editor and client assets are separate Git submodules; retain their modifications as well as the root scripts/documentation/runtime change when committing. No commits or server deployment were made.

For iteration, run `python tools/warrior_visuals/build_assets.py`, then `python tools/warrior_visuals/author_visuals.py` to produce inspectable drafts. `--apply` validates and installs them with backups under `generated/warrior_visuals/backup_*`. Generated drafts, backups and the listening reel are local ignored outputs; the durable authoring sources are in `tools/warrior_visuals/` and installed assets are in the data submodules. Do not restore an entire backup over newer unrelated work; copy only the intended visualization references/entries.

### Focused in-game acceptance route

Use a human warrior first with 1H/shield. Compare both Strike ranks, then Rend, Crippling Strike, Execute, Skullbash and Shield Slam. Confirm one effect per cast/contact and no stuck movement/overlay after rapidly chaining attacks. Test Charge at minimum and maximum range; only its departure should emit in this pass. Test Cleave on two enemies and Shockwave/Demoralizing Shout on several enemies without a selected target. Cast Battlecry with party members, then Bloodrush and Last Stand during combat. Repeat on both human models and available orc models, with a 2H weapon as an explicit fallback check. Finally spam impacts on a surviving target, change targets/despawn them and inspect emitter/node counts; check existing mage one-shot sounds for appropriate levels after the fade correction.
