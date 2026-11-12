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

			/// True once nothing more can be drawn: callers then only count, without evaluating anything.
			bool Full() const
			{
				return !m_budgetLeft || m_geometry.lines.size() >= m_limit;
			}

			/// Counts lines that are not drawn.
			void Skip(const size_t count)
			{
				m_budgetLeft = false;
				m_geometry.linesTotal += count;
			}

		private:
			DebugGeometry& m_geometry;
			size_t m_limit;
			bool m_budgetLeft = true;
		};

		/// Height a rebuild writes at (x, z) on the given base.
		float ComposedHeight(const PreparedTerrain& prepared, const float x, const float z, const float baseHeight)
		{
			const TerrainTarget target = prepared.Evaluate(x, z, baseHeight, {}, nullptr, terrain_channel::Heights);
			if (target.heightWeight <= 0.0f)
			{
				return baseHeight;
			}
			return baseHeight + (target.height - baseHeight) * std::min(1.0f, target.heightWeight);
		}

		bool IsVertical(const Vector3& a, const Vector3& b)
		{
			return a.x == b.x && a.z == b.z;
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

		// A polyline on the composed ground (onGround) or at its own heights, lifted by TerrainDebugOutlineLift.
		const auto drawPolyline = [&sink, &groundPoint](const std::vector<Vector3>& points, const bool closed, const uint32 argb, const bool onGround)
		{
			const size_t count = points.size();
			if (count < 2)
			{
				return;
			}
			const size_t segments = count - 1 + (closed && count >= 3 ? 1 : 0);
			if (sink.Full())
			{
				// A vertical segment would read as a tick, so it is never drawn.
				size_t drawable = 0;
				for (size_t i = 0; i < segments; ++i)
				{
					drawable += IsVertical(points[i], points[(i + 1) % count]) ? 0 : 1;
				}
				sink.Skip(drawable);
				return;
			}
			std::vector<Vector3> lifted;
			lifted.reserve(count);
			for (const Vector3& p : points)
			{
				lifted.push_back(onGround ? groundPoint(p.x, p.z) : Vector3(p.x, p.y + TerrainDebugOutlineLift, p.z));
			}
			for (size_t i = 0; i < segments; ++i)
			{
				const Vector3& a = lifted[i];
				const Vector3& b = lifted[(i + 1) % count];
				if (!IsVertical(a, b))
				{
					sink.Add(DebugLine{ a, b, argb });
				}
			}
		};

		// Outlines of the ops on the composed ground; a river's surface line at the water surface.
		std::vector<TerrainOpOutline> outlines;
		prepared.GetOutlines(outlines);
		for (const TerrainOpOutline& outline : outlines)
		{
			if (outline.type == TerrainOpType::River)
			{
				drawPolyline(outline.points, false, TerrainDebugRiverSurfaceArgb, false);
				for (const std::vector<Vector3>& edge : outline.bedEdges)
				{
					drawPolyline(edge, false, TerrainDebugRiverBedArgb, true);
				}
			}
			else
			{
				drawPolyline(outline.points, outline.closed, argbRaise, true);
			}

			// Basin: one tick at the area's centroid, from the composed bed up to the water level.
			if (outline.depth > 0.0f)
			{
				if (sink.Full())
				{
					sink.Skip(1);
				}
				else
				{
					const Vector3 bed = groundPoint(outline.centroid.x, outline.centroid.z);
					sink.Add(DebugLine{ Vector3(bed.x, bed.y - TerrainDebugOutlineLift, bed.z), Vector3(bed.x, outline.level, bed.z), argbLower });
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
					if (sink.Full())
					{
						sink.Skip(static_cast<size_t>(pieces));
						continue;
					}

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
			const double columns = std::max(0.0, lastX - firstX + 1.0);
			for (double iz = firstZ; iz <= lastZ; iz += 1.0)
			{
				const float z = static_cast<float>(iz * step);
				for (double ix = firstX; ix <= lastX; ix += 1.0)
				{
					if (sink.Full())
					{
						// Every remaining grid sample counts as a tick (an upper bound, nothing is evaluated).
						const double remaining = (lastX - ix + 1.0) + (lastZ - iz) * columns;
						sink.Skip(static_cast<size_t>(remaining));
						geometry.linesDrawn = geometry.lines.size();
						return geometry;
					}

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
