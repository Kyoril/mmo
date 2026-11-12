// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "evaluator.h"
#include "graph.h"
#include "graph_data.h"
#include "terrain_op.h"

#include "pcg/grid_layout.h"
#include "pcg/placement.h"
#include "pcg/section_asset.h"
#include "pcg/terrain_contribution.h"
#include "terrain_io/water_flow.h"

#include <functional>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace mmo::pcg::graph
{
	/// @brief Supplies section assets by path (the editor loads them through the asset registry; tests pass
	///        in-memory assets). Returns null with a message in `error` if the asset cannot be supplied.
	using SectionAssetProvider = std::function<const SectionAsset*(const String& path, String& error)>;

	/// @brief Maps a texture key to the tile's layer index at (x, z), -1 if the tile has no layer with that texture.
	using TextureLayerLookup = std::function<int8(float x, float z, const String& textureKey)>;

	/// @brief One terrain op with the geometry of the item that carried it.
	struct TerrainOpInput
	{
		/// @brief The op descriptor.
		TerrainOp op;

		/// @brief The item's spline (Area: closed loop).
		PathSpline spline;

		/// @brief Stable point ids of the spline (may be empty for generated splines).
		std::vector<uint64> pointIds;

		/// @brief The item's points.
		PointSet points;

		/// @brief Producing control id (0 = generated).
		uint64 sourceId = 0;

		/// @brief Index of the item in its node's Terrain output; distinguishes generated items of one node (their
		///        fallback point ids mix it in).
		uint32 itemIndex = 0;

		/// @brief SectionPath: shapes whose positions get no section props (DataItem::exclusions).
		std::vector<ExclusionShape> exclusions;
	};

	/// @brief What Evaluate found while evaluating, for the caller to report once after a pass.
	struct TerrainEvaluateNotes
	{
		/// @brief Indices (in prepared op order) of Paint ops that were skipped somewhere because the tile has no
		///        layer with their texture.
		std::set<size_t> missingLayerOps;

		/// @brief (op index, section layer 0..3) of SectionPath ops whose section layer, painted somewhere in the asset,
		///        names a texture the lookup found no tile layer for at a sample where the section paints (any layer). That
		///        layer's paint may have been skipped there (it is not checked whether the layer has weight at the sample).
		std::set<std::pair<size_t, uint8>> missingSectionLayers;
	};

	/// @brief The tile material a Paint op with `tileMaterial` asks for on one terrain tile
	///        (PreparedTerrain::ResolveTileMaterials).
	struct TileMaterialRequest
	{
		/// @brief Global tile index along x (GridLayout: floor((x - originX) / tile size)).
		int32 tileX = 0;

		/// @brief Global tile index along z.
		int32 tileZ = 0;

		/// @brief The material the tile is switched to.
		String material;
	};

	/// @brief Where a prepared op acts, for debug drawing.
	struct TerrainOpOutline
	{
		/// @brief The op.
		TerrainOpType type = TerrainOpType::Noise;

		/// @brief Producing node id (op.order).
		uint32 order = 0;

		/// @brief The op's spline or area as sampled for the op (y is the spline height). River: the water surface
		///        line, sampled every 1 m downstream (y is the water surface).
		std::vector<Vector3> points;

		/// @brief River: the bed edges at +-width across the spline (left of the flow, then right), one point per
		///        surface sample (y is the bed height). Empty for other ops.
		std::vector<std::vector<Vector3>> bedEdges;

		/// @brief True if the last point joins the first.
		bool closed = false;

		/// @brief SectionPath: the influence rect of every section copy (GetSectionCopyRects), in layout order.
		std::vector<WorldRect> copyRects;

		/// @brief WaterArea: basin depth (m); 0 = no basin (then level and centroid are unused).
		float depth = 0.0f;

		/// @brief WaterArea with a basin: water surface height (level + levelOffset).
		float level = 0.0f;

		/// @brief WaterArea with a basin: centroid of the area (x/z; y is 0), where the debug view marks the depth.
		Vector3 centroid = Vector3::Zero;
	};

	/// @brief Gathers the Terrain items of every node's output pin named "Terrain" (nodes of the graph only),
	///        sorted by (op.stackOrder, op.order, node id, item index). Items of other kinds on that pin are ignored.
	std::vector<TerrainOpInput> CollectTerrainOps(const Graph& graph, const EvalResult& result);

	/// @brief Terrain ops prepared on the base terrain and composed into one terrain target.
	///
	/// Composition: the running height h starts at the base height. Every op that touches (x, z) returns
	/// (target, weight), then h = lerp(h, target, weight x fade), in op order. fade is the region fade: 1 inside the
	/// region, smoothstep to 0 over borderFade metres at the border, 0 outside. Op semantics (params by name, missing
	/// ones take the defaults in brackets):
	/// - SectionPath (op.asset = .hsec path, op.params = SetSectionPathParam names): EvaluateSectionTerrain on h, its
	///   paint remapped to tile layers through the lookup over the asset's layerTextures (identity if the asset names
	///   no textures).
	/// - Noise (closed area; amplitude 1.5, wavelength 24, octaves 3, falloff 8, seed): target = h + amplitude x
	///   FractalNoise (world-anchored, [-1, 1]) seeded from the seed param (Int, or Float rounded to the nearest
	///   integer; op.order when absent); weight smoothstep from 0 at the area edge to 1 at falloff inside.
	/// - Raise (spline; width 40, falloffPower 2): peak = max(0, spline y - base ground under the spline) per spline
	///   sample (base sampled in Prepare), interpolated at the nearest point; target = h + peak x
	///   smoothstep(1 - d / width)^falloffPower, d = distance to the spline.
	/// - Carve (spline; width 4, depth 1.5, bank 3): profile 1 within width (per side) of the spline, falling (smoothstep)
	///   to 0 over bank metres beyond; target = h - depth x profile.
	/// - Paint (texture = texture key, weight 1, width 4, falloff 2): a closed spline paints inside the area, an open
	///   one within width of the spline, both fading (smoothstep) from 0 at the edge to full over falloff metres
	///   inwards; the splat is the layer the lookup gives for the texture at (x, z), at `weight`. Skipped where the
	///   lookup gives -1 (noted in TerrainEvaluateNotes). With `tileMaterial` (String; absent or empty = none) the op
	///   also asks for that material on every tile it touches (ResolveTileMaterials); once resolved, it paints nothing
	///   on a tile that resolved to another material (a later stacked op won it).
	/// - WaterArea (closed area; level 0, levelOffset 0, margin 4, depth 0, shoreWidth 6, op.asset = water material):
	///   waterType 1 and waterHeight = level + levelOffset inside the area and up to margin metres outside its edge.
	///   With depth > 0 the area is also a basin (heights channel only): with d the inward distance from the edge and
	///   s = smoothstep(0, shoreWidth, d), target = min(h, h + (level + levelOffset - depth - h) x s) at weight 1 inside
	///   the area, so the shore starts at the running height and the bed lies depth under the water after shoreWidth.
	///   Outside the edge heights are untouched. Prepare then also warns once if the base ground along the outline
	///   (sampled every 2 m) is below the water level. depth 0 writes no heights.
	/// - River (open spline of at least 2 points; width 5, bank 4, depth 1.5, surfaceOffset -0.4, waterMargin 3,
	///   op.asset = water material): the spline is sampled every 1 m downstream (first to last point); the water surface
	///   surface(s) is the running minimum of y(s) + surfaceOffset, so it never climbs (Prepare warns once, naming the
	///   largest climb, where the spline rises more than 0.5 m above it). With d the distance to the spline, t the
	///   surface at the projected arc distance and top = max(h, t + 0.25): target = t - depth within width; over bank,
	///   target = lerp(t - depth, top, smoothstep(width, width + bank, d)), so the bank lowers high ground and lifts low
	///   ground just above the water; past width + bank, ground below t + 0.25 is lifted to it up to
	///   reach = width + bank + waterMargin and fades back over another bank (weight 1 wherever written). Water:
	///   waterType 1 and waterHeight = t where d <= reach, so the water's visible edge is where the bank crosses the
	///   surface, never the edge of the watered quads. Past the upstream end, d is width plus the distance to the bed's
	///   end edge (the end closes like the banks); past the downstream end there is no water and no owned bed, only a
	///   bank that lowers the running height towards the bed by that distance (the river flows into a lake or a fall).
	/// Splat targets are composited in op order (each op's layer weights over the previous ones at its weight x
	/// fade); water is last writer wins inside the region. Heights are only written where an op touches: the result
	/// is then (final h, weight 1), else weight 0. Splat and water do not depend on the base height. Like all ops,
	/// Raise, Carve and Noise are relative to the running height and therefore commute with each other; SectionPath
	/// targets an absolute height, so its place in the order matters.
	///
	/// Evaluate and BuildPlacements do not modify the object (what they find goes to caller-owned outputs), so they
	/// may be called concurrently.
	class PreparedTerrain final
	{
	public:
		PreparedTerrain();
		~PreparedTerrain();
		PreparedTerrain(PreparedTerrain&&) noexcept;
		PreparedTerrain& operator=(PreparedTerrain&&) noexcept;
		PreparedTerrain(const PreparedTerrain&) = delete;
		PreparedTerrain& operator=(const PreparedTerrain&) = delete;

		/// @brief Prepares every op on the given base ground (called after Unapply). Ops with unusable geometry or
		///        settings are skipped with a warning.
		/// @param ops The ops in composition order (see CollectTerrainOps).
		/// @param baseGround Base terrain height at a world position (may be empty: 0).
		/// @param sections Supplies the section assets of SectionPath ops.
		/// @param region The region loop (closed, at least 3 points); writes are clipped to it.
		/// @param borderFade Width (m) of the fade inside the region border; 0 = no fade.
		/// @param error Receives a message naming the node and asset for a missing asset, a rejected section param
		///        or a degenerate section path, or the problem with the region.
		/// @param neighbours Areas of the other graph instances in the world that contain terrain nodes (closed loops).
		///        A region edge segment is a seam where it lies on a neighbour border: nine points along it (both end
		///        points and the eighths between) are all within 0.05 m of the neighbour loop. A segment that only ends at a
		///        neighbour corner or cuts across it is not. The border fade ignores seam edges, so terrain does not fade
		///        there. Writes still stop at the region edge. Loops that are not usable are ignored.
		/// @return False on such an error (the object is then empty).
		bool Prepare(const std::vector<TerrainOpInput>& ops, const std::function<float(float, float)>& baseGround,
			const SectionAssetProvider& sections, const PathSpline& region, float borderFade, String& error,
			const std::vector<PathSpline>& neighbours = {});

		/// @brief Union of op influences clipped to the region bounds; empty (all 0) if there are no ops.
		WorldRect GetInfluence() const;

		/// @brief Hash of the region's seam segments (the edges it shares with neighbours), 0 if it has none.
		uint64 GetSeamHash() const
		{
			return m_seamHash;
		}

		/// @brief Indices (into the neighbours passed to Prepare, ascending) of the neighbours that share a seam.
		const std::vector<size_t>& GetSeamNeighbours() const
		{
			return m_seamNeighbours;
		}

		/// @brief True if no op was prepared.
		bool IsEmpty() const;

		/// @brief Stacks all ops on `baseHeight` in order (see the class comment).
		/// @param layers Texture key to tile layer lookup; empty = Paint ops are skipped and sections paint their
		///        layers unmapped.
		/// @param notes If not null, receives what was skipped (see DescribeNotes); owned by the caller.
		/// @param channels The channels the caller reads (pcg::terrain_channel flags). Those are filled exactly as with
		///        terrain_channel::All; the others stay at their defaults, and the ops only they need are skipped: a
		///        splat-only or water-only call runs no Noise, Raise or Carve and lays no section heights, a
		///        heights-only call does no texture lookups. With terrain_channel::Material, `material` is the material
	///        ResolveTileMaterials resolved for the tile containing (x, z) (empty for other tiles, or before it was
	///        called), wherever (x, z) lies; setMaterial is never set.
		TerrainTarget Evaluate(float x, float z, float baseHeight, const TextureLayerLookup& layers,
			TerrainEvaluateNotes* notes = nullptr, terrain_channel::Type channels = terrain_channel::All) const;

		/// @brief Decides the tile materials of the Paint ops with `tileMaterial`: every tile of
		///        pcg::TileRangeOverlapping(layout, area) (the tiles Apply over `area` names materials on) gets the material of the last stacked such op whose painted region (the area polygon,
		///        or the spline grown by its width; ops with weight 0 paint nothing) intersects the tile rectangle and the
		///        region. Intersection is geometric (an edge crosses the tile, a vertex lies in it, or the tile centre is
		///        inside), not a tile-centre sample; merely touching a tile border does not count. A tile that two such
		///        ops ask different materials for is reported by DescribeNotes. Replaces the result of an earlier call.
		///        Must not run concurrently with Evaluate or IsTileMaterialPaint.
		/// @param layout The terrain grid layout (tile size = vertexSpacing x verticesPerTile).
		/// @param area The tiles to consider (pass the rect Apply writes over); an inverted rect resolves no tile.
		void ResolveTileMaterials(const GridLayout& layout, const WorldRect& area);

		/// @brief Drops every resolved tile of a material (one the caller cannot load): those tiles keep their own
		///        material, and the Paint ops paint there as if nothing had been resolved for them. Conflict warnings
		///        stay. Must not run concurrently with Evaluate or IsTileMaterialPaint.
		void DiscardTileMaterial(const String& material);

		/// @brief The tiles ResolveTileMaterials switched, sorted by (tileZ, tileX).
		const std::vector<TileMaterialRequest>& GetTileMaterials() const;

		/// @brief True where a Paint op with `tileMaterial` paints with a weight above 0 (region fade included), the
		///        texture lookup aside. An op that lost the tile to another material does not count there.
		bool IsTileMaterialPaint(float x, float z) const;

		/// @brief Appends one warning per noted op: a Paint texture that is not a layer of some tile, or a SectionPath
		///        op whose painted section layers name textures that are not (the warning names the section asset and
		///        those textures, whose paint may have been skipped there). Then one warning per tile and losing
		///        material of ResolveTileMaterials: "Tile (<x>, <z>): '<loser>' and '<winner>' both requested; '<winner>'
		///        wins".
		void DescribeNotes(const TerrainEvaluateNotes& notes, std::vector<String>& warnings) const;

		/// @brief Section placements of every SectionPath op, keyed Hash(order, sourceId, section key), kept where the
		///        region fade is at least 0.5 (no props beside a road that has faded out at the border), sorted by key
		///        (a duplicate key is dropped with a warning). Props inside an exclusion area of their op, or within
		///        the radius of one of its exclusion splines, are skipped (same tests as Difference); exclusion shapes
		///        that are not usable are dropped with a warning in Prepare.
		/// @param ground Final ground, passed to BuildSectionPlacements.
		/// @param out Cleared, then receives the placements.
		/// @param warnings If not null, receives the warnings.
		void BuildPlacements(const std::function<float(float, float)>& ground, std::vector<Placement>& out,
			std::vector<String>* warnings = nullptr) const;

		/// @brief The water material: the first non-empty River material, else the first non-empty WaterArea material,
		///        in op order (River materials win because only they read the flow; a warning is recorded for every
		///        differing material that is not used).
		const String& GetWaterMaterial() const;

		/// @brief The flow paths of every River op, in op order: the river's samples every 4 m downstream (and its last
		///        point), with halfWidth = width + waterMargin, cut into runs where the region fade is > 0. Each run of
		///        two points or more is one path, in downstream order.
		/// @param out Cleared, then receives the paths.
		void GetFlowPaths(std::vector<terrain_io::water_flow::FlowPath>& out) const;

		/// @brief Warnings of Prepare, each text once.
		const std::vector<String>& GetWarnings() const;

		/// @brief The outline of every prepared op, in op order (for debug drawing).
		/// @param out Cleared, then receives one outline per op.
		void GetOutlines(std::vector<TerrainOpOutline>& out) const;

	private:
		struct PreparedOp;

		/// @brief Region fade at a position: 1 inside, smoothstep to 0 over the border fade, 0 outside.
		float RegionFade(float x, float z) const;

		/// @brief Adds a warning unless the same text is already recorded.
		void Warn(const String& message);

		/// @brief Paint op weight at a position before op.weight and the region fade (0 outside its painted region).
		static float PaintEdgeWeight(const PreparedOp& op, float x, float z);

		/// @brief The resolved material of the tile containing (x, z); null if none was resolved for it.
		const String* ResolvedTileMaterial(float x, float z) const;

		/// @brief Whether a Paint op with `tileMaterial` gave up the tile at (x, z) to another material.
		bool LostTile(const PreparedOp& op, float x, float z) const;

	private:
		std::vector<std::unique_ptr<PreparedOp>> m_ops;
		std::unique_ptr<PreparedOp> m_region;
		float m_borderFade = 0.0f;
		uint64 m_seamHash = 0;
		std::vector<size_t> m_seamNeighbours;
		WorldRect m_influence;
		String m_waterMaterial;
		/// True once m_waterMaterial is a River material.
		bool m_waterMaterialFromRiver = false;
		std::vector<String> m_warnings;
		/// ResolveTileMaterials: the layout, its tiles sorted by (tileZ, tileX) and the conflict warnings.
		GridLayout m_tileLayout;
		std::vector<TileMaterialRequest> m_tileMaterials;
		std::vector<String> m_tileMaterialConflicts;
	};
}
