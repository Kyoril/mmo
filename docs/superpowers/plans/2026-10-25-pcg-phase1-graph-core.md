# PCG Phase 1 — Graph Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A headless, deterministic, data-driven PCG graph core: graph files of typed nodes and links, an evaluator with caching, control types and placed controls, and the v1 input/sampler/filter/point nodes — fully unit-tested, ready for the editor (phase 2) and terrain/section nodes (phase 3).

**Architecture:** New sub-folder `src/shared/pcg/graph/` inside the existing `pcg` library (headless, linked only by `mmo_edit` and `pcg_tests`). Data flows between nodes as collections of tagged items (points with attribute columns, splines, areas, params), Unreal-style but simplified. Nodes are registered types with a settings schema and a stateless execute function. Graphs, control types and placed controls are JSON files (human-reviewed tilesets — readable diffs).

**Tech Stack:** C++17, nlohmann/json (no exceptions: `parse(..., nullptr, false)`), Catch2, existing `pcg_random.h`, `math/path_spline.h`.

**Spec:** `docs/superpowers/specs/2026-10-25-pcg-graphs-design.md` (phase 1; "Concepts and data model", "Node set v1", "Resolved after the builder check" items 3–4).

## Global Constraints

- Branch `feature/pcg-oasis-poc`; no ship/merge/push. Map 0 frozen (this phase writes no world data at all).
- Pure library: headless, gcc-compatible, no exceptions, no graphics/scene_graph/terrain deps. Namespace `mmo::pcg::graph`. Tests in `src/tests/pcg_tests/test_graph_*.cpp`.
- Determinism: hash-based randomness only (`pcg::Hash`, `HashToUnit`, `HashRange`, `ValueNoise`, `FractalNoise`); no `std::` distributions/random_device/unordered containers on any output path; attribute maps are `std::map`; output order defined and documented per node. Same graph + controls + terrain + seed → identical outputs and identical content hashes.
- Seeds: node seed = `Hash(instanceSeed, nodeId)`; point seed = `Hash(nodeSeed, cellX, cellZ, salt)` from world-anchored cells (or the producing control's stable id) — never from a running counter.
- Locality (spec item 4): samplers and Self Prune use world-anchored cells, so moving one control changes outputs only near it (tested).
- File formats (deterministic key order, 2-space indent JSON, versioned with `"version": 1`):
  graph `.hpcgg`, control type `.hctl`, placed control `<id>.hpcgc` (world folder `data/client/Editor/Pcg/Worlds/<world>/`). Biome layout `data/client/Editor/Pcg/Biomes/<Biome>/{Graphs,Controls,Sections}` (used from phase 2; tests use in-memory JSON).
- Code style per CLAUDE.md (Allman, tabs, braces on every `if`, `m_camelCase`, `PascalCase`, `#pragma once`, Doxygen on public members, copyright header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`).

## Review Focus

1. **Graphs with cycles, dangling links, unknown node types or pin-kind mismatches** — load/validate reports a clear per-node error and evaluation refuses; never crashes. (Task 2 + 3 tests.)
2. **Empty inputs** (no controls of a tag, empty area, zero-length spline) — every node outputs an empty collection, no NaN, no crash. (Each node task: one empty-input test.)
3. **Attribute type mismatches** (filtering a string attribute numerically, missing attribute) — node reports a warning and passes/drops by a documented rule. (Task 7/8 tests.)
4. **Very dense requests** (min spacing 0, density 1 over a large area) — samplers cap output with a per-node limit and report it, instead of exhausting memory. (Task 6 test.)
5. **Cache staleness** — changing a control, a parameter or a node setting re-evaluates exactly the affected downstream nodes; unchanged branches are served from cache. (Task 3 test.)

---

### Task 1: Data items, attributes and content hashing

**Files:** create `src/shared/pcg/graph/graph_data.h/.cpp`; test `src/tests/pcg_tests/test_graph_data.cpp`.

**Produces:**
```cpp
namespace mmo::pcg::graph
{
	enum class AttributeType : uint8 { Float, Int, Bool, Vector3, String };
	using AttributeValue = std::variant<float, int64, bool, Vector3, String>;
	AttributeType TypeOf(const AttributeValue& v);
	/// Item-level attributes and graph parameters.
	using AttributeSet = std::map<String, AttributeValue>;

	struct Point
	{
		Vector3 position; Quaternion rotation; Vector3 scale = Vector3(1, 1, 1);
		float density = 1.0f; uint64 seed = 0;
		Vector3 boundsMin = Vector3(-0.5f, -0.5f, -0.5f), boundsMax = Vector3(0.5f, 0.5f, 0.5f);
	};
	/// Points plus per-point attribute columns (each column has points.size() entries, one type).
	struct PointSet
	{
		std::vector<Point> points;
		std::map<String, std::pair<AttributeType, std::vector<AttributeValue>>> columns;
		bool SetAttribute(size_t index, const String& name, const AttributeValue& value); // creates column, refuses type change
		const AttributeValue* GetAttribute(size_t index, const String& name) const;
		void Append(const PointSet& other);   // merges columns, missing values = type default
		void Keep(const std::vector<uint8>& keepMask); // filter in place, preserving order
	};

	enum class ItemKind : uint8 { Points, Spline, Area, Params };
	struct DataItem
	{
		ItemKind kind = ItemKind::Params;
		std::set<String> tags;
		AttributeSet attributes;   // item-level (control properties, etc.)
		PointSet points;           // Points
		PathSpline spline;         // Spline (open or closed) and Area (closed loop)
		uint64 sourceId = 0;       // producing control id (0 = generated)
	};
	using Collection = std::vector<DataItem>;
	uint64 ContentHash(const Collection& c);   // order-sensitive, deterministic, covers every field
}
```
- [ ] **Step 1: Failing tests** `[graph_data]`: SetAttribute creates a column and refuses a type change; Append merges columns with defaults; Keep preserves order and columns; ContentHash equal for equal collections, differs when any point field, attribute, tag or spline point changes; empty collection hash stable.
- [ ] **Step 2–4:** RED → implement (add `graph/` to the pcg CMake glob if needed — `add_lib` recurse or explicit) → GREEN.
- [ ] **Step 5: Commit** `feat(pcg): graph data items and attributes`

### Task 2: Graph model and graph files

**Files:** `graph/graph.h/.cpp`, `graph/graph_io.h/.cpp`; test `test_graph_model.cpp`.

**Produces:**
```cpp
namespace mmo::pcg::graph
{
	struct NodeInstance { uint32 id = 0; String type; AttributeSet settings; float editorX = 0, editorY = 0; bool debug = false; String comment; };
	struct Link { uint32 fromNode = 0; String fromPin; uint32 toNode = 0; String toPin; };
	struct ParameterDef { String name; AttributeValue defaultValue; String description; };
	struct Graph { String name; std::vector<NodeInstance> nodes; std::vector<Link> links; std::vector<ParameterDef> parameters; uint32 nextNodeId = 1; };
	bool ParseGraph(const String& json, Graph& out, String& error);
	String WriteGraph(const Graph& graph);   // deterministic: nodes by id, links sorted, settings by key
}
```
Validation lives in Task 3 (needs the registry); this task covers model + IO only.
- [ ] **Step 1: Failing tests** `[graph_model]`: round trip of a 4-node graph with links, params, settings of every attribute type is text-identical; malformed JSON / wrong version / duplicate node id → false with a message; unknown keys ignored with no crash.
- [ ] **Step 2–4:** RED → implement → GREEN.
- [ ] **Step 5: Commit** `feat(pcg): graph model and .hpcgg files`

### Task 3: Node registry, validation, evaluator and cache

**Files:** `graph/node_registry.h/.cpp`, `graph/evaluator.h/.cpp`; test `test_graph_evaluator.cpp`.

**Produces:**
```cpp
namespace mmo::pcg::graph
{
	struct PinDef { String name; std::vector<ItemKind> accepts; bool multiple = true; bool required = false; };
	struct SettingDef { String name; AttributeValue defaultValue; float min = 0, max = 0; std::vector<String> enumValues; String description; };
	struct EvalContext;   // below
	struct NodeOutput { std::map<String, Collection> pins; std::vector<String> warnings; String error; };
	using NodeExecute = std::function<NodeOutput(const EvalContext&, uint64 nodeSeed, const AttributeSet& settings, const std::map<String, Collection>& inputs)>;
	struct NodeTypeDef { String type; String category; String title; std::vector<PinDef> inputs; std::vector<PinDef> outputs; std::vector<SettingDef> settings; bool readsContext = false; NodeExecute execute; };
	class NodeRegistry
	{
	public:
		void Register(NodeTypeDef def);
		const NodeTypeDef* Find(const String& type) const;
		std::vector<const NodeTypeDef*> All() const;   // sorted by category, title
		static NodeRegistry& Default();                // all built-in nodes registered (Tasks 5–8 add theirs)
	};
	struct GraphIssue { uint32 node = 0; String message; };
	std::vector<GraphIssue> ValidateGraph(const Graph& g, const NodeRegistry& r); // unknown types, unknown pins, kind mismatch, cycles, missing required inputs, settings type mismatch

	struct ITerrainSampler { virtual ~ITerrainSampler() = default; virtual bool Height(float x, float z, float& h) const = 0; virtual Vector3 Normal(float x, float z) const = 0; virtual float SplatWeight(const String& textureKey, float x, float z) const = 0; virtual bool WaterLevel(float x, float z, float& level) const = 0; virtual uint64 Revision() const = 0; };
	struct EvalContext { uint64 instanceSeed = 1; PathSpline region; AttributeSet parameters; const std::vector<struct ResolvedControl>* controls = nullptr; const ITerrainSampler* terrain = nullptr; uint64 controlsRevision = 0; size_t pointLimit = 2'000'000; };

	struct NodeResult { NodeOutput output; uint64 hash = 0; bool fromCache = false; double milliseconds = 0; };
	class GraphCache { public: void Clear(); size_t Size() const; /* keyed by (node id, input hashes, settings hash, seed, context hash for readsContext nodes) */ };
	struct EvalResult { std::map<uint32, NodeResult> nodes; std::vector<GraphIssue> issues; bool ok = false; };
	EvalResult Evaluate(const Graph& g, const NodeRegistry& r, const EvalContext& ctx, GraphCache* cache);
}
```
Evaluation: validate first (issues → ok=false, nothing executed); topological order by id among ready nodes; inputs of a pin = concatenation of linked outputs in link order; node seed = `Hash(ctx.instanceSeed, node.id)`; settings missing in the instance take the def's default; a node error stops its downstream nodes (they report "upstream failed").
- [ ] **Step 1: Failing tests** `[graph_eval]` with test-only node types registered in a local registry: chain A→B→C evaluates in order; cycle → issue, nothing executed; kind mismatch → issue; missing required input → issue; deterministic re-run → equal hashes; **cache**: second evaluation with no change → all fromCache; change one node's setting → that node and its downstream re-run, sibling branch cached; change ctx.parameters → only nodes reading params (readsContext) and downstream re-run; node error → downstream "upstream failed".
- [ ] **Step 2–4:** RED → implement → GREEN.
- [ ] **Step 5: Commit** `feat(pcg): graph node registry, validation and cached evaluation`

### Task 4: Control types and placed controls

**Files:** `graph/controls.h/.cpp`; test `test_graph_controls.cpp`.

**Produces:**
```cpp
namespace mmo::pcg::graph
{
	enum class ControlShape : uint8 { ClosedLoop, OpenSpline, Volume, Point };
	enum class PropertyType : uint8 { Float, Int, Bool, Enum, String, Asset, AssetList, Color };
	struct PropertyDef { String name; PropertyType type = PropertyType::Float; AttributeValue defaultValue; float min = 0, max = 0; std::vector<String> enumValues; String description; };
	struct PropertyGroup { String name; std::vector<PropertyDef> properties; };
	struct ControlType { String name; ControlShape shape = ControlShape::ClosedLoop; std::set<String> defaultTags; std::vector<PropertyGroup> groups; };
	struct ControlInstance
	{
		uint64 id = 0; String typePath; String name; std::set<String> extraTags;
		PathSpline spline; std::vector<uint64> pointIds; uint64 nextPointId = 1;   // loops/splines
		Vector3 position; Quaternion rotation; Vector3 halfExtents = Vector3(5, 5, 5);   // volume/point
		AttributeSet overrides;
	};
	/// Type + instance merged: tags = defaultTags ∪ extraTags, attributes = defaults overridden (validated & clamped).
	struct ResolvedControl { const ControlType* type = nullptr; const ControlInstance* instance = nullptr; std::set<String> tags; AttributeSet attributes; };
	bool ParseControlType(const String& json, ControlType& out, String& error);   String WriteControlType(const ControlType&);
	bool ParseControlInstance(const String& json, ControlInstance& out, String& error); String WriteControlInstance(const ControlInstance&);
	bool ResolveControl(const ControlType& type, const ControlInstance& inst, ResolvedControl& out, std::vector<String>& warnings);
	uint64 ControlsHash(const std::vector<ResolvedControl>& controls);   // for the evaluator's context hash
}
```
AssetList values are stored as a String attribute holding `asset|weight;asset|weight` (documented); Color as Vector3. Overrides of unknown properties are kept but warned; type mismatches fall back to the default with a warning; numeric values clamp to [min,max] when max > min.
- [ ] **Step 1: Failing tests** `[graph_controls]`: round trip of a "Forest Area" type with two groups and every property type; instance round trip (loop with point ids, overrides); resolve merges tags, applies defaults, clamps, warns on unknown/mismatched overrides; shape/kind consistency (loop type requires closed spline ≥ 3 points → warning/refusal).
- [ ] **Step 2–4:** RED → implement → GREEN.
- [ ] **Step 5: Commit** `feat(pcg): control types and placed controls`

### Task 5: Input nodes

**Files:** `graph/nodes_input.cpp` (+ registration in `NodeRegistry::Default`); test `test_graph_nodes_input.cpp`.

Nodes: **Get Controls** (settings: `tags` (comma list, all must match), `excludeTags`, `controlType` (optional path); output pin `Out`: one item per matching control — ClosedLoop → Area, OpenSpline → Spline, Volume → Area (box loop) with attribute `volumeHeight`, Point → Points (single point); tags + resolved attributes on the item, sourceId = control id; only controls whose bounds intersect the region bounds); **Graph Parameters** (output `Out`: Params item with ctx.parameters over graph defaults); **Get Region** (output: the instance region as an Area item); **Get Terrain** is not a data node — terrain is read via `ctx.terrain` by nodes that need it (documented).
- [ ] **Step 1: Failing tests** `[graph_nodes_input]`: tag filter (all-of, exclude), type filter, region culling, shape → kind mapping, attributes carried, empty controls → empty output; parameters merge graph defaults with overrides.
- [ ] **Step 2–4:** RED → implement → GREEN. **Step 5: Commit** `feat(pcg): graph input nodes`

### Task 6: Samplers

**Files:** `graph/nodes_sampler.cpp`; test `test_graph_nodes_sampler.cpp`.

Nodes: **Sample Spline** (input Splines/Areas; settings `spacing`, `offset` (signed lateral), `alignToPath`, `endsOnly` (emit only the two end points of open splines); points carry attribute `distance` and item attributes copied as columns); **Sample Area** (input Areas; settings `cellSize`, `minSpacing`, `jitter` 0–1, `densityAttribute` (optional item attribute scaling acceptance); world-anchored cells: per cell one candidate at `cell + jitter·Hash` accepted if inside the area and hash < density; Poisson-like spacing via a second pass equal to Self Prune with `minSpacing` — locality preserved); **Sample Terrain Surface** (inputs optional Areas, else region; `cellSize`; y from `ctx.terrain`, rotation from the normal when `alignToNormal`). All: point seed from world cell; output order sorted by (cellZ, cellX); point limit → stop + warning.
- [ ] **Step 1: Failing tests** `[graph_nodes_sampler]`: spline sampling count/spacing/offset on a straight and a curved spline; area sampling stays inside, honours min spacing (no pair closer than minSpacing·0.999), deterministic, **locality** (moving one far vertex of the loop leaves points > 2·cellSize away from the changed edge identical), density attribute halves count ±10 %; terrain surface y equals the fake terrain height; point limit triggers a warning and caps output; empty input → empty.
- [ ] **Step 2–4:** RED → implement → GREEN. **Step 5: Commit** `feat(pcg): graph sampler nodes`

### Task 7: Filters and spatial nodes

**Files:** `graph/nodes_filter.cpp`; test `test_graph_nodes_filter.cpp`.

Nodes: **Filter by Tag** (items in/out by tag rules; pins `In`, `Out`, `Rejected`), **Filter by Attribute** (point or item attribute vs value with op `<,<=,==,!=,>=,>` or `in range`; missing attribute → rejected + one warning; string attributes support only `==`/`!=`), **Filter by Terrain** (slope range °, height range, `splatKey` + min weight, `underWater` exclude), **Density Filter** (keep density in [min,max]), **Self Prune** (min spacing; priority: higher density first, then lower seed; world grid of cell = minSpacing; deterministic; local), **Difference** (points minus Areas (inside) and minus Splines within `distance` (+ optional item attribute `clearance` added per spline, e.g. road width); Areas minus Areas is phase 3), **Intersection** (points inside Areas), **Distance To** (writes attribute `distanceTo<Name>` = nearest distance to splines/area edges of the second input; inside areas negative).
- [ ] **Step 1: Failing tests** `[graph_nodes_filter]` for each node incl. empty inputs, missing attribute rule, Self Prune spacing + determinism + locality, Difference with a road spline carrying `clearance`, Distance To sign inside/outside.
- [ ] **Step 2–4:** RED → implement → GREEN. **Step 5: Commit** `feat(pcg): graph filter and spatial nodes`

### Task 8: Point operations

**Files:** `graph/nodes_point.cpp`; test `test_graph_nodes_point.cpp`.

Nodes: **Transform Points** (offset min/max, yaw/pitch/roll ranges, uniform/non-uniform scale ranges, `alignToTerrain` | `upright`, all from point seed), **Density Noise** (world FractalNoise with frequency/octaves/amount, mode multiply|add|set), **Attribute Math** (target attribute or `density`/`scale`/`position.y`, op add/sub/mul/div/min/max/clamp/remap with constants or another attribute), **Copy Attributes from Control** (for each point, copy named item attributes from the first input Area containing it, or nearest Spline within distance).
- [ ] **Step 1: Failing tests** `[graph_nodes_point]`: ranges respected and deterministic; align to the fake terrain normal; noise is world-anchored (translated sample set gets the same values at the same world points); remap math; copy picks the containing area's `density`; empty inputs.
- [ ] **Step 2–4:** RED → implement → GREEN. **Step 5: Commit** `feat(pcg): graph point operation nodes`

### Task 9: Acceptance graphs

**Files:** `src/tests/pcg_tests/test_graph_acceptance.cpp` (+ JSON fixtures inline).

- [ ] **Step 1: Forest scenario** — controls: two Forest Areas (`forest`, densities 0.6/0.3), one Exclusion loop (`exclusion`), one Road spline (`road`,`exclusion`, `clearance` 6); fake terrain with a steep band. Graph JSON: Get Controls(forest) → Sample Area → Difference(Get Controls(exclusion)) → Self Prune(6 m) → Filter by Terrain(slope ≤ 30°) → Transform Points. Assert: no point inside the exclusion or within 6 m of the road, none on the steep band, min spacing holds, the denser forest has ≈2× points per area, two runs identical, moving the exclusion changes only points near it.
- [ ] **Step 2: Waterfall rule** — controls: two River splines (`river`) and one Lake area (`water`, attribute `level`). Graph: Get Controls(river) → Sample Spline(`endsOnly`) → Distance To(Get Controls(water)) → Filter by Attribute(distanceToWater ≤ 15) → Attribute Math(drop = position.y − level) → Filter by Attribute(drop ≥ 3). Assert: exactly the river end that sits 10 m from the lake and 8 m above it yields a waterfall point; the other end does not.
- [ ] **Step 3: Performance sanity** (tagged `[.perf]`, not in the default run): forest scenario over a 1.6 km² region with 4 m cells → evaluation time printed; record the number in the report.
- [ ] **Step 4: Commit** `test(pcg): graph acceptance scenarios`
