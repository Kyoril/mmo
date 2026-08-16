// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/ray.h"
#include "math/vector3.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mmo
{
	namespace terrain
	{
		/// Exact, front-to-back ray traversal of a uniform terrain height grid.
		///
		/// The terrain's outer-vertex lattice is uniform in world XZ, so the world-to-grid
		/// mapping inverts in closed form and the ray can walk exactly the cells it crosses
		/// instead of testing an approximated proxy surface. Every cell is a four-triangle
		/// fan around a stored inner vertex, matching how the terrain is rendered.
		///
		/// Everything in here is pure math over a caller-supplied sampler, so it carries no
		/// dependency on Terrain, Page or the renderer and is unit-testable headless.
		namespace raycast
		{
			/// Describes the uniform outer-vertex lattice the ray is walked over.
			struct GridParams
			{
				/// World units between two adjacent outer vertices.
				float cellSize = 1.0f;

				/// World X of global outer vertex 0.
				float originX = 0.0f;

				/// World Z of global outer vertex 0.
				float originZ = 0.0f;

				/// Number of cells (vertex quads) along X. Vertices are cellCountX + 1.
				int32 cellCountX = 0;

				/// Number of cells (vertex quads) along Z. Vertices are cellCountZ + 1.
				int32 cellCountZ = 0;
			};

			/// The four corner heights of one cell plus its stored inner vertex height.
			struct CellHeights
			{
				/// Corner heights ordered (x, z), (x+1, z), (x, z+1), (x+1, z+1).
				float corners[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

				/// Height of the inner (centre) vertex as stored, not as averaged.
				float inner = 0.0f;
			};

			/// Outcome of a grid raycast.
			struct Result
			{
				/// True when the ray hit the terrain surface.
				bool hit = false;

				/// Distance along the ray direction to the hit point.
				float distance = 0.0f;

				/// World-space hit position. Only meaningful when hit is true.
				Vector3 position = Vector3::Zero;

				/// Global cell indices of the cell that was hit.
				int32 cellX = 0;
				int32 cellZ = 0;
			};

			/// Moeller-Trumbore ray/triangle intersection, two-sided.
			/// @param ray The ray to test.
			/// @param v0 First triangle vertex.
			/// @param v1 Second triangle vertex.
			/// @param v2 Third triangle vertex.
			/// @param out_t Receives the hit distance along the ray direction on success.
			/// @returns True when the ray hits the triangle in front of its origin.
			inline bool IntersectsTriangle(const Ray& ray, const Vector3& v0, const Vector3& v1, const Vector3& v2, float& out_t)
			{
				constexpr float epsilon = 1e-6f;

				const Vector3 edge1 = v1 - v0;
				const Vector3 edge2 = v2 - v0;
				const Vector3 h = ray.GetDirection().Cross(edge2);
				const float a = edge1.Dot(h);

				if (std::abs(a) < epsilon)
				{
					// Ray is parallel to the triangle plane.
					return false;
				}

				const float f = 1.0f / a;
				const Vector3 s = ray.origin - v0;
				const float u = f * s.Dot(h);
				if (u < 0.0f || u > 1.0f)
				{
					return false;
				}

				const Vector3 q = s.Cross(edge1);
				const float v = f * ray.GetDirection().Dot(q);
				if (v < 0.0f || u + v > 1.0f)
				{
					return false;
				}

				const float t = f * edge2.Dot(q);
				if (t <= epsilon)
				{
					return false;
				}

				out_t = t;
				return true;
			}

			/// Clips a ray against the grid's XZ rectangle.
			/// @param ray The ray to clip.
			/// @param params The grid description.
			/// @param out_tEnter Receives the parametric distance at which the ray enters.
			/// @param out_tExit Receives the parametric distance at which the ray leaves.
			/// @returns True when the ray overlaps the rectangle within its own length.
			inline bool ClipToGridBounds(const Ray& ray, const GridParams& params, float& out_tEnter, float& out_tExit)
			{
				const float minX = params.originX;
				const float minZ = params.originZ;
				const float maxX = params.originX + static_cast<float>(params.cellCountX) * params.cellSize;
				const float maxZ = params.originZ + static_cast<float>(params.cellCountZ) * params.cellSize;

				float tEnter = 0.0f;
				float tExit = ray.GetLength();
				if (tExit <= 0.0f)
				{
					return false;
				}

				const Vector3& dir = ray.GetDirection();

				// Slab test on X and Z only; Y is unbounded because terrain height is not
				// known here. Height rejection happens per cell during traversal.
				const float originComponents[2] = { ray.origin.x, ray.origin.z };
				const float dirComponents[2] = { dir.x, dir.z };
				const float minComponents[2] = { minX, minZ };
				const float maxComponents[2] = { maxX, maxZ };

				for (int32 axis = 0; axis < 2; ++axis)
				{
					const float d = dirComponents[axis];
					const float o = originComponents[axis];

					if (std::abs(d) < 1e-9f)
					{
						// Parallel to this slab: either always inside or never.
						if (o < minComponents[axis] || o > maxComponents[axis])
						{
							return false;
						}

						continue;
					}

					const float invD = 1.0f / d;
					float t0 = (minComponents[axis] - o) * invD;
					float t1 = (maxComponents[axis] - o) * invD;
					if (t0 > t1)
					{
						std::swap(t0, t1);
					}

					tEnter = std::max(tEnter, t0);
					tExit = std::min(tExit, t1);

					if (tEnter > tExit)
					{
						return false;
					}
				}

				out_tEnter = tEnter;
				out_tExit = tExit;
				return true;
			}

			/// Walks the ray through the height grid front to back and returns the first hit.
			///
			/// The sampler is called at most once per crossed cell and has the signature
			/// `bool(int32 cellX, int32 cellZ, CellHeights& out)`. Returning false marks the
			/// cell as not testable (for example a page that is not resident) and the walk
			/// continues through it.
			///
			/// @param ray The ray in world space. Its length bounds the search.
			/// @param params The grid description.
			/// @param sampler Per-cell height provider.
			/// @returns The nearest hit, or a result with hit == false.
			template <typename CellSampler>
			Result RaycastHeightGrid(const Ray& ray, const GridParams& params, CellSampler&& sampler)
			{
				Result result;

				if (params.cellCountX <= 0 || params.cellCountZ <= 0 || params.cellSize <= 0.0f)
				{
					return result;
				}

				float tEnter = 0.0f;
				float tExit = 0.0f;
				if (!ClipToGridBounds(ray, params, tEnter, tExit))
				{
					return result;
				}

				const Vector3& dir = ray.GetDirection();
				const float invCellSize = 1.0f / params.cellSize;

				// Nudge into the interval so a ray entering exactly on a cell border lands in
				// the cell it is about to traverse rather than the one behind it.
				const float tStart = tEnter + 1e-4f * params.cellSize;
				const Vector3 entry = ray.origin + dir * std::min(tStart, tExit);

				const float gridX = (entry.x - params.originX) * invCellSize;
				const float gridZ = (entry.z - params.originZ) * invCellSize;

				int32 cellX = static_cast<int32>(std::floor(gridX));
				int32 cellZ = static_cast<int32>(std::floor(gridZ));
				cellX = std::clamp(cellX, 0, params.cellCountX - 1);
				cellZ = std::clamp(cellZ, 0, params.cellCountZ - 1);

				constexpr float infinity = std::numeric_limits<float>::max();

				int32 stepX = 0;
				float tMaxX = infinity;
				float tDeltaX = infinity;
				if (std::abs(dir.x) > 1e-9f)
				{
					stepX = (dir.x > 0.0f) ? 1 : -1;
					const float nextBorder = params.originX + static_cast<float>(cellX + (stepX > 0 ? 1 : 0)) * params.cellSize;
					tMaxX = (nextBorder - ray.origin.x) / dir.x;
					tDeltaX = params.cellSize / std::abs(dir.x);
				}

				int32 stepZ = 0;
				float tMaxZ = infinity;
				float tDeltaZ = infinity;
				if (std::abs(dir.z) > 1e-9f)
				{
					stepZ = (dir.z > 0.0f) ? 1 : -1;
					const float nextBorder = params.originZ + static_cast<float>(cellZ + (stepZ > 0 ? 1 : 0)) * params.cellSize;
					tMaxZ = (nextBorder - ray.origin.z) / dir.z;
					tDeltaZ = params.cellSize / std::abs(dir.z);
				}

				// A degenerate direction cannot outlive the grid diagonal; the bound only
				// guards against a walk that fails to advance.
				const int64 maxIterations = 2ll * (static_cast<int64>(params.cellCountX) + static_cast<int64>(params.cellCountZ)) + 8ll;

				float tCell = tEnter;

				for (int64 iteration = 0; iteration < maxIterations; ++iteration)
				{
					const float tCellExit = std::min(std::min(tMaxX, tMaxZ), tExit);

					CellHeights heights;
					if (sampler(cellX, cellZ, heights))
					{
						const float yA = ray.origin.y + dir.y * tCell;
						const float yB = ray.origin.y + dir.y * tCellExit;
						const float yLow = std::min(yA, yB);
						const float yHigh = std::max(yA, yB);

						float hLow = heights.inner;
						float hHigh = heights.inner;
						for (const float corner : heights.corners)
						{
							hLow = std::min(hLow, corner);
							hHigh = std::max(hHigh, corner);
						}

						// The cell's surface lies entirely within [hLow, hHigh], so a ray
						// segment fully above or fully below it cannot hit any triangle.
						if (yLow <= hHigh && yHigh >= hLow)
						{
							const float x0 = params.originX + static_cast<float>(cellX) * params.cellSize;
							const float z0 = params.originZ + static_cast<float>(cellZ) * params.cellSize;
							const float x1 = x0 + params.cellSize;
							const float z1 = z0 + params.cellSize;

							const Vector3 vTL(x0, heights.corners[0], z0);
							const Vector3 vTR(x1, heights.corners[1], z0);
							const Vector3 vBL(x0, heights.corners[2], z1);
							const Vector3 vBR(x1, heights.corners[3], z1);
							const Vector3 vC((x0 + x1) * 0.5f, heights.inner, (z0 + z1) * 0.5f);

							float nearest = infinity;

							// The same fan the renderer and collision build: centre to each edge.
							float t;
							if (IntersectsTriangle(ray, vC, vTR, vTL, t) && t < nearest)
							{
								nearest = t;
							}
							if (IntersectsTriangle(ray, vC, vBR, vTR, t) && t < nearest)
							{
								nearest = t;
							}
							if (IntersectsTriangle(ray, vC, vBL, vBR, t) && t < nearest)
							{
								nearest = t;
							}
							if (IntersectsTriangle(ray, vC, vTL, vBL, t) && t < nearest)
							{
								nearest = t;
							}

							// Traversal is front to back and a cell's triangles never leave the
							// cell's XZ extent, so the first cell that yields a hit is final.
							if (nearest < infinity && nearest <= tExit)
							{
								result.hit = true;
								result.distance = nearest;
								result.position = ray.origin + dir * nearest;
								result.cellX = cellX;
								result.cellZ = cellZ;
								return result;
							}
						}
					}

					if (tCellExit >= tExit)
					{
						break;
					}

					if (tMaxX < tMaxZ)
					{
						tCell = tMaxX;
						cellX += stepX;
						tMaxX += tDeltaX;
					}
					else
					{
						tCell = tMaxZ;
						cellZ += stepZ;
						tMaxZ += tDeltaZ;
					}

					if (cellX < 0 || cellX >= params.cellCountX || cellZ < 0 || cellZ >= params.cellCountZ)
					{
						break;
					}
				}

				return result;
			}
		}
	}
}
