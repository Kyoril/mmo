// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "section_path.h"

#include "parallel_for.h"
#include "pcg_random.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
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

		/// Cell size of the nearest-segment grid (m).
		constexpr float kNearestCell = 8.0f;
		/// Consecutive segments per bounding box when the nearest-segment grid is built.
		constexpr size_t kNearestChunk = 16;
		/// Slack on the candidate distance, against float rounding (extra candidates never change a result).
		constexpr double kNearestSlack = 0.01;

		struct Box
		{
			double minX = 0.0, minZ = 0.0, maxX = 0.0, maxZ = 0.0;
		};

		double BoxPointDistanceSq(const Box& box, const double x, const double z)
		{
			const double dx = std::max({ box.minX - x, 0.0, x - box.maxX });
			const double dz = std::max({ box.minZ - z, 0.0, z - box.maxZ });
			return dx * dx + dz * dz;
		}

		double BoxBoxDistanceSq(const Box& a, const Box& b)
		{
			const double dx = std::max({ a.minX - b.maxX, 0.0, b.minX - a.maxX });
			const double dz = std::max({ a.minZ - b.maxZ, 0.0, b.minZ - a.maxZ });
			return dx * dx + dz * dz;
		}

		/// Squared distance from a point to the farthest corner of a box: an upper bound of the distance from any
		/// point of the box to that point.
		double FarCornerDistanceSq(const Box& box, const double x, const double z)
		{
			const double dx = std::max(std::fabs(x - box.minX), std::fabs(x - box.maxX));
			const double dz = std::max(std::fabs(z - box.minZ), std::fabs(z - box.maxZ));
			return dx * dx + dz * dz;
		}

		double SegmentPointDistanceSq(const double ax, const double az, const double bx, const double bz, const double x, const double z)
		{
			const double sx = bx - ax;
			const double sz = bz - az;
			const double lenSq = sx * sx + sz * sz;
			const double t = lenSq > 0.0 ? std::clamp(((x - ax) * sx + (z - az) * sz) / lenSq, 0.0, 1.0) : 0.0;
			const double px = x - (ax + sx * t);
			const double pz = z - (az + sz * t);
			return px * px + pz * pz;
		}

		/// Liang-Barsky: true if the segment touches the box.
		bool SegmentTouchesBox(const Box& box, const double ax, const double az, const double bx, const double bz)
		{
			double t0 = 0.0;
			double t1 = 1.0;
			const double dx = bx - ax;
			const double dz = bz - az;
			const double p[4] = { -dx, dx, -dz, dz };
			const double q[4] = { ax - box.minX, box.maxX - ax, az - box.minZ, box.maxZ - az };
			for (int i = 0; i < 4; ++i)
			{
				if (p[i] == 0.0)
				{
					if (q[i] < 0.0)
					{
						return false;
					}
					continue;
				}
				const double r = q[i] / p[i];
				if (p[i] < 0.0)
				{
					t0 = std::max(t0, r);
				}
				else
				{
					t1 = std::min(t1, r);
				}
				if (t0 > t1)
				{
					return false;
				}
			}
			return true;
		}

		/// Squared distance between a box and a segment (0 if they touch): attained at a vertex of one of them.
		double BoxSegmentDistanceSq(const Box& box, const double ax, const double az, const double bx, const double bz)
		{
			if (SegmentTouchesBox(box, ax, az, bx, bz))
			{
				return 0.0;
			}
			double best = std::min(BoxPointDistanceSq(box, ax, az), BoxPointDistanceSq(box, bx, bz));
			best = std::min(best, SegmentPointDistanceSq(ax, az, bx, bz, box.minX, box.minZ));
			best = std::min(best, SegmentPointDistanceSq(ax, az, bx, bz, box.maxX, box.minZ));
			best = std::min(best, SegmentPointDistanceSq(ax, az, bx, bz, box.minX, box.maxZ));
			best = std::min(best, SegmentPointDistanceSq(ax, az, bx, bz, box.maxX, box.maxZ));
			return best;
		}

		void BuildNearestGrid(const SampledPath& path, const float reach, SectionNearestGrid& grid)
		{
			grid = SectionNearestGrid();
			const std::vector<SplineSample>& samples = path.GetSamples();
			const size_t n = samples.size();
			if (n < 2 || !(reach > 0.0f))
			{
				return;
			}

			const size_t segments = path.IsClosed() ? n : n - 1;
			float minX = 0.0f, minZ = 0.0f, maxX = 0.0f, maxZ = 0.0f;
			path.GetBounds(minX, minZ, maxX, maxZ);
			grid.cellSize = kNearestCell;
			grid.reach = reach;
			grid.originX = minX - (reach + kNearestCell);
			grid.originZ = minZ - (reach + kNearestCell);
			grid.columns = static_cast<int32>(std::ceil((maxX + reach + kNearestCell - grid.originX) / kNearestCell)) + 1;
			grid.rows = static_cast<int32>(std::ceil((maxZ + reach + kNearestCell - grid.originZ) / kNearestCell)) + 1;

			// Chunks of consecutive segments with their bounds, and probe points (segment ends) for the upper bound.
			std::vector<Box> chunks;
			for (size_t first = 0; first < segments; first += kNearestChunk)
			{
				Box box{ samples[first].x, samples[first].z, samples[first].x, samples[first].z };
				const size_t last = std::min(segments, first + kNearestChunk);
				for (size_t i = first; i < last; ++i)
				{
					const SplineSample& b = samples[(i + 1) % n];
					box.minX = std::min(box.minX, static_cast<double>(b.x));
					box.minZ = std::min(box.minZ, static_cast<double>(b.z));
					box.maxX = std::max(box.maxX, static_cast<double>(b.x));
					box.maxZ = std::max(box.maxZ, static_cast<double>(b.z));
				}
				chunks.push_back(box);
			}
			std::vector<size_t> probes;
			for (size_t i = 0; i < n; i += kNearestChunk)
			{
				probes.push_back(i);
			}
			probes.push_back(n - 1);

			const Box pathBox{ minX, minZ, maxX, maxZ };
			const double reachSq = (reach + kNearestSlack) * (reach + kNearestSlack);

			// Rows are independent: each collects its cells' candidates on some thread, then they are joined in row order.
			const size_t columns = static_cast<size_t>(grid.columns);
			std::vector<std::vector<uint32>> rowSegments(static_cast<size_t>(grid.rows));
			std::vector<std::vector<uint32>> rowCounts(static_cast<size_t>(grid.rows), std::vector<uint32>(columns, 0));
			ParallelFor(static_cast<size_t>(grid.rows), HardwareWorkers(), [&](size_t, const size_t row)
			{
				const int32 cz = static_cast<int32>(row);
				std::vector<uint32>& found = rowSegments[row];
				for (int32 cx = 0; cx < grid.columns; ++cx)
				{
					const size_t before = found.size();
					const Box box{ grid.originX + static_cast<double>(cx) * kNearestCell, grid.originZ + static_cast<double>(cz) * kNearestCell,
						grid.originX + static_cast<double>(cx + 1) * kNearestCell, grid.originZ + static_cast<double>(cz + 1) * kNearestCell };
					if (BoxBoxDistanceSq(box, pathBox) > reachSq)
					{
						continue;
					}

					// Every point of the cell is at most `upper` from the path (the distance to a probe's far corner).
					double upperSq = std::numeric_limits<double>::max();
					for (const size_t p : probes)
					{
						upperSq = std::min(upperSq, FarCornerDistanceSq(box, samples[p].x, samples[p].z));
					}
					const double limit = std::min(std::sqrt(upperSq), static_cast<double>(reach)) + kNearestSlack;
					const double limitSq = limit * limit;

					for (size_t c = 0; c < chunks.size(); ++c)
					{
						if (BoxBoxDistanceSq(box, chunks[c]) > limitSq)
						{
							continue;
						}
						const size_t last = std::min(segments, (c + 1) * kNearestChunk);
						for (size_t i = c * kNearestChunk; i < last; ++i)
						{
							const SplineSample& a = samples[i];
							const SplineSample& b = samples[(i + 1) % n];
							if (BoxSegmentDistanceSq(box, a.x, a.z, b.x, b.z) <= limitSq)
							{
								found.push_back(static_cast<uint32>(i));
							}
						}
					}
					rowCounts[row][static_cast<size_t>(cx)] = static_cast<uint32>(found.size() - before);
				}
			});

			const size_t cells = columns * static_cast<size_t>(grid.rows);
			grid.cellStart.assign(cells + 1, 0);
			for (size_t row = 0; row < rowSegments.size(); ++row)
			{
				for (size_t cx = 0; cx < columns; ++cx)
				{
					const size_t cell = row * columns + cx;
					grid.cellStart[cell + 1] = grid.cellStart[cell] + rowCounts[row][cx];
				}
				grid.segments.insert(grid.segments.end(), rowSegments[row].begin(), rowSegments[row].end());
			}
		}
	}

	SectionLayoutCounts CountSectionCopies(const SectionLayout& layout)
	{
		SectionLayoutCounts counts;
		for (const SectionCopy& copy : layout.copies)
		{
			switch (copy.role)
			{
			case SectionVariantRole::InnerCorner:
				++counts.inner;
				break;
			case SectionVariantRole::OuterCorner:
				++counts.outer;
				break;
			default:
				++counts.straight;
				break;
			}
		}
		return counts;
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
			float localZ = copy.stretchedLength > 0.0f
				? (distance - copy.startDistance) * shape.asset.length / copy.stretchedLength
				: 0.0f;
			const SectionCopyFacade facade = GetCopyFacade(shape, copy);
			if (facade.mirrored)
			{
				localZ = shape.asset.length - localZ;
			}
			out.height = SampleHeight(variant.heights, across, localZ);
			if (facade.heightScale != 1.0f)
			{
				// Eased to 1 (smoothstep) over a quarter of the length at both ends, so neighbouring copies with different
				// scales meet at the same height.
				constexpr float easeFraction = 0.25f;
				const float t = std::clamp(std::min(localZ, shape.asset.length - localZ) / (easeFraction * shape.asset.length), 0.0f, 1.0f);
				out.height *= 1.0f + (facade.heightScale - 1.0f) * t * t * (3.0f - 2.0f * t);
			}
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

		constexpr uint64 kMassSalt = 0x4d415353u;
		constexpr uint64 kMirrorSalt = 0x4d495252u;
		constexpr uint64 kHeightScaleSalt = 0x48534341u;
		/// The mass weight falls to 0 over this last fraction of the mass depth.
		constexpr float kMassFall = 0.3f;
		/// Section heights at least this high (m) count as the wall when the mass paint layer is chosen.
		constexpr float kWallHeight = 1.0f;
		/// Facade height jitter is clamped to this.
		constexpr float kMaxHeightJitter = 0.9f;
		/// Lift (m) of the mass over the section over which the section's paint hands over to the mass paint.
		constexpr float kPaintHandover = 1.0f;

		float FiniteOr(const float value, const float fallback)
		{
			return std::isfinite(value) ? value : fallback;
		}

		/// Clamps the mass and facade settings into their valid ranges; a mass without depth is switched off.
		void SanitiseMassParams(SectionPathParams& p)
		{
			const SectionPathParams defaults;
			p.massHeight = FiniteOr(p.massHeight, defaults.massHeight);
			p.massDepth = FiniteOr(p.massDepth, 0.0f);
			if (!(p.massDepth > 0.0f))
			{
				p.massEnabled = false;
			}
			p.massRise = std::clamp(FiniteOr(p.massRise, defaults.massRise), 0.01f, 1.0f - kMassFall);
			p.massNoiseAmplitude = std::max(0.0f, FiniteOr(p.massNoiseAmplitude, 0.0f));
			p.massNoiseWavelength = FiniteOr(p.massNoiseWavelength, 0.0f);
			p.massDetailAmplitude = std::max(0.0f, FiniteOr(p.massDetailAmplitude, 0.0f));
			p.massDetailWavelength = FiniteOr(p.massDetailWavelength, 0.0f);
			if (p.massPaintLayer < -1 || p.massPaintLayer > 3)
			{
				p.massPaintLayer = -1;
			}
			p.facadeHeightJitter = std::clamp(FiniteOr(p.facadeHeightJitter, 0.0f), 0.0f, kMaxHeightJitter);
		}

		/// The section layer the straight variants paint most where they are at least kWallHeight high, or -1.
		int8 DominantWallLayer(const SectionAsset& asset)
		{
			std::array<double, 4> sums{};
			for (const SectionVariant& variant : asset.variants)
			{
				if (variant.role != SectionVariantRole::Straight)
				{
					continue;
				}

				const SectionPaint& paint = variant.paint;
				const size_t count = std::min({ paint.packed.size(), paint.coverage.size(), static_cast<size_t>(paint.width) * paint.depth });
				for (size_t index = 0; index < count; ++index)
				{
					if (paint.coverage[index] == 0)
					{
						continue;
					}
					const uint32 i = static_cast<uint32>(index % paint.width);
					const uint32 j = static_cast<uint32>(index / paint.width);
					const float x = static_cast<float>(paint.originX + static_cast<int32>(i)) * paint.spacing;
					const float z = static_cast<float>(paint.originZ + static_cast<int32>(j)) * paint.spacing;
					if (!(SampleHeight(variant.heights, x, z) >= kWallHeight))
					{
						continue;
					}
					for (size_t layer = 0; layer < 4; ++layer)
					{
						sums[layer] += static_cast<double>((paint.packed[index] >> (layer * 8)) & 0xFFu) * paint.coverage[index];
					}
				}
			}

			int8 best = -1;
			for (size_t layer = 0; layer < 4; ++layer)
			{
				if (sums[layer] > 0.0 && (best < 0 || sums[layer] > sums[static_cast<size_t>(best)]))
				{
					best = static_cast<int8>(layer);
				}
			}
			return best;
		}
	}

	bool PrepareSectionPath(const PathSpline& spline, const std::vector<uint64>& pointIds, const SectionAsset& asset,
		const SectionPathParams& params, const std::function<float(float, float)>& baseGround, SectionPathShape& out, String& error,
		const bool buildLookups)
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
		if (params.heightMode == SectionHeightMode::KeepGround && params.massEnabled)
		{
			error = "keepGround cannot be combined with the mass layer";
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
		SanitiseMassParams(out.params);

		if (out.params.massEnabled)
		{
			out.massReach = std::max(0.0f, out.params.massDepth - asset.halfWidth);
			out.massSeed = Hash(out.params.seed, 0, 0, kMassSalt);
			out.massLayer = out.params.massPaintLayer >= 0 ? out.params.massPaintLayer : DominantWallLayer(asset);
			if (buildLookups && out.massReach > asset.halfWidth + asset.sideFalloff)
			{
				BuildNearestGrid(*out.path, out.massReach, out.nearestGrid);
			}
		}

		const std::vector<SplineSample>& samples = out.path->GetSamples();
		out.centreHeights.reserve(samples.size());
		for (const SplineSample& sample : samples)
		{
			// keepGround never writes the height; the value is only read when props are laid without a ground function.
			const bool useSpline = params.heightMode == SectionHeightMode::FollowSpline || (params.heightMode == SectionHeightMode::KeepGround && !baseGround);
			out.centreHeights.push_back(useSpline ? sample.y : baseGround(sample.x, sample.z));
		}
		if (params.heightMode == SectionHeightMode::SmoothedGround)
		{
			out.params.groundSmoothing = params.groundSmoothing > 0.0f ? params.groundSmoothing : 0.0f;
			SmoothAlongPath(*out.path, out.params.groundSmoothing, out.centreHeights);
		}

		// Influence: everything a terrain sample or a prop can reach from the path.
		float reach = std::max(asset.halfWidth + asset.sideFalloff, out.massReach);
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

	SectionCopyFacade GetCopyFacade(const SectionPathShape& shape, const SectionCopy& copy)
	{
		SectionCopyFacade facade;
		const SectionPathParams& params = shape.params;
		// Corner pieces are laid as authored: their apex and turn are not symmetric in general.
		if (params.mirrorCopies && copy.role == SectionVariantRole::Straight)
		{
			facade.mirrored = HashToUnit(Hash(params.seed, static_cast<int64>(copy.spanKey), copy.copy, kMirrorSalt)) < 0.5f;
		}
		if (params.facadeHeightJitter > 0.0f)
		{
			const float j = params.facadeHeightJitter;
			facade.heightScale = 1.0f + HashRange(Hash(params.seed, static_cast<int64>(copy.spanKey), copy.copy, kHeightScaleSalt), -j, j);
		}
		return facade;
	}

	void GetSectionCopyRects(const SectionPathShape& shape, std::vector<WorldRect>& out)
	{
		out.clear();
		if (!shape.path)
		{
			return;
		}

		// GetInfluenceRect grows the path bounds by one amount on every side; recover it.
		float pathMinX = 0.0f, pathMinZ = 0.0f, pathMaxX = 0.0f, pathMaxZ = 0.0f;
		shape.path->GetBounds(pathMinX, pathMinZ, pathMaxX, pathMaxZ);
		const WorldRect influence = GetInfluenceRect(shape);
		const float grow = std::max(0.0f, pathMinX - influence.minX);

		const std::vector<SplineSample>& samples = shape.path->GetSamples();
		for (const SectionCopy& copy : shape.layout.copies)
		{
			const float begin = copy.startDistance;
			const float end = copy.startDistance + copy.stretchedLength;

			const SplineSample first = shape.path->SampleAtDistance(begin);
			const SplineSample last = shape.path->SampleAtDistance(end);
			WorldRect rect{ std::min(first.x, last.x), std::min(first.z, last.z), std::max(first.x, last.x), std::max(first.z, last.z) };

			const auto from = std::lower_bound(samples.begin(), samples.end(), begin,
				[](const SplineSample& sample, const float distance) { return sample.distance < distance; });
			const auto include = [&rect](const SplineSample& sample)
			{
				rect.minX = std::min(rect.minX, sample.x);
				rect.minZ = std::min(rect.minZ, sample.z);
				rect.maxX = std::max(rect.maxX, sample.x);
				rect.maxZ = std::max(rect.maxZ, sample.z);
			};
			for (auto it = from; it != samples.end() && it->distance <= end; ++it)
			{
				include(*it);
			}

			// On a loop the corner copy at the first point wraps past the path end into its start.
			const float loopLength = shape.layout.loopLength;
			if (loopLength > 0.0f && end > loopLength)
			{
				for (auto it = samples.begin(); it != samples.end() && it->distance <= end - loopLength; ++it)
				{
					include(*it);
				}
			}

			rect.minX -= grow;
			rect.minZ -= grow;
			rect.maxX += grow;
			rect.maxZ += grow;
			out.push_back(rect);
		}
	}

	namespace
	{
		bool ParseFloatText(const String& text, float& out)
		{
			char* end = nullptr;
			const float value = std::strtof(text.c_str(), &end);
			if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(value))
			{
				return false;
			}
			out = value;
			return true;
		}

		bool ParseBoolText(const String& text, bool& out)
		{
			if (text == "1" || text == "true")
			{
				out = true;
				return true;
			}
			if (text == "0" || text == "false")
			{
				out = false;
				return true;
			}
			return false;
		}
	}

	bool SetSectionPathParam(SectionPathParams& params, const String& name, const String& value, String& error)
	{
		SectionPathParams p = params;
		bool known = true;
		bool parsed = false;

		const std::pair<const char*, float SectionPathParams::*> floats[] = {
			{ "joinBlend", &SectionPathParams::joinBlend },
			{ "propDensity", &SectionPathParams::propDensity },
			{ "groundSmoothing", &SectionPathParams::groundSmoothing },
			{ "massHeight", &SectionPathParams::massHeight },
			{ "massDepth", &SectionPathParams::massDepth },
			{ "massRise", &SectionPathParams::massRise },
			{ "massNoiseAmplitude", &SectionPathParams::massNoiseAmplitude },
			{ "massNoiseWavelength", &SectionPathParams::massNoiseWavelength },
			{ "massDetailAmplitude", &SectionPathParams::massDetailAmplitude },
			{ "massDetailWavelength", &SectionPathParams::massDetailWavelength },
			{ "facadeHeightJitter", &SectionPathParams::facadeHeightJitter },
		};
		const std::pair<const char*, bool SectionPathParams::*> bools[] = {
			{ "sequentialVariants", &SectionPathParams::sequentialVariants },
			{ "floorInside", &SectionPathParams::floorInside },
			{ "massEnabled", &SectionPathParams::massEnabled },
			{ "mirrorCopies", &SectionPathParams::mirrorCopies },
		};

		const auto findFloat = std::find_if(std::begin(floats), std::end(floats), [&name](const auto& f) { return name == f.first; });
		const auto findBool = std::find_if(std::begin(bools), std::end(bools), [&name](const auto& b) { return name == b.first; });
		if (findFloat != std::end(floats))
		{
			parsed = ParseFloatText(value, p.*(findFloat->second));
		}
		else if (findBool != std::end(bools))
		{
			parsed = ParseBoolText(value, p.*(findBool->second));
		}
		else if (name == "seed")
		{
			char* end = nullptr;
			const unsigned long long seed = std::strtoull(value.c_str(), &end, 0);
			parsed = !value.empty() && value[0] != '-' && end == value.c_str() + value.size();
			p.seed = static_cast<uint64>(seed);
		}
		else if (name == "massPaintLayer")
		{
			char* end = nullptr;
			const long layer = std::strtol(value.c_str(), &end, 10);
			parsed = !value.empty() && end == value.c_str() + value.size() && layer >= -1 && layer <= 3;
			p.massPaintLayer = static_cast<int8>(layer);
		}
		else if (name == "heightMode")
		{
			parsed = true;
			if (value == "followSpline")
			{
				p.heightMode = SectionHeightMode::FollowSpline;
			}
			else if (value == "followGround")
			{
				p.heightMode = SectionHeightMode::FollowGround;
			}
			else if (value == "smoothedGround")
			{
				p.heightMode = SectionHeightMode::SmoothedGround;
			}
			else if (value == "keepGround")
			{
				p.heightMode = SectionHeightMode::KeepGround;
			}
			else
			{
				parsed = false;
			}
		}
		else
		{
			known = false;
		}

		if (!known)
		{
			error = "Unknown section path parameter '" + name + "'.";
			return false;
		}
		if (!parsed)
		{
			error = "Invalid value '" + value + "' for section path parameter '" + name + "'.";
			return false;
		}

		params = p;
		error.clear();
		return true;
	}

	namespace
	{
		/// The section's blended sample at a projected position: the copy containing the distance cross-faded with its
		/// neighbour within joinBlend of the nearer boundary. False if the distance lies outside every copy.
		bool SampleSection(const SectionPathShape& shape, float& distance, const float across, CopySample& sample)
		{
			const SampledPath& path = *shape.path;
			const SectionAsset& asset = shape.asset;

			// A closed loop whose first point has a corner: the start of the path belongs to the copy wrapping past its end.
			if (shape.layout.loopLength > 0.0f && distance < shape.layout.copies.front().startDistance)
			{
				distance += shape.layout.loopLength;
			}

			float localZ = 0.0f;
			const SectionCopy* found = FindCopy(shape.layout, distance, asset.length, localZ);
			if (found == nullptr)
			{
				return false;
			}

			const std::vector<SectionCopy>& copies = shape.layout.copies;
			const size_t count = copies.size();
			const size_t index = static_cast<size_t>(found - copies.data());
			const bool closed = path.IsClosed();
			const float length = path.GetLength();

			sample = SampleCopy(shape, *found, distance, across);

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
			return true;
		}

		/// The section's paint as a target splat (remapped, renormalised) at weight coverage x falloff.
		void PaintSection(const CopySample& sample, const float falloff, const std::array<int8, 4>& map, TerrainTarget& target)
		{
			const float paintWeight = sample.coverage / 255.0f * falloff;
			if (!(paintWeight > 0.0f))
			{
				return;
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

		/// Mass height above the centre line before the rise: massHeight plus the world-anchored noise, never negative.
		float MassAmplitude(const SectionPathShape& shape, const float x, const float z)
		{
			const SectionPathParams& p = shape.params;
			float h = p.massHeight;
			if (p.massNoiseAmplitude > 0.0f && p.massNoiseWavelength > 0.0f)
			{
				h += p.massNoiseAmplitude * FractalNoise(shape.massSeed, x, z, 1.0f / p.massNoiseWavelength, 4, 0.5f);
			}
			if (p.massDetailAmplitude > 0.0f && p.massDetailWavelength > 0.0f)
			{
				h += p.massDetailAmplitude * FractalNoise(Hash(shape.massSeed, 1), x, z, 1.0f / p.massDetailWavelength, 2, 0.5f);
			}
			return std::max(0.0f, h);
		}

		/// Slope (deg) below which the mass is not painted, and from which it is painted fully.
		constexpr float kMassPaintSlopeLow = 10.0f;
		constexpr float kMassPaintSlopeHigh = 30.0f;
		/// Finite difference step (m) of the mass noise gradient.
		constexpr float kMassGradientStep = 0.5f;
	}

	TerrainTarget EvaluateSectionTerrain(const SectionPathShape& shape, const float x, const float z, const float baseHeight, const LayerRemapFunction& remap)
	{
		TerrainTarget target;
		if (!shape.path || shape.layout.copies.empty() || shape.asset.variants.empty())
		{
			return target;
		}

		const SampledPath& path = *shape.path;
		const SectionAsset& asset = shape.asset;
		const bool mass = shape.params.massEnabled;
		const float sectionReach = asset.halfWidth + asset.sideFalloff;
		float distance = 0.0f;
		float across = 0.0f;
		if (!ProjectToSectionPath(shape, x, z, mass ? std::max(sectionReach, shape.massReach) : sectionReach, distance, across))
		{
			return target;
		}

		// Distance to the nearest path point: |across| along the path, radial past the ends of an open path.
		const SplineSample nearest = path.SampleAtDistance(distance);
		const float radialSq = (x - nearest.x) * (x - nearest.x) + (z - nearest.z) * (z - nearest.z);
		const float falloff = SideFalloff(asset, std::sqrt(radialSq));

		// Position across the mass: < 0 on the floor side, where the mass changes nothing.
		const float u = mass ? (across + asset.halfWidth) / shape.params.massDepth : -1.0f;
		if (!(u >= 0.0f))
		{
			if (falloff <= 0.0f)
			{
				return target;
			}

			CopySample sample;
			if (!SampleSection(shape, distance, across, sample))
			{
				return target;
			}

			target.height = CentreHeightAt(shape, distance) + sample.height;
			// keepGround only paints: the terrain keeps its height.
			target.heightWeight = shape.params.heightMode == SectionHeightMode::KeepGround ? 0.0f : falloff;
			target.setMaterial = false;
			std::array<int8, 4> map{ 0, 1, 2, 3 };
			if (remap && sample.coverage / 255.0f * falloff > 0.0f)
			{
				map = remap(x, z);
			}
			PaintSection(sample, falloff, map, target);
			return target;
		}

		// Mass: rises over massRise of the depth, holds, and its weight falls over the last kMassFall of it; past the ends of an
		// open path its height and weight taper out over halfWidth + sideFalloff, like the section's own end.
		const SectionPathParams& p = shape.params;
		const float rise = Smooth(u / p.massRise);
		float weight = u >= 1.0f ? 0.0f : 1.0f - Smooth((u - (1.0f - kMassFall)) / kMassFall);
		float taper = 1.0f;
		const float length = path.GetLength();
		if (!path.IsClosed() && (distance <= 0.0f || distance >= length))
		{
			const float beyond = std::sqrt(std::max(0.0f, radialSq - across * across));
			taper = 1.0f - Smooth(beyond / sectionReach);
			weight *= taper;
		}
		if (falloff <= 0.0f && weight <= 0.0f)
		{
			return target;
		}

		const float amplitude = MassAmplitude(shape, x, z) * taper;
		const float massHeight = rise * amplitude;

		CopySample sample;
		const float sectionFalloff = falloff > 0.0f && SampleSection(shape, distance, across, sample) ? falloff : 0.0f;
		const float centre = CentreHeightAt(shape, distance);

		// The section and the mass as Apply would write them, combined so that the mass only raises the ground.
		const float section = baseHeight + (centre + sample.height - baseHeight) * sectionFalloff;
		const float massTarget = baseHeight + (centre + massHeight - baseHeight) * weight;
		target.height = section + rise * std::max(0.0f, massTarget - section);
		// The height already is the blend of base, section and mass; weight 1 writes it as is (a height equal to the
		// base is skipped by Apply, so the mass never touches what it does not raise).
		target.heightWeight = 1.0f;
		target.setMaterial = false;

		// Paint: the section's paint hands over to the mass paint as the mass lifts the ground above the section, over
		// kPaintHandover metres of lift. Measured over the centre line (base-free, as paint must not depend on the base).
		const float lift = rise * std::max(0.0f, massHeight * weight - sample.height * sectionFalloff);
		const float handover = Smooth(lift / kPaintHandover);
		const float sectionPaint = sectionFalloff > 0.0f ? sample.coverage / 255.0f * sectionFalloff * (1.0f - handover) : 0.0f;
		std::array<int8, 4> map{ 0, 1, 2, 3 };
		const bool massPaints = shape.massLayer >= 0 && weight > 0.0f && handover > 0.0f;
		if (remap && (sectionPaint > 0.0f || massPaints))
		{
			map = remap(x, z);
		}
		if (!massPaints)
		{
			if (sectionPaint > 0.0f)
			{
				PaintSection(sample, sectionFalloff * (1.0f - handover), map, target);
			}
			return target;
		}

		// Gradient of the mass surface: the rise across the path plus the noise along both axes.
		const float t = u / p.massRise;
		const float riseSlope = t > 0.0f && t < 1.0f ? 6.0f * t * (1.0f - t) / (p.massRise * p.massDepth) : 0.0f;
		const SectionFrame frame = FrameAt(path, distance);
		const float e = kMassGradientStep;
		const float dx = (MassAmplitude(shape, x + e, z) - MassAmplitude(shape, x - e, z)) / (2.0f * e);
		const float dz = (MassAmplitude(shape, x, z + e) - MassAmplitude(shape, x, z - e)) / (2.0f * e);
		const float gx = rise * dx + amplitude * riseSlope * frame.rightX;
		const float gz = rise * dz + amplitude * riseSlope * frame.rightZ;
		const float slopeDeg = std::atan(std::sqrt(gx * gx + gz * gz)) * kRadToDeg;
		const int8 to = map[static_cast<size_t>(shape.massLayer)];
		const float massPaint = to >= 0 && to <= 3
			? weight * handover * Smooth((slopeDeg - kMassPaintSlopeLow) / (kMassPaintSlopeHigh - kMassPaintSlopeLow))
			: 0.0f;

		// Both paints blended by their weights.
		std::array<float, 4> tile{};
		float total = 0.0f;
		if (sectionPaint > 0.0f)
		{
			float sum = 0.0f;
			std::array<float, 4> own{};
			for (size_t layer = 0; layer < 4; ++layer)
			{
				const int8 into = map[layer];
				if (into >= 0 && into <= 3 && sample.weights[layer] > 0.0f)
				{
					own[static_cast<size_t>(into)] += sample.weights[layer];
					sum += sample.weights[layer];
				}
			}
			if (sum > 0.0f)
			{
				for (size_t i = 0; i < 4; ++i)
				{
					tile[i] += own[i] / sum * sectionPaint;
				}
				total += sectionPaint;
			}
		}
		if (massPaint > 0.0f)
		{
			tile[static_cast<size_t>(to)] += massPaint;
			total += massPaint;
		}
		if (total > 0.0f)
		{
			target.splat = NormalizeTo255(tile, total);
			target.splatWeight = std::min(1.0f, total);
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
			// Far queries (the mass layer): the candidates of the cell are the only segments that can be nearest.
			const SectionNearestGrid& near = shape.nearestGrid;
			if (near.columns <= 0 || near.rows <= 0 || !(maxAcross <= near.reach))
			{
				return ProjectToPath(*shape.path, x, z, maxAcross, distance, across);
			}

			const float fx = CellCoordinate(x, near.originX, near.cellSize);
			const float fz = CellCoordinate(z, near.originZ, near.cellSize);
			if (!(fx >= 0.0f && fx < static_cast<float>(near.columns) && fz >= 0.0f && fz < static_cast<float>(near.rows)))
			{
				// Outside the grid every point is further than its reach from the path.
				return false;
			}

			const size_t cell = static_cast<size_t>(fz) * near.columns + static_cast<size_t>(fx);
			const std::vector<SplineSample>& samples = shape.path->GetSamples();
			Projection best;
			for (uint32 k = near.cellStart[cell]; k < near.cellStart[cell + 1]; ++k)
			{
				TestSegment(samples, near.segments[k], x, z, best);
			}
			return FinishProjection(best, maxAcross, distance, across);
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
			const bool mirrored = GetCopyFacade(shape, copy).mirrored;

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
				float localZ = mirrored ? asset.length - source.placement.position.z : source.placement.position.z;
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
				// A mirrored copy reflects the authored rotation across its local XY plane: (w, x, y, z) -> (w, -x, -y, z).
				const Quaternion& authored = source.placement.rotation;
				const Quaternion local = mirrored ? Quaternion(authored.w, -authored.x, -authored.y, authored.z) : authored;
				placement.rotation = Quaternion(Radian(yaw), Vector3::UnitY) * local;
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
