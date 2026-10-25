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
		if (!(asset.length > 0.0f) || !(asset.halfWidth > 0.0f) || !(asset.sideFalloff >= 0.0f))
		{
			error = "The section needs a positive length and half width and a non-negative side falloff.";
			return false;
		}
		if (params.heightMode == SectionHeightMode::FollowGround && !baseGround)
		{
			error = "Follow-ground mode needs the base ground.";
			return false;
		}

		out.path.emplace(spline, kSectionPathSampleStep);
		if (!(out.path->GetLength() > 0.0f))
		{
			out = SectionPathShape();
			error = "The section path has no length.";
			return false;
		}

		out.layout = LayOutSections(spline, *out.path, pointIds, asset.length);
		if (out.layout.copies.empty())
		{
			out = SectionPathShape();
			error = "No section copy fits the path.";
			return false;
		}

		out.asset = asset;
		out.params = params;
		out.params.joinBlend = params.joinBlend > 0.0f ? params.joinBlend : 0.0f;
		out.params.propDensity = params.propDensity > 0.0f ? std::min(params.propDensity, 1.0f) : 0.0f;

		const std::vector<SplineSample>& samples = out.path->GetSamples();
		out.centreHeights.reserve(samples.size());
		for (const SplineSample& sample : samples)
		{
			out.centreHeights.push_back(params.heightMode == SectionHeightMode::FollowGround ? baseGround(sample.x, sample.z) : sample.y);
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
		const size_t count = shape.asset.variants.size();
		if (count == 0)
		{
			return 0;
		}

		if (shape.params.sequentialVariants)
		{
			const std::vector<SectionCopy>& copies = shape.layout.copies;
			const auto it = std::lower_bound(copies.begin(), copies.end(), copy,
				[](const SectionCopy& a, const SectionCopy& b) { return a.span < b.span || (a.span == b.span && a.copy < b.copy); });
			const size_t position = (it != copies.end() && it->span == copy.span && it->copy == copy.copy)
				? static_cast<size_t>(it - copies.begin())
				: copy.copy;
			return static_cast<uint32>(position % count);
		}

		return static_cast<uint32>(Hash(shape.params.seed, static_cast<int64>(copy.spanKey), copy.copy, kVariantSalt) % count);
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
		if (!ProjectToPath(path, x, z, asset.halfWidth + asset.sideFalloff, distance, across))
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
			const uint64 copyKey = Hash(copy.spanKey, copy.copy, variantIndex, kKeySalt);
			const float stretch = copy.stretchedLength / asset.length;

			for (size_t i = 0; i < variant.placements.size(); ++i)
			{
				const SectionPlacement& source = variant.placements[i];
				const PropRule* rule = FindRule(asset.rules, source.placement.asset);
				const uint64 h = Hash(copySeed, static_cast<int64>(i));

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

				const SectionFrame frame = FrameAt(path, copy.startDistance + localZ * stretch);
				yaw += std::atan2(frame.forwardX, frame.forwardZ);

				Placement placement = source.placement;
				placement.key = Hash(copyKey, static_cast<int64>(i), 0, kKeySalt);
				placement.position.x = frame.position.x + frame.rightX * localX;
				placement.position.z = frame.position.z + frame.rightZ * localX;
				const float groundHeight = ground ? ground(placement.position.x, placement.position.z) : frame.position.y;
				placement.position.y = groundHeight + source.heightAboveTerrain;
				placement.rotation = Quaternion(Radian(yaw), Vector3::UnitY) * source.placement.rotation;
				placement.scale = source.placement.scale * scaleFactor;

				if (rule != nullptr && !rule->swapGroup.empty())
				{
					const String& swapped = rule->swapGroup[Hash(h, 6) % rule->swapGroup.size()];
					if (swapped != placement.asset)
					{
						placement.asset = swapped;
						placement.materialOverrides.clear();
						placement.kitPaths.clear();
					}
				}

				out.push_back(std::move(placement));
			}
		}

		// Keys are 64-bit hashes; a collision is practically impossible, but keep the first one deterministically.
		std::stable_sort(out.begin(), out.end(), [](const Placement& a, const Placement& b) { return a.key < b.key; });
		out.erase(std::unique(out.begin(), out.end(), [](const Placement& a, const Placement& b) { return a.key == b.key; }), out.end());
	}
}
