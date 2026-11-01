// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "debug_geometry.h"

#include "node_util.h"

#include "math/path_spline.h"

#include <algorithm>
#include <cmath>

namespace mmo::pcg::graph
{
	namespace
	{
		/// Scales the rgb channels of a colour by brightness(density), keeping alpha.
		uint32 ShadeByDensity(const uint32 argb, const float density)
		{
			// NaN and negative densities count as 0.
			const float clamped = density >= 0.0f ? std::min(density, 1.0f) : 0.0f;
			const float factor = 0.3f + 0.7f * clamped;
			const auto channel = [argb, factor](const int shift)
			{
				const float value = static_cast<float>((argb >> shift) & 0xFFu) * factor + 0.5f;
				return std::min(static_cast<uint32>(value), 0xFFu) << shift;
			};
			return (argb & 0xFF000000u) | channel(16) | channel(8) | channel(0);
		}

		void AddBox(const Point& point, const uint32 argb, std::vector<DebugLine>& lines)
		{
			// Corner i takes boundsMax on axis x if bit 0 is set, y for bit 1, z for bit 2.
			const auto corner = [&point](const int i)
			{
				const Vector3 local(
					((i & 1) != 0 ? point.boundsMax.x : point.boundsMin.x) * point.scale.x,
					((i & 2) != 0 ? point.boundsMax.y : point.boundsMin.y) * point.scale.y,
					((i & 4) != 0 ? point.boundsMax.z : point.boundsMin.z) * point.scale.z);
				return point.position + point.rotation * local;
			};
			const Vector3 corners[8] = { corner(0), corner(1), corner(2), corner(3), corner(4), corner(5), corner(6), corner(7) };

			// The 12 edges join corners that differ in exactly one bit.
			for (int i = 0; i < 8; ++i)
			{
				for (int bit = 1; bit < 8; bit <<= 1)
				{
					if ((i & bit) == 0)
					{
						lines.push_back(DebugLine { corners[i], corners[i | bit], argb });
					}
				}
			}
		}

		bool FiniteSpline(const PathSpline& spline)
		{
			return std::all_of(spline.points.begin(), spline.points.end(), [](const SplinePoint& p)
			{
				return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
			});
		}

		/// Adds a spline's polyline while the line budget lasts; always counts its segments in linesTotal.
		/// @return False once the budget ran out.
		bool AddPolyline(const PathSpline& spline, const uint32 argb, const size_t lineLimit, bool budgetLeft, DebugGeometry& geometry)
		{
			if (spline.points.size() < 2 || !FiniteSpline(spline))
			{
				return budgetLeft;
			}

			const SampledPath path(spline, EvaluationStep(spline, SamplingStep));
			const std::vector<SplineSample>& samples = path.GetSamples();
			if (samples.size() < 2)
			{
				return budgetLeft;
			}

			const size_t segments = samples.size() - 1 + (path.IsClosed() ? 1 : 0);
			geometry.linesTotal += segments;
			if (!budgetLeft)
			{
				return false;
			}

			const auto toVector = [](const SplineSample& s)
			{
				return Vector3(s.x, s.y, s.z);
			};
			// Segment i joins sample i and sample i + 1 (wrapping to 0 for the closing segment).
			for (size_t i = 0; i < segments; ++i)
			{
				if (geometry.lines.size() >= lineLimit)
				{
					return false;
				}
				geometry.lines.push_back(DebugLine { toVector(samples[i]), toVector(samples[(i + 1) % samples.size()]), argb });
			}
			return true;
		}
	}

	DebugGeometry BuildDebugGeometry(const Collection& items, const uint32 argb, const size_t boxLimit, const size_t pointLimit, const size_t lineLimit)
	{
		DebugGeometry geometry;
		for (const DataItem& item : items)
		{
			if (item.kind == ItemKind::Points)
			{
				geometry.pointsTotal += item.points.points.size();
			}
		}

		// Every stride-th point (in collection order) is drawn.
		const size_t stride = pointLimit == 0 ? 0 : std::max<size_t>(1, (geometry.pointsTotal + pointLimit - 1) / pointLimit);

		// Once something does not fit the line budget, nothing more is added (totals keep counting).
		bool budgetLeft = true;
		size_t index = 0;
		size_t sampled = 0;
		for (const DataItem& item : items)
		{
			if (item.kind == ItemKind::Points)
			{
				for (const Point& point : item.points.points)
				{
					const size_t current = index++;
					if (stride == 0 || current % stride != 0)
					{
						continue;
					}

					const bool box = sampled++ < boxLimit;
					const size_t needed = box ? 12 : 1;
					geometry.linesTotal += needed;
					if (!budgetLeft || geometry.lines.size() + needed > lineLimit)
					{
						budgetLeft = false;
						continue;
					}

					const uint32 color = ShadeByDensity(argb, point.density);
					if (box)
					{
						AddBox(point, color, geometry.lines);
						++geometry.boxesDrawn;
					}
					else
					{
						geometry.lines.push_back(DebugLine { point.position, point.position + Vector3(0.0f, DebugTickHeight, 0.0f), color });
					}
					++geometry.pointsDrawn;
				}
			}
			else if (item.kind == ItemKind::Spline || item.kind == ItemKind::Area)
			{
				budgetLeft = AddPolyline(item.spline, argb, lineLimit, budgetLeft, geometry);
			}
		}
		geometry.linesDrawn = geometry.lines.size();
		return geometry;
	}
}
