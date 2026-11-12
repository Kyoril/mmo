# Sands of Korash demo: breaking it into PCG pieces

Date: 2026-10-25. Research only, no game data changed. This note supports the desert PCG control set
that follows the oasis PoC ([design](../specs/2026-10-24-pcg-oasis-poc-design.md),
[task 11 report](../specs/pcg-oasis-poc/task-11-report.md)).

## Sources and conventions

- **The UE import is the source of truth:** `H:\tmp\desert_import\out\placements.json`
  holds 9,023 records: 1,136 StaticMeshActors, 7,867 instanced-foliage instances and 20 `BP_Water`
  planes. It is backed by the level T3D (`out/landscape/Demonstration_Desert.t3d`), the UE landscape
  (`out/landscape/height_m.npy`, 1009×1009 at 1 m) and the per-layer weight maps
  (`out/landscape/layer_*.png`). The level has **no outliner folders**: actor labels are just
  `SM_<mesh><n>`, so the grouping into pieces below comes from my own spatial analysis, not from
  artist names. The level also holds 16 Niagara waterfall splashes, 4 `BP_TumbleweedZone` actors and
  32 CineCameras.
- **Coordinates:** demo-local metres (UE cm / 100), origin at the demo centre. Engine world is
  `x = 3466.67 + lx`, `z = 9866.67 + ly`, and height is roughly the UE height minus 0.3 m in the
  core; the outer 60 m was re-fitted by `build_korash_terrain.py`. **North is −ly**, which is the top
  of the top-down captures. The demo covers lx, ly ∈ [−504, 504]. Pages: x page 37 if
  lx < −267, 38 if lx is between −267 and +267, 39 if lx > +267. The z page follows the same rule
  with ly, giving pages 49, 50 and 51.
- **Mesh sizes** come from `out/meshes.json` (UE bounds, unscaled):
  - CliffA/B/C: 35–128 m wide, ~51–57 m tall.
  - CliffHalf: 39–99 m wide, 20–55 m tall.
  - HoodooCliff: 13–48 m wide, 51–61 m tall.
  - Hoodoo pillar: 3–9 m wide, 15–25 m tall.
  - Layered: 15–68 m wide, 19–32 m tall.
  - Shelf: 19–34 m wide, 11–25 m tall.
  - Platform: 19–43 m wide, 13–22 m tall.
  - Clump: 3–12 m wide.
  - Rock: 0.4–5 m wide.
  - Waterfall: 4–43 m tall.
  - Sandfall: 11–43 m tall.
  - Palms: 10–17 m tall.
- **Water:** `SM_WaterPlane` is 18 m square, scaled and yawed. The pool footprints below are where
  the plane rectangle lies above the UE landscape. Where a plane rectangle extends over a terrain
  drop, the footprint overestimates.
- **Scope of the statistics:** they are computed over the whole demo with throwaway scripts in the
  session scratchpad; nothing was committed. Anything marked *(estimate)* was read from captures or
  hillshade images rather than computed.

## Whole-demo facts that shape the system

| Fact | Number |
|---|---|
| Land with slope > 30° / > 45° / > 60° | 32 % / 21 % / 13 % |
| Local relief (15 m window) at cells steeper than 55°, p25/p50/p75 | 24 / 40 / 52 m |
| Height distribution | Clusters at 0–30 m (coast, lowlands), 55–70 m, 95–120 m, 140–160 m (plateau tops) |
| Splat coverage (weight > 0.5) | DesertSand 84.5 %, Grass 8.6 %, DesertDirt 3.9 %, DesertGrass 1.4 % |
| Big cliff actors (CliffA/B/C), median vertical offset from landscape | −39 m (A, C), −25 m (B) |
| Rock actors with pitch/roll > 5° | 30–60 % of Cliff/Shelf/Clump; Hoodoo, HoodooCliff and Layered are upright |
| Rock actors with non-uniform scale | ~33 % of Cliff pieces |
| Grass foliage (`SM_Grass2`, 1,029) more than 1.5 m above the landscape | 88 % (median +16 m) |

What these facts mean for the system:

1. **The terrain already contains the cliffs.** Steep cells have a median relief of 40 m. The
   terrain material's auto-cliff (slope-driven striped rock in `Desert_Terrain.hmat`) shows rock on
   those faces. The 55 m cliff meshes are buried 25–40 m deep and act as a skin over the terrain
   steps. They do not form the shape on their own. A plateau rule must therefore **write the step
   into the terrain first and then skin it**.
2. **Grass on rock tops.** Almost all `SM_Grass2` instances sit on top of rock meshes, mainly the
   tops of grass-topped Shelf terraces. They are not on the landscape. A rule that places grass only
   on terrain cannot reproduce this. Rock tops need their own scatter surface, or rock variants that
   carry the grass, which the existing `*_Grass` material variants partly do.
3. **The rocks are freely posed.** Cliff pieces are tilted and scaled non-uniformly, and some CliffB
   pieces are rotated almost 180° (used upside down as overhangs). A rule that only snaps pieces
   upright to the ground and varies their yaw will look more regular than the demo.

## 1. Inventory of piece types

The demo is a 1 km island. The central plateau is the high ground. A river network falls from it
west and then east to the sea. The lowlands around it each have their own character. The pieces are
ordered roughly by how much of the demo they make up.

### P1 Stepped plateau / mesa massif
- **Location:** lx −250…+250, ly −450…+100 (pages 38/49–50, extending into 37). Tops at 100–165 m;
  the central-mesa sample (r = 140 m around (0, −150)) has heights p10/p50/p90 of 101/135/158 m.
- **Terrain:** two or three terrace levels (95–120 and 140–160 m), separated by near-vertical steps
  of 15–60 m. The tops are sand, sometimes lightly dished.
- **Meshes:**
  - CliffA (88), CliffB (168) and CliffC (108) skin the steps. Nearest-neighbour spacing along the
    wall has a median of 16–25 m and a p90 of 40–60 m, which means overlapping pieces with
    occasional gaps.
  - CliffHalf (154) appears on lower or partial steps (median offset −9 m).
  - Shelf (206 in the whole demo) forms ledges at the wall foot.
  - HoodooCliff and Hoodoo appear at a few spur ends.
- **Sandfalls:** 10 `SM_Sandfall*` actors, 11–53 m tall, placed on step faces 53–148 m from any
  water. Sand pours from the tops down the cliff faces.
- **Splat and foliage:** mostly sand. DesertDirt appears in the gaps, and Grass only where the
  river crosses the plateau. The tops carry sparse cactus (389 in the demo), bushes and weeds.
- **Borders:** the river pools sit in terrace pockets on the plateau (P3). Slot canyons (P2) cut
  through it. The NE field (P6) and the green valley (P4) are at its foot.

### P2 Slot canyon / passage
- **Location:** between the central mesas, e.g. around (100, −50) and (−50, −100). Shown by
  `eye_cam5_mesa.png`.
- **Shape:** a sand floor 10–30 m wide *(estimate, from top-down captures)* with walls of 40+ m on
  both sides. The walls are the same plateau skin, P1 seen from inside. The floor has soft
  dune-like undulation.
- **Content:** cactus, clumps, dry bushes and the occasional sandfall in the wall.
- **Not a separate asset set.** A slot canyon is P1 with a corridor subtracted, so the corridor
  belongs to the path/opening control (P10).

### P3 River cascade (tiered pools, waterfalls)
- **Location:** a branched network. The source pool is at (−155, −275), level 135 m. The river runs
  south-west down the plateau, turns east around (−340, 0), runs through the green valley and splits
  at pool 2 (65, 195). One arm reaches the east sea near (305, 51) and the other the south sea near
  (130, 370). A tributary comes from the west plateau (pools 10 and 9, around (−210, 70)).
- **Water:** 19 raised planes. Their levels, in metres, are: 135.2 → 110.5 / 110.4 / 109.7 (three
  near-equal pools) → 107.8 → 103.0 / 101.8 → 93.1 → 73.2 → 63.7 → 55.7 → 39.8 (tributary 67.2) →
  26.3 → 19.8 → 16.8 → 13.8 → 3.8 → sea (−0.2).
  - Steps between pools are 1–25 m.
  - Pool footprints are 150–7,200 m²; most are 500–3,000 m².
  - Pools are 2–8 m deep where they are not over a drop.
  - The channel between pools is about 12–20 m wide *(estimate, from `top_cascade.png`)*.
- **Waterfalls:** 24 meshes (`SM_Waterfall1–8`, `SM_WaterfallSplit2`). Every one has its base at
  the lower pool's surface (offset from ground +0.5 m) and is sized to the drop:
  - `Waterfall1/2/3`, 2–6 m tall, sit between near-equal pools (riffles).
  - `Waterfall5/6/8`, 25–44 m tall, sit at the big terrace steps, e.g. 73.2 → 101.8 at (−296, −83)
    and 67.2 → 103 at (−217, 90).
  - 14 Niagara splashes sit at the waterfall bases; they are not ported.
- **Banks:** the river is the main carrier of green. Grass splat cells have a median distance to
  water of 23 m (p90 50 m). Foliage that hugs the water:
  - ElephantEars: median 11 m from water.
  - Fern: 9 m.
  - Palms: 24 m.
  - Bushes: 20 m.
  - Marshtail: 0 m.
  - Shore Rock and Clump foliage reach right to the edge (p10 = 0 m).
- **Rocks along the channel:** pool edges run into cliff meshes. Inferred from the footprints:
  plane edges that would float over drops are hidden inside cliff and Shelf meshes, so waterfalls,
  rock and plane edges have to be placed together.

### P4 Green valley / grass-topped terraces
- **Location:** the lower river valley, lx −340…+320, ly +50…+260 (pages 37–39/50), plus the
  upper cascade pockets.
- **Terrain:** valley floor at 15–40 m. The flanks step down in rounded terraces made of Shelf
  pieces (37 in the r = 150 m sample around (−60, 215)), with grass on top. These show as round
  green pads in `top_cascade.png` and `top_sw_dunes.png`.
- **Splat:** Grass, about 9,000 m² in that sample, with DesertGrass and DesertDirt where green
  meets sand.
- **Foliage:** most palms (885 foliage + 26 actor palms), Grass (on the shelf tops), flowers
  (2,330 in the demo, half on Grass splat), ferns and elephant ears. Windswept trees (13 actors)
  appear only here, at lx −460…+178, ly 183…322.
- **Borders:** contains P3. Ends abruptly at a cliff band against the SW dunes (P8): a wall of
  Shelf and CliffB along ly ≈ 220–260.

### P5 Oasis basin (the PoC target)
- **Location:** (286, −318), world (3752.7, 9548.7), page 39/49.
- **Water:** pool `BP_Water_C_18` at 6.5 m, a 1,932 m² hourglass of about 47 × 63 m, 3.4 m deep at
  most. The basin floor is 8–15 m high, inside rim rock that reaches 26 m+.
- **Splat:** Grass out to a radius of ~57 m (4,762 m²), DesertDirt and DesertGrass towards the rim.
- **Content within 90 m:**
  - 117 bushes, 76 palms (61 foliage + 15 actors), 58 clump-rock foliage, 52 marshtail at the
    waterline, 29 flowers, 25 weeds, 12 twig roots and 10 cacti.
  - Rocks: 12 Layered, 9 Shelf, 7 Clump actors, 5 Rock actors, 3 CliffB and 2 sandfalls.
- **Rim:** the rim is mostly the *surrounding* Layered "mushroom" field (P6), with no closed ring
  of its own. The basin is a hole in that field.

### P6 Layered "mushroom" rock field with sink bowls
- **Location:** NE lowland, lx +100…+480, ly −480…−50 (pages 38–39/49–50), heights 10–35 m.
  Shown by `top_ne_field.png`.
- **Meshes:**
  - Layered (62 in the demo, almost all here): 15–68 m wide, 19–32 m tall. They are nearly
    upright (13 % tilted) and almost all at scale 1.
  - Spacing has a median of 17 m and a p90 of 42 m. Pieces cluster into 2–5 overlapping
    "mushroom" groups with sand gaps of 20–60 m between groups *(estimate)*.
  - Shelf and Platform appear at the edges of the groups.
- **Terrain:** sandy and gently rolling, with small round bowls about 20–50 m across *(estimate,
  from hillshade)*, often holding a Layered group or a palm or two. DesertDirt patches surround
  them.
- **Foliage:** sparse. Dry palms, cactus, twig roots and clumps.

### P7 Hoodoo finger ridges (west coast)
- **Location:** lx −504…−380, ly −480…−150 (page 37/49). Shown by `top_w_ridges.png`.
- **Terrain:** a comb of parallel spurs running roughly east–west. They are spaced about 55 m apart
  (FFT of the height profile), 100–150 m long and up to ~60 m tall (p95), and fall to sea level.
  The terrain carries the striped rock on its faces. DesertDirt covers about 4,000 m² in the
  r = 110 m sample.
- **Meshes:** HoodooCliff (74; 51–61 m tall, upright, median spacing 22 m) along the crests and
  spur ends. Hoodoo pillars (40, 15–25 m) stand as free-standing tips off the spur ends and in the
  sand.
- **Foliage:** very little. 15 palms and 11 cacti in the sample.

### P8 Dune field
- **Location:** SW, lx −450…+120, ly +250…+470 (pages 37–38/50–51). Shown by `top_sw_dunes.png`.
- **Terrain:** smooth dunes, ±6.8 m after detrending (p5/p95), with wavelengths of about 85 m (N–S)
  and 110 m (E–W). Slopes have a median of 18° and a p95 of 33°.
- **Content:** almost empty. In ~60,000 m² there are 26 weeds, 23 clumps, 5 cacti, 4 Shelf pieces,
  sparse DesertDirt patches and one tumbleweed zone. A sand beach runs to the sea at the south edge.

### P9 Ring crater / stepped amphitheatre
- **Location:** centred near (360, 395), page 39/51. Shown by `top_se_crater.png`.
- **Terrain:** a raised rim at 35–50 m and an inner floor at 20–25 m, about 140 m across from rim
  to rim. A central pit drops to about 4 m.
- **Meshes:** Shelf pieces (20) set in two concentric rings on the inner wall, plus a CliffHalf
  ring (15) on the outer rim.
- **Splat:** DesertDirt floor (5,050 m², radius ≤ 65 m).
- **Foliage:** 59 clump rocks, 40 weeds and 37 cacti.

### P10 Paths, passages and gaps (implicit)
- The demo has **no authored paths**: no path splat layer and no road meshes. Passage happens in
  three ways:
  - Slot canyons through the plateau (P2).
  - Sand gaps between rock groups (P6).
  - Breaks in walls, for example where the river leaves the plateau, or the shelf terraces where
    the green valley meets the dunes.
- The NE oasis is reachable from all sides through gaps in the mushroom field rather than through
  one gate.
- A PCG path in this biome is therefore mostly a **negative**: it removes rim and cliff pieces,
  flattens the floor to sand or DesertDirt and clears scatter. It is not a road.

### P11 Coast, beach and estuary
- **Coast:** most of the coast is beach that falls into the sea plane at −0.2 m.
- **Estuary (SE):** between the plateau foot and the crater, lx 0…480, ly 180…330. Braided channels
  and sand bars at sea level, pools 0 (3.8 m) and 18 shoreline, plus two river mouths with small
  falls (Waterfall6 and Waterfall2 at levels 0–4 m).
- **Foliage:**
  - `SM_Beach_*` shells and starfish: 89, every one within 1 m of water.
  - Marshtail: 100, within 0–2 m of water. 52 of them are at the oasis; the rest are at the east
    river mouth and estuary.
- **Rocks:** Platform rocks (12; median spacing 92 m) stand alone near coasts and inside the
  valley as landmarks. Large `Rock` actors (77, scale 1.2–2.9) sit at water edges.

### P12 Background ground clutter (fill)
- **Coverage:** everywhere that is not water and not a rock top.
- **Foliage:**
  - Weeds: 1,186. Median distance to water 82 m. Effectively never on the Grass layer (1 %).
  - Cactus: 389. 64 m from water.
  - Twig roots: 135. 70 m from water.
  - Clump rocks: 630. Spread uniformly.
  - Small Rock foliage: 146. Spacing 2.4 m, so it comes in small clusters.
- **Splat:** DesertDirt patches. These cover 3.9 % of the land, with median slope 17° and median
  height 27 m.
- **Other:** 4 tumbleweed zones.
- **Appearance:** `eye_cam27_ground_clutter.png` shows cracked-dirt patches with weeds, twig roots,
  pebble clusters and a cactus.
- **Density varies a lot:** the dunes are nearly empty, while the plateau foot and the NE field are
  dense.

## 2. Control object per piece

"Reads" and "writes" refer to world inputs. Every control writes ownership records, as the PoC
already does.

| Piece | Control | Obvious parameters | Reads | Writes |
|---|---|---|---|---|
| P1 Plateau / mesa | **Closed spline per terrace level**, nested. Alternatively one closed spline plus a level count and step inset. | Top height (absolute, or relative to surroundings), step height, wall batter/overhang, edge noise, cliff family mix (A/B/C/Half) and sink per family, spacing 15–25 m with overlap, tilt range, sandfall chance per wall metre, top dishing, cap material | Terrain height (base), other controls (openings, river) | Terrain height (raise to top, steep step), splat (sand top, DesertDirt at foot), cliff meshes, Shelf ledges at the foot, sandfalls, top scatter |
| P2 Slot canyon | Not its own control: **open spline (P10 path)** with a "corridor" profile, used inside a P1 | Width 10–30 m, floor height or follow, floor dune noise, wall reuse of the P1 family | P1 output (heights), terrain | Terrain (cut the floor), removes P1 wall pieces in the corridor, places wall skin along both corridor sides, floor scatter |
| P3 River / cascade | **Open spline** (downstream), per point: half-width plus optional *water level* (a level change = a pool boundary); branches as separate splines that end on another spline | Width, bank width, bed depth below water, pool length, max step per pool, waterfall mesh by drop height (<6 m small, ≥ 6 m big), riffle threshold, bank flora set, shore rock density, flow speed | Terrain height (to fit the levels if none are given), slope | Terrain (carve bed, flatten each pool to its level, steep step at each drop), water heights per page + water type (raised pools = type 1), the existing WFLW flow (`.hflow` FlowPath is already a spline of {x, z, halfWidth}), splat (Grass bank band, DesertGrass/DesertDirt fringe), waterfall meshes at drops, shore rocks, bank foliage |
| P4 Green terraces | **Closed spline region** (the green zone), usually generated by P3 as a bank band; terraces as an option | Terrace count, step height 5–15 m, Shelf family and grass-top variant, palm/fern/elephant-ear densities, distance-to-water falloff | P3 water distance, terrain slope | Splat Grass, Shelf pieces, palms, under-storey, grass on rock tops |
| P5 Oasis | **Closed spline** (existing) plus an optional **inner closed spline for the pool** (PoC finding 2) | Existing PoC params plus pool shape independent of the rim, rim mode (own ring vs. "use surrounding field"), shore-rock rule, marshtail band | Terrain, P6 fields, opening controls, inflowing P3 rivers | As now: terrain basin, water, splat, rim rocks, palms; plus marshtail and shore rocks |
| P6 Mushroom rock field | **Closed spline (area)** | Group density, groups of 2–5 Layered pieces, inner spacing 6–20 m, gap between groups 20–60 m, bowl chance and radius, edge Shelf/Platform | Terrain slope, exclusion by other controls | Rock meshes, shallow bowls in the terrain, DesertDirt patches, dry foliage |
| P7 Hoodoo ridges | **Open spline per ridge (crest line)**, or a closed area that generates parallel ridges | Ridge height/width profile, spacing (~55 m), taper to the sea, HoodooCliff spacing (~22 m), free pillar count off the tip | Terrain, sea level | Terrain (ridge raise), splat (DesertDirt), HoodooCliff and Hoodoo meshes |
| P8 Dunes | **None: background fill** (a biome region volume at most) | Dune amplitude (±7 m), wavelength (85–110 m), wind direction, clutter density (very low) | Terrain base | Terrain noise (additive, low frequency), sand splat |
| P9 Ring crater | **Point plus radius** (or a closed spline for the rim) | Rim height, floor height, pit depth/radius, ring count, Shelf ring spacing, rim CliffHalf | Terrain | Terrain (rim, floor, pit), DesertDirt floor, ring rocks, clutter |
| P10 Path / opening | **Open spline** (path) and **closed spline/volume** (opening) | Width, floor material (sand/DesertDirt), flatten strength, falloff, "cuts rims/walls" flag, clearance for scatter | All other controls' outputs | Removes or blocks placements in the corridor (rim, cliff, field, scatter), flattens terrain, splat, and turns wall ends where the wall is cut |
| P11 Coast / beach | **Open spline** (shoreline), or derived from sea level | Beach width, shell density, marshtail band, landmark Platform count | Terrain, sea level, water mask | Terrain (beach slope), splat, beach foliage, marshtail |
| P12 Ground clutter | **None: background fill**, density modulated by a large-scale noise and by distance to water, rocks and slope | Per-type densities and falloffs (weeds far from water, cactus mid, flowers on grass), dirt-patch noise, tumbleweed zones | Splat, water distance, slope, every control's exclusion mask | Foliage, DesertDirt patches |

## 3. Interactions and combination rules

These are ordered by evaluation, from the largest scale to the smallest. Each later stage reads the
earlier stages' terrain and masks.

1. **Base terrain and dunes (P8):** applied first and everywhere. Every other control writes on top
   and replaces the dunes inside its footprint, using a falloff band.
2. **Landforms, which add height (P1, P7, P9):** a plateau raises terrain to its top. Nested terrace
   splines take the maximum. Where two landforms overlap, the higher top wins and the step face
   moves to the outside.
3. **Water (P3, P5):** subtracts from landforms.
   - A river crossing a plateau carves its channel into the plateau. Each drop between pools must
     fall on, or be moved to, a terrace step of P1. In the demo the big falls (25–44 m) are exactly
     the plateau steps and the small falls (2–6 m) are inside a terrace.
   - Where a river spline ends inside an oasis spline, the oasis pool becomes the river's next pool
     and inherits its level, or forces the river's last level. The rim gets an **opening** where the
     river enters, plus a waterfall if the levels differ.
   - The water plane extends under rock and cliff meshes. The waterfall's base sits at the lower
     water level, and its top at the upper level.
4. **Openings and paths (P10):** a difference applied *after* the landforms and water have planned
   their placements and *before* they are emitted.
   - They remove rim/cliff pieces whose footprint intersects the corridor. At a wall end they should
     place a "return" piece (rotated cliff or Shelf) so the cut is not a sliced mesh.
   - They lower the terrain to the corridor floor and clear the scatter.
   - An opening in an oasis rim is just a path that ends inside the basin.
   - A slot canyon (P2) is a path inside a plateau whose walls are re-skinned with P1's cliff
     family.
5. **Rock fields (P6) and fills:** exclusion-based.
   - Fields do not place inside any landform top, water, bank band or path.
   - An oasis inside a field takes the field's rocks as its rim. The oasis subtracts a hole of
     radius ≈ basin radius from the field and places only shore rocks itself, which matches the
     NE demo.
6. **Bank and green (P4):** derived from water distance.
   - Grass splat out to ~25–55 m, with palms, ferns and elephant ears weighted towards the water.
   - On terraced banks, Shelf pieces get grass tops: grass scatter on rock-top surfaces, or grass
     variants of the materials.
7. **Background clutter (P12):** last, excluded inside every control's footprint plus a margin,
   inside water and on steep slopes. Densities follow water distance. The demo pattern is
   flowers/ferns near water, weeds/cactus/twig roots far from it, and almost nothing on dunes.
8. **Ownership:** every placement belongs to exactly one control. Exclusions are evaluated from the
   other controls' *footprints*, not their outputs, so that rebuilding one control does not ripple
   through everything. That is the PoC's locality requirement.

## 4. Minimal desert control set

Ordered by value per unit of effort. The first five reproduce roughly 80 % of the demo by area and
by asset count *(estimate)*.

1. **Background biome fill (no control):** dune noise, sand splat, ground clutter with
   water-distance falloff, DesertDirt patches, and exclusion by all controls. This covers P8, P12
   and the dry parts of P6 and P1 tops, roughly 30–35 % of the land.
2. **Plateau / mesa (closed spline, nested for terraces):** terrain step plus cliff skin, Shelf
   foot, sandfalls and top scatter. Covers P1, which carries ~45 % of all rock actors and most of
   the silhouette.
3. **River (open spline with per-point water levels, branches allowed):** carve, pools, waterfalls
   by drop, bank band (P4 green and flora) and shore rocks. Covers P3 and most of P4, the
   water-and-green half of the look. It extends the existing `FlowPath` spline.
4. **Oasis (closed spline, existing):** add an inner pool spline, a "rim from surrounding field"
   mode, and an opening/river hand-off.
5. **Path / opening (open spline / closed volume):** the difference operator for rims, walls, fields
   and scatter. It also produces slot canyons (P2). The user's "openings so a path can leave an
   oasis" requirement lives here.
6. **Rock field (closed spline area, family preset):** Layered mushrooms, plus the same control with
   a hoodoo-pillar preset. Covers P6 and the free pillars of P7.

**The remaining ~20 %:**
- The west hoodoo finger ridges (P7 terrain): either a ridge spline or hand-sculpted terrain plus
  preset 6.
- The ring crater (P9), a one-off landmark best done as a point control or by hand.
- The coast details (P11): beach slope, shells, estuary sand bars and lone Platform landmarks.
- Grass-on-rock-top scatter, unless the Shelf grass-top variants are used.
- The art-directed irregularities: flipped or overhanging cliff pieces, sandfalls framed for
  specific camera views (32 hand-placed CineCameras show the demo was composed for views), and the
  windswept trees.
- The non-ported effects that are not PCG at all: RVT rock-to-ground blending, wind, waterfall
  splashes and tumbleweeds.

## Reference captures

All in `docs/superpowers/research/korash/`. They are rendered with `mmo_edit --capture-view` at
time 0.45. Top-down views use yaw 0 / pitch −89.5 / fov 60, with north at the top. The eye-level
views are the demo's own UE CineCamera positions from `out/cameras.json`.

1. `top_cascade.png`: the upper river cascade at (−230, −100). A winding channel between sand-capped
   mesa tops, tiered pools (pale), small falls at riffles, and round grass-topped Shelf terraces
   (lower left and right). Shows P1, P3 and P4 abutting.
2. `top_ne_field.png`: the NE oasis (P5) at (300, −300) sitting as a hole inside the Layered
   mushroom field (P6). Hourglass pool, grass disk ~57 m in radius, palms on the east side, no
   closed rim ring of its own. Open sand gaps between the rock groups are the "paths".
3. `top_se_crater.png`: the ring crater (P9) with two concentric Shelf rings, a CliffHalf outer rim,
   a DesertDirt floor and a central pit. The upper left shows estuary sand bars and the shallow sea
   inlet (P11).
4. `top_w_ridges.png`: the hoodoo finger ridges (P7). Parallel terrain spurs with striped auto-cliff
   faces, HoodooCliff stacks along the crests, and free Hoodoo pillars off the spur ends standing in
   sand.
5. `eye_cam5_mesa.png` (CineCamera 5): a slot canyon (P2) inside the plateau. Sand floor between
   40 m+ cliff walls, cactus and bushes on the floor, a sandfall on the far wall.
6. `eye_cam17_cascade.png` (CineCamera 17): a big waterfall (~28 m) into a pool at a terrace step of
   the cascade (P3/P4). Grass banks, palms, shore boulders at the pool lip, and a second fall on the
   left.

Two more captures are in the folder without annotation: `top_sw_dunes.png` (dunes P8 and beach
P11, with the valley's grass-topped Shelf cliff band at the top) and `eye_cam27_ground_clutter.png`
(close-up of P12).
