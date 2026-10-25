// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "placement.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

#include <array>
#include <vector>

namespace mmo::pcg
{
	/// How a prop of the section world is re-rolled when a section is stamped.
	struct PropRule
	{
		/// Asset path the rule applies to.
		String asset;
		/// Probability (0..1) that a placement of this asset is kept.
		float keepChance = 1.0f;
		/// Maximum random offset along x and z in metres.
		float positionJitter = 0.0f;
		/// Maximum random yaw change in degrees.
		float yawJitterDeg = 0.0f;
		/// Maximum random relative scale change.
		float scaleJitter = 0.0f;
		/// Assets the placement may be swapped with (empty = no swap).
		std::vector<String> swapGroup;
	};

	/// One captured placement. Position x/z are local to the variant origin.
	struct SectionPlacement
	{
		/// The placement; position.x/z are relative to the variant origin, y is the world height.
		Placement placement;
		/// Height of the placement above the terrain surface at its position.
		float heightAboveTerrain = 0.0f;
	};

	/// One piece of a connected prop run (a fence board, a wall segment).
	struct SectionRunPiece
	{
		/// The piece as captured: asset, kind, material overrides, kit paths, scale, collision and the section object's
		/// unique id as key. position.x/z are local to the variant origin (informational); rotation is the authored
		/// rotation relative to the run direction (+Z).
		Placement placement;
		/// Height of the piece above the terrain surface at its pivot.
		float heightAboveTerrain = 0.0f;
		/// Length of the piece's footprint along the run in metres (the rotated and scaled bounds projected onto +Z).
		float length = 0.0f;
		/// Signed offset of the footprint start from the pivot along the run (negative when the pivot is inside).
		float startOffset = 0.0f;
	};

	/// Pieces of a section variant laid end to end along the bent path when stamped, kept or dropped as a whole.
	struct SectionRun
	{
		/// Name of the run; continuous runs of the same name in consecutive copies are joined into one chain.
		String name;
		/// Probability (0..1) that the whole run (chain) is kept.
		float keepChance = 1.0f;
		/// Lateral offset of the run line (mean local x of the pieces).
		float x = 0.0f;
		/// Local z where the pieces' footprints start.
		float startZ = 0.0f;
		/// Local z where the pieces' footprints end.
		float endZ = 0.0f;
		/// The pattern, sorted by local z (ties by key).
		std::vector<SectionRunPiece> pieces;
	};

	/// How close (m) a run must come to both section ends to count as continuous.
	constexpr float kRunContinuityTolerance = 0.5f;

	/// True if the run covers the whole section: startZ <= kRunContinuityTolerance and
	/// endZ >= sectionLength - kRunContinuityTolerance.
	bool IsContinuousRun(const SectionRun& run, float sectionLength);

	/// Regular height grid: sample (i, j) lies at local ((originX + i) * spacing, (originZ + j) * spacing).
	struct SectionGrid
	{
		float spacing = 0.0f;
		int32 originX = 0;
		int32 originZ = 0;
		uint32 width = 0;
		uint32 depth = 0;
		/// Heights relative to 0, row-major (z, then x).
		std::vector<float> values;
	};

	/// Regular paint grid, same indexing as SectionGrid.
	struct SectionPaint
	{
		float spacing = 0.0f;
		int32 originX = 0;
		int32 originZ = 0;
		uint32 width = 0;
		uint32 depth = 0;
		/// Four 8-bit layer weights per sample, layer 0 in the low byte.
		std::vector<uint32> packed;
		/// 0..255, 0 = untouched default paint.
		std::vector<uint8> coverage;
	};

	/// What a section variant is used for when a section is laid along a path.
	enum class SectionVariantRole : uint8
	{
		/// An ordinary piece laid along straight and gently bent stretches.
		Straight,
		/// A corner piece for a turn towards the floor side; its middle (local z = length / 2) is the corner point.
		InnerCorner,
		/// A corner piece for a turn away from the floor side; its middle (local z = length / 2) is the corner point.
		OuterCorner
	};

	/// One authored shape of a section.
	struct SectionVariant
	{
		/// World x the variant was captured around (informational).
		float originX = 0.0f;
		/// What the variant is used for (version 1 and 2 assets read as Straight).
		SectionVariantRole role = SectionVariantRole::Straight;
		SectionGrid heights;
		SectionPaint paint;
		/// Placements stamped one by one (run members are not in here).
		std::vector<SectionPlacement> placements;
		/// Connected prop runs, in definition order.
		std::vector<SectionRun> runs;
	};

	/// A baked road/path section: terrain shape, paint and props for every variant.
	struct SectionAsset
	{
		String name;
		float length = 20.0f;
		float halfWidth = 4.0f;
		float sideFalloff = 4.0f;
		/// Turn angle (degrees) above which a corner piece is used instead of bending (version 1 and 2 assets read as the default).
		float cornerAngleDeg = 35.0f;
		/// Albedo texture path per paint layer of the section's terrain material.
		std::array<String, 4> layerTextures;
		/// Sorted by asset.
		std::vector<PropRule> rules;
		std::vector<SectionVariant> variants;
		/// Hash of the section world's files at bake time (stale check).
		uint64 sourceStamp = 0;
	};

	/// Writes the asset as binary (magic PSEC, version 3 = version 2 plus a trailer with the corner angle and the
	/// role of every variant; version 2 = version 1 plus the runs of every variant). Deterministic.
	/// @return False if a grid or paint array does not match its dimensions.
	bool WriteSectionAsset(io::Writer& writer, const SectionAsset& asset);

	/// Reads an asset written by WriteSectionAsset. Version 1 assets (from before runs) read with no runs,
	/// versions 1 and 2 read with all roles Straight and the default corner angle. A version 3 corner angle is clamped
	/// to 0..180; a non-finite one counts as corrupt data.
	/// @return False on a bad magic, unsupported version or truncated/corrupt data; out is then unspecified.
	bool ReadSectionAsset(io::Reader& reader, SectionAsset& out);

	/// Bilinear height at a local position, clamped at the grid edges. 0 for an empty grid.
	float SampleHeight(const SectionGrid& grid, float localX, float localZ);

	/// Bilinear paint at a local position, clamped at the edges. Weights and coverage are in 0..255.
	void SamplePaint(const SectionPaint& paint, float localX, float localZ, std::array<float, 4>& weights, float& coverage);

	/// Texture key in the form layer textures are compared in: lower case, '\' replaced by '/'.
	String NormalizeTextureKey(const String& key);

	/// True if two texture keys name the same texture (compared via NormalizeTextureKey).
	bool TextureKeysMatch(const String& a, const String& b);
}
