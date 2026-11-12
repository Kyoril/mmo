// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "section_asset.h"
#include "terrain_access.h"

#include <array>
#include <vector>

namespace mmo::pcg
{
	/// Where one variant of the section lives in the section world.
	struct SectionVariantDef
	{
		/// World x of the variant's centre line; the line runs along +Z from world z = 0.
		float originX = 0.0f;
		/// What the variant is used for. Written to the .hsecdef only when not Straight.
		SectionVariantRole role = SectionVariantRole::Straight;
	};

	/// A connected prop run as authored: map entities (meshes or world models) of one variant laid end to end.
	struct SectionRunDef
	{
		/// Display name; continuous runs of the same name in consecutive copies are joined when stamped.
		String name;
		/// Probability (0..1) that the whole run is kept.
		float keepChance = 1.0f;
		/// Unique ids of the member map entities, ascending.
		std::vector<uint64> members;
	};

	/// Author-written description of a section (the .hsecdef file).
	struct SectionDefinition
	{
		String name;
		float length = 20.0f;
		float halfWidth = 4.0f;
		float sideFalloff = 4.0f;
		/// Turn angle (degrees) above which a corner piece is used. Written to the .hsecdef only when not 35.
		float cornerAngleDeg = 35.0f;
		std::vector<SectionVariantDef> variants;
		std::vector<PropRule> rules;
		/// Connected prop runs, in authoring order. A map entity belongs to at most one run.
		std::vector<SectionRunDef> runs;
	};

	/// Footprint of a run member along the run direction (+Z), measured by the editor from the mesh or world
	/// model bounds after the member's authored rotation and scale.
	struct SectionPieceExtent
	{
		/// Unique id of the map entity.
		uint64 key = 0;
		/// Length of the footprint along +Z (m).
		float length = 0.0f;
		/// Signed offset of the footprint start from the pivot along +Z.
		float startOffset = 0.0f;
	};

	/// Shortest piece a run accepts (m); shorter pieces would never advance along the path.
	constexpr float kMinRunPieceLength = 0.05f;

	/// Height of the terrain surface at a world position. Between outer vertices the surface is the
	/// engine's fan triangulation: each cell is four triangles around its inner (centre) vertex.
	/// Exact outer vertex positions and cell edges never read inner heights or cells beyond them, so
	/// the last row and column of the terrain can be sampled.
	float SampleTerrainHeight(const ITerrainAccess& terrain, double worldX, double worldZ);

	/// Captures the section world into a section asset.
	///
	/// Each variant captures the rectangle local x in [-halfWidth - sideFalloff, +halfWidth + sideFalloff],
	/// z in [0, length] around world (originX, 0). The terrain under the rectangle must exist; the
	/// caller keeps the section world big enough.
	/// - Heights are sampled on a grid at the outer vertex spacing, aligned to local 0. Grid points
	///   that are not outer vertices are sampled from the surface with SampleTerrainHeight, so the
	///   inner vertices are folded in there.
	/// - Paint is sampled at the splat pixel spacing (nearest pixel). Coverage is 255 where the packed
	///   splat differs from defaultPaint, else 0.
	/// - A placement belongs to the first variant whose rectangle contains its world (x, z). Its
	///   position becomes world minus (originX, 0, 0) (y stays the world height) and
	///   heightAboveTerrain = y - terrain height there. Placements outside every rectangle are ignored.
	/// - Rules are copied sorted by asset.
	///
	/// @param worldPlacements Placements of the section world, in world space.
	/// @param defaultPaint The packed splat value of an untouched section world (coverage reference).
	/// @param ignoredPlacements Receives the number of placements outside every rectangle if not null.
	/// @return False with a message in error for: no variants, length/halfWidth <= 0, negative
	///   sideFalloff or overlapping variant rectangles (rectangles that only touch are fine).
	bool BakeSection(const SectionDefinition& def, const ITerrainAccess& terrain, const std::vector<Placement>& worldPlacements,
		uint32 defaultPaint, const std::array<String, 4>& layerTextures, uint64 sourceStamp, SectionAsset& out, String& error,
		size_t* ignoredPlacements = nullptr);

	/// BakeSection with connected prop runs. Every run of the definition becomes a SectionRun of the variant that
	/// owns its members:
	/// - members are matched to non-foliage world placements by key; members that are not in the world are skipped
	///   (missingMembers counts them), a run without any member left is dropped;
	/// - x = mean local x of the members, pieces sorted by local z (ties by key), each with its placement (local
	///   x/z, authored rotation), heightAboveTerrain and the length/startOffset from pieceExtents;
	///   startZ / endZ = the smallest footprint start / largest footprint end (z + startOffset [+ length]);
	/// - run members are removed from the variant's placements, so no prop rule applies to them.
	/// @return False with a message for anything BakeSection refuses, and for: a run without a name, two runs of
	///   one variant with the same name, a member in two runs, a run whose members lie in different variants or
	///   outside every variant, or a member without an extent of at least kMinRunPieceLength.
	bool BakeSection(const SectionDefinition& def, const ITerrainAccess& terrain, const std::vector<Placement>& worldPlacements,
		const std::vector<SectionPieceExtent>& pieceExtents, uint32 defaultPaint, const std::array<String, 4>& layerTextures,
		uint64 sourceStamp, SectionAsset& out, String& error, size_t* ignoredPlacements = nullptr, size_t* missingMembers = nullptr);

	/// Clamps a rule's values into their valid ranges: keepChance to 0..1, the jitters to >= 0.
	void ClampPropRule(PropRule& rule);

	/// Parses a .hsecdef JSON document. Missing fields keep their defaults; rule values are clamped
	/// with ClampPropRule, run keep chances to 0..1, run members are sorted and de-duplicated.
	/// @return False if the text is not a JSON object.
	bool ParseSectionDefinition(const String& json, SectionDefinition& out);

	/// Writes a .hsecdef JSON document. Deterministic: keys in alphabetical order, rules, runs and
	/// variants in definition order. "runs" is only written when there is at least one run, so definitions
	/// without runs keep their text (and source stamp).
	String WriteSectionDefinition(const SectionDefinition& def);

	/// Canonical form of a .hsecdef document, used to hash it into the source stamp: WriteSectionDefinition of
	/// the parsed definition, so line endings (CRLF checkouts), whitespace and key order do not matter. Text that
	/// does not parse is returned with every '\r' removed.
	String CanonicalSectionDefinitionText(const String& json);
}
