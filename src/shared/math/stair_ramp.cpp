// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "stair_ramp.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace mmo
{
	namespace
	{
		/// Flat enough to be a tread: the angle to the vertical is below ~25 degrees.
		constexpr float TreadMinNormalY = 0.9f;

		/// Faces facing the stair's sides more than this are stringers or walls and are kept.
		constexpr float SideFacingDot = 0.7f;

		/// Faces reaching to within this distance below the ramp belong to the steps.
		constexpr float StepTolerance = 0.5f;

		/// Treads within this height of the highest one form the top landing.
		constexpr float LandingTolerance = 0.05f;

		/// Slack around the treads' footprint when deciding which faces belong to the steps.
		constexpr float FootprintSlack = 0.01f;

		constexpr float MinSlopeDegrees = 10.0f;
		constexpr float MaxSlopeDegrees = 55.0f;

		constexpr float RadToDeg = 57.29577951308232f;

		/// Solves the 3x3 system m * x = r by Cramer's rule. Returns false if it is singular.
		bool Solve3(const double m[3][3], const double r[3], double x[3])
		{
			const auto det = [](const double a[3][3])
			{
				return a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
					- a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
					+ a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
			};

			const double d = det(m);
			if (std::abs(d) < 1e-12)
			{
				return false;
			}

			for (int column = 0; column < 3; ++column)
			{
				double replaced[3][3];
				for (int i = 0; i < 3; ++i)
				{
					for (int j = 0; j < 3; ++j)
					{
						replaced[i][j] = j == column ? r[i] : m[i][j];
					}
				}
				x[column] = det(replaced) / d;
			}
			return true;
		}
	}

	StairRampResult BuildStairRamp(const std::vector<Vector3>& vertices, const std::vector<uint32>& indices, const std::vector<uint16>& faceSubMeshes)
	{
		StairRampResult result;

		const size_t faceCount = indices.size() / 3;
		if (faceCount == 0 || vertices.empty())
		{
			result.message = "No collision geometry.";
			return result;
		}

		const bool hasSubMeshes = faceSubMeshes.size() == faceCount;

		float minY = std::numeric_limits<float>::max();
		for (const Vector3& v : vertices)
		{
			minY = std::min(minY, v.y);
		}

		// Treads: flat faces above the base, either winding
		std::vector<bool> isTread(faceCount, false);
		size_t treadCount = 0;
		double normal[3][3] = {};
		double right[3] = {};
		int upwardWound = 0;
		std::map<uint16, float> treadSubMeshArea;
		for (size_t f = 0; f < faceCount; ++f)
		{
			const Vector3& a = vertices[indices[f * 3 + 0]];
			const Vector3& b = vertices[indices[f * 3 + 1]];
			const Vector3& c = vertices[indices[f * 3 + 2]];
			const Vector3 n = (b - a).Cross(c - a);
			const float length = n.GetLength();
			if (length < 1e-6f)
			{
				continue;
			}

			const Vector3 centroid = (a + b + c) / 3.0f;
			if (std::abs(n.y) / length < TreadMinNormalY || centroid.y < minY + 0.05f)
			{
				continue;
			}

			isTread[f] = true;
			++treadCount;
			upwardWound += n.y > 0.0f ? 1 : -1;
			if (hasSubMeshes)
			{
				treadSubMeshArea[faceSubMeshes[f]] += length;
			}

			// Least squares for y = p0 * x + p1 * z + p2 over the treads' area. The triangle's exact
			// second moments (area / 12 * (sum of r r^T over its corners + s s^T, s the corner sum))
			// keep the fit independent of how a tread happens to be triangulated: centroids alone
			// correlate x and z within a split quad and tilt the run direction.
			const double area = length * 0.5;
			const double corners[3][3] = { { a.x, a.z, 1.0 }, { b.x, b.z, 1.0 }, { c.x, c.z, 1.0 } };
			double sum[3] = {};
			for (const auto& r : corners)
			{
				for (int i = 0; i < 3; ++i)
				{
					sum[i] += r[i];
				}
			}
			for (int i = 0; i < 3; ++i)
			{
				for (int j = 0; j < 3; ++j)
				{
					double second = sum[i] * sum[j];
					for (const auto& r : corners)
					{
						second += r[i] * r[j];
					}
					normal[i][j] += area / 12.0 * second;
				}
				right[i] += area / 3.0 * sum[i] * centroid.y;
			}
		}

		double plane[3];
		if (treadCount == 0)
		{
			result.message = "No treads found (flat, upward facing faces above the base).";
			return result;
		}
		if (!Solve3(normal, right, plane))
		{
			result.message = "The treads do not rise along one direction - not a straight staircase.";
			return result;
		}

		const double gradient = std::sqrt(plane[0] * plane[0] + plane[1] * plane[1]);
		result.slopeDegrees = static_cast<float>(std::atan(gradient) * RadToDeg);
		if (result.slopeDegrees < MinSlopeDegrees || result.slopeDegrees > MaxSlopeDegrees)
		{
			result.message = "The treads rise at " + std::to_string(static_cast<int>(std::round(result.slopeDegrees))) +
				" degrees - not a straight staircase (10 to 55 degrees).";
			return result;
		}

		// Run direction (uphill) and side direction in the XZ plane
		const float runX = static_cast<float>(plane[0] / gradient);
		const float runZ = static_cast<float>(plane[1] / gradient);
		const auto along = [runX, runZ](const Vector3& v) { return v.x * runX + v.z * runZ; };
		const auto across = [runX, runZ](const Vector3& v) { return -v.x * runZ + v.z * runX; };

		// Extent of the stairs: the whole geometry along the run, the treads across it
		float bottom = std::numeric_limits<float>::max();
		for (const Vector3& v : vertices)
		{
			bottom = std::min(bottom, along(v));
		}

		float topHeight = std::numeric_limits<float>::lowest();
		float side0 = std::numeric_limits<float>::max();
		float side1 = std::numeric_limits<float>::lowest();
		for (size_t f = 0; f < faceCount; ++f)
		{
			if (!isTread[f])
			{
				continue;
			}
			for (int k = 0; k < 3; ++k)
			{
				const Vector3& v = vertices[indices[f * 3 + k]];
				topHeight = std::max(topHeight, v.y);
				side0 = std::min(side0, across(v));
				side1 = std::max(side1, across(v));
			}
		}

		// The ramp reaches the top where the topmost tread (the landing) begins
		float top = std::numeric_limits<float>::max();
		for (size_t f = 0; f < faceCount; ++f)
		{
			if (!isTread[f])
			{
				continue;
			}
			for (int k = 0; k < 3; ++k)
			{
				const Vector3& v = vertices[indices[f * 3 + k]];
				if (v.y >= topHeight - LandingTolerance)
				{
					top = std::min(top, along(v));
				}
			}
		}

		if (top <= bottom + 0.1f)
		{
			result.message = "The stairs have no run between their foot and their top tread.";
			return result;
		}

		const float rise = topHeight - minY;
		const auto rampHeight = [&](const float t)
		{
			return minY + rise * std::clamp((t - bottom) / (top - bottom), 0.0f, 1.0f);
		};
		result.slopeDegrees = std::atan(rise / (top - bottom)) * RadToDeg;

		// Keep everything that is not part of the steps
		uint16 rampSubMesh = 0;
		float bestArea = -1.0f;
		for (const auto& [subMesh, area] : treadSubMeshArea)
		{
			if (area > bestArea)
			{
				bestArea = area;
				rampSubMesh = subMesh;
			}
		}

		std::vector<uint32> used(vertices.size(), std::numeric_limits<uint32>::max());
		const auto keepVertex = [&](const uint32 index)
		{
			if (used[index] == std::numeric_limits<uint32>::max())
			{
				used[index] = static_cast<uint32>(result.vertices.size());
				result.vertices.push_back(vertices[index]);
			}
			return used[index];
		};

		for (size_t f = 0; f < faceCount; ++f)
		{
			const Vector3& a = vertices[indices[f * 3 + 0]];
			const Vector3& b = vertices[indices[f * 3 + 1]];
			const Vector3& c = vertices[indices[f * 3 + 2]];

			bool partOfSteps = true;
			float maxY = std::numeric_limits<float>::lowest();
			for (const Vector3* v : { &a, &b, &c })
			{
				const float t = along(*v);
				const float u = across(*v);
				partOfSteps = partOfSteps && t >= bottom - FootprintSlack && t <= top + FootprintSlack &&
					u >= side0 - FootprintSlack && u <= side1 + FootprintSlack;
				maxY = std::max(maxY, v->y);
			}

			const Vector3 n = (b - a).Cross(c - a);
			const float length = n.GetLength();
			if (partOfSteps && length > 1e-6f)
			{
				const float sideFacing = std::abs(-n.x * runZ + n.z * runX) / length;
				const float t = along((a + b + c) / 3.0f);
				partOfSteps = sideFacing < SideFacingDot && maxY >= rampHeight(t) - StepTolerance;
			}

			if (partOfSteps)
			{
				++result.removedFaces;
				continue;
			}

			for (int k = 0; k < 3; ++k)
			{
				result.indices.push_back(keepVertex(indices[f * 3 + k]));
			}
			if (hasSubMeshes)
			{
				result.faceSubMeshes.push_back(faceSubMeshes[f]);
			}
		}

		// The ramp: a quad from the foot to the top tread across the treads' width
		const auto corner = [&](const float t, const float u)
		{
			return Vector3(t * runX - u * runZ, rampHeight(t), t * runZ + u * runX);
		};
		const uint32 base = static_cast<uint32>(result.vertices.size());
		result.vertices.push_back(corner(bottom, side0));
		result.vertices.push_back(corner(bottom, side1));
		result.vertices.push_back(corner(top, side1));
		result.vertices.push_back(corner(top, side0));

		// Wind it like the treads
		const Vector3 rampNormal = (result.vertices[base + 1] - result.vertices[base]).Cross(result.vertices[base + 2] - result.vertices[base]);
		const bool flip = (rampNormal.y > 0.0f) != (upwardWound >= 0);
		const uint32 quad[2][6] = { { 0, 1, 2, 0, 2, 3 }, { 0, 2, 1, 0, 3, 2 } };
		for (const uint32 index : quad[flip ? 1 : 0])
		{
			result.indices.push_back(base + index);
		}
		if (hasSubMeshes)
		{
			result.faceSubMeshes.push_back(rampSubMesh);
			result.faceSubMeshes.push_back(rampSubMesh);
		}

		result.success = true;
		result.message = "Replaced " + std::to_string(result.removedFaces) + " step faces with a " +
			std::to_string(static_cast<int>(std::round(result.slopeDegrees))) + " degree ramp.";
		return result;
	}
}
