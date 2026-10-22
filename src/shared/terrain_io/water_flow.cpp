// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "water_flow.h"

#include "terrain/constants.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mmo
{
	namespace terrain_io
	{
		namespace water_flow
		{
			namespace
			{
				constexpr uint32 OuterSide = terrain::constants::OuterVerticesPerPageSide;

				// Three separable box blurs of radius 3 approximate a Gaussian of about 3 vertices (12 m).
				constexpr int32 BlurRadius = 3;
				constexpr int32 BlurPasses = 3;

				// Every pass spreads by the radius, so this border makes a page's vertices independent of
				// where the computed grid ends.
				constexpr int32 Border = BlurRadius * BlurPasses;

				void Blur(std::vector<float>& values, const uint32 side)
				{
					std::vector<float> scratch(values.size());
					for (int32 pass = 0; pass < BlurPasses; ++pass)
					{
						for (uint32 z = 0; z < side; ++z)
						{
							for (uint32 x = 0; x < side; ++x)
							{
								float sum = 0.0f;
								for (int32 k = -BlurRadius; k <= BlurRadius; ++k)
								{
									const int32 sx = std::clamp(static_cast<int32>(x) + k, 0, static_cast<int32>(side) - 1);
									sum += values[static_cast<size_t>(z) * side + sx];
								}
								scratch[static_cast<size_t>(z) * side + x] = sum / static_cast<float>(BlurRadius * 2 + 1);
							}
						}
						for (uint32 z = 0; z < side; ++z)
						{
							for (uint32 x = 0; x < side; ++x)
							{
								float sum = 0.0f;
								for (int32 k = -BlurRadius; k <= BlurRadius; ++k)
								{
									const int32 sz = std::clamp(static_cast<int32>(z) + k, 0, static_cast<int32>(side) - 1);
									sum += scratch[static_cast<size_t>(sz) * side + x];
								}
								values[static_cast<size_t>(z) * side + x] = sum / static_cast<float>(BlurRadius * 2 + 1);
							}
						}
					}
				}
			}

			FlowCoordinate ProjectOntoFlowPath(const FlowPath& path, const float x, const float z)
			{
				FlowCoordinate best;
				float bestDistance = std::numeric_limits<float>::max();
				float start = 0.0f;
				for (size_t i = 0; i + 1 < path.size(); ++i)
				{
					const FlowPoint& a = path[i];
					const FlowPoint& b = path[i + 1];
					const float dx = b.x - a.x;
					const float dz = b.z - a.z;
					const float length = std::sqrt(dx * dx + dz * dz);
					if (length <= 0.0f)
					{
						continue;
					}

					const float t = std::clamp(((x - a.x) * dx + (z - a.z) * dz) / (length * length), 0.0f, 1.0f);
					const float px = a.x + dx * t;
					const float pz = a.z + dz * t;
					const float distance = std::hypot(x - px, z - pz);
					if (distance < bestDistance)
					{
						bestDistance = distance;
						best.along = start + t * length;
						// Left of the flow direction (dx, dz) is (-dz, dx): for flow along +X that is +Z,
						// matching v = z of the world mapping.
						best.across = ((x - px) * -dz + (z - pz) * dx) / length;
						best.halfWidth = a.halfWidth + (b.halfWidth - a.halfWidth) * t;
					}
					start += length;
				}

				return best;
			}

			bool HasUsablePath(const std::vector<FlowPath>& paths)
			{
				return std::any_of(paths.begin(), paths.end(), [](const FlowPath& path) { return path.size() >= 2; });
			}

			bool IsRectInReach(const std::vector<FlowPath>& paths, const FlowSettings& settings, const float minX, const float minZ, const float maxX, const float maxZ)
			{
				for (const FlowPath& path : paths)
				{
					if (path.size() < 2)
					{
						continue;
					}

					// Conservative: a segment is in reach if its bounds, grown by the widest channel plus
					// the falloff, touch the rectangle.
					for (size_t i = 0; i + 1 < path.size(); ++i)
					{
						const FlowPoint& a = path[i];
						const FlowPoint& b = path[i + 1];
						const float reach = std::max(a.halfWidth, b.halfWidth) + settings.falloff;
						const float segMinX = std::min(a.x, b.x) - reach;
						const float segMaxX = std::max(a.x, b.x) + reach;
						const float segMinZ = std::min(a.z, b.z) - reach;
						const float segMaxZ = std::max(a.z, b.z) + reach;
						if (segMaxX >= minX && segMinX <= maxX && segMaxZ >= minZ && segMinZ <= maxZ)
						{
							return true;
						}
					}
				}

				return false;
			}

			void ComputePageFlow(const std::vector<FlowPath>& paths, const FlowSettings& settings, const float pageOriginX, const float pageOriginZ,
				std::vector<float>& outUVs, std::vector<uint8>& outSpeeds)
			{
				const float vertexSpacing = static_cast<float>(terrain::constants::PageSize) / static_cast<float>(OuterSide - 1);
				const uint32 side = OuterSide + Border * 2;
				const size_t count = static_cast<size_t>(side) * side;

				std::vector<float> along(count);
				std::vector<float> across(count);
				std::vector<float> speed(count);
				for (uint32 z = 0; z < side; ++z)
				{
					for (uint32 x = 0; x < side; ++x)
					{
						const float wx = pageOriginX + (static_cast<float>(x) - Border) * vertexSpacing;
						const float wz = pageOriginZ + (static_cast<float>(z) - Border) * vertexSpacing;

						FlowCoordinate c;
						float distance = std::numeric_limits<float>::max();
						for (const FlowPath& path : paths)
						{
							if (path.size() < 2)
							{
								continue;
							}

							const FlowCoordinate p = ProjectOntoFlowPath(path, wx, wz);
							const float pd = std::max(0.0f, std::abs(p.across) - p.halfWidth);
							if (pd < distance || (pd == distance && std::abs(p.across) < std::abs(c.across)))
							{
								c = p;
								distance = pd;
							}
						}

						const size_t i = static_cast<size_t>(z) * side + x;
						along[i] = c.along;
						across[i] = c.across;
						speed[i] = std::clamp(1.0f - distance / std::max(settings.falloff, 1.0f), settings.minSpeed, 1.0f);
					}
				}

				Blur(along, side);
				Blur(across, side);
				Blur(speed, side);

				outUVs.assign(static_cast<size_t>(OuterSide) * OuterSide * 2, 0.0f);
				outSpeeds.assign(static_cast<size_t>(OuterSide) * OuterSide, 0);
				for (uint32 vz = 0; vz < OuterSide; ++vz)
				{
					for (uint32 vx = 0; vx < OuterSide; ++vx)
					{
						const size_t g = static_cast<size_t>(vz + Border) * side + (vx + Border);
						const size_t index = static_cast<size_t>(vz) * OuterSide + vx;
						outUVs[index * 2] = along[g] * settings.uvScale;
						outUVs[index * 2 + 1] = across[g] * settings.uvScale;
						outSpeeds[index] = static_cast<uint8>(std::lround(std::clamp(speed[g], 0.0f, 1.0f) * 255.0f));
					}
				}
			}
		}
	}
}
