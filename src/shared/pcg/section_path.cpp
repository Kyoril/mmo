// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "section_path.h"

#include "pcg_random.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>

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

		/// Nearest segment found so far by a projection.
		struct Projection
		{
			float bestSq = std::numeric_limits<float>::max();
			float distance = 0.0f;
			float across = 0.0f;
			size_t segment = std::numeric_limits<size_t>::max();
		};

		/// Tests polyline segment i (sample i to the next, wrapping). Nearest wins, ties go to the lowest index, so
		/// the result does not depend on the order segments are tested in.
		void TestSegment(const std::vector<SplineSample>& samples, const size_t i, const float x, const float z, Projection& best)
		{
			const size_t n = samples.size();
			const SplineSample& a = samples[i];
			const SplineSample& b = samples[(i + 1) % n];
			const float sx = b.x - a.x;
			const float sz = b.z - a.z;
			const float lenSq = sx * sx + sz * sz;
			if (lenSq <= 0.0f)
			{
				return;
			}

			const float t = std::clamp(((x - a.x) * sx + (z - a.z) * sz) / lenSq, 0.0f, 1.0f);
			const float px = x - (a.x + sx * t);
			const float pz = z - (a.z + sz * t);
			const float distSq = px * px + pz * pz;
			if (distSq < best.bestSq || (distSq == best.bestSq && i < best.segment))
			{
				const float len = std::sqrt(lenSq);
				best.bestSq = distSq;
				best.segment = i;
				best.distance = a.distance + len * t;

				// Offset of the query from the path point, projected on the segment's right (sz, -sx).
				best.across = (px * sz - pz * sx) / len;
			}
		}

		/// Smallest cell size of the segment grid: keeps the grid coarse for narrow paths.
		constexpr float kMinSegmentCell = 16.0f;
		/// Slack between the grid's exact reach and its cell size, against float rounding at cell borders.
		constexpr float kSegmentCellSlack = 0.5f;

		/// Cell coordinate of v as a float (floor), so far-away queries never overflow an int.
		float CellCoordinate(const float v, const float origin, const float cellSize)
		{
			return std::floor((v - origin) / cellSize);
		}

		void BuildSegmentGrid(const SampledPath& path, const float reach, SectionSegmentGrid& grid)
		{
			grid = SectionSegmentGrid();
			const std::vector<SplineSample>& samples = path.GetSamples();
			const size_t n = samples.size();
			if (n < 2)
			{
				return;
			}

			float minX = 0.0f, minZ = 0.0f, maxX = 0.0f, maxZ = 0.0f;
			path.GetBounds(minX, minZ, maxX, maxZ);
			grid.cellSize = std::max(kMinSegmentCell, reach + kSegmentCellSlack);
			grid.reach = grid.cellSize - kSegmentCellSlack;
			grid.originX = minX - grid.cellSize;
			grid.originZ = minZ - grid.cellSize;
			grid.columns = static_cast<int32>(CellCoordinate(maxX, grid.originX, grid.cellSize)) + 2;
			grid.rows = static_cast<int32>(CellCoordinate(maxZ, grid.originZ, grid.cellSize)) + 2;

			const size_t segments = path.IsClosed() ? n : n - 1;
			const auto cellRange = [&](const size_t i, int32& x0, int32& z0, int32& x1, int32& z1)
			{
				const SplineSample& a = samples[i];
				const SplineSample& b = samples[(i + 1) % n];
				x0 = std::clamp(static_cast<int32>(CellCoordinate(std::min(a.x, b.x), grid.originX, grid.cellSize)), 0, grid.columns - 1);
				x1 = std::clamp(static_cast<int32>(CellCoordinate(std::max(a.x, b.x), grid.originX, grid.cellSize)), 0, grid.columns - 1);
				z0 = std::clamp(static_cast<int32>(CellCoordinate(std::min(a.z, b.z), grid.originZ, grid.cellSize)), 0, grid.rows - 1);
				z1 = std::clamp(static_cast<int32>(CellCoordinate(std::max(a.z, b.z), grid.originZ, grid.cellSize)), 0, grid.rows - 1);
			};

			const size_t cells = static_cast<size_t>(grid.columns) * static_cast<size_t>(grid.rows);
			grid.cellStart.assign(cells + 1, 0);
			for (size_t i = 0; i < segments; ++i)
			{
				int32 x0, z0, x1, z1;
				cellRange(i, x0, z0, x1, z1);
				for (int32 cz = z0; cz <= z1; ++cz)
				{
					for (int32 cx = x0; cx <= x1; ++cx)
					{
						++grid.cellStart[static_cast<size_t>(cz) * grid.columns + cx + 1];
					}
				}
			}
			for (size_t c = 0; c < cells; ++c)
			{
				grid.cellStart[c + 1] += grid.cellStart[c];
			}

			// Filled in ascending segment order, so every cell lists its segments ascending.
			grid.segments.resize(grid.cellStart[cells]);
			std::vector<uint32> cursor(grid.cellStart.begin(), grid.cellStart.end() - 1);
			for (size_t i = 0; i < segments; ++i)
			{
				int32 x0, z0, x1, z1;
				cellRange(i, x0, z0, x1, z1);
				for (int32 cz = z0; cz <= z1; ++cz)
				{
					for (int32 cx = x0; cx <= x1; ++cx)
					{
						grid.segments[cursor[static_cast<size_t>(cz) * grid.columns + cx]++] = static_cast<uint32>(i);
					}
				}
			}
		}

		bool FinishProjection(const Projection& best, const float maxAcross, float& distance, float& across)
		{
			if (best.segment == std::numeric_limits<size_t>::max() || std::sqrt(best.bestSq) > maxAcross)
			{
				return false;
			}

			distance = best.distance;
			across = best.across;
			return true;
		}
	}

	namespace
	{
		constexpr uint64 kCornerSalt = 0x434f524eu;
		constexpr float kRadToDeg = 57.29577951308232f;

		/// Signed x/z turn angle (degrees) at an arc distance between the chord arriving over [d - reach, d] and the
		/// chord leaving over [d, d + reach]; positive for a left turn (towards -X, the floor side).
		float TurnAngleDeg(const SampledPath& path, const float distance, const float reach)
		{
			const SplineSample a = path.SampleAtDistance(distance - reach);
			const SplineSample b = path.SampleAtDistance(distance);
			const SplineSample c = path.SampleAtDistance(distance + reach);
			const float inX = b.x - a.x;
			const float inZ = b.z - a.z;
			const float outX = c.x - b.x;
			const float outZ = c.z - b.z;
			if (!(inX * inX + inZ * inZ > 1e-8f) || !(outX * outX + outZ * outZ > 1e-8f))
			{
				return 0.0f;
			}

			// The left of forward (fx, fz) is (-fz, fx): a positive cross product turns left.
			const float cross = inX * outZ - inZ * outX;
			const float dot = inX * outX + inZ * outZ;
			return std::atan2(cross, dot) * kRadToDeg;
		}

		/// Which corner roles a layout may use.
		struct CornerOptions
		{
			float angleDeg = 0.0f;
			bool inner = false;
			bool outer = false;
		};

		SectionLayout LayOut(const PathSpline& spline, const SampledPath& path, const std::vector<uint64>& pointIds, const float sectionLength,
			const CornerOptions& corners)
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
			layout.pointRoles.assign(pointCount, SectionVariantRole::Straight);
			layout.loopLength = closed ? path.GetLength() : 0.0f;

			const auto spanStart = [&](const size_t span)
			{
				return path.GetControlPointDistance(span);
			};
			const auto spanEnd = [&](const size_t span)
			{
				return (closed && span + 1 == pointCount) ? path.GetLength() : path.GetControlPointDistance(span + 1);
			};

			// Corners, decided in point order: a corner must fit into both of its spans and leave at least half a section
			// of straight path to an earlier corner at the far end of a span, else it falls back to bending.
			const float half = 0.5f * sectionLength;
			std::vector<bool> isCorner(pointCount, false);
			if ((corners.inner || corners.outer) && std::isfinite(corners.angleDeg))
			{
				const auto fits = [&](const size_t span, const size_t otherPoint)
				{
					const float length = spanEnd(span) - spanStart(span);
					return isCorner[otherPoint] ? length - sectionLength >= half : length - half >= 0.0f;
				};

				for (size_t i = 0; i < pointCount; ++i)
				{
					if (!closed && (i == 0 || i + 1 == pointCount))
					{
						continue;
					}

					const float angle = TurnAngleDeg(path, path.GetControlPointDistance(i), half);
					if (!(std::fabs(angle) >= corners.angleDeg))
					{
						continue;
					}

					const SectionVariantRole role = angle > 0.0f ? SectionVariantRole::InnerCorner : SectionVariantRole::OuterCorner;
					if ((role == SectionVariantRole::InnerCorner && !corners.inner) || (role == SectionVariantRole::OuterCorner && !corners.outer))
					{
						continue;
					}

					const size_t previousSpan = i > 0 ? i - 1 : spanCount - 1;
					const size_t previousPoint = i > 0 ? i - 1 : pointCount - 1;
					if (fits(previousSpan, previousPoint) && fits(i, (i + 1) % pointCount))
					{
						isCorner[i] = true;
						layout.pointRoles[i] = role;
					}
				}
			}

			// Straight part of each span between the half corners at its ends. A part shorter than half a section next to
			// a corner would become a sliver copy: the corner piece is stretched over it instead (by construction such a
			// span has a corner at one end only, since a span between two corners keeps at least half a section).
			std::vector<float> straightStart(spanCount, 0.0f);
			std::vector<float> straightEnd(spanCount, 0.0f);
			std::vector<float> absorbBefore(pointCount, 0.0f);
			std::vector<float> absorbAfter(pointCount, 0.0f);
			for (size_t span = 0; span < spanCount; ++span)
			{
				const size_t endPoint = (span + 1) % pointCount;
				straightStart[span] = spanStart(span) + (isCorner[span] ? half : 0.0f);
				straightEnd[span] = spanEnd(span) - (isCorner[endPoint] ? half : 0.0f);
				const float length = straightEnd[span] - straightStart[span];
				if (!(length > 0.0f) || !(length < half) || !(isCorner[span] || isCorner[endPoint]))
				{
					continue;
				}

				if (isCorner[span])
				{
					absorbAfter[span] += length;
				}
				else
				{
					absorbBefore[endPoint] += length;
				}
				straightEnd[span] = straightStart[span];
			}

			std::array<uint32, 3> sequence{};
			const auto addCorner = [&](const size_t point, const float centre)
			{
				SectionCopy c;
				c.span = static_cast<uint32>(point);
				c.copy = 0;
				c.startDistance = centre - half - absorbBefore[point];
				c.stretchedLength = sectionLength + absorbBefore[point] + absorbAfter[point];
				c.role = layout.pointRoles[point];
				c.spanKey = Hash(pointIds[point], static_cast<int64>(c.role), 0, kCornerSalt);
				c.sequence = sequence[static_cast<size_t>(c.role)]++;
				layout.copies.push_back(c);
			};

			for (size_t span = 0; span < spanCount; ++span)
			{
				// The corner at the start of a closed loop wraps past the path end; it is laid after the last span.
				if (isCorner[span] && (span > 0 || !closed))
				{
					addCorner(span, spanStart(span));
				}

				const float start = straightStart[span];
				const float length = straightEnd[span] - start;
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
					c.sequence = sequence[static_cast<size_t>(SectionVariantRole::Straight)]++;
					layout.copies.push_back(c);
				}
			}
			if (closed && isCorner[0])
			{
				addCorner(0, path.GetLength());
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
	}

	SectionLayout LayOutSections(const PathSpline& spline, const SampledPath& path, const std::vector<uint64>& pointIds, const float sectionLength)
	{
		return LayOut(spline, path, pointIds, sectionLength, CornerOptions());
	}

	SectionLayout LayOutSections(const PathSpline& spline, const SampledPath& path, const std::vector<uint64>& pointIds, const SectionAsset& asset)
	{
		CornerOptions corners;
		corners.angleDeg = asset.cornerAngleDeg;
		for (const SectionVariant& variant : asset.variants)
		{
			corners.inner = corners.inner || variant.role == SectionVariantRole::InnerCorner;
			corners.outer = corners.outer || variant.role == SectionVariantRole::OuterCorner;
		}
		return LayOut(spline, path, pointIds, asset.length, corners);
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
		Projection best;
		for (size_t i = 0; i < segments; ++i)
		{
			TestSegment(samples, i, x, z, best);
		}

		return FinishProjection(best, maxAcross, distance, across);
	}

	const SectionCopy* FindCopy(const SectionLayout& layout, float distance, const float sectionLength, float& localZ)
	{
		localZ = 0.0f;
		if (layout.loopLength > 0.0f && !layout.copies.empty() && distance < layout.copies.front().startDistance)
		{
			distance += layout.loopLength;
		}

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

	namespace
	{
		constexpr uint64 kVariantSalt = 0x56415249u;
		constexpr float kInfluenceMargin = 1.0f;

		/// What one copy's grids say at a local position.
		struct CopySample
		{
			float height = 0.0f;
			std::array<float, 4> weights{};
			float coverage = 0.0f;
		};

		/// 1 within halfWidth, smoothstep down to 0 at halfWidth + sideFalloff.
		float SideFalloff(const SectionAsset& asset, const float distance)
		{
			if (distance <= asset.halfWidth)
			{
				return 1.0f;
			}
			if (!(asset.sideFalloff > 0.0f))
			{
				return 0.0f;
			}
			const float t = (distance - asset.halfWidth) / asset.sideFalloff;
			if (t >= 1.0f)
			{
				return 0.0f;
			}
			return 1.0f - t * t * (3.0f - 2.0f * t);
		}

		/// Centre line height at an arc distance, interpolated between the path samples.
		float CentreHeightAt(const SectionPathShape& shape, float distance)
		{
			const SampledPath& path = *shape.path;
			const std::vector<SplineSample>& samples = path.GetSamples();
			const std::vector<float>& heights = shape.centreHeights;
			const size_t n = samples.size();
			if (n == 0 || heights.size() != n)
			{
				return 0.0f;
			}

			const float length = path.GetLength();
			if (path.IsClosed() && length > 0.0f)
			{
				distance = std::fmod(distance, length);
				if (distance < 0.0f)
				{
					distance += length;
				}
			}
			else
			{
				distance = std::clamp(distance, 0.0f, length);
			}

			const auto it = std::upper_bound(samples.begin(), samples.end(), distance,
				[](const float value, const SplineSample& sample) { return value < sample.distance; });
			const size_t i = it == samples.begin() ? 0 : static_cast<size_t>(it - samples.begin()) - 1;
			size_t next = i + 1;
			float endDistance = 0.0f;
			if (next < n)
			{
				endDistance = samples[next].distance;
			}
			else if (path.IsClosed())
			{
				next = 0;
				endDistance = length;
			}
			else
			{
				return heights[i];
			}

			const float span = endDistance - samples[i].distance;
			const float t = span > 0.0f ? std::clamp((distance - samples[i].distance) / span, 0.0f, 1.0f) : 0.0f;
			return heights[i] + (heights[next] - heights[i]) * t;
		}

		/// Samples a copy's variant at an arc distance (may lie beyond the copy; the grids clamp) and offset.
		CopySample SampleCopy(const SectionPathShape& shape, const SectionCopy& copy, const float distance, const float across)
		{
			CopySample out;
			const SectionVariant& variant = shape.asset.variants[GetVariant(shape, copy)];
			const float localZ = copy.stretchedLength > 0.0f
				? (distance - copy.startDistance) * shape.asset.length / copy.stretchedLength
				: 0.0f;
			out.height = SampleHeight(variant.heights, across, localZ);
			SamplePaint(variant.paint, across, localZ, out.weights, out.coverage);
			return out;
		}

		float Smooth(const float t)
		{
			const float c = std::clamp(t, 0.0f, 1.0f);
			return c * c * (3.0f - 2.0f * c);
		}

		/// Scales weights to integers summing to exactly 255 (largest remainder, ties to the lower layer).
		std::array<uint8, 4> NormalizeTo255(const std::array<float, 4>& weights, const float sum)
		{
			std::array<uint8, 4> out{};
			std::array<float, 4> fraction{};
			int32 total = 0;
			for (size_t i = 0; i < 4; ++i)
			{
				const float scaled = weights[i] * 255.0f / sum;
				const float floored = std::floor(scaled);
				out[i] = static_cast<uint8>(std::clamp(floored, 0.0f, 255.0f));
				fraction[i] = scaled - floored;
				total += out[i];
			}

			while (total < 255)
			{
				size_t best = 0;
				for (size_t i = 1; i < 4; ++i)
				{
					if (fraction[i] > fraction[best])
					{
						best = i;
					}
				}
				++out[best];
				fraction[best] = -1.0f;
				++total;
			}
			return out;
		}

		/// Shoelace area of a path's samples on x/z (closing segment included); positive when the interior lies on
		/// the left of the travel direction.
		double SignedArea(const SampledPath& path)
		{
			const std::vector<SplineSample>& samples = path.GetSamples();
			const size_t n = samples.size();
			double twice = 0.0;
			for (size_t i = 0; i < n; ++i)
			{
				const SplineSample& a = samples[i];
				const SplineSample& b = samples[(i + 1) % n];
				twice += static_cast<double>(a.x) * b.z - static_cast<double>(b.x) * a.z;
			}
			return 0.5 * twice;
		}

		/// Replaces every value (one per path sample, linear in between) by its mean over the arc window
		/// [d - window / 2, d + window / 2] around the sample: clamped to the path on open paths, wrapping on closed ones
		/// (a window longer than a loop averages the whole loop). A window <= 0 keeps the values.
		void SmoothAlongPath(const SampledPath& path, const float window, std::vector<float>& values)
		{
			const std::vector<SplineSample>& samples = path.GetSamples();
			const size_t n = samples.size();
			const bool closed = path.IsClosed();
			const double length = path.GetLength();
			if (!(window > 0.0f) || n < 2 || values.size() != n || !(length > 0.0))
			{
				return;
			}

			// Segment k runs from sample k to the next (a closed path's last one back to sample 0 at the path length).
			const size_t segments = closed ? n : n - 1;
			const auto segmentEnd = [&](const size_t k)
			{
				return k + 1 < n ? static_cast<double>(samples[k + 1].distance) : length;
			};
			std::vector<double> prefix(segments + 1, 0.0);
			for (size_t k = 0; k < segments; ++k)
			{
				const double span = segmentEnd(k) - samples[k].distance;
				prefix[k + 1] = prefix[k] + 0.5 * (static_cast<double>(values[k]) + values[(k + 1) % n]) * span;
			}

			// Integral of the values from 0 to an arc distance in [0, length].
			const auto integralTo = [&](const double distance)
			{
				const auto it = std::upper_bound(samples.begin(), samples.end(), distance,
					[](const double value, const SplineSample& sample) { return value < sample.distance; });
				size_t k = it == samples.begin() ? 0 : static_cast<size_t>(it - samples.begin()) - 1;
				k = std::min(k, segments - 1);
				const double start = samples[k].distance;
				const double span = segmentEnd(k) - start;
				const double t = std::clamp(distance - start, 0.0, span);
				const double a = values[k];
				const double b = values[(k + 1) % n];
				const double at = span > 0.0 ? a + (b - a) * t / span : a;
				return prefix[k] + 0.5 * (a + at) * t;
			};

			// Closed paths: the integral continues periodically past both ends.
			const double total = prefix[segments];
			const auto wrappedIntegralTo = [&](const double distance)
			{
				const double turns = std::floor(distance / length);
				return turns * total + integralTo(distance - turns * length);
			};

			const double half = 0.5 * (closed ? std::min(static_cast<double>(window), length) : static_cast<double>(window));
			std::vector<float> smoothed(n);
			for (size_t i = 0; i < n; ++i)
			{
				const double d = samples[i].distance;
				double a = d - half;
				double b = d + half;
				if (!closed)
				{
					a = std::max(0.0, a);
					b = std::min(length, b);
				}
				if (!(b - a > 0.0))
				{
					smoothed[i] = values[i];
					continue;
				}

				const double integral = closed ? wrappedIntegralTo(b) - wrappedIntegralTo(a) : integralTo(b) - integralTo(a);
				smoothed[i] = static_cast<float>(integral / (b - a));
			}
			values = std::move(smoothed);
		}

		/// The same closed curve traversed the other way, still starting at point 0: point k = point (n - k) % n, with
		/// its arriving and leaving handles swapped.
		PathSpline ReverseLoop(const PathSpline& spline)
		{
			PathSpline out = spline;
			const size_t n = spline.points.size();
			for (size_t k = 0; k < n; ++k)
			{
				SplinePoint p = spline.points[(n - k) % n];
				std::swap(p.inX, p.outX);
				std::swap(p.inY, p.outY);
				std::swap(p.inZ, p.outZ);
				out.points[k] = p;
			}
			return out;
		}
	}

	bool PrepareSectionPath(const PathSpline& spline, const std::vector<uint64>& pointIds, const SectionAsset& asset,
		const SectionPathParams& params, const std::function<float(float, float)>& baseGround, SectionPathShape& out, String& error)
	{
		out = SectionPathShape();
		if (spline.points.size() < 2)
		{
			error = "A section path needs at least two control points.";
			return false;
		}
		if (pointIds.size() != spline.points.size())
		{
			error = "Every control point needs an id.";
			return false;
		}

		std::vector<uint64> sortedIds = pointIds;
		std::sort(sortedIds.begin(), sortedIds.end());
		if (std::adjacent_find(sortedIds.begin(), sortedIds.end()) != sortedIds.end())
		{
			error = "Control point ids must be unique.";
			return false;
		}
		if (asset.variants.empty())
		{
			error = "The section has no variants.";
			return false;
		}
		if (std::none_of(asset.variants.begin(), asset.variants.end(), [](const SectionVariant& v) { return v.role == SectionVariantRole::Straight; }))
		{
			error = "The section has no straight variant.";
			return false;
		}
		if (!(asset.length > 0.0f) || !(asset.halfWidth > 0.0f) || !(asset.sideFalloff >= 0.0f))
		{
			error = "The section needs a positive length and half width and a non-negative side falloff.";
			return false;
		}
		if ((params.heightMode == SectionHeightMode::FollowGround || params.heightMode == SectionHeightMode::SmoothedGround) && !baseGround)
		{
			error = "Follow-ground and smoothed-ground modes need the base ground.";
			return false;
		}

		out.path.emplace(spline, kSectionPathSampleStep);
		if (!(out.path->GetLength() > 0.0f))
		{
			out = SectionPathShape();
			error = "The section path has no length.";
			return false;
		}

		// The floor side (-X) is the path's left; a loop running clockwise (negative area) has its interior on the right,
		// so it is laid against the point order.
		const size_t pointCount = spline.points.size();
		PathSpline reversedSpline;
		std::vector<uint64> reversedIds;
		if (spline.closed && params.floorInside && SignedArea(*out.path) < 0.0)
		{
			out.reversed = true;
			reversedSpline = ReverseLoop(spline);
			reversedIds.resize(pointCount);
			for (size_t k = 0; k < pointCount; ++k)
			{
				reversedIds[k] = pointIds[(pointCount - k) % pointCount];
			}
			out.path.emplace(reversedSpline, kSectionPathSampleStep);
		}
		const PathSpline& laid = out.reversed ? reversedSpline : spline;
		const std::vector<uint64>& laidIds = out.reversed ? reversedIds : pointIds;

		out.layout = LayOutSections(laid, *out.path, laidIds, asset);
		if (out.layout.copies.empty())
		{
			out = SectionPathShape();
			error = "No section copy fits the path.";
			return false;
		}
		if (out.reversed)
		{
			// pointRoles in the caller's point order: laid point k is caller point (n - k) % n.
			std::vector<SectionVariantRole> roles(pointCount, SectionVariantRole::Straight);
			for (size_t k = 0; k < pointCount && k < out.layout.pointRoles.size(); ++k)
			{
				roles[(pointCount - k) % pointCount] = out.layout.pointRoles[k];
			}
			out.layout.pointRoles = std::move(roles);
		}

		for (size_t v = 0; v < asset.variants.size(); ++v)
		{
			const size_t role = static_cast<size_t>(asset.variants[v].role);
			if (role < out.variantsByRole.size())
			{
				out.variantsByRole[role].push_back(static_cast<uint32>(v));
			}
		}

		BuildSegmentGrid(*out.path, asset.halfWidth + asset.sideFalloff + kInfluenceMargin, out.segmentGrid);

		out.asset = asset;
		out.params = params;
		out.params.joinBlend = params.joinBlend > 0.0f ? params.joinBlend : 0.0f;
		out.params.propDensity = params.propDensity > 0.0f ? std::min(params.propDensity, 1.0f) : 0.0f;

		const std::vector<SplineSample>& samples = out.path->GetSamples();
		out.centreHeights.reserve(samples.size());
		for (const SplineSample& sample : samples)
		{
			out.centreHeights.push_back(params.heightMode == SectionHeightMode::FollowSpline ? sample.y : baseGround(sample.x, sample.z));
		}
		if (params.heightMode == SectionHeightMode::SmoothedGround)
		{
			out.params.groundSmoothing = params.groundSmoothing > 0.0f ? params.groundSmoothing : 0.0f;
			SmoothAlongPath(*out.path, out.params.groundSmoothing, out.centreHeights);
		}

		// Influence: everything a terrain sample or a prop can reach from the path.
		float reach = asset.halfWidth + asset.sideFalloff;
		float jitter = 0.0f;
		for (const SectionVariant& variant : asset.variants)
		{
			for (const SectionPlacement& placement : variant.placements)
			{
				reach = std::max(reach, std::fabs(placement.placement.position.x));
			}
		}
		for (const PropRule& rule : asset.rules)
		{
			jitter = std::max(jitter, std::fabs(rule.positionJitter));
		}

		// A run piece's pivot lies within |x| + |startOffset| of the path; its footprint and the overshoot at a
		// chain end (at most half a piece) stay within |x| + length + |startOffset|.
		for (const SectionVariant& variant : asset.variants)
		{
			for (const SectionRun& run : variant.runs)
			{
				for (const SectionRunPiece& piece : run.pieces)
				{
					reach = std::max(reach, std::fabs(run.x) + std::fabs(piece.length) + std::fabs(piece.startOffset));
				}
			}
		}
		const float grow = reach + jitter * 1.41421356f + kInfluenceMargin;

		float minX = 0.0f, minZ = 0.0f, maxX = 0.0f, maxZ = 0.0f;
		out.path->GetBounds(minX, minZ, maxX, maxZ);
		out.influence.minX = minX - grow;
		out.influence.minZ = minZ - grow;
		out.influence.maxX = maxX + grow;
		out.influence.maxZ = maxZ + grow;

		error.clear();
		return true;
	}

	WorldRect GetInfluenceRect(const SectionPathShape& shape)
	{
		return shape.influence;
	}

	uint32 GetVariant(const SectionPathShape& shape, const SectionCopy& copy)
	{
		const size_t role = static_cast<size_t>(copy.role);
		const std::vector<uint32>* candidates = role < shape.variantsByRole.size() ? &shape.variantsByRole[role] : nullptr;
		if (candidates == nullptr || candidates->empty())
		{
			return 0;
		}

		const size_t count = candidates->size();
		const size_t choice = shape.params.sequentialVariants
			? copy.sequence % count
			: static_cast<size_t>(Hash(shape.params.seed, static_cast<int64>(copy.spanKey), copy.copy, kVariantSalt) % count);
		return (*candidates)[choice];
	}

	TerrainTarget EvaluateSectionTerrain(const SectionPathShape& shape, const float x, const float z, float /*baseHeight*/, const LayerRemapFunction& remap)
	{
		TerrainTarget target;
		if (!shape.path || shape.layout.copies.empty() || shape.asset.variants.empty())
		{
			return target;
		}

		const SampledPath& path = *shape.path;
		const SectionAsset& asset = shape.asset;
		float distance = 0.0f;
		float across = 0.0f;
		if (!ProjectToSectionPath(shape, x, z, asset.halfWidth + asset.sideFalloff, distance, across))
		{
			return target;
		}

		// Distance to the nearest path point: |across| along the path, radial past the ends of an open path.
		const SplineSample nearest = path.SampleAtDistance(distance);
		const float falloff = SideFalloff(asset, std::sqrt((x - nearest.x) * (x - nearest.x) + (z - nearest.z) * (z - nearest.z)));
		if (falloff <= 0.0f)
		{
			return target;
		}

		// A closed loop whose first point has a corner: the start of the path belongs to the copy wrapping past its end.
		if (shape.layout.loopLength > 0.0f && distance < shape.layout.copies.front().startDistance)
		{
			distance += shape.layout.loopLength;
		}

		float localZ = 0.0f;
		const SectionCopy* found = FindCopy(shape.layout, distance, asset.length, localZ);
		if (found == nullptr)
		{
			return target;
		}

		const std::vector<SectionCopy>& copies = shape.layout.copies;
		const size_t count = copies.size();
		const size_t index = static_cast<size_t>(found - copies.data());
		const bool closed = path.IsClosed();
		const float length = path.GetLength();

		CopySample sample = SampleCopy(shape, *found, distance, across);

		// Cross-fade with the neighbouring copy within joinBlend of the nearer boundary.
		const float toStart = distance - found->startDistance;
		const float toEnd = found->startDistance + found->stretchedLength - distance;
		const SectionCopy* other = nullptr;
		float otherDistance = distance;
		float edge = 0.0f;
		float blend = 0.0f;
		if (toStart <= toEnd && (index > 0 || closed))
		{
			other = &copies[index > 0 ? index - 1 : count - 1];
			otherDistance = index > 0 ? distance : distance + length;
			edge = toStart;
		}
		else if (toEnd < toStart && (index + 1 < count || closed))
		{
			other = &copies[index + 1 < count ? index + 1 : 0];
			otherDistance = index + 1 < count ? distance : distance - length;
			edge = toEnd;
		}

		if (other != nullptr && other != found)
		{
			blend = std::min(shape.params.joinBlend, 0.5f * std::min(found->stretchedLength, other->stretchedLength));
		}

		if (blend > 0.0f && edge < blend)
		{
			// Own weight runs from 0.5 at the boundary to 1 at joinBlend inside the copy.
			const float own = Smooth(0.5f + 0.5f * edge / blend);
			const CopySample neighbour = SampleCopy(shape, *other, otherDistance, across);
			sample.height = neighbour.height + (sample.height - neighbour.height) * own;
			sample.coverage = neighbour.coverage + (sample.coverage - neighbour.coverage) * own;
			for (size_t i = 0; i < 4; ++i)
			{
				sample.weights[i] = neighbour.weights[i] + (sample.weights[i] - neighbour.weights[i]) * own;
			}
		}

		target.height = CentreHeightAt(shape, distance) + sample.height;
		target.heightWeight = falloff;
		target.setMaterial = false;

		const float paintWeight = sample.coverage / 255.0f * falloff;
		if (paintWeight > 0.0f)
		{
			std::array<int8, 4> map{ 0, 1, 2, 3 };
			if (remap)
			{
				map = remap(x, z);
			}

			std::array<float, 4> tile{};
			float sum = 0.0f;
			for (size_t layer = 0; layer < 4; ++layer)
			{
				const int8 to = map[layer];
				if (to < 0 || to > 3 || !(sample.weights[layer] > 0.0f))
				{
					continue;
				}
				tile[static_cast<size_t>(to)] += sample.weights[layer];
				sum += sample.weights[layer];
			}

			if (sum > 0.0f)
			{
				target.splat = NormalizeTo255(tile, sum);
				target.splatWeight = std::min(1.0f, paintWeight);
			}
		}

		return target;
	}

	namespace
	{
		constexpr uint64 kPropSalt = 0x50524f50u;
		constexpr uint64 kKeySalt = 0x4b455953u;
		constexpr float kMinScaleFactor = 0.01f;
		constexpr float kDegToRad = 3.14159265358979f / 180.0f;

		/// FrameAt, but the direction probes stay inside [lo, hi], so the frame depends on that arc range only.
		SectionFrame FrameInRange(const SampledPath& path, const float distance, const float lo, const float hi)
		{
			const float before = std::max(lo, distance - kFrameProbe);
			const float after = std::min(hi, distance + kFrameProbe);
			if (!(after > before))
			{
				return FrameAt(path, distance);
			}

			SectionFrame frame;
			const SplineSample here = path.SampleAtDistance(distance);
			frame.position = Vector3(here.x, here.y, here.z);
			const SplineSample a = path.SampleAtDistance(before);
			const SplineSample b = path.SampleAtDistance(after);
			const float dx = b.x - a.x;
			const float dz = b.z - a.z;
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

		/// The rule of an asset (rules are sorted by asset), or nullptr.
		const PropRule* FindRule(const std::vector<PropRule>& rules, const String& asset)
		{
			const auto it = std::lower_bound(rules.begin(), rules.end(), asset,
				[](const PropRule& rule, const String& value) { return rule.asset < value; });
			if (it == rules.end() || it->asset != asset)
			{
				return nullptr;
			}
			return &*it;
		}

		/// True if the asset path names a world model (".hwmo", case-insensitive).
		bool IsWorldModelAsset(const String& asset)
		{
			static const char suffix[] = ".hwmo";
			constexpr size_t length = sizeof(suffix) - 1;
			if (asset.size() < length)
			{
				return false;
			}

			for (size_t i = 0; i < length; ++i)
			{
				const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(asset[asset.size() - length + i])));
				if (c != suffix[i])
				{
					return false;
				}
			}
			return true;
		}

		/// Applies a prop rule's swap group: picks from {original} + swapGroup (index 0 keeps the original). A swap
		/// derives the kind from the new asset (foliage stays foliage and never swaps to a world model) and drops
		/// the material overrides and prop kits, which belong to the original asset.
		void ApplySwap(const PropRule& rule, const uint64 h, Placement& placement)
		{
			const size_t choice = static_cast<size_t>(h % (rule.swapGroup.size() + 1));
			if (choice == 0)
			{
				return;
			}

			const String& swapped = rule.swapGroup[choice - 1];
			if (swapped == placement.asset)
			{
				return;
			}

			const bool worldModel = IsWorldModelAsset(swapped);
			if (placement.kind == PlacementKind::Foliage)
			{
				if (worldModel)
				{
					return;
				}
			}
			else
			{
				placement.kind = worldModel ? PlacementKind::WorldModel : PlacementKind::Entity;
			}

			placement.asset = swapped;
			placement.materialOverrides.clear();
			placement.kitPaths.clear();
		}

		constexpr uint64 kRunChainSalt = 0x52554e43u;
		constexpr uint64 kRunKeepSalt = 0x52554e4bu;
		constexpr uint64 kRunPieceSalt = 0x52554e50u;
		/// Bisection steps when solving for a piece's end; far below float precision on a sample step.
		constexpr uint32 kRunBisectSteps = 40;
		/// Upper bound of pieces per chain, against degenerate input.
		constexpr uint32 kMaxRunPieces = 100000;
		/// Slack on the "at most half a piece past the chain end" rule, against float rounding.
		constexpr float kRunOvershootSlack = 1e-3f;

		/// FNV-1a 64 of a string (run names into chain keys).
		uint64 HashName(const String& name)
		{
			uint64 hash = 14695981039346656037ull;
			for (const char c : name)
			{
				hash ^= static_cast<uint8>(c);
				hash *= 1099511628211ull;
			}
			return hash;
		}

		/// Runs of consecutive copies joined into one line: one keep roll, one piece sequence.
		struct RunChain
		{
			/// Index of the first copy in the layout.
			size_t firstCopy = 0;
			/// The run of every copy of the chain, in order (copy firstCopy + i, wrapping on a closed path, has runs[i]).
			std::vector<const SectionRun*> runs;
			/// Start distance of the copy of every run, ascending (+ path length for copies past a closed path's wrap).
			std::vector<float> starts;
			/// Arc range the pieces are laid over.
			float start = 0.0f;
			float end = 0.0f;
			/// Hash of the first copy's span key, its copy index and the run name.
			uint64 key = 0;
		};

		/// Joins continuous runs of the same name in consecutive copies; every other run is a chain of its own.
		std::vector<RunChain> BuildRunChains(const SectionPathShape& shape)
		{
			std::vector<RunChain> chains;
			const SectionAsset& asset = shape.asset;
			const std::vector<SectionCopy>& copies = shape.layout.copies;

			// Chains whose last copy is the previous copy and that may still grow, by run name.
			std::map<String, size_t> open;
			// The open chains after the first copy: a closed path's chains still open at its end continue into these.
			std::map<String, size_t> openAtStart;
			for (size_t c = 0; c < copies.size(); ++c)
			{
				const SectionCopy& copy = copies[c];
				const SectionVariant& variant = asset.variants[GetVariant(shape, copy)];
				const float stretch = copy.stretchedLength / asset.length;

				std::map<String, size_t> nextOpen;
				for (const SectionRun& run : variant.runs)
				{
					if (run.pieces.empty())
					{
						continue;
					}

					const bool continuous = IsContinuousRun(run, asset.length);
					const auto joined = continuous ? open.find(run.name) : open.end();
					if (joined != open.end() && nextOpen.find(run.name) == nextOpen.end())
					{
						RunChain& chain = chains[joined->second];
						chain.runs.push_back(&run);
						chain.starts.push_back(copy.startDistance);
						chain.end = copy.startDistance + run.endZ * stretch;
						nextOpen.emplace(run.name, joined->second);
						open.erase(joined);
						continue;
					}

					RunChain chain;
					chain.firstCopy = c;
					chain.runs.push_back(&run);
					chain.starts.push_back(copy.startDistance);
					chain.start = copy.startDistance + run.startZ * stretch;
					chain.end = copy.startDistance + run.endZ * stretch;
					chain.key = Hash(Hash(copy.spanKey, copy.copy, 0, kRunChainSalt), static_cast<int64>(HashName(run.name)), 0, kRunChainSalt);
					chains.push_back(std::move(chain));
					if (continuous && nextOpen.find(run.name) == nextOpen.end())
					{
						nextOpen.emplace(run.name, chains.size() - 1);
					}
				}
				open = std::move(nextOpen);
				if (c == 0)
				{
					openAtStart = open;
				}
			}

			// Closed paths have no seam: a chain open at the last copy continues into the chain of the same name that
			// the first copy started (unless that is the same chain, which then already rings the whole loop).
			if (shape.path && shape.path->IsClosed() && copies.size() > 1)
			{
				const float length = shape.path->GetLength();
				std::vector<size_t> absorbed;
				for (const auto& tail : open)
				{
					const auto head = openAtStart.find(tail.first);
					if (head == openAtStart.end() || head->second == tail.second)
					{
						continue;
					}

					RunChain& into = chains[tail.second];
					const RunChain& from = chains[head->second];
					into.runs.insert(into.runs.end(), from.runs.begin(), from.runs.end());
					for (const float start : from.starts)
					{
						into.starts.push_back(start + length);
					}
					into.end = from.end + length;
					absorbed.push_back(head->second);
				}

				std::sort(absorbed.begin(), absorbed.end());
				for (auto it = absorbed.rbegin(); it != absorbed.rend(); ++it)
				{
					chains.erase(chains.begin() + static_cast<std::ptrdiff_t>(*it));
				}
			}
			return chains;
		}

		/// Lays the pieces of one chain along its offset line.
		class RunLayer final
		{
		public:
			RunLayer(const SectionPathShape& shape, const RunChain& chain)
				: m_shape(shape)
				, m_path(*shape.path)
				, m_chain(chain)
				, m_length(shape.path->GetLength())
			{
			}

			/// The run of the chain's copy at an arc distance (clamped to the chain's copies).
			const SectionRun& RunAt(const float distance) const
			{
				// Last copy of the chain whose start is not beyond the distance.
				const std::vector<float>& starts = m_chain.starts;
				const auto first = starts.begin() + 1;
				const auto it = std::upper_bound(first, starts.end(), distance);
				return *m_chain.runs[static_cast<size_t>(it - first)];
			}

			/// Point of the offset line at an arc distance: frame position + right * x. Past the ends of an open
			/// path the line continues straight along the end direction.
			void OffsetPoint(const float distance, float& x, float& z) const
			{
				const float lateral = RunAt(distance).x;
				float along = 0.0f;
				SectionFrame frame;
				if (!m_path.IsClosed() && distance > m_length)
				{
					frame = FrameAt(m_path, m_length);
					along = distance - m_length;
				}
				else if (!m_path.IsClosed() && distance < 0.0f)
				{
					frame = FrameAt(m_path, 0.0f);
					along = distance;
				}
				else
				{
					frame = FrameAt(m_path, distance);
				}
				x = frame.position.x + frame.forwardX * along + frame.rightX * lateral;
				z = frame.position.z + frame.forwardZ * along + frame.rightZ * lateral;
			}

			float DistanceFrom(const float x0, const float z0, const float distance) const
			{
				float x = 0.0f, z = 0.0f;
				OffsetPoint(distance, x, z);
				return std::sqrt((x - x0) * (x - x0) + (z - z0) * (z - z0));
			}

			/// First arc distance s1 > s0 whose offset point lies `length` from the one at s0 (marching along the
			/// path samples, then bisecting). False if none lies before `limit`.
			bool SolveEnd(const float s0, const float length, const float limit, float& s1) const
			{
				float x0 = 0.0f, z0 = 0.0f;
				OffsetPoint(s0, x0, z0);

				float lo = s0;
				float hi = s0;
				bool found = false;
				while (hi < limit)
				{
					lo = hi;
					hi = std::min(limit, hi + kSectionPathSampleStep);
					if (DistanceFrom(x0, z0, hi) >= length)
					{
						found = true;
						break;
					}
				}
				if (!found)
				{
					return false;
				}

				for (uint32 i = 0; i < kRunBisectSteps; ++i)
				{
					const float mid = 0.5f * (lo + hi);
					if (!(mid > lo && mid < hi))
					{
						break;
					}
					if (DistanceFrom(x0, z0, mid) >= length)
					{
						hi = mid;
					}
					else
					{
						lo = mid;
					}
				}
				s1 = hi;
				return true;
			}

			void Lay(const std::function<float(float, float)>& ground, std::vector<Placement>& out) const
			{
				float s0 = m_chain.start;
				float endX = 0.0f, endZ = 0.0f;
				OffsetPoint(m_chain.end, endX, endZ);

				for (uint32 index = 0; index < kMaxRunPieces; ++index)
				{
					const SectionRun& run = RunAt(s0);
					const SectionRunPiece& piece = run.pieces[index % run.pieces.size()];
					const float length = piece.length;
					if (!(length >= 1e-3f))
					{
						return;
					}

					// The offset line is never longer per arc metre than 1 + |x| / bend radius; four piece lengths plus
					// a metre is ample to find the end of any piece that may still be laid.
					const float limit = std::max(s0, m_chain.end) + 4.0f * length + 1.0f;
					float s1 = 0.0f;
					if (!SolveEnd(s0, length, limit, s1))
					{
						return;
					}

					float x0 = 0.0f, z0 = 0.0f, x1 = 0.0f, z1 = 0.0f;
					OffsetPoint(s0, x0, z0);
					OffsetPoint(s1, x1, z1);
					if (s1 > m_chain.end)
					{
						const float overshoot = std::sqrt((x1 - endX) * (x1 - endX) + (z1 - endZ) * (z1 - endZ));
						if (overshoot > 0.5f * length + kRunOvershootSlack)
						{
							return;
						}
					}

					const float chord = std::sqrt((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0));
					const float dirX = chord > 0.0f ? (x1 - x0) / chord : 0.0f;
					const float dirZ = chord > 0.0f ? (z1 - z0) / chord : 1.0f;

					Placement placement = piece.placement;
					placement.key = Hash(m_chain.key, static_cast<int64>(index), 0, kRunPieceSalt);
					placement.position.x = x0 - dirX * piece.startOffset;
					placement.position.z = z0 - dirZ * piece.startOffset;
					const float groundHeight = ground ? ground(placement.position.x, placement.position.z) : CentreHeightAt(m_shape, s0);
					placement.position.y = groundHeight + piece.heightAboveTerrain;
					placement.rotation = Quaternion(Radian(std::atan2(dirX, dirZ)), Vector3::UnitY) * piece.placement.rotation;
					out.push_back(std::move(placement));

					s0 = s1;
				}
			}

		private:
			const SectionPathShape& m_shape;
			const SampledPath& m_path;
			const RunChain& m_chain;
			float m_length;
		};

		/// Places every kept run chain of the path.
		void BuildRunPlacements(const SectionPathShape& shape, const std::function<float(float, float)>& ground, std::vector<Placement>& out)
		{
			for (const RunChain& chain : BuildRunChains(shape))
			{
				const float keep = chain.runs.front()->keepChance * shape.params.propDensity;
				if (!(HashToUnit(Hash(shape.params.seed, static_cast<int64>(chain.key), 0, kRunKeepSalt)) < keep))
				{
					continue;
				}
				RunLayer(shape, chain).Lay(ground, out);
			}
		}
	}

	uint64 SectionPlacementKey(const SectionCopy& copy, const uint32 variant, const uint64 sourceKey, const PlacementKind sourceKind)
	{
		return Hash(Hash(copy.spanKey, copy.copy, variant, kKeySalt), static_cast<int64>(sourceKey), static_cast<int64>(sourceKind), kKeySalt);
	}

	bool ProjectToSectionPath(const SectionPathShape& shape, const float x, const float z, const float maxAcross, float& distance, float& across)
	{
		if (!shape.path)
		{
			return false;
		}

		const SectionSegmentGrid& grid = shape.segmentGrid;
		if (grid.columns <= 0 || grid.rows <= 0 || !(maxAcross <= grid.reach))
		{
			return ProjectToPath(*shape.path, x, z, maxAcross, distance, across);
		}

		// Every segment within reach lies in the 3 x 3 cells around the query; outside them nothing is near.
		const float fx = CellCoordinate(x, grid.originX, grid.cellSize);
		const float fz = CellCoordinate(z, grid.originZ, grid.cellSize);
		if (!(fx >= -1.0f && fx <= static_cast<float>(grid.columns) && fz >= -1.0f && fz <= static_cast<float>(grid.rows)))
		{
			return false;
		}

		const int32 cx = static_cast<int32>(fx);
		const int32 cz = static_cast<int32>(fz);
		const std::vector<SplineSample>& samples = shape.path->GetSamples();
		Projection best;
		for (int32 z0 = std::max(0, cz - 1); z0 <= std::min(grid.rows - 1, cz + 1); ++z0)
		{
			for (int32 x0 = std::max(0, cx - 1); x0 <= std::min(grid.columns - 1, cx + 1); ++x0)
			{
				const size_t cell = static_cast<size_t>(z0) * grid.columns + x0;
				for (uint32 k = grid.cellStart[cell]; k < grid.cellStart[cell + 1]; ++k)
				{
					TestSegment(samples, grid.segments[k], x, z, best);
				}
			}
		}

		return FinishProjection(best, maxAcross, distance, across);
	}

	void BuildSectionPlacements(const SectionPathShape& shape, const std::function<float(float, float)>& ground, std::vector<Placement>& out)
	{
		out.clear();
		if (!shape.path || shape.asset.variants.empty() || !(shape.asset.length > 0.0f))
		{
			return;
		}

		const SampledPath& path = *shape.path;
		const SectionAsset& asset = shape.asset;
		for (const SectionCopy& copy : shape.layout.copies)
		{
			const uint32 variantIndex = GetVariant(shape, copy);
			const SectionVariant& variant = asset.variants[variantIndex];
			const uint64 copySeed = Hash(shape.params.seed, static_cast<int64>(copy.spanKey), copy.copy, kPropSalt);
			const float stretch = copy.stretchedLength / asset.length;

			for (const SectionPlacement& source : variant.placements)
			{
				const PropRule* rule = FindRule(asset.rules, source.placement.asset);
				const uint64 h = Hash(copySeed, static_cast<int64>(source.placement.key), static_cast<int64>(source.placement.kind));

				const float keep = (rule != nullptr ? rule->keepChance : 1.0f) * shape.params.propDensity;
				if (!(HashToUnit(Hash(h, 1)) < keep))
				{
					continue;
				}

				float localX = source.placement.position.x;
				float localZ = source.placement.position.z;
				float yaw = 0.0f;
				float scaleFactor = 1.0f;
				if (rule != nullptr)
				{
					const float jitter = std::fabs(rule->positionJitter);
					localX += HashRange(Hash(h, 2), -jitter, jitter);
					localZ += HashRange(Hash(h, 3), -jitter, jitter);
					const float yawJitter = std::fabs(rule->yawJitterDeg);
					yaw += HashRange(Hash(h, 4), -yawJitter, yawJitter) * kDegToRad;
					const float scaleJitter = std::fabs(rule->scaleJitter);
					scaleFactor = std::max(kMinScaleFactor, 1.0f + HashRange(Hash(h, 5), -scaleJitter, scaleJitter));
				}

				// Clamped to the span so a jittered prop never takes its frame from a neighbouring span; a corner copy
				// stays inside its own range around the corner point.
				const bool corner = copy.role != SectionVariantRole::Straight;
				const float spanStart = corner ? copy.startDistance : path.GetControlPointDistance(copy.span);
				const float spanEnd = corner ? copy.startDistance + copy.stretchedLength : path.GetControlPointDistance(copy.span + 1);
				const float s = std::clamp(copy.startDistance + localZ * stretch, spanStart, std::max(spanStart, spanEnd));
				const SectionFrame frame = FrameInRange(path, s, spanStart, spanEnd);
				yaw += std::atan2(frame.forwardX, frame.forwardZ);

				Placement placement = source.placement;
				placement.key = SectionPlacementKey(copy, variantIndex, source.placement.key, source.placement.kind);
				placement.position.x = frame.position.x + frame.rightX * localX;
				placement.position.z = frame.position.z + frame.rightZ * localX;
				const float groundHeight = ground ? ground(placement.position.x, placement.position.z) : CentreHeightAt(shape, s);
				placement.position.y = groundHeight + source.heightAboveTerrain;
				placement.rotation = Quaternion(Radian(yaw), Vector3::UnitY) * source.placement.rotation;
				placement.scale = source.placement.scale * scaleFactor;

				if (rule != nullptr && !rule->swapGroup.empty())
				{
					ApplySwap(*rule, Hash(h, 6), placement);
				}

				out.push_back(std::move(placement));
			}
		}

		BuildRunPlacements(shape, ground, out);

		// Keys are 64-bit hashes; a collision is practically impossible, but keep the first one deterministically.
		std::stable_sort(out.begin(), out.end(), [](const Placement& a, const Placement& b) { return a.key < b.key; });
		out.erase(std::unique(out.begin(), out.end(), [](const Placement& a, const Placement& b) { return a.key == b.key; }), out.end());
	}
}
