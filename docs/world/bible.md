# Alestia Online — World Bible (v1)

Version 1, 2026-09-27. Built from the user's setting notes
([setting-notes-2026-09-27.md](setting-notes-2026-09-27.md)) and the content already in the game
(`python tools/world/extract_canon.py --map 0`). Places are referenced by their atlas id in
`data/world/atlas/map_0.json` (see `tools/world/README.md`).

## 1. How to use this bible

Every statement ends with a tag saying where it comes from:

- **[E] Established.** From the user's notes, or confirmed by the user. This is binding.
- **[C] In-game canon.** Already in the shipped quests, units, items or zones. It is binding until the user changes it. Where possible the tag names its source: `q12` for quest 12, `u41` for unit 41.
- **[?] Open.** An undecided question or a contradiction. Do not build on it without asking; section 12 lists them all.

Rules for agents:

- Agents may add **[C]** statements when they ship content, naming the quest and unit ids. Only the user promotes anything to **[E]**.
- When new content contradicts this document, stop and ask. Do not quietly pick one side.
- **Compass:** north is **−Z** and east is **+X**, as on the in-game minimap (`src/mmo_client/ui/minimap.cpp`). Review maps from `tools/world/render_map.py` are drawn north-up. Always check a direction word against the map before writing it into quest text. [C]

## 2. Identity and tone

- Alestia Online is medieval high fantasy with a grounded, lived-in feel. [E]
- The look is stylized, colourful and hand-painted, not realistic. Locations should feel distinct through colour and texture. [E]
- Local troubles drive the opening region: dangerous wildlife, banditry and kobolds. Ordinary people give places their everyday life: farmers, healers, guards, trainers. [E]
- Inspiration is the sense of place and adventure of classic MMORPGs, especially WoW Classic. [E]
- The world should feel huge, and journeys through it should keep their meaning. [E]
- The in-game tone is practical, dry and humane. People talk about work, harvests, supplies, bounties and rations. Heroism is shown as duty and competence rather than glory ("Steel is easy to swing. Duty is harder to bear." — Baldric Steelheart, q21). [C]
- Wider history and mythology are still undefined. [E]

## 3. Geography

The opening region is **Falwyn Forest** and its neighbour **Briarwatch March**, on map 0 (Development World). The playable area is roughly 1 km × 1.4 km (x −650..930, z −480..980). [C]

### 3.1 Falwyn Forest (zone 1)

- The light forest region containing the human starting experience. [E]
- The zone covers the western lowlands: the Mirewater, the Barrowfield and the far west. Its quest content runs from level 7 to 10. [C]
- Sub-zones listed in the data but with **no terrain**: Mage Tower (zone 5) and Greystone Pass (zone 6). [C]

### 3.2 Oakenshire (zone 2, sub-zone of Falwyn Forest) — atlas `oakenshire_hub`, `forest_camp`

- A settlement near a valley pass, connected by road to Haven. [E]
- It was redesigned because the first version felt too small and too close to Haven. [E]
- In the game, Oakenshire is the first hub (levels 1–6). [C] It has:
  - a **Town Hall** whose ground floor holds the class trainers (q3, q4, q7, q23);
  - an **Inn** run by Old Harbin, with a rat-infested cellar (q10);
  - a **west gate** held by the **Oakenshire Watch** (u46, u47).
- Farms and woodcutters lie in the valley south-east of town, and boars trample them (q1, q5, q6). [C]
- A bandit camp lies in the woods south of town (q9, q24, q31, q33). [C]
- **Forest Camp** is where the human story begins, and the player starts there, not in a town. [E]
  - Its formal name is not settled. [E]
  - In the data, its NPCs (Farmer Haldor, Healer Mirenna, Guard Emrik, the binder Aralin the Kindling) stand at about (300, 550), just west of the Oakenshire trainers (`forest_camp`). [C]
  - Whether Forest Camp is part of Oakenshire or a separate camp is open. [?]

### 3.3 Haven (zone 3; Market, zone 4, is its sub-zone) — atlas `haven` (placeholder)

- A walled town and major human hub, reached around level 10. It has guards, vendors and class trainers. [E]
- **Haven has no terrain yet.** The zone rows exist, but no terrain tile carries its area id. [C]
- The data strongly suggests Haven once stood around the map origin:
  - Five vendors stand at about (0, 21): Eliza Thorne (General Goods), Garret Blackwood (Weapons), Roland Stonehelm (Mail & Plate), Derrick Longstride (Leather Armor) and Mariana Silkspun (Clothier).
  - Injured Guards stand "north east of Haven" near (−55, −45) (q8).
  - Haven's binder is Magister Calloran (u25), who is not spawned.
  - All of these NPCs are buried 3–7 m below the current terrain. [C]
- Where Haven will be built is open. [?]

### 3.4 Westwatch Training Yard

- A training yard run by Armsmaster Theobald Kerrin, "not far" from Oakenshire (q21). The warrior's level-10 trial is fought in its ring (q22). [C]
- In the data it stands at about (46, −48), next to the buried Haven vendors (atlas `haven`). [C]
- "Westwatch" also names armour in the notes. [E] Whether Westwatch is part of Haven or a place of its own is open. [?]

### 3.5 Briarwatch March (zone 8) and its sub-zones

- A frontier march west of Oakenshire's west gate that is being reclaimed. [C]
  - The gatewarden's view: "Now it belongs to whoever is stubborn enough to keep it." (q25) [C]
  - Its quest content runs from level 4 to 7. [C]
- **Thalric's Camp / Briarwatch Camp** (zone 9, atlas `briarwatch_camp`, about (−60, 510)):
  - Foreman Thalric Stonehammer's work camp just beyond Oakenshire's west gate. It raises ditches and palisades (q19, q25). [C]
  - Also here: Quartermaster Harlan Pike, Scout Mirelle Voss, the soul binder Hearthkeeper Maida Thorn, and a full row of class trainers. [C]
- **Ruined Cottages** (zone 10): young wolf packs settle around ruined cottages west along the road (q26). [C] The Kingsroad Waypost also lies in this zone. [C]
- **Ruined Watchtower** (zone 11, atlas `kingsroad`): a hardened bandit gang (Kingsroad Cutthroats and Hexers) nests around the ruined watchtower on the Old King's Road (q27, q39). [C]
- **Deepwood Fringe** (zone 12): the thicker forest where the grown Forest Wolves hunt (q29). [C]
- **Marsh Edge** (zone 13): broken ground and rotten reeds where marsh ambushers lie in wait (q30). [C]

### 3.6 The Old King's Road / Kingsroad — atlas `kingsroad_waypost`, `kingsroad`

- The old road running west from Oakenshire's west gate through Briarwatch March. [C]
- Wayfarer Captain Aldric Vane holds a new **Kingsroad Waypost** on it, at about (−262, 405) (q41). [C]
- Roadside Marauders prey on the wagon trains at night (q42). [C]
- The road "belongs to the King" and its wagons carry the King's seal (q42, q43). [C] See section 4 on the King.
- How this relates to the **Westroad** and **Northroad** named in the notes is open. [?] Those two are placeholder stubs from Oakenshire in the atlas. [E]

### 3.7 The Mirewater — atlas `mirewater`, `mirewater_camp`

- A marsh north-west of the Kingsroad Waypost (the quest text says "southwest"; see section 12). [C]
- Expedition Leader Maren Duskvale charts the old causeways from a camp at about (−330, 230) (q44). [C]
- Its dangers: dog-sized **Bog Rats** hunting in packs (q45), **Mire Prowlers** that mark territory with musk (q46), and **Mirewater Poachers** who buy bandit plunder and lay snares (q43, q47). [C]

### 3.8 The Barrowfield and the Barrowfront — atlas `barrowfield`, `barrowfront`, `barrowfront_camp`

- Old grave mounds west of the Mirewater (q48). The mounds are "older than the kingdom" (q48). [C]
- **Barrowfront Camp:** Chaplain Osric Dawnmere's palisade camp at about (−436, 262), with Scout Brialla Fenn, a soul binder and a row of class trainers. [C]
- The dead walk here:
  - **Risen Laborers**: causeway builders from a hundred years ago (q49);
  - **Barrow Skeletons**: honed into weapons (q50);
  - **Gravebound Cultists** ("gravespeakers"): they sing the dead up with bone fetishes (q51, q52, q64).
- They serve **Barrow-King Morthas**, a warlord buried under the greatest mound four centuries ago (q53). [C]
- A stair under the third mound leads down into **The Hollow Choir** (map 1). [C]

### 3.9 The Hollow Choir (map 1, group dungeon)

- A crypt under the Barrowfield (q57, q58). [C] It is the "dungeon beyond the forest line" planned for the opening slice. [E]
- Its keepers:
  - **Sevrin Wax, the Coffinwright** — "Warden of the Hollow Choir", who seals the dead in and returns to his workbench (q59, q60);
  - **Ossuar, the Bonebinder** — the north ossuary (q58);
  - **Choirmistress Vell** — the south wing, "Voice of the Second Verse" (q58). [C]
- Where its entrance lies on the surface, and the lore behind it, are open. [?] The atlas has a parked `dungeon_entrance` pin.

### 3.10 The wider world

- A possible expansion order was discussed: Heartlands → Northern Mountains → Far Coast → a foreign kingdom → an unknown continent. This is not established. [?]

## 4. Peoples and factions

- **Humans of Falwyn** are the playable people of the opening region. [E] Other playable peoples are open. [?]
- **The Oakenshire Watch** guards Oakenshire's gate and roads (u46, u47, q24: "Take this with the Watch's thanks"). [C]
- **Guards of Haven:** a Haven patrol was ambushed near "the eastern ridge" (q8). Haven Guard gear exists in drafts. [E]/[C]
- **The Crown / the King:** the Old King's Road, the King's wagons and the King's seal (q27, q42, q43), and a *kingdom* younger than the barrows (q48). [C] The notes called a monarchy unconfirmed. [E] See section 12. [?]
- **Forest Bandits** operate in the woods south of Oakenshire. They wear a **red armband marked with a black thorn** (q24), and self-taught **Bandit Mages** ("hedge-wizards with stolen incantations") ride with them (q33). [C] Bandits also ambush travellers on the western approach. [E]
- **Kingsroad bandits** are "no hedge thieves" but a hardened gang with cutthroats and hexers (q27). They are followed by bannerless **Roadside Marauders** (q42). [C]
- **Mirewater Poachers** buy plunder from the road bandits and move it through the reeds (q43). [C]
- **The Mirewater Expedition** (Maren Duskvale, Surveyor Wick Farrow) charts the old causeways. [C]
- **Gravespeakers** (Gravebound Cultists) sing the dead awake. Their numbers regrow ("They are not being recruited. They are being made." — q64). [C]
- **Kobolds** are an established creature group of the region. [E] The only kobold units in the data are marked OBSOLETE and are not spawned (u2, u3). Their role and society are open. [?]

## 5. Named characters

| Name | Role | Where (atlas) | Unit | Gives / ends quests | Layer |
|---|---|---|---|---|---|
| Farmer Haldor | farmer; boars threaten his fields | `forest_camp` | 15 | 1, 5, 6 / 1, 5, 6 | [E][C] |
| Healer Mirenna | healer; sells minor potions after q2 | `forest_camp` | 16 | 2, 7 / 2 | [E][C] |
| Guard Emrik | camp guard; sends new adventurers to their trainers | `forest_camp` | 17 | 3, 4, 23 / – | [E][C] |
| Aralin the Kindling | Oakenshire binder | `forest_camp` | 24 | – | [C] |
| Baldric Steelheart | warrior trainer | `oakenshire_hub` (Town Hall) | 10 | 21, 31, 32 / 3, 31, 32 | [E][C] |
| Vivienne Emberglow | mage trainer | `oakenshire_hub` | 11 | 33, 34 / 4, 33, 34 | [C] |
| Clarissa Whiteshield | cleric trainer | `oakenshire_hub` | 12 | 8, 35, 36 / 7, 8, 35, 36 | [C] |
| Malrik Duskwright | shadowmancer trainer | `oakenshire_hub` | 13 | 37, 38 / 37, 38 | [C] |
| Elira Hawke | scout trainer | `oakenshire_hub` | 38 | 39, 40 / 23, 39, 40 | [C] |
| Milly Goldwyn | clothier | `oakenshire_hub` | 20 | – | [C] |
| Old Harbin | innkeeper of the Oakenshire Inn | `hub_near_old_harbin` | 27 | 10 / 10 | [C] |
| Lina Groveroot | herbalist; robbed on the south road | `hub_near_lina_groveroot` | 26 | 9 / 9 | [C] |
| Guard-Corporal Rowan Hale | Oakenshire Watch, west gate | `oakenshire_west_gate` | 46 | 24, 54 / 24, 54 | [C] |
| Gatewarden Elias Rook | Oakenshire Watch, west gate | `briarwatch_west_gate` | 47 | 25 / – | [C] |
| Armsmaster Theobald Kerrin | runs the Westwatch Training Yard | `haven` (placeholder) | 41 | 22 / 21, 22 | [E][C] |
| Sergeant Bram Halford | veteran; final opponent of q22 | `haven` | 43 | – | [E][C] |
| Thalric Stonehammer | foreman and master builder of the Briarwatch camp | `briarwatch_camp` | 36 | 19, 26, 27 / 19, 25, 26, 27 | [C] |
| Quartermaster Harlan Pike | Briarwatch supplies | `briarwatch_camp` | 53 | 28, 55 / 28, 55 | [C] |
| Scout Mirelle Voss | Briarwatch pathfinder | `briarwatch_camp` | 52 | 29, 30, 41 / 29, 30 | [C] |
| Hearthkeeper Maida Thorn | soul binder | `briarwatch_camp` | 68 | – | [C] |
| Wayfarer Captain Aldric Vane | holds the Kingsroad Waypost | `kingsroad_waypost` | 62 | 42, 43, 44 / 41, 42, 43 | [C] |
| Expedition Leader Maren Duskvale | leads the Mirewater expedition | `mirewater_camp` | 63 | 45–48 / 44–47 | [C] |
| Surveyor Wick Farrow | expedition surveyor; escort in the Barrowfield | `barrowfield` | 80 | 56 / – | [C] |
| Chaplain Osric Dawnmere | holds the Barrowfront Camp | `barrowfront_camp` | 64 | 49, 50, 53, 57–62 / 48–50, 53, 57–62 | [C] |
| Scout Brialla Fenn | Barrowfront scout | `barrowfront_camp` | 65 | 51, 52, 63–65 / same | [C] |
| Lanternkeeper Edran Mosswick | soul binder | `barrowfront_camp` | 69 | – | [C] |
| Barrow-King Morthas | buried warlord; "Lord of the Sunken Barrow" | `barrowfield` | 61 | – | [C] |
| Sevrin Wax, the Coffinwright | Warden of the Hollow Choir | map 1 | 81 | – | [C] |
| Ossuar, the Bonebinder | Keeper of the North Ossuary | map 1 | 84 | – | [C] |
| Choirmistress Vell | Voice of the Second Verse | map 1 | 85 | – | [C] |
| Magister Calloran | Haven binder | not spawned | 25 | – / 20 | [C] |
| Captain Arlen, Brakk the Redhand | draft names without a place | not in data | – | – | [?] |

Class trainers also stand at the Briarwatch camp and the Barrowfront camp:

- Briarwatch camp: Battlemage Serah Wren, Drillmaster Coren Ashfield, Sister Almira Deene, Duskscholar Veslin Marr, Trailmaster Joren Slate (u70–u74). [C]
- Barrowfront camp: War-Adept Ilsa Corvain, Veteran Hargen Dray, Field-Chaplain Rosalind Merek, Duskcaller Nerith Vael, Pathfinder Quinn Aldery (u75–u79). [C]
- The Haven vendors are listed in section 3.3. [C]

## 6. Creatures and threats

| Family | Where | Levels | Meaning in the story | Layer |
|---|---|---|---|---|
| Young Forest Boar / Forest Boar / **Grimtusk, the Ironback** | Oakenshire valley, south-east | 1–5 | Trample farms; Grimtusk is the old herd leader (q6) | [E][C] |
| Cellar Rat | Oakenshire Inn cellar | 2–3 | Nuisance (q10) | [C] |
| Forest Bandit / Bandit Mage | Woods south of Oakenshire | 3–4 | Robbers wearing the black-thorn armband | [E][C] |
| Young Forest Wolf / Forest Wolf | Briarwatch March, Deepwood Fringe | 3–8 | Packs circling the camps; a sickness runs through the deep packs (q35) | [C] |
| Duskfang Stalker | West of the gate | 5–6 | New black-pelted pack; pulls couriers off horses (q54) | [C] |
| Kingsroad Cutthroat / Hexer / Roadside Marauder | Old King's Road, ruined watchtower | 6–7 | Hardened road gang and scavengers | [C] |
| Marshland Ambusher / Hexer | Broken ground by the marsh | 7 | Road bandits turned ambushers (q30) | [C] |
| Bog Rat / Mire Prowler | Mirewater | 7–8 | Marsh beasts preying on the expedition | [C] |
| Mirewater Poacher | Mirewater | 7–8 | Fences for the road bandits | [C] |
| Risen Laborer / Barrow Skeleton / Gravebound Cultist / Barrow-King Morthas | Barrowfield | 8–10 | The raised dead and those who sing them up | [C] |
| Hollow Choir (husks, acolytes, bosses) | Map 1 | 11–12 | The crypt beneath the Barrowfield | [C] |
| Kobolds | – | – | Established threat, not in the data | [E][?] |

## 7. Classes in the fiction

| Class | Identity | Trainers | Layer |
|---|---|---|---|
| Warrior | Martial training; a formal trial of skill (Westwatch Training Yard, q21, q22). "Welcome to the brotherhood of the shield" (q32). | Baldric Steelheart; Theobald Kerrin; Coren Ashfield; Hargen Dray | [E][C] |
| Mage | Discipline over raw power ("Power without discipline burns its bearer first", q33); fire and frost. | Vivienne Emberglow; Serah Wren; Ilsa Corvain | [E][C] |
| Cleric | Healer and holy caster of **the Light** ("The Light is not merely a gift; it is a responsibility", q7). | Clarissa Whiteshield; Almira Deene; Rosalind Merek | [E][C] |
| Scout | Agile fighter with daggers or one-handed swords: "Eyes open, feet quiet" (q40). | Elira Hawke; Joren Slate; Quinn Aldery | [E][C] |
| Shadowmancer | Shadow and damage-over-time magic that feeds on the essence released at death ("the shadow takes", q37). Its students are called **Acolytes** (q38). | Malrik Duskwright; Veslin Marr; Nerith Vael | [E][C] |

The source of magic, its institutions and its costs are undefined. [E] Whether "the Light" is a faith, a force or a church is open (section 12). [?]

## 8. Objects and motifs

- **Silverleaf Sprigs:** a local medicinal herb with a minty scent that grows near large trees. It goes into Healer Mirenna's potions (q2, item 37). [E][C]
- **Letters and dispatches** travel between camps and towns (q3, q25, q41). [E][C]
- **Crates, carts, wagons, mortar and stakes** show that supply and rebuilding matter on the frontier (q9, q19, q42). [E][C]
- **The red armband with the black thorn:** the mark of the southern bandits (q24). [C]
- **Ritual bone fetishes** anchor the gravespeakers' ritual (q51). [C]
- **Morthas' seal:** "not a mark of dominion, but of its ending" (q53). [C]
- **Mine carts** exist as concept art; the transport fiction is undefined (section 11). [E]
- **Field Journal / Bestiary** is a planned zone-organized creature lore book. Its form is open. [E]

## 9. Naming guide

Derived from the names already in the game. [C]

- **People:** given name + family name. Family names are often compounds of nature and craft words: Steelheart, Whiteshield, Emberglow, Duskwright, Stonehammer, Groveroot, Dawnmere, Duskvale.
  - Role titles come first: Farmer, Healer, Guard, Guard-Corporal, Gatewarden, Quartermaster, Scout, Chaplain, Expedition Leader, Wayfarer Captain, Armsmaster, Sergeant, Hearthkeeper, Lanternkeeper.
  - Trainer titles tie the class to the camp: Drillmaster, Battlemage, Sister, Duskscholar, Trailmaster, War-Adept, Veteran, Field-Chaplain, Duskcaller, Pathfinder.
  - Old folk go by a descriptor: "Old Harbin".
- **Named foes:** "<Name>, the <Epithet>" (Grimtusk, the Ironback; Ossuar, the Bonebinder; Sevrin Wax, the Coffinwright). Bosses carry a subname title: "Lord of the Sunken Barrow", "Keeper of the North Ossuary".
- **Creature units:** plain descriptive names made of a place or age word plus a noun: Young Forest Boar, Kingsroad Cutthroat, Mire Prowler, Gravebound Cultist. No digits in unit names.
- **Places:** English compounds built from landscape and settlement words (Oakenshire, Briarwatch, Barrowfield, Barrowfront, Mirewater, Deepwood, Westwatch, Kingsroad), and descriptive phrases ("Ruined Cottages", "the Old King's Road").
- **Spawn names:** `<Place> - <Unit name> NN`, where the place is an atlas place name (for example `Mirewater - Bog Rat 03`). The linter warns about new spawns that break this.
- **Items:** plain and physical (Torn Bandit Armband, Unbroken Wolf Fang, Reinforced Mortar Sack). Quest items are never grey quality.
- **Avoid:** modern words, puns, real-world names and in-jokes (the unit "Gossip Tester" is a test NPC, not canon). Don't use mismatched direction words: check the map.

## 10. Quest-writing guide

- **Voice:** plain and grounded. The NPC speaks to the player (`$N`) as someone with a job to do. Short sentences, concrete nouns, one clear ask. Dry humour is welcome; melodrama is not ("Maybe now my wine won't taste like rat feet." — q10). [C]
- **Text lengths:**
  - details: 2–5 sentences;
  - objectives: 1 sentence naming the target, the count, the place and whom to return to;
  - request: 1–2 sentences;
  - reward text: 1–3 sentences.
  Only milestone quests such as class trials run longer. [C]
- **Every quest ends with a turn-in at an NPC or object.** Gathering or killing never completes a quest on its own, and `AutoRewarded` is rejected by the validator. [E]
- **The arc of the opening region:** a local problem, then the road, then Haven. Small immediate problems (boars, herbs, rats) grow into regional ones (bandits on the road, the raised dead) and point towards the larger world (Haven, the King's road, the crypt). [E][C]
- **Design against the atlas:**
  - Every objective sits in a named atlas place, within about 250 m of its hub; the reachability report checks this.
  - A new place goes into the atlas as a placeholder with an `ask`.
  - Direction words must match the north-up map (north = −Z).
- **Give reasons that connect NPCs.** Most chains hand the player between named people (Emrik → trainers; Rook → Thalric → Vane → Duskvale → Dawnmere). Keep doing that instead of sending players to "a camp".
- **Rewards:** quest rewards and collect objectives are never grey items. Class chains reward the class's starter weapon. [C]

## 11. Travel and scale

- The world should feel huge; journeys keep their meaning. [E]
- A faster transport system without personal mounts is being considered. [E]
- Roads, passes, mine carts, carts and wagons, and ships all came up. [E]
- The Oakenshire–Haven road was sketched at 500–1,000 m. [E]
- No route network, operators or fast-travel rules are established. [?]

## 12. Open questions and contradictions

### From the setting notes [?]

| Topic | Question |
|---|---|
| World | Is Alestia the world, a continent, a kingdom, or only the game? |
| History | What happened before the player arrives, and why does it matter now? |
| Politics | Who governs Haven, Oakenshire and Falwyn Forest? (The game already speaks of a King: see below.) |
| Geography | Where do the Westroad and Northroad lead, and how does the region connect to the wider world? |
| Peoples | Which playable peoples exist, where are their homelands, and how do they relate to humans? |
| Religion and magic | What do people believe, and where do cleric, mage and shadow magic come from? |
| Conflict | What makes the bandits and kobolds more than local enemies? |
| Dungeon | Who built the Hollow Choir, and why must a group enter it? Where is its entrance? |
| Travel | Which forms of transport exist, and who maintains them? |
| Tone | Which hardships and wonders define an ordinary journey? |

### Where the notes and the game disagree [?]

1. **Where the trainers are.**
   - Notes: Haven has trainers for Warrior, Mage, Cleric and Shadowmancer, and the Scout's training place is unknown.
   - Game: all five class trainers teach on the ground floor of **Oakenshire Town Hall** (q3, q4, q7, q23), and more trainer rows stand at the Briarwatch and Barrowfront camps.
2. **An Urgent Request.**
   - Notes: Guard Emrik forwards a letter from Baldric Steelheart and sends a warrior to Haven.
   - Game (q3): Emrik sends the player to Baldric in Oakenshire Town Hall, and there is no letter.
3. **Forest Camp vs Oakenshire.**
   - Notes: the player starts in a small camp, not a town.
   - Game: Haldor, Mirenna and Emrik stand inside the Oakenshire zone, about 40 m from the Town Hall trainers.
4. **The Light and the King.**
   - Notes: "the Light" and "the Crown" were only possible expressions.
   - Game: clerics serve the Light (q7, q8, q35, q36), Haldor swears "By the Light" (q6), and the road, wagons and seals belong to the King (q27, q42, q43). Promote them to [E], or rewrite those texts.
5. **Shadowmancer or Acolyte?** Malrik is a "Shadowmancer Trainer", but his chain is "Path of the Acolyte" and the camp trainers are "Acolyte Trainer"s. Is "Acolyte" the rank of a shadowmancer student, or a different name for the class?
6. **Kobolds** are established in the notes but absent from the game (the only kobold units are OBSOLETE).

### Errors in the game data found while building this bible

7. **q21 "The Warrior's Trial":** the reward text says "So. **Garrick** sent you", but the quest comes from Baldric Steelheart.
8. **q8 "Hands of the Light":** it tells the player to return "to Clarissa Whiteshield **in Haven**", but she teaches in Oakenshire Town Hall.
9. **Compass directions:** some quests have north and south swapped. North is −Z (the minimap).
   - q29 says the Deepwood fringe is "south-west of camp"; the objective is **north-west**.
   - q30 says the broken ground is "south of camp"; it is **north-west**.
   - q44 says the Mirewater camp is "southwest of the Kingsroad Waypost"; it is **north**.
   - q55 says the Mirewater is "to the south"; it is **north-west**.
   - q56 says the Barrowfront palisade is "just north of here"; it is **south**.
   The Oakenshire quests (5, 6, 9, 24, 31, 33) are correct.
10. **q56 "Out of the Barrowfield"** has no NPC that accepts the turn-in (the escort cannot be completed). The content report lists it.
11. **q26:** the young wolves are "around the ruined cottages farther west". The nearest Young Forest Wolves spawn 45–70 m **east** of Thalric's camp, around (−15, 515). The Ruined Cottages (zone 10) lie about 200 m west.

### World facts that need a decision

12. **Haven, Market, Mage Tower and Greystone Pass** are zone rows without terrain. About 95% of map 0's terrain has no zone at all.
13. **The Haven vendors, the Westwatch Training Yard NPCs and the Injured Guards** around the origin stand 3–7 m below the terrain (`tools/world/lint.py`, baselined). They look like leftovers of a removed or never-placed Haven.
14. **Ossuar's spawn** on map 1 is set inactive in the uncommitted `data/editor` working copy (active in the committed data). Nothing else spawns him, so q58 and q61 cannot be completed with the working copy.
