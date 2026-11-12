// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "path_spline.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mmo
{
	namespace
	{
		float catmullRom(float p0, float p1, float p2, float p3, float t)
		{
			const float t2 = t * t;
			const float t3 = t2 * t;
			return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
		}

		/// Cubic Bezier from p0 with control offsets c1, c2 and end offset e3, all relative to p0 (keeps
		/// precision at large world coordinates).
		float bezier(float p0, float c1, float c2, float e3, float t)
		{
			const float u = 1.0f - t;
			return p0 + 3.0f * u * u * t * c1 + 3.0f * u * t * t * c2 + t * t * t * e3;
		}

		float pointSegmentDistance(float px, float pz, float ax, float az, float bx, float bz)
		{
			const float dx = bx - ax;
			const float dz = bz - az;
			const float lenSq = dx * dx + dz * dz;
			float t = 0.0f;
			if (lenSq > 0.0f)
			{
				t = std::clamp(((px - ax) * dx + (pz - az) * dz) / lenSq, 0.0f, 1.0f);
			}
			const float cx = ax + dx * t - px;
			const float cz = az + dz * t - pz;
			return std::sqrt(cx * cx + cz * cz);
		}

		float cross(float ax, float az, float bx, float bz, float cx, float cz)
		{
			return (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
		}

		/// Proper (strict) crossing of segments ab and cd.
		bool segmentsCross(const SplineSample& a, const SplineSample& b, const SplineSample& c, const SplineSample& d)
		{
			const float d1 = cross(a.x, a.z, b.x, b.z, c.x, c.z);
			const float d2 = cross(a.x, a.z, b.x, b.z, d.x, d.z);
			const float d3 = cross(c.x, c.z, d.x, d.z, a.x, a.z);
			const float d4 = cross(c.x, c.z, d.x, d.z, b.x, b.z);
			return ((d1 > 0.0f && d2 < 0.0f) || (d1 < 0.0f && d2 > 0.0f)) &&
				((d3 > 0.0f && d4 < 0.0f) || (d3 < 0.0f && d4 > 0.0f));
		}
	}

	SplineTangents GetEffectiveTangents(const PathSpline& spline, const size_t index)
	{
		SplineTangents t;
		const std::vector<SplinePoint>& pts = spline.points;
		if (index >= pts.size())
		{
			return t;
		}

		const SplinePoint& point = pts[index];
		if (!point.autoTangents)
		{
			t.inX = point.inX;
			t.inY = point.inY;
			t.inZ = point.inZ;
			t.outX = point.outX;
			t.outY = point.outY;
			t.outZ = point.outZ;
			return t;
		}

		// Same neighbour rule as the sampling: a closed path (three or more points) wraps, an open one
		// repeats its end points.
		const size_t n = pts.size();
		const bool closed = spline.closed && n >= 3;
		size_t previous = index;
		size_t next = index;
		if (closed)
		{
			previous = (index + n - 1) % n;
			next = (index + 1) % n;
		}
		else
		{
			previous = index > 0 ? index - 1 : 0;
			next = std::min(index + 1, n - 1);
		}

		t.outX = (pts[next].x - pts[previous].x) / 6.0f;
		t.outY = (pts[next].y - pts[previous].y) / 6.0f;
		t.outZ = (pts[next].z - pts[previous].z) / 6.0f;
		t.inX = -t.outX;
		t.inY = -t.outY;
		t.inZ = -t.outZ;
		return t;
	}

	void GetEffectiveTangents(const PathSpline& spline, const size_t index, float& inX, float& inZ, float& outX, float& outZ)
	{
		const SplineTangents t = GetEffectiveTangents(spline, index);
		inX = t.inX;
		inZ = t.inZ;
		outX = t.outX;
		outZ = t.outZ;
	}

	void MakeTangentsManual(PathSpline& spline, const size_t index)
	{
		if (index >= spline.points.size())
		{
			return;
		}

		const SplineTangents t = GetEffectiveTangents(spline, index);
		SplinePoint& point = spline.points[index];
		point.inX = t.inX;
		point.inY = t.inY;
		point.inZ = t.inZ;
		point.outX = t.outX;
		point.outY = t.outY;
		point.outZ = t.outZ;
		point.autoTangents = false;
	}

	SampledPath::SampledPath(const PathSpline& spline, float maxStep)
		: m_closed(spline.closed)
	{
		const std::vector<SplinePoint>& pts = spline.points;
		const int n = static_cast<int>(pts.size());
		if (n == 0)
		{
			m_closed = false;
			return;
		}

		if (maxStep <= 0.0f)
		{
			maxStep = 1.0f;
		}

		// A closed path needs at least three points to enclose anything.
		if (n < 3)
		{
			m_closed = false;
		}

		const int segmentCount = m_closed ? n : n - 1;
		auto fetch = [&](int i) -> const SplinePoint&
		{
			if (m_closed)
			{
				return pts[((i % n) + n) % n];
			}
			return pts[std::clamp(i, 0, n - 1)];
		};

		const bool curved = spline.interpolation == SplineInterpolation::CatmullRom;
		for (int s = 0; s < segmentCount; ++s)
		{
			const SplinePoint& p0 = fetch(s - 1);
			const SplinePoint& p1 = fetch(s);
			const SplinePoint& p2 = fetch(s + 1);
			const SplinePoint& p3 = fetch(s + 2);

			// Between two automatic points the Bezier below is exactly the uniform Catmull-Rom curve;
			// evaluating it in the Catmull-Rom form keeps the samples bit-identical to splines
			// authored before tangents existed, so rebuilding old content changes nothing.
			const bool catmullRomSegment = curved && p1.autoTangents && p2.autoTangents;

			// Bezier controls: P1 = p1 + out(p1), P2 = p2 + in(p2); relative to p1. Each component is
			// evaluated on its own, so the height never influences the planar curve.
			float c1x = 0.0f, c1y = 0.0f, c1z = 0.0f, c2x = 0.0f, c2y = 0.0f, c2z = 0.0f;
			const float ex = p2.x - p1.x;
			const float ey = p2.y - p1.y;
			const float ez = p2.z - p1.z;
			if (curved)
			{
				const SplineTangents leaving = GetEffectiveTangents(spline, static_cast<size_t>(s));
				const SplineTangents arriving = GetEffectiveTangents(spline, static_cast<size_t>((s + 1) % n));
				c1x = leaving.outX;
				c1y = leaving.outY;
				c1z = leaving.outZ;
				c2x = ex + arriving.inX;
				c2y = ey + arriving.inY;
				c2z = ez + arriving.inZ;
			}

			const float cdx = p2.x - p1.x;
			const float cdz = p2.z - p1.z;
			const float chord = std::sqrt(cdx * cdx + cdz * cdz);
			const int steps = std::max(1, static_cast<int>(std::ceil(chord / maxStep)));

			for (int k = 0; k < steps; ++k)
			{
				const float t = static_cast<float>(k) / static_cast<float>(steps);
				SplineSample sample{};
				if (catmullRomSegment)
				{
					sample.x = catmullRom(p0.x, p1.x, p2.x, p3.x, t);
					sample.z = catmullRom(p0.z, p1.z, p2.z, p3.z, t);
					sample.y = catmullRom(p0.y, p1.y, p2.y, p3.y, t);
				}
				else if (curved)
				{
					sample.x = bezier(p1.x, c1x, c2x, ex, t);
					sample.z = bezier(p1.z, c1z, c2z, ez, t);
					sample.y = bezier(p1.y, c1y, c2y, ey, t);
				}
				else
				{
					sample.x = p1.x + (p2.x - p1.x) * t;
					sample.z = p1.z + (p2.z - p1.z) * t;
					sample.y = p1.y + (p2.y - p1.y) * t;
				}
				sample.width = p1.width + (p2.width - p1.width) * t;
				sample.distance = 0.0f;
				if (k == 0)
				{
					m_controlSampleIndices.push_back(m_samples.size());
				}
				m_samples.push_back(sample);
				m_controlSegments.push_back(static_cast<uint32>(s));
			}
		}

		if (!m_closed)
		{
			const SplinePoint& last = pts.back();
			m_controlSampleIndices.push_back(m_samples.size());
			m_samples.push_back({ last.x, last.z, last.width, 0.0f, last.y });
			m_controlSegments.push_back(static_cast<uint32>(std::max(0, segmentCount - 1)));
		}

		float accumulated = 0.0f;
		for (size_t i = 1; i < m_samples.size(); ++i)
		{
			const float dx = m_samples[i].x - m_samples[i - 1].x;
			const float dz = m_samples[i].z - m_samples[i - 1].z;
			accumulated += std::sqrt(dx * dx + dz * dz);
			m_samples[i].distance = accumulated;
		}

		for (const size_t sampleIndex : m_controlSampleIndices)
		{
			m_controlDistances.push_back(m_samples[sampleIndex].distance);
		}

		if (m_closed)
		{
			const float dx = m_samples.front().x - m_samples.back().x;
			const float dz = m_samples.front().z - m_samples.back().z;
			accumulated += std::sqrt(dx * dx + dz * dz);

			// The shoelace sign gives the winding.
			// Relative to the first sample and in double, so large world coordinates do not cancel.
			const double ox = m_samples[0].x;
			const double oz = m_samples[0].z;
			double area = 0.0;
			for (size_t i = 0; i < m_samples.size(); ++i)
			{
				const SplineSample& a = m_samples[i];
				const SplineSample& b = GetSegmentEnd(i);
				area += (a.x - ox) * (b.z - oz) - (b.x - ox) * (a.z - oz);
			}
			m_windingPositive = area >= 0.0f;
		}

		m_length = accumulated;
	}

	size_t SampledPath::GetSegmentCount() const
	{
		if (m_samples.size() < 2)
		{
			return 0;
		}
		return m_closed ? m_samples.size() : m_samples.size() - 1;
	}

	const SplineSample& SampledPath::GetSegmentEnd(size_t i) const
	{
		return m_samples[(i + 1) % m_samples.size()];
	}

	float SampledPath::SignedDistance(float x, float z) const
	{
		if (m_samples.empty())
		{
			return std::numeric_limits<float>::max();
		}

		float best = std::numeric_limits<float>::max();
		if (m_samples.size() == 1)
		{
			best = std::hypot(x - m_samples[0].x, z - m_samples[0].z);
		}

		for (size_t i = 0; i < GetSegmentCount(); ++i)
		{
			const SplineSample& a = m_samples[i];
			const SplineSample& b = GetSegmentEnd(i);
			best = std::min(best, pointSegmentDistance(x, z, a.x, a.z, b.x, b.z));
		}

		return Contains(x, z) ? -best : best;
	}

	bool SampledPath::Contains(float x, float z) const
	{
		if (!m_closed)
		{
			return false;
		}

		bool inside = false;
		for (size_t i = 0; i < m_samples.size(); ++i)
		{
			const SplineSample& a = m_samples[i];
			const SplineSample& b = GetSegmentEnd(i);
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

	float SampledPath::Normalize(float distance) const
	{
		if (m_length <= 0.0f)
		{
			return 0.0f;
		}

		if (m_closed)
		{
			distance = std::fmod(distance, m_length);
			if (distance < 0.0f)
			{
				distance += m_length;
			}
			return distance;
		}

		return std::clamp(distance, 0.0f, m_length);
	}

	size_t SampledPath::FindSegment(float distance) const
	{
		const size_t count = GetSegmentCount();
		if (count == 0)
		{
			return 0;
		}

		// Last segment whose start distance is <= distance.
		size_t lo = 0;
		size_t hi = count - 1;
		while (lo < hi)
		{
			const size_t mid = (lo + hi + 1) / 2;
			if (m_samples[mid].distance <= distance)
			{
				lo = mid;
			}
			else
			{
				hi = mid - 1;
			}
		}

		return lo;
	}

	SplineSample SampledPath::SampleAtDistance(float distance) const
	{
		if (m_samples.empty())
		{
			return { 0.0f, 0.0f, 0.0f, 0.0f };
		}

		if (m_samples.size() == 1)
		{
			return m_samples[0];
		}

		distance = Normalize(distance);
		const size_t i = FindSegment(distance);
		const SplineSample& a = m_samples[i];
		const SplineSample& b = GetSegmentEnd(i);
		const float endDistance = (m_closed && i + 1 == m_samples.size()) ? m_length : b.distance;
		const float span = endDistance - a.distance;
		const float t = span > 0.0f ? std::clamp((distance - a.distance) / span, 0.0f, 1.0f) : 0.0f;
		return { a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t, a.width + (b.width - a.width) * t, distance, a.y + (b.y - a.y) * t };
	}

	void SampledPath::NormalAtDistance(float distance, float& nx, float& nz) const
	{
		nx = 0.0f;
		nz = 0.0f;
		if (m_samples.size() < 2)
		{
			return;
		}

		const size_t i = FindSegment(Normalize(distance));
		const SplineSample& a = m_samples[i];
		const SplineSample& b = GetSegmentEnd(i);
		float tx = b.x - a.x;
		float tz = b.z - a.z;
		const float len = std::sqrt(tx * tx + tz * tz);
		if (len <= 0.0f)
		{
			return;
		}
		tx /= len;
		tz /= len;

		if (m_closed && m_windingPositive)
		{
			// Right of travel is outward for a positive shoelace area.
			nx = tz;
			nz = -tx;
		}
		else
		{
			nx = -tz;
			nz = tx;
		}
	}

	void SampledPath::GetBounds(float& minX, float& minZ, float& maxX, float& maxZ) const
	{
		if (m_samples.empty())
		{
			minX = minZ = maxX = maxZ = 0.0f;
			return;
		}

		minX = maxX = m_samples[0].x;
		minZ = maxZ = m_samples[0].z;
		for (const SplineSample& s : m_samples)
		{
			minX = std::min(minX, s.x);
			maxX = std::max(maxX, s.x);
			minZ = std::min(minZ, s.z);
			maxZ = std::max(maxZ, s.z);
		}
	}

	uint32 SampledPath::SegmentAtDistance(float distance) const
	{
		if (m_samples.empty())
		{
			return 0;
		}

		return m_controlSegments[FindSegment(Normalize(distance))];
	}

	float SampledPath::GetControlPointDistance(const size_t index) const
	{
		return index < m_controlDistances.size() ? m_controlDistances[index] : m_length;
	}

	bool SampledPath::IsSelfIntersecting() const
	{
		const size_t count = GetSegmentCount();
		for (size_t i = 0; i < count; ++i)
		{
			for (size_t j = i + 2; j < count; ++j)
			{
				// The first and last segment of a closed path are adjacent.
				if (m_closed && i == 0 && j == count - 1)
				{
					continue;
				}

				if (segmentsCross(m_samples[i], GetSegmentEnd(i), m_samples[j], GetSegmentEnd(j)))
				{
					return true;
				}
			}
		}

		return false;
	}
}
