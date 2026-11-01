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
		WaterArea
	};

	/// @brief Small descriptor of one terrain operation; the geometry lives in the owning item.
	struct TerrainOp
	{
		/// @brief The operation.
		TerrainOpType type = TerrainOpType::Noise;

		/// @brief SectionPath: .hsec path; WaterArea: water material (.hmat), may be empty.
		String asset;

		/// @brief Op parameters by name (section params use SetSectionPathParam names).
		AttributeSet params;

		/// @brief Producing node id (set by the node), composition order.
		uint32 order = 0;
	};

	/// @brief Feeds every field of an op into a hasher.
	void HashTerrainOp(Hasher& h, const TerrainOp& op);

	/// @brief Converts a Float, Int, Bool or String value to the text pcg::SetSectionPathParam parses.
	/// @return False for other types (out unchanged).
	bool SectionParamText(const AttributeValue& value, String& out);

	/// @brief Stable display name of an op type.
	const char* TerrainOpTypeName(TerrainOpType type);
}
