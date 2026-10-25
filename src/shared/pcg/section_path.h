// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "section_asset.h"
#include "terrain_contribution.h"
#include "base/typedefs.h"
#include "math/path_spline.h"
#include "math/vector3.h"

#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace mmo::pcg
{
	/// An upright frame on a path. Sections are authored along local +Z with +X to the right.
	struct SectionFrame
	{
		/// World position on the path (height follows the spline).
		Vector3 position;

		/// Horizontal travel direction (unit, x/z).
		float forwardX = 0.0f;
		float forwardZ = 1.0f;

		/// Horizontal right direction (unit, x/z) when looking along forward with +Y up.
		float rightX = 1.0f;
		float rightZ = 0.0f;
	};

	/// One copy of a section, stretched to fit its span (or, for a corner copy, centred on its control point).
	struct SectionCopy
	{
		/// Index of the control point span (segment) this copy lies in; for a corner copy the index of its control point.
		uint32 span = 0;

		/// Index of the copy inside its span (0 for a corner copy).
		uint32 copy = 0;

		/// Arc distance of the copy's start from the path start.
		float startDistance = 0.0f;

		/// Arc length the copy covers (the section stretched to fit).
		float stretchedLength = 0.0f;

		/// Stable key of the span, derived from its two control point ids; for a corner copy Hash(point id, role).
		uint64 spanKey = 0;

		/// Straight for an ordinary copy, InnerCorner / OuterCorner for a corner piece at control point `span`.
		SectionVariantRole role = SectionVariantRole::Straight;

		/// Position among the layout's copies of the same role (what sequentialVariants counts).
		uint32 sequence = 0;
	};

	/// Result of fitting sections along a spline.
	struct SectionLayout
	{
		/// Copies ordered along the spline.
		std::vector<SectionCopy> copies;

		/// Smallest x/z radius of curvature found; the largest float for a path without any bend.
		float minimumBendRadius = 0.0f;

		/// Per control point of the laid spline: the role of the corner piece laid there, Straight where the path
		/// just bends (always Straight at the ends of an open path).
		std::vector<SectionVariantRole> pointRoles;

		/// Closed paths: the path length. The copies then cover [copies.front().startDistance, + loopLength), and an
		/// arc distance below the first copy's start belongs to the copy wrapping past the path end. 0 for open paths.
		float loopLength = 0.0f;
	};

	/// Sample step every path handed to this API must be built with, so arc distances agree between the
	/// layout, frames, projection and copy lookup.
	constexpr float kSectionPathSampleStep = 0.5f;

	/// Fits whole copies of a section into every control point span; each span gets
	/// max(1, round(spanLength / sectionLength)) copies stretched evenly to the span.
	/// With Catmull-Rom auto tangents moving a point also reshapes the neighbouring spans (span keys stay stable).
	/// @param spline The control points the path was sampled from.
	/// @param path The spline sampled with kSectionPathSampleStep.
	/// @param pointIds Stable id per control point (same size as spline.points); spanKey = Hash(id_j, id_{j+1}).
	/// @param sectionLength Authored length of the section along +Z; must be positive.
	SectionLayout LayOutSections(const PathSpline& spline, const SampledPath& path, const std::vector<uint64>& pointIds, float sectionLength);

	/// LayOutSections with corner pieces. The floor side is the path's left (-X); the path is laid as given.
	/// Turn angle at a control point (every point of a closed path, the inner points of an open one) = signed x/z
	/// angle between the chord arriving over the last L/2 and the chord leaving over the next L/2 (L = asset.length),
	/// positive when turning towards the floor side. If |angle| >= asset.cornerAngleDeg and the asset has a variant of
	/// the matching role (towards the floor = InnerCorner, away = OuterCorner), one corner copy (stretch 1) covers
	/// [d_i - L/2, d_i + L/2] with spanKey = Hash(point id, role). Points are decided in index order; a corner falls
	/// back to bending if it would leave less than L/2 of straight path to an earlier corner, or would not fit into
	/// one of its spans. The straight copies of each span fill what remains (max(1, round(remaining / L)) copies).
	/// An asset without corner variants lays exactly like the length overload.
	SectionLayout LayOutSections(const PathSpline& spline, const SampledPath& path, const std::vector<uint64>& pointIds, const SectionAsset& asset);

	/// Upright frame at an arc distance (clamped or wrapped like SampledPath).
	SectionFrame FrameAt(const SampledPath& path, float distance);

	/// Projects a world point onto the nearest point of the path.
	/// @param distance Receives the arc distance of that point.
	/// @param across Receives the signed offset to the right of the path (negative = left).
	/// @return False if the point is further than maxAcross from the path.
	bool ProjectToPath(const SampledPath& path, float x, float z, float maxAcross, float& distance, float& across);

	/// Finds the copy containing an arc distance (on a closed loop a distance below the first copy's start is taken
	/// + layout.loopLength, so it finds the corner copy wrapping past the path end).
	/// @param localZ Receives the position inside the copy mapped to 0..sectionLength.
	/// @return The copy, or nullptr if the distance is outside every copy.
	const SectionCopy* FindCopy(const SectionLayout& layout, float distance, float sectionLength, float& localZ);

	/// Where the centre line of a section path gets its height from.
	enum class SectionHeightMode : uint8
	{
		/// Centre line height = the spline's height.
		FollowSpline,
		/// Centre line height = the base ground under the spline.
		FollowGround
	};

	/// Per-path settings of a section path.
	struct SectionPathParams
	{
		/// Seed of the variant choice and the prop variation.
		uint64 seed = 1;
		/// Variants in layout order (copy n gets variant n % count) instead of hashed per copy. This is not
		/// local: inserting a copy shifts the variants of every later copy.
		bool sequentialVariants = false;
		/// Distance (m) on either side of a copy boundary over which the terrain targets of the two copies
		/// cross-fade (clamped to half of each copy's length).
		float joinBlend = 2.0f;
		/// Multiplies every rule's keep chance (0..1).
		float propDensity = 1.0f;
		/// Where the centre line height comes from.
		SectionHeightMode heightMode = SectionHeightMode::FollowSpline;
		/// Closed paths: lay the section so that its floor side (-X) faces the loop interior, whatever the winding
		/// (the path then runs against the control point order when the loop is clockwise). False lays it in control
		/// point order. Ignored for open paths.
		bool floorInside = true;
	};

	/// layerRemap[sectionLayer] = target tile layer index, or -1 = not available on that tile.
	using LayerRemapFunction = std::function<std::array<int8, 4>(float x, float z)>;

	/// Uniform x/z grid of polyline segment indices, so a projection only tests the segments near the query.
	/// Cell (i, j) covers [originX + i * cellSize, +cellSize) x [originZ + j * cellSize, +cellSize) and lists, in
	/// ascending order, every segment whose bounding box touches it. cellSize >= reach, so every segment within
	/// reach of a point is listed in the 3 x 3 cells around it.
	struct SectionSegmentGrid
	{
		float originX = 0.0f;
		float originZ = 0.0f;
		float cellSize = 0.0f;
		/// Largest maxAcross the grid answers exactly (larger queries fall back to the full scan).
		float reach = 0.0f;
		int32 columns = 0;
		int32 rows = 0;
		/// CSR layout: the segments of cell c are segments[cellStart[c] .. cellStart[c + 1]).
		std::vector<uint32> cellStart;
		std::vector<uint32> segments;
	};

	/// A section path ready to evaluate. Filled by PrepareSectionPath; treat every member as read-only.
	struct SectionPathShape
	{
		/// The spline sampled with kSectionPathSampleStep (empty until prepared); reversed if `reversed`.
		std::optional<SampledPath> path;
		/// True if the path runs against the control point order (a clockwise loop laid with floorInside).
		bool reversed = false;
		/// Copies fitted to the spline's spans (span indices along `path`); layout.pointRoles is in the caller's
		/// control point order.
		SectionLayout layout;
		/// Copy of the section asset, so the shape does not depend on the caller keeping it alive.
		SectionAsset asset;
		/// Indices of the asset's variants per SectionVariantRole, ascending.
		std::array<std::vector<uint32>, 3> variantsByRole;
		/// Sanitised parameters (joinBlend >= 0, propDensity in 0..1).
		SectionPathParams params;
		/// Centre line height per path sample (spline height, or the base ground under it).
		std::vector<float> centreHeights;
		/// See GetInfluenceRect.
		WorldRect influence;
		/// Segment lookup for ProjectToSectionPath; reach = halfWidth + sideFalloff + 1 m.
		SectionSegmentGrid segmentGrid;
	};

	/// Samples and lays out a section path.
	/// @param spline Control points (at least two).
	/// @param pointIds Stable, unique id per control point (same size as spline.points).
	/// @param asset The section; needs at least one Straight variant, length > 0 and halfWidth > 0. Corner variants are
	///        laid at sharp control points (see the asset overload of LayOutSections).
	/// @param params Path settings; out-of-range values are clamped.
	/// @param baseGround Base terrain height at a world position; required for FollowGround, may be empty otherwise.
	/// @param out Receives the shape.
	/// @param error Receives a message on failure.
	/// @return False on invalid input.
	bool PrepareSectionPath(const PathSpline& spline, const std::vector<uint64>& pointIds, const SectionAsset& asset,
		const SectionPathParams& params, const std::function<float(float, float)>& baseGround, SectionPathShape& out, String& error);

	/// Rectangle containing every terrain sample the path may change and every prop it may place: the path's
	/// sample bounds grown by max(halfWidth + sideFalloff, largest |x| of any baked placement, largest
	/// |run x| + piece length + |piece startOffset| of any run piece), plus the largest position jitter of any rule
	/// times sqrt(2) (jitter is per local axis), plus a 1 m margin.
	WorldRect GetInfluenceRect(const SectionPathShape& shape);

	/// Variant index of a copy, chosen among the variants of the copy's role (shape.variantsByRole): candidate
	/// Hash(seed, spanKey, copy) % candidateCount, or copy.sequence % candidateCount with sequentialVariants. Straight
	/// copies never get a corner variant and corner copies never a straight one. 0 if the role has no variant.
	uint32 GetVariant(const SectionPathShape& shape, const SectionCopy& copy);

	/// ProjectToPath on the shape's path, testing only the segments of the segment grid around the point. The
	/// result is bit-identical to ProjectToPath(*shape.path, ...) (same per-segment arithmetic, nearest wins, ties
	/// go to the lowest segment index). A maxAcross above the grid's reach falls back to the full scan.
	bool ProjectToSectionPath(const SectionPathShape& shape, float x, float z, float maxAcross, float& distance, float& across);

	/// Stable key of a baked placement of `variant` stamped by `copy`: Hash(spanKey, copy, variant, sourceKey, sourceKind).
	/// sourceKey is the baked placement's key (the section world object's unique id), so adding or removing another
	/// prop in the section leaves the keys of the others unchanged.
	uint64 SectionPlacementKey(const SectionCopy& copy, uint32 variant, uint64 sourceKey, PlacementKind sourceKind);

	/// Terrain target of the path at a world position.
	/// Height = centre line height + the variant's height; weight 1 within halfWidth of the path, fading
	/// (smoothstep) to 0 at halfWidth + sideFalloff (the distance is radial past the ends of an open path).
	/// Paint = the variant's weights remapped to tile layers (unavailable layers dropped, the rest renormalised
	/// to 255); paint weight = coverage x falloff. Within joinBlend of a copy boundary the two copies' targets are
	/// cross-faded. setMaterial is always false. baseHeight is not used, so paint never depends on it.
	/// @param remap Section layer to tile layer mapping at the position; empty = identity.
	TerrainTarget EvaluateSectionTerrain(const SectionPathShape& shape, float x, float z, float baseHeight, const LayerRemapFunction& remap);

	/// Places the baked props of every copy along the path. Per copy and baked placement p (variation from
	/// Hash(seed, spanKey, copy, p.key, p.kind), the placement's rule found by asset; no rule = no jitter, swap or keepChance,
	/// propDensity still applies):
	/// - kept if a hash is below keepChance x propDensity;
	/// - local x/z jittered by up to +-positionJitter each, then s = copy start + localZ x stretch, clamped to the
	///   span's arc range (a corner copy's own range), and position = frame(s).position + right x localX,
	///   y = ground(x, z) + heightAboveTerrain;
	///   the frame's direction is measured inside the span too, so a prop never depends on a neighbouring span;
	/// - rotation = yaw of the frame and a yaw jitter (+-yawJitterDeg) about world up times the local rotation,
	///   so props stay upright on slopes; scale x (1 +- scaleJitter);
	/// - when the swap group is not empty, one of {original} + swapGroup is picked by hash (index 0 keeps the
	///   original). A swap to another asset drops the material overrides and prop kits, which belong to the
	///   original asset, and takes the kind from the new asset (".hwmo" = WorldModel, else Entity); foliage stays
	///   foliage and never swaps to a world model;
	/// - key = SectionPlacementKey(copy, variant, p.key, p.kind), so editing one span leaves the keys of the others
	///   unchanged, and so does adding or removing another prop in the section.
	/// Connected prop runs (SectionVariant::runs) are laid as whole lines instead:
	/// - chains: continuous runs (IsContinuousRun) with the same name in consecutive copies, across spans, are joined
	///   into one chain; any other run is a chain of its own copy. A chain covers the arc range [first copy start +
	///   startZ x stretch, last copy start + endZ x stretch]. On a closed path a chain still open at the last copy
	///   continues into the chain of the same name started by the first copy (no seam at the loop start);
	/// - one keep roll per chain: Hash(seed, chainKey) < keepChance (of the first run) x propDensity, with chainKey =
	///   Hash of the first copy's spanKey and copy index and the run name, so it is stable when other spans change;
	/// - the pieces follow the offset line L(s) = frame(s).position + right(s) x run.x (the x of the run of the
	///   copy containing s; past the ends of an open path the line continues straight). The run's pieces are walked
	///   in order, repeating: a piece starting at s0 ends at the first s1 > s0 with |L(s1) - L(s0)| = its length
	///   (x/z), its yaw is the direction L(s0) -> L(s1), its pivot is L(s0) - direction x startOffset, its rotation
	///   is that yaw times the authored rotation, y = ground + heightAboveTerrain; the next piece starts at s1. The
	///   last piece laid is the last one that ends at most half its length (x/z) past L(chain end);
	/// - no prop rule, jitter or swap applies; key = Hash(chainKey, piece index in the chain).
	/// All props lie inside GetInfluenceRect.
	/// @param ground Final ground (base + this path's terrain). Empty = the centre line height of the height mode
	///        at the prop's arc position (spline height, or the base ground under the spline for FollowGround).
	/// @param out Cleared, then receives the placements sorted by key; keys are unique.
	void BuildSectionPlacements(const SectionPathShape& shape, const std::function<float(float, float)>& ground, std::vector<Placement>& out);
}
