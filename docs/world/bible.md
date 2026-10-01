# Alestia Online — World Bible (v1.1)

Version 1.1, 2026-09-28. Built from:

- the user's setting notes ([setting-notes-2026-09-27.md](setting-notes-2026-09-27.md));
- the user's **layout sketch** of Oakenshire and its surroundings
  ([reference/oakenshire-layout-sketch.webp](reference/oakenshire-layout-sketch.webp));
- the user's **continent sketch**, "Alestia — The Known Lands"
  ([reference/alestia-known-lands.webp](reference/alestia-known-lands.webp));
- the content already in the game (`python tools/world/extract_canon.py --map 0`) and the terrain itself.

Places are referenced by their atlas id in `data/world/atlas/map_0.json` (see `tools/world/README.md`).
The two sketches show **intent**, not a survey. Where a sketch and the terrain disagree about an
exact position, the terrain and the atlas pins decide; where they disagree about what exists, section 12
lists it.

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

The opening region is **Falwyn Forest** and its neighbour **Briarwatch March**, on map 0 (Development World). [C]

- The spawns cover roughly 1 km × 1.4 km (x −650..930, z −480..980).
- The sculpted terrain covers x −1090..1040, z −530..1600.
- The terrain pages reach much further (x −2670..2130, z −3200..2130), but that land is flat and empty, at height 0. [C]

### 3.0 The shape of the land (terrain and layout sketch)

A north-up overview is in `generated/world/review/oakenshire-sketch/map_0.png` (regenerate it with `tools/world/render_map.py`).

- **Oakenshire lies in a basin inside a ring of mountains.** [E] (layout sketch) The terrain has this ring. [C]
  - The basin covers about x 90–420, z 440–840, with its floor at 0–20 m.
  - The ring rises to 60–83 m, and the south-west massif to about 120 m.
- **The ring's only gap faces west, and the West Gate stands in it** (the fortress towers at x 84–109). [E][C]
  - The westward road is the main route to civilisation (Haven). [E]
  - The ring makes Oakenshire feel protected but also isolated. [E]
- **North of the ring** a plateau at about 45 m falls away to lower uplands (20–27 m) with cliff edges. [C] The sketch calls this the direction of the hunting areas. [E]
- **A river runs north–south about 600 m west of the gate**, at x ≈ −520. [C]
  - It is 30–50 m wide and about 7 m deep.
  - It has a loop at its north end (z −450), a branch east at z 50, and a shallow ford (2 m) at z 150–200.
  - Its riverbed is painted with the path splat layer; the tools ignore submerged cells when finding roads.
  - The layout sketch has this river flowing south past Haven. [E] It has no name yet. [?]
- **A lake** up to 23 m deep lies north-west of everything (x −1050..−650, z −270..80). It is on neither sketch. The user removed its atlas pin (2026-10-01), so it is not a named place. Whether it stays in the terrain is open. [C][?]
- **West of the river** the land is flat and empty, with no zone. [C] The layout sketch puts the river valley, farmlands, a crossroads inn and Haven there (section 3.11). [E]
- **East of the ring** the land is flat and unsculpted. [C] The sketch calls the east "wilder, more dangerous areas" and the west "civilized lands". [E]

### 3.1 Falwyn Forest (zone 1)

- The light forest region containing the human starting experience. [E]
- The zone covers the western lowlands: the Mirewater, the Barrowfield and the far west. Its quest content runs from level 7 to 10. [C]
- Sub-zones listed in the data but with **no terrain**: Mage Tower (zone 5) and Greystone Pass (zone 6). [C]

### 3.2 Oakenshire (zone 2, sub-zone of Falwyn Forest) — atlas `oakenshire_hub`, `forest_camp`

- A settlement near a valley pass, connected by road to Haven. [E]
- It was redesigned because the first version felt too small and too close to Haven. [E]
- The sketch shows a palisaded village in the middle of the basin. Its only exit is the West Gate (atlas `oakenshire_west_gate`). [E]
- **Around the ring** (layout sketch) [E]:

  | Place | Sketch | Atlas pin |
  |---|---|---|
  | North High Ridge | Steep and rocky, with little paths; leads to the hunting areas | `north_high_ridge`, `high_ridge_hunting_grounds` |
  | Hidden cave entrance | Small cave on the east side, not obvious. **Kobolds live inside, perhaps with a hidden treasure chest.** [E] | `hidden_cave`, at the east cliff (421, 644) |
  | Waterfall | South-east; a good landmark, with herbs growing | `ring_waterfall`, at (427, 763); no water exists there yet |
  | South Ridge | Rocky slopes, some leading to a quarry or mine. **The quarry is separate from the bandit camp.** [E] | `south_ridge_quarry`, beside the Forest Bandit tents |

  All of these pins are canon (2026-10-01). The user dropped the sketch's Old Watchtower, so the only watchtower in the region is the Kingsroad bandits' Ruined Watchtower (zone 11). [E]
- **Ring dressing** (layout sketch), to use when dressing any stretch of the ring [E]:
  - rocky outcrops and cliffs;
  - pines and dead trees;
  - small caves and mine entrances;
  - an abandoned watch post;
  - hunting camps;
  - herbalist and gathering spots;
  - ruined stone walls and an old road;
  - occasional waterfalls and streams.
- In the game, Oakenshire is the first hub (levels 1–6). [C] It has:
  - a **Town Hall** whose ground floor holds the class trainers (q3, q4, q7, q23);
  - an **Inn** run by Old Harbin, with a rat-infested cellar (q10);
  - a **west gate** held by the **Oakenshire Watch** (u46, u47).
- Farms and woodcutters lie in the valley south-east of town, and boars trample them (q1, q5, q6). [C]
- A bandit camp lies in the woods south of town (q9, q24, q31, q33). [C]
- **Forest Camp** is where the human story begins, and the player starts there, not in a town. [E]
  - Its formal name is not settled. [E]
  - In the data, its NPCs (Farmer Haldor, Healer Mirenna, Guard Emrik, the binder Aralin the Kindling) stand at about (300, 550), just west of the Oakenshire trainers (`forest_camp`). [C]
  - **Forest Camp is part of Oakenshire.** Its atlas pin is named "Oakenshire". [E] Quest texts may still say "camp" for the NPCs' corner of town.

### 3.3 Haven (zone 3; Market, zone 4, is its sub-zone) — atlas `haven` (placeholder)

- A walled town and major human hub, reached around level 10. It has guards, vendors and class trainers. [E]
- **Haven has no terrain yet.** The zone rows exist, but no terrain tile carries its area id. [C]
- The data strongly suggests Haven once stood around the map origin:
  - Five vendors stand at about (0, 21): Eliza Thorne (General Goods), Garret Blackwood (Weapons), Roland Stonehelm (Mail & Plate), Derrick Longstride (Leather Armor) and Mariana Silkspun (Clothier).
  - Injured Guards stand "north east of Haven" near (−55, −45) (q8).
  - Haven's binder is Magister Calloran (u25), who is not spawned.
  - All of these NPCs are buried 3–7 m below the current terrain. [C]
- **Haven lies south-west of Oakenshire, at the end of the river valley.** [E] It is reached by the Haven Road (section 3.11), with the river passing on its east side.
  - The atlas pin `haven` is canon at (−781, 1249), radius about 310 m: a big walled city on flat, empty ground with no zone yet.
  - It is about 1.3 km from the Oakenshire Town Hall in a straight line.
- The continent sketch shows Haven just **south** of Oakenshire. [E]
- The buried Haven NPCs at the map origin are leftovers. They have to move to the new Haven, or be removed. [?]
- The Westwatch Training Yard (q21: "not far from here") stands with them. Does it move into Haven, or stay near Oakenshire as a place of its own? [?]

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
- The road painted on the terrain runs from the West Gate west past Briarwatch Camp and the Kingsroad Waypost, then north-west to the river at (−495, 355). [C] A house and two piers stand at that crossing. The atlas traces it as `old_kings_road`.
- The layout sketch calls the first stretch beyond the gate the **Forest Road**, and has it reach the river at the **Old Stone Bridge** (atlas `old_stone_bridge`). [E]
- **The Westroad is Oakenshire's road from the town through the West Gate** (atlas `westroad`). Beyond the gate it continues as **the Old King's Road** (atlas `old_kings_road`) to the river. Both are canon. [E] The sketch's "Forest Road" is that stretch beyond the gate.
- The user drew the Northroad north from about (−56, 408) towards (−24, 116), the old Haven site. Where it leads now that Haven is in the south-west is open. [?]

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
- **On the surface it is a ruined abbey with a bell tower that can be seen from far.** [E] (layout sketch)
  - It stands on a side path north-west of Oakenshire, off the main road and easy to miss.
  - An overgrown path leads up to it from the valley.
- **Its pin (`dungeon_entrance`) is canon at (103, 346).** That is the western edge of the plateau north of the ring, at 29 m, next to a prototype Cylinder marker at (107, 345). [E]
- The name fits an abbey: a choir is the part of an abbey church where the monks sang. [?]
- q57 still puts the stair "under the third mound" of the Barrowfield, about 600 m further west. The quest text has to be reconciled with the abbey (section 12, item 16). [?]
- The lore behind the abbey is open. [?]

### 3.10 The wider world: Alestia, the Known Lands (continent sketch)

- The continent sketch is titled **"Alestia — The Known Lands"**. [E]
  - Its legend reads: "A world of ancient realms, forged by light, shadow and time. From the high peaks of the North to the sunlit shores of the South, nations rise, empires fall, and legends endure."
  - So Alestia names the world, or at least its known lands. The map shows one large landmass with islands. [E]
- **Oakenshire and Haven lie near the centre of the landmass**, in green, wooded country crossed by rivers and roads. [E] Falwyn Forest is not labelled on it. [?]
- The sketch's scale bar reads 0–200 miles, which makes the landmass roughly 1,400 miles across. The game compresses distance heavily: Oakenshire to Haven is about 60 miles on the sketch and 500–1,000 m in the game. How that compression works is open. [?]

| Region | Position on the sketch (relative to Oakenshire) | Look |
|---|---|---|
| Whispering Woods | Just north | Dense forest |
| Frostward Peaks | Far north | Snow-capped mountains |
| Stonehelm Clans | North-east | Mountain realm |
| Ironspine Wastes | North-east, below the Stonehelm mountains | Brown badlands |
| Dawnbreak Plains | East | Open golden plains |
| Emberreach | Far east, across the water | Volcanic land with a dark fortress |
| The Blackmoors | South-east | Dark moorland |
| Silvermere | South, below Haven | A town or city with a fortress |
| Sands of Korash | Far south | Desert with a fortress city |
| Greenvale Forest | South-west | Forest |
| Valemarch | West | A realm with a fortress |
| Westerfell | Far west | Hills with a fortress |
| The Silver Sea | North-west | Sea |
| The Shattered Sea | North-east | Sea |
| The Trade Sea | South-east | Sea |
| The Sunset Sea | South-west | Sea |

All entries are [E] from the continent sketch.

- The legend on the sketch distinguishes major cities, cities or towns, fortresses, points of interest, roads, rivers and borders. [E] Which markers are which is not readable at this size. [?]
- A possible expansion order was discussed: Heartlands → Northern Mountains → Far Coast → a foreign kingdom → an unknown continent. This is not established. [?] On the continent sketch:
  - the Heartlands would be the central lands around Oakenshire and Haven;
  - the Northern Mountains would be the Frostward Peaks or the Stonehelm Clans;
  - the unknown continent would lie beyond the four seas.
  This mapping is a suggestion. [?]

### 3.11 The river valley and the road to Haven (layout sketch) — atlas `old_stone_bridge`, `river_valley`, `farmlands_and_hamlets`, `crossroads_inn`, `haven`, road `haven_road`

The sketch numbers the stops beyond the West Gate [E]:

1. West Gate — Oakenshire
2. Forest Road
3. Forward Outpost, north-west across the river
4. Old Stone Bridge
5. River Valley, where the views get wider and the terrain softer
6. Farmlands and Hamlets
7. Crossroads Inn
8. Haven Road (south-west)
9. Haven (major city)

On the map [C]:

- Stops 1, 2 and 4 match what exists: the gate, the painted road and the river crossing.
- Stops 5–9 fall on the flat, empty land west and south-west of the river, where nothing exists yet.
- Stop 3 matches no existing camp west of the river. Briarwatch Camp (Thalric's work camp) and Barrowfront Camp (Osric's palisade, on the east bank) are the nearest candidates. [?]
- Today the quest levels rise from the gate westward: Briarwatch March is 4–7, the Mirewater 7–8 and the Barrowfield 8–10.
- The valley beyond the river would therefore be the natural ground for the approach to Haven, around levels 9–11. This is a suggestion. [?]

The sketch's side view reads, from west to east: Haven, the valley, Oakenshire, the mountain ring. It is labelled "civilized lands ← main road through the valley → wilder, more dangerous areas". [E]

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
- **Kobolds** are an established creature group of the region. [E]
  - They live in the hidden cave in Oakenshire's ring (atlas `hidden_cave`), perhaps with a hidden treasure chest. [E]
  - The only kobold units in the data are marked OBSOLETE and are not spawned (u2, u3).
  - Their society is open. [?]

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
  - The continent sketch uses the same style: Whispering Woods, Frostward Peaks, Ironspine Wastes, Dawnbreak Plains, Greenvale Forest, Silvermere, Valemarch, Westerfell, the Blackmoors.
  - It also uses peoples' realms (Stonehelm Clans), one foreign-sounding name (the Sands of **Korash**), and one fiery name (Emberreach). [E]
  - Local landmarks from the layout sketch are plain: Old Stone Bridge, Crossroads Inn, Forward Outpost, High Ridge.
  - **Stonehelm** is both a realm (the Stonehelm Clans) and the family name of the Haven vendor Roland Stonehelm (Mail & Plate). A link or a coincidence? [?]
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
- From the Town Hall through the gate, over the bridge and down the valley to Haven's edge, the road is about 1.6 km (atlas `westroad`, `old_kings_road`, `haven_road`). The straight line to Haven's centre is about 1.3 km. [C]
- No route network, operators or fast-travel rules are established. [?]

## 12. Open questions and contradictions

### From the setting notes [?]

| Topic | Question |
|---|---|
| World | Partly answered: the continent sketch calls Alestia "a world of ancient realms" and maps its Known Lands (section 3.10). Are there lands beyond them? |
| History | What happened before the player arrives, and why does it matter now? |
| Politics | Who governs Haven, Oakenshire and Falwyn Forest? (The game already speaks of a King: see below.) |
| Geography | Partly answered: the Forest Road runs west over the Old Stone Bridge to Haven (section 3.11). Where does the Northroad lead: north to the Whispering Woods? Where does the east (the "wilder areas") lead? |
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
   - **Resolved 2026-10-01:** Forest Camp is part of Oakenshire (section 3.2). The notes' "small camp, not a town" is superseded.
4. **The Light and the King.**
   - Notes: "the Light" and "the Crown" were only possible expressions.
   - Game: clerics serve the Light (q7, q8, q35, q36), Haldor swears "By the Light" (q6), and the road, wagons and seals belong to the King (q27, q42, q43). Promote them to [E], or rewrite those texts.
5. **Shadowmancer or Acolyte?** Malrik is a "Shadowmancer Trainer", but his chain is "Path of the Acolyte" and the camp trainers are "Acolyte Trainer"s. Is "Acolyte" the rank of a shadowmancer student, or a different name for the class?
6. **Kobolds** are established in the notes but absent from the game (the only kobold units are OBSOLETE). Their home is now decided: the hidden cave in Oakenshire's ring (section 4). New kobold units are still needed.

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

### From the two sketches (2026-09-28)

15. **Where Haven is.** **Resolved 2026-10-01:** south-west, at (−781, 1249) (section 3.3).
    Still open:
    - the buried Haven NPCs at the origin have to move or go;
    - does the Westwatch Training Yard move with Haven?
    - where does the Northroad lead now?
    - the continent sketch's "Haven just south of Oakenshire" is only approximate at that scale.
16. **The Hollow Choir's entrance.**
    - Sketch and the user's pin: a ruined abbey on the plateau edge north-west of Oakenshire, at (103, 346).
    - q57: "a stair under the third mound" of the Barrowfield, at about (−474, 227), 600 m west. The Barrowfront chaplain gives the whole dungeon chain (q57–q62).

    **The abbey pin is canon (2026-10-01).** q57 (and the "beneath the Barrowfield" objective wording of q57–q62) still has to be reconciled. Options:
    - reword q57 to send the player to the abbey;
    - keep the Barrowfront chaplain as the chain's giver, with the abbey as the way in;
    - give the crypt a second, collapsed entrance under the third mound.
17. **Which way is dangerous.**
    - The sketch calls the west "civilized lands" and the east, beyond the ring, the "wilder, more dangerous areas".
    - In the game, the west beyond the gate is a frontier being reclaimed (Briarwatch March, q25), and it holds the level 8–10 raised dead.

    These fit together if the civilized lands begin across the river.
18. **Two watchtowers.** **Resolved 2026-10-01:** the sketch's Old Watchtower was dropped; zone 11's Ruined Watchtower is the only one.
19. **Where the farms are.**
    - The game has farms in the basin south-east of town (q1, q5, q6).
    - The sketch shows farmlands and hamlets in the river valley beyond the gate.

    Both can be true: the basin farms feed Oakenshire, the valley farms feed Haven.
20. **The South Ridge quarry and the bandit camp.** The Forest Bandit camp tents stand at the foot of the south ridge, exactly where the sketch draws the quarry buildings. Do the bandits hold the old quarry, or is the quarry a separate, working place? **Resolved 2026-10-01: separate.** Who works it is open.
21. **The Forward Outpost** (sketch stop 3) matches no existing camp west of the river (section 3.11).
