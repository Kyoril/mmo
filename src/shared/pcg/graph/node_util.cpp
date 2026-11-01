// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "node_util.h"

#include <algorithm>
#include <cmath>

namespace mmo::pcg::graph
{
	float SettingFloat(const AttributeSet& settings, const String& name, const float fallback)
	{
		const auto it = settings.find(name);
		if (it == settings.end())
		{
			return fallback;
		}

		float value = fallback;
		if (const float* f = std::get_if<float>(&it->second))
		{
			value = *f;
		}
		else if (const int64* i = std::get_if<int64>(&it->second))
		{
			value = static_cast<float>(*i);
		}
		return std::isfinite(value) ? value : fallback;
	}

	bool SettingBool(const AttributeSet& settings, const String& name, const bool fallback)
	{
		const auto it = settings.find(name);
		if (it == settings.end())
		{
			return fallback;
		}
		const bool* b = std::get_if<bool>(&it->second);
		return b != nullptr ? *b : fallback;
	}

	String SettingString(const AttributeSet& settings, const String& name, const String& fallback)
	{
		const auto it = settings.find(name);
		if (it == settings.end())
		{
			return fallback;
		}
		const String* s = std::get_if<String>(&it->second);
		return s != nullptr ? *s : fallback;
	}

	std::vector<String> SplitList(const String& list)
	{
		std::vector<String> result;
		size_t start = 0;
		while (start <= list.size())
		{
			size_t end = list.find(',', start);
			if (end == String::npos)
			{
				end = list.size();
			}

			size_t a = start;
			size_t b = end;
			while (a < b && (list[a] == ' ' || list[a] == '\t'))
			{
				++a;
			}
			while (b > a && (list[b - 1] == ' ' || list[b - 1] == '\t'))
			{
				--b;
			}
			if (b > a)
			{
				result.push_back(list.substr(a, b - a));
			}
			start = end + 1;
		}
		return result;
	}

	void Bounds2D::Add(const float x, const float z)
	{
		if (!valid)
		{
			minX = maxX = x;
			minZ = maxZ = z;
			valid = true;
			return;
		}
		minX = std::min(minX, x);
		maxX = std::max(maxX, x);
		minZ = std::min(minZ, z);
		maxZ = std::max(maxZ, z);
	}

	bool Bounds2D::Intersects(const Bounds2D& other) const
	{
		return valid && other.valid && minX <= other.maxX && other.minX <= maxX && minZ <= other.maxZ && other.minZ <= maxZ;
	}

	Bounds2D SplineBounds(const PathSpline& spline)
	{
		Bounds2D bounds;
		const bool curved = spline.interpolation == SplineInterpolation::CatmullRom;
		for (size_t i = 0; i < spline.points.size(); ++i)
		{
			const SplinePoint& p = spline.points[i];
			bounds.Add(p.x, p.z);
			if (curved)
			{
				float inX = 0, inZ = 0, outX = 0, outZ = 0;
				GetEffectiveTangents(spline, i, inX, inZ, outX, outZ);
				bounds.Add(p.x + inX, p.z + inZ);
				bounds.Add(p.x + outX, p.z + outZ);
			}
		}
		return bounds;
	}

	bool SaneBounds(const Bounds2D& bounds)
	{
		if (!bounds.valid)
		{
			return false;
		}
		for (const float v : { bounds.minX, bounds.minZ, bounds.maxX, bounds.maxZ })
		{
			if (!std::isfinite(v) || std::fabs(v) > MaxWorldCoordinate)
			{
				return false;
			}
		}
		return true;
	}

	float EvaluationStep(const PathSpline& spline, const float preferred)
	{
		float step = std::isfinite(preferred) ? std::max(preferred, SamplingStep) : SamplingStep;
		double chords = 0.0;
		const size_t n = spline.points.size();
		for (size_t i = 0; i + 1 < n || (spline.closed && n >= 3 && i < n); ++i)
		{
			const SplinePoint& a = spline.points[i];
			const SplinePoint& b = spline.points[(i + 1) % n];
			chords += std::hypot(static_cast<double>(b.x) - a.x, static_cast<double>(b.z) - a.z);
		}
		// Curved segments are longer than their chords; the factor 2 leaves room for that.
		const double coarse = chords * 2.0 / 100000.0;
		// Doubling (instead of using `coarse` directly) keeps the step, and so the polyline of unchanged segments,
		// stable while a vertex moves; it only changes when the spline's size crosses a power of two.
		while (std::isfinite(coarse) && coarse > step && step < MaxWorldCoordinate)
		{
			step *= 2.0f;
		}
		return step;
	}

	namespace
	{
		int64 RowOf(const float z, const float rowHeight)
		{
			return static_cast<int64>(std::floor(static_cast<double>(z) / static_cast<double>(rowHeight)));
		}
	}

	RowIndexedPolygon::RowIndexedPolygon(const std::vector<SplineSample>& polygon, const float rowHeight)
		: m_rowHeight(rowHeight > 0.0f ? rowHeight : 1.0f)
	{
		if (polygon.size() < 3)
		{
			return;
		}
		m_polygon = polygon;

		int64 lo = RowOf(polygon[0].z, m_rowHeight);
		int64 hi = lo;
		for (const SplineSample& s : polygon)
		{
			const int64 row = RowOf(s.z, m_rowHeight);
			lo = std::min(lo, row);
			hi = std::max(hi, row);
		}
		m_firstRow = lo;
		m_rows.resize(static_cast<size_t>(hi - lo + 1));

		const size_t n = m_polygon.size();
		for (size_t i = 0; i < n; ++i)
		{
			const SplineSample& a = m_polygon[i];
			const SplineSample& b = m_polygon[(i + 1) % n];
			const int64 r0 = RowOf(std::min(a.z, b.z), m_rowHeight);
			const int64 r1 = RowOf(std::max(a.z, b.z), m_rowHeight);
			for (int64 row = r0; row <= r1; ++row)
			{
				m_rows[static_cast<size_t>(row - m_firstRow)].push_back(static_cast<uint32>(i));
			}
		}
	}

	bool RowIndexedPolygon::Contains(const float x, const float z) const
	{
		if (m_rows.empty())
		{
			return false;
		}
		const int64 row = RowOf(z, m_rowHeight);
		if (row < m_firstRow || row >= m_firstRow + static_cast<int64>(m_rows.size()))
		{
			return false;
		}

		// A segment crossing the line at z spans z, so it is in z's row bucket.
		bool inside = false;
		const size_t n = m_polygon.size();
		for (const uint32 i : m_rows[static_cast<size_t>(row - m_firstRow)])
		{
			const SplineSample& a = m_polygon[i];
			const SplineSample& b = m_polygon[(i + 1) % n];
			if ((a.z > z) != (b.z > z))
			{
				const float crossX = a.x + (z - a.z) / (b.z - a.z) * (b.x - a.x);
				if (x < crossX)
				{
					inside = !inside;
				}
			}
		}
		return inside;
	}
}
