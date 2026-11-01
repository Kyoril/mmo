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

	void PolygonCrossingsAtZ(const std::vector<SplineSample>& polygon, const float z, std::vector<float>& crossings)
	{
		crossings.clear();
		const size_t n = polygon.size();
		if (n < 3)
		{
			return;
		}
		for (size_t i = 0; i < n; ++i)
		{
			const SplineSample& a = polygon[i];
			const SplineSample& b = polygon[(i + 1) % n];
			// Same predicate and formula as SampledPath::Contains.
			if ((a.z > z) != (b.z > z))
			{
				crossings.push_back(a.x + (z - a.z) / (b.z - a.z) * (b.x - a.x));
			}
		}
		std::sort(crossings.begin(), crossings.end());
	}

	bool InsideCrossings(const std::vector<float>& crossings, const float x)
	{
		// SampledPath::Contains toggles for every crossing with x < crossX.
		const auto it = std::upper_bound(crossings.begin(), crossings.end(), x);
		return (static_cast<size_t>(crossings.end() - it) & 1u) != 0;
	}
}
