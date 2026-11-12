// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "terrain_op.h"
#include "hashing.h"

#include <cstdio>

namespace mmo::pcg::graph
{
	void HashTerrainOp(Hasher& h, const TerrainOp& op)
	{
		h.U64(static_cast<uint64>(op.type));
		h.Str(op.asset);
		h.Attributes(op.params);
		h.U64(op.order);
		if (op.stackOrder != 0)
		{
			// Only when set, so ops of graphs that never use it keep their hash.
			h.U64(static_cast<uint64>(static_cast<int64>(op.stackOrder)));
		}
	}

	bool SectionParamText(const AttributeValue& value, String& out)
	{
		char buffer[64];
		switch (TypeOf(value))
		{
		case AttributeType::Float:
			std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(std::get<float>(value)));
			out = buffer;
			return true;
		case AttributeType::Int:
			std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(std::get<int64>(value)));
			out = buffer;
			return true;
		case AttributeType::Bool:
			out = std::get<bool>(value) ? "1" : "0";
			return true;
		case AttributeType::String:
			out = std::get<String>(value);
			return true;
		default:
			return false;
		}
	}

	const char* TerrainOpTypeName(const TerrainOpType type)
	{
		switch (type)
		{
		case TerrainOpType::SectionPath: return "SectionPath";
		case TerrainOpType::Noise: return "Noise";
		case TerrainOpType::Raise: return "Raise";
		case TerrainOpType::Carve: return "Carve";
		case TerrainOpType::Paint: return "Paint";
		case TerrainOpType::WaterArea: return "WaterArea";
		case TerrainOpType::River: return "River";
		}
		return "Noise";
	}
}
