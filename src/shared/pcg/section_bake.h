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
	};

	/// Author-written description of a section (the .hsecdef file).
	struct SectionDefinition
	{
		String name;
		float length = 20.0f;
		float halfWidth = 4.0f;
		float sideFalloff = 4.0f;
		std::vector<SectionVariantDef> variants;
		std::vector<PropRule> rules;
	};

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

	/// Clamps a rule's values into their valid ranges: keepChance to 0..1, the jitters to >= 0.
	void ClampPropRule(PropRule& rule);

	/// Parses a .hsecdef JSON document. Missing fields keep their defaults; rule values are clamped
	/// with ClampPropRule.
	/// @return False if the text is not a JSON object.
	bool ParseSectionDefinition(const String& json, SectionDefinition& out);

	/// Writes a .hsecdef JSON document. Deterministic: keys in alphabetical order, rules and
	/// variants in definition order.
	String WriteSectionDefinition(const SectionDefinition& def);
}
