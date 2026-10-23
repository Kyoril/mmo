// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <cmath>
#include <vector>

namespace mmo
{
	/// @brief The ocean surface mesh: a polar grid centred on the camera (VS_Ocean.hlsl places it).
	///
	/// Each vertex stores (cos theta, sin theta, radius). Rings are spaced geometrically with the same
	/// ratio as the angular step, so every cell is close to square and its size grows linearly with
	/// distance; seen from the camera the cells stay about equally large on screen out to the horizon.
	/// A single connected mesh needs no level stitching, so it cannot crack.
	struct OceanGrid
	{
		/// @brief Vertex positions, three floats each.
		std::vector<float> vertices;

		/// @brief Triangle list.
		std::vector<uint32> indices;

		/// @brief Cell size divided by the distance from the centre.
		float angularStep { 0.0f };
	};

	/// @brief Builds the ocean grid.
	/// @param segments Vertices per ring.
	/// @param innerRadius Radius of the first ring (the centre vertex sits inside it).
	/// @param outerRadius Radius of the last ring.
	inline OceanGrid BuildOceanGrid(const uint32 segments, const float innerRadius, const float outerRadius)
	{
		OceanGrid grid;
		constexpr float pi = 3.14159265358979f;
		grid.angularStep = 2.0f * pi / static_cast<float>(segments);
		const float growth = 1.0f + grid.angularStep;

		std::vector<float> radii;
		for (float radius = innerRadius; ; radius *= growth)
		{
			if (radius >= outerRadius)
			{
				radii.push_back(outerRadius);
				break;
			}
			radii.push_back(radius);
		}

		grid.vertices.reserve((1 + radii.size() * segments) * 3);
		grid.vertices.insert(grid.vertices.end(), { 1.0f, 0.0f, 0.0f });
		for (const float radius : radii)
		{
			for (uint32 s = 0; s < segments; ++s)
			{
				const float angle = grid.angularStep * static_cast<float>(s);
				grid.vertices.insert(grid.vertices.end(), { std::cos(angle), std::sin(angle), radius });
			}
		}

		const auto ringVertex = [segments](const size_t ring, const uint32 segment)
		{
			return static_cast<uint32>(1 + ring * segments + (segment % segments));
		};

		grid.indices.reserve(segments * 3 + (radii.size() - 1) * segments * 6);
		for (uint32 s = 0; s < segments; ++s)
		{
			grid.indices.insert(grid.indices.end(), { 0u, ringVertex(0, s + 1), ringVertex(0, s) });
		}

		for (size_t ring = 0; ring + 1 < radii.size(); ++ring)
		{
			for (uint32 s = 0; s < segments; ++s)
			{
				const uint32 a = ringVertex(ring, s);
				const uint32 b = ringVertex(ring, s + 1);
				const uint32 c = ringVertex(ring + 1, s);
				const uint32 d = ringVertex(ring + 1, s + 1);
				grid.indices.insert(grid.indices.end(), { a, b, d, a, d, c });
			}
		}

		return grid;
	}
}
