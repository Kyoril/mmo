// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "attribute_value.h"

namespace mmo::pcg::graph
{
	class Hasher;

	/// @brief What a Terrain item asks the terrain stage to do.
	enum class TerrainOpType : uint8
	{
		SectionPath,
		Noise,
		Raise,
		Carve,
		Paint,
		WaterArea,
		River
	};

	/// @brief Small descriptor of one terrain operation; the geometry lives in the owning item.
	struct TerrainOp
	{
		/// @brief The operation.
		TerrainOpType type = TerrainOpType::Noise;

		/// @brief SectionPath: .hsec path; WaterArea, River: water material (.hmat), may be empty.
		String asset;

		/// @brief Op parameters by name (section params use SetSectionPathParam names).
		AttributeSet params;

		/// @brief Producing node id (set by the node), composition order.
		uint32 order = 0;

		/// @brief Composition order across nodes (lower is stacked first); ties fall back to `order` (node id), then the
		///        item index. Never part of a placement key.
		int32 stackOrder = 0;
	};

	/// @brief Params of a SectionPath op made by Stamp Section at Points with length 0 (= the section asset's length).
	///        The terrain stage reads them, does not pass them on to the section parameters, and lays the op's two
	///        spline points out again for the asset's length: StampLengthParam (Float, 0) marks such an op, and
	///        StampAnchorParam (String, "center" or "start") says whether the first-to-last chord is centred on or
	///        starts at the stamp point. The spline then only carries the stamp direction and point.
	inline constexpr const char* StampLengthParam = "stampLength";
	inline constexpr const char* StampAnchorParam = "stampAnchor";

	/// @brief Feeds every field of an op into a hasher.
	void HashTerrainOp(Hasher& h, const TerrainOp& op);

	/// @brief Converts a Float, Int, Bool or String value to the text pcg::SetSectionPathParam parses.
	/// @return False for other types (out unchanged).
	bool SectionParamText(const AttributeValue& value, String& out);

	/// @brief Stable display name of an op type.
	const char* TerrainOpTypeName(TerrainOpType type);
}
