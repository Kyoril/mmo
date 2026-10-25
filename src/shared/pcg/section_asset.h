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

	/// One authored shape of a section.
	struct SectionVariant
	{
		/// World x the variant was captured around (informational).
		float originX = 0.0f;
		SectionGrid heights;
		SectionPaint paint;
		std::vector<SectionPlacement> placements;
	};

	/// A baked road/path section: terrain shape, paint and props for every variant.
	struct SectionAsset
	{
		String name;
		float length = 20.0f;
		float halfWidth = 4.0f;
		float sideFalloff = 4.0f;
		/// Albedo texture path per paint layer of the section's terrain material.
		std::array<String, 4> layerTextures;
		/// Sorted by asset.
		std::vector<PropRule> rules;
		std::vector<SectionVariant> variants;
		/// Hash of the section world's files at bake time (stale check).
		uint64 sourceStamp = 0;
	};

	/// Writes the asset as binary (magic PSEC, version 1). Deterministic.
	/// @return False if a grid or paint array does not match its dimensions.
	bool WriteSectionAsset(io::Writer& writer, const SectionAsset& asset);

	/// Reads an asset written by WriteSectionAsset.
	/// @return False on a bad magic, unsupported version or truncated/corrupt data; out is then unspecified.
	bool ReadSectionAsset(io::Reader& reader, SectionAsset& out);

	/// Bilinear height at a local position, clamped at the grid edges. 0 for an empty grid.
	float SampleHeight(const SectionGrid& grid, float localX, float localZ);

	/// Bilinear paint at a local position, clamped at the edges. Weights and coverage are in 0..255.
	void SamplePaint(const SectionPaint& paint, float localX, float localZ, std::array<float, 4>& weights, float& coverage);
}
