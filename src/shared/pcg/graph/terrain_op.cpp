// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "terrain_op.h"
#include "hashing.h"

namespace mmo::pcg::graph
{
	void HashTerrainOp(Hasher& h, const TerrainOp& op)
	{
		h.U64(static_cast<uint64>(op.type));
		h.Str(op.asset);
		h.Attributes(op.params);
		h.U64(op.order);
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
		}
		return "Noise";
	}
}
