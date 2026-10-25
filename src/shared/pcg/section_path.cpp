// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "section_path.h"

#include "pcg_random.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mmo::pcg
{
	namespace
	{
		constexpr float kFrameProbe = 0.25f;

		/// Samples between the three points a bend radius is measured on (2 m at the sample step), so that
		/// the estimate is not dominated by float noise or single polyline corners.
		constexpr size_t kBendStride = 4;
		constexpr uint64 kSpanSalt = 0x5350414eu;

		/// Circumradius of the triangle abc on x/z; false for (nearly) collinear points.
		bool circumRadius(const SplineSample& a, const SplineSample& b, const SplineSample& c, float& radius)
		{
			const float abx = b.x - a.x;
			const float abz = b.z - a.z;
			const float acx = c.x - a.x;
			const float acz = c.z - a.z;
			const float bcx = c.x - b.x;
			const float bcz = c.z - b.z;
			const float cross = std::fabs(abx * acz - abz * acx);
			if (cross < 1e-4f)
			{
				return false;
			}

			const float lab = std::sqrt(abx * abx + abz * abz);
			const float lac = std::sqrt(acx * acx + acz * acz);
			const float lbc = std::sqrt(bcx * bcx + bcz * bcz);
			radius = (lab * lac * lbc) / (2.0f * cross);
			return true;
		}
	}

	SectionLayout LayOutSections(const PathSpline& spline, const SampledPath& path, const std::vector<uint64>& pointIds, const float sectionLength)
	{
		SectionLayout layout;
		layout.minimumBendRadius = std::numeric_limits<float>::max();

		const size_t pointCount = spline.points.size();
		if (pointCount < 2 || sectionLength <= 0.0f || pointIds.size() != pointCount)
		{
			return layout;
		}

		const std::vector<SplineSample>& samples = path.GetSamples();

		const bool closed = path.IsClosed();
		const size_t spanCount = closed ? pointCount : pointCount - 1;
		for (size_t span = 0; span < spanCount; ++span)
		{
			const float start = path.GetControlPointDistance(span);
			const float end = (closed && span + 1 == pointCount) ? path.GetLength() : path.GetControlPointDistance(span + 1);
			const float length = end - start;
			if (length <= 0.0f)
			{
				continue;
			}

			const uint32 count = std::max(1u, static_cast<uint32>(std::floor(length / sectionLength + 0.5f)));
			const float stretched = length / static_cast<float>(count);
			const uint64 key = Hash(pointIds[span], static_cast<int64>(pointIds[(span + 1) % pointCount]), 0, kSpanSalt);
			for (uint32 copy = 0; copy < count; ++copy)
			{
				SectionCopy c;
				c.span = static_cast<uint32>(span);
				c.copy = copy;
				c.startDistance = start + stretched * static_cast<float>(copy);
				c.stretchedLength = stretched;
				c.spanKey = key;
				layout.copies.push_back(c);
			}
		}

		const size_t n = samples.size();
		for (size_t i = 0; i < n; ++i)
		{
			if (!closed && (i < kBendStride || i + kBendStride >= n))
			{
				continue;
			}

			float radius = 0.0f;
			if (circumRadius(samples[(i + n - kBendStride) % n], samples[i], samples[(i + kBendStride) % n], radius))
			{
				layout.minimumBendRadius = std::min(layout.minimumBendRadius, radius);
			}
		}

		return layout;
	}

	SectionFrame FrameAt(const SampledPath& path, const float distance)
	{
		SectionFrame frame;
		const SplineSample here = path.SampleAtDistance(distance);
		frame.position = Vector3(here.x, here.y, here.z);

		const SplineSample before = path.SampleAtDistance(distance - kFrameProbe);
		const SplineSample after = path.SampleAtDistance(distance + kFrameProbe);
		const float dx = after.x - before.x;
		const float dz = after.z - before.z;
		const float len = std::sqrt(dx * dx + dz * dz);
		if (len > 1e-6f)
		{
			frame.forwardX = dx / len;
			frame.forwardZ = dz / len;
		}

		// right = up x forward with up = +Y.
		frame.rightX = frame.forwardZ;
		frame.rightZ = -frame.forwardX;
		return frame;
	}

	bool ProjectToPath(const SampledPath& path, const float x, const float z, const float maxAcross, float& distance, float& across)
	{
		const std::vector<SplineSample>& samples = path.GetSamples();
		const size_t n = samples.size();
		if (n < 2)
		{
			return false;
		}

		const size_t segments = path.IsClosed() ? n : n - 1;
		float bestSq = std::numeric_limits<float>::max();
		float bestDistance = 0.0f;
		float bestAcross = 0.0f;
		for (size_t i = 0; i < segments; ++i)
		{
			const SplineSample& a = samples[i];
			const SplineSample& b = samples[(i + 1) % n];
			const float sx = b.x - a.x;
			const float sz = b.z - a.z;
			const float lenSq = sx * sx + sz * sz;
			if (lenSq <= 0.0f)
			{
				continue;
			}

			const float t = std::clamp(((x - a.x) * sx + (z - a.z) * sz) / lenSq, 0.0f, 1.0f);
			const float px = x - (a.x + sx * t);
			const float pz = z - (a.z + sz * t);
			const float distSq = px * px + pz * pz;
			if (distSq < bestSq)
			{
				const float len = std::sqrt(lenSq);
				bestSq = distSq;
				bestDistance = a.distance + len * t;

				// Offset of the query from the path point, projected on the segment's right (sz, -sx).
				bestAcross = (px * sz - pz * sx) / len;
			}
		}

		if (bestSq == std::numeric_limits<float>::max() || std::sqrt(bestSq) > maxAcross)
		{
			return false;
		}

		distance = bestDistance;
		across = bestAcross;
		return true;
	}

	const SectionCopy* FindCopy(const SectionLayout& layout, const float distance, const float sectionLength, float& localZ)
	{
		localZ = 0.0f;
		if (layout.copies.empty() || distance < layout.copies.front().startDistance)
		{
			return nullptr;
		}

		// Last copy whose start is not beyond the distance; closes the sub-ulp gaps between copies.
		const auto it = std::upper_bound(layout.copies.begin(), layout.copies.end(), distance,
			[](const float value, const SectionCopy& copy) { return value < copy.startDistance; });
		const SectionCopy& copy = *(it - 1);
		const float end = copy.startDistance + copy.stretchedLength;
		if (&copy == &layout.copies.back() && distance > end + 1e-4f)
		{
			return nullptr;
		}

		const float t = copy.stretchedLength > 0.0f ? (distance - copy.startDistance) / copy.stretchedLength : 0.0f;
		localZ = std::clamp(t, 0.0f, 1.0f) * sectionLength;
		return &copy;
	}
}
