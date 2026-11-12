// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "pcg/graph/evaluator.h"

#include <cmath>
#include <map>

namespace
{
	/// Analytic terrain for graph node tests: a plane h = base + slopeX * x + slopeZ * z inside
	/// [minX,maxX] x [minZ,maxZ] (no terrain outside), constant splat weights per texture key and an
	/// optional constant water level. Bump `revision` after changing anything.
	class FakeTerrainSampler final : public mmo::pcg::graph::ITerrainSampler
	{
	public:
		bool Height(const float x, const float z, float& h) const override
		{
			if (x < minX || x > maxX || z < minZ || z > maxZ)
			{
				return false;
			}
			h = base + slopeX * x + slopeZ * z;
			return true;
		}

		mmo::Vector3 Normal(float, float) const override
		{
			mmo::Vector3 n(-slopeX, 1.0f, -slopeZ);
			n.Normalize();
			return n;
		}

		float SplatWeight(const mmo::String& textureKey, float, float) const override
		{
			const auto it = splat.find(textureKey);
			return it != splat.end() ? it->second : 0.0f;
		}

		bool WaterLevel(float, float, float& level) const override
		{
			if (!hasWater)
			{
				return false;
			}
			level = waterLevel;
			return true;
		}

		mmo::uint64 Revision() const override
		{
			return revision;
		}

		float base = 10.0f;
		float slopeX = 0.0f;
		float slopeZ = 0.0f;
		float minX = -1.0e6f;
		float maxX = 1.0e6f;
		float minZ = -1.0e6f;
		float maxZ = 1.0e6f;
		std::map<mmo::String, float> splat;
		bool hasWater = false;
		float waterLevel = 0.0f;
		mmo::uint64 revision = 1;
	};
}
