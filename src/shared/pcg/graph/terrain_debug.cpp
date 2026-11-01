// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "terrain_debug.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mmo::pcg::graph
{
	namespace
	{
		/// Adds lines while the budget lasts; always counts them in linesTotal.
		class LineSink final
		{
		public:
			LineSink(DebugGeometry& geometry, const size_t limit)
				: m_geometry(geometry)
				, m_limit(limit)
			{
			}

			/// Adds a line if it fits; once one does not fit, nothing more is added (a polyline is cut there).
			void Add(const DebugLine& line)
			{
				++m_geometry.linesTotal;
				if (!m_budgetLeft || m_geometry.lines.size() + 1 > m_limit)
				{
					m_budgetLeft = false;
					return;
				}
				m_geometry.lines.push_back(line);
			}

		private:
			DebugGeometry& m_geometry;
			size_t m_limit;
			bool m_budgetLeft = true;
		};

		/// Height a rebuild writes at (x, z) on the given base.
		float ComposedHeight(const PreparedTerrain& prepared, const float x, const float z, const float baseHeight)
		{
			const TerrainTarget target = prepared.Evaluate(x, z, baseHeight, {});
			if (target.heightWeight <= 0.0f)
			{
				return baseHeight;
			}
			return baseHeight + (target.height - baseHeight) * std::min(1.0f, target.heightWeight);
		}
	}

	DebugGeometry BuildTerrainDebugGeometry(const PreparedTerrain& prepared, const std::function<float(float, float)>& base,
		const float step, const uint32 argbRaise, const uint32 argbLower, const size_t lineLimit)
	{
		DebugGeometry geometry;
		if (prepared.IsEmpty())
		{
			return geometry;
		}

		const auto baseAt = [&base](const float x, const float z)
		{
			return base ? base(x, z) : 0.0f;
		};
		const auto groundPoint = [&prepared, &baseAt](const float x, const float z)
		{
			return Vector3(x, ComposedHeight(prepared, x, z, baseAt(x, z)) + TerrainDebugOutlineLift, z);
		};

		LineSink sink(geometry, lineLimit);

		// Outlines of the ops on the composed ground.
		std::vector<TerrainOpOutline> outlines;
		prepared.GetOutlines(outlines);
		for (const TerrainOpOutline& outline : outlines)
		{
			const size_t count = outline.points.size();
			if (count >= 2)
			{
				std::vector<Vector3> ground;
				ground.reserve(count);
				for (const Vector3& p : outline.points)
				{
					ground.push_back(groundPoint(p.x, p.z));
				}
				const size_t segments = count - 1 + (outline.closed && count >= 3 ? 1 : 0);
				for (size_t i = 0; i < segments; ++i)
				{
					const Vector3& a = ground[i];
					const Vector3& b = ground[(i + 1) % count];
					// A vertical segment would read as a tick.
					if (a.x != b.x || a.z != b.z)
					{
						sink.Add(DebugLine{ a, b, argbRaise });
					}
				}
			}

			// Section copy rects, edges split so they follow the ground.
			for (const WorldRect& rect : outline.copyRects)
			{
				const float corners[5][2] = {
					{ rect.minX, rect.minZ }, { rect.maxX, rect.minZ }, { rect.maxX, rect.maxZ }, { rect.minX, rect.maxZ }, { rect.minX, rect.minZ }
				};
				for (int edge = 0; edge < 4; ++edge)
				{
					const float x0 = corners[edge][0];
					const float z0 = corners[edge][1];
					const float x1 = corners[edge + 1][0];
					const float z1 = corners[edge + 1][1];
					const float length = std::abs(x1 - x0) + std::abs(z1 - z0);
					if (!(length > 0.0f) || !std::isfinite(length))
					{
						continue;
					}
					const int pieces = step > 0.0f ? static_cast<int>(std::min(4096.0f, std::max(1.0f, std::ceil(length / step)))) : 1;
					std::vector<Vector3> ground;
					ground.reserve(static_cast<size_t>(pieces) + 1);
					for (int i = 0; i <= pieces; ++i)
					{
						// The ends are exactly the corners and a side keeps its coordinate, so the edge stays on the rect.
						const float t = static_cast<float>(i) / static_cast<float>(pieces);
						const float x = i == pieces ? x1 : (x0 == x1 ? x0 : x0 + (x1 - x0) * t);
						const float z = i == pieces ? z1 : (z0 == z1 ? z0 : z0 + (z1 - z0) * t);
						ground.push_back(groundPoint(x, z));
					}
					for (int i = 0; i < pieces; ++i)
					{
						sink.Add(DebugLine{ ground[i], ground[i + 1], argbRaise });
					}
				}
			}
		}

		// Height ticks on the world-anchored grid inside the influence.
		const WorldRect influence = prepared.GetInfluence();
		if (step > 0.0f && std::isfinite(step) && influence.maxX >= influence.minX && influence.maxZ >= influence.minZ)
		{
			const double firstX = std::ceil(static_cast<double>(influence.minX) / step);
			const double lastX = std::floor(static_cast<double>(influence.maxX) / step);
			const double firstZ = std::ceil(static_cast<double>(influence.minZ) / step);
			const double lastZ = std::floor(static_cast<double>(influence.maxZ) / step);
			for (double iz = firstZ; iz <= lastZ; iz += 1.0)
			{
				const float z = static_cast<float>(iz * step);
				for (double ix = firstX; ix <= lastX; ix += 1.0)
				{
					const float x = static_cast<float>(ix * step);
					const float baseHeight = baseAt(x, z);
					const float composed = ComposedHeight(prepared, x, z, baseHeight);
					if (composed == baseHeight || !std::isfinite(composed))
					{
						continue;
					}
					sink.Add(DebugLine{ Vector3(x, baseHeight, z), Vector3(x, composed, z), composed > baseHeight ? argbRaise : argbLower });
				}
			}
		}

		geometry.linesDrawn = geometry.lines.size();
		return geometry;
	}
}
