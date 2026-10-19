// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "terrain/constants.h"
#include "terrain/water_lookup.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace mmo
{
	namespace terrain
	{
		/// @brief Pure colour and water-field maths for baking minimap tiles.
		///
		/// @remark Deliberately free of any dependency on the terrain library itself (only base, math and
		///			header-only terrain helpers) so it compiles into the headless terrain_tests target.
		///
		/// @remark The minimap baker renders a page top-down into a G-buffer and shades it on the CPU: land
		///			gets fixed relief lighting from the G-buffer normals, water is drawn analytically from the
		///			page's water data rather than rendered, which is what lets it have depth-dependent colour,
		///			rounded shorelines and foam instead of a flat, 4 m-blocky placeholder.
		namespace minimap_shading
		{
			/// @brief A linear RGB colour.
			struct Rgb
			{
				float r{ 0.0f };
				float g{ 0.0f };
				float b{ 0.0f };
			};

			inline float Clamp01(const float value)
			{
				return std::clamp(value, 0.0f, 1.0f);
			}

			inline float Smoothstep(const float edge0, const float edge1, const float value)
			{
				const float t = Clamp01((value - edge0) / (edge1 - edge0));
				return t * t * (3.0f - 2.0f * t);
			}

			inline Rgb Lerp(const Rgb& a, const Rgb& b, const float t)
			{
				return Rgb{ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
			}

			inline Rgb Scale(const Rgb& c, const float s)
			{
				return Rgb{ c.r * s, c.g * s, c.b * s };
			}

			inline Rgb Multiply(const Rgb& a, const Rgb& b)
			{
				return Rgb{ a.r * b.r, a.g * b.g, a.b * b.b };
			}

			/// @brief Gamma 2.2 decode, matching the rest of the bake code.
			inline float SrgbToLinear(const float value)
			{
				return std::pow(Clamp01(value), 2.2f);
			}

			/// @brief Gamma 2.2 encode, matching the rest of the bake code.
			inline float LinearToSrgb(const float value)
			{
				return std::pow(Clamp01(value), 1.0f / 2.2f);
			}

			/// @brief Builds a linear colour from 8-bit sRGB components, the way palettes are written below.
			inline Rgb FromSrgb8(const uint8 r, const uint8 g, const uint8 b)
			{
				return Rgb{ SrgbToLinear(r / 255.0f), SrgbToLinear(g / 255.0f), SrgbToLinear(b / 255.0f) };
			}

			/// @brief Deterministic hash of an integer lattice point to [0, 1).
			inline float Hash2(const int32 x, const int32 z)
			{
				uint32 h = static_cast<uint32>(x) * 0x8da6b343u ^ static_cast<uint32>(z) * 0xd8163841u;
				h ^= h >> 13;
				h *= 0x5bd1e995u;
				h ^= h >> 15;
				return static_cast<float>(h & 0x00ffffffu) / static_cast<float>(0x01000000u);
			}

			/// @brief Smooth value noise in [0, 1], continuous across page borders (world coordinates).
			inline float ValueNoise(const float x, const float z)
			{
				const float fx = std::floor(x);
				const float fz = std::floor(z);
				const int32 ix = static_cast<int32>(fx);
				const int32 iz = static_cast<int32>(fz);
				const float tx = x - fx;
				const float tz = z - fz;
				const float sx = tx * tx * (3.0f - 2.0f * tx);
				const float sz = tz * tz * (3.0f - 2.0f * tz);

				const float a = Hash2(ix, iz);
				const float b = Hash2(ix + 1, iz);
				const float c = Hash2(ix, iz + 1);
				const float d = Hash2(ix + 1, iz + 1);
				return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sz;
			}

			/// @brief Fractal value noise in [0, 1].
			inline float Fbm(float x, float z, const int32 octaves)
			{
				float sum = 0.0f;
				float amplitude = 0.5f;
				float total = 0.0f;
				for (int32 i = 0; i < octaves; ++i)
				{
					sum += ValueNoise(x, z) * amplitude;
					total += amplitude;
					amplitude *= 0.5f;
					// Rotate a little per octave so lattice axes do not line up into a grid look.
					const float rx = x * 1.6f + z * 1.2f;
					const float rz = -x * 1.2f + z * 1.6f;
					x = rx + 17.3f;
					z = rz - 9.1f;
				}

				return sum / total;
			}

			/// @brief How a liquid type looks on the minimap.
			struct WaterLook
			{
				/// @brief Colour of the liquid body where it is very shallow.
				Rgb shallow;
				/// @brief Colour of the liquid body where it is deep.
				Rgb deep;
				/// @brief Colour of the shoreline foam (or, for lava, the glowing rim).
				Rgb foam;
				/// @brief Depth in world units at which the body is ~63% of the way to the deep colour.
				float depthScale{ 4.0f };
				/// @brief How much of the floor shows through very shallow liquid, [0, 1].
				float clarity{ 0.5f };
				/// @brief Water depth (world units) over which the foam fades out.
				float foamDepth{ 0.4f };
				/// @brief Distance from the shoreline (world units) over which the foam fades out.
				float foamDistance{ 1.2f };
				/// @brief Strength of the ripple brightness variation, [0, 1].
				float rippleStrength{ 0.08f };
				/// @brief Distance on land (world units) over which the wet/scorched bank fades out.
				float bankDistance{ 1.6f };
				/// @brief Brightness factor of the bank right at the shoreline.
				float bankDarkening{ 0.7f };
				/// @brief Tint added to the bank right at the shoreline (lava glow); black for none.
				Rgb bankTint;
			};

			/// @brief The minimap look of a liquid type. None falls back to plain water.
			inline WaterLook LookFor(const WaterType type)
			{
				WaterLook look;
				switch (type)
				{
				case WaterType::Ocean:
					look.shallow = FromSrgb8(70, 176, 178);
					look.deep = FromSrgb8(14, 46, 98);
					look.foam = FromSrgb8(226, 242, 240);
					look.depthScale = 7.0f;
					look.clarity = 0.4f;
					look.foamDepth = 0.6f;
					look.foamDistance = 2.2f;
					look.rippleStrength = 0.1f;
					look.bankDistance = 2.4f;
					look.bankDarkening = 0.74f;
					break;
				case WaterType::Lava:
					look.shallow = FromSrgb8(255, 184, 52);
					look.deep = FromSrgb8(196, 44, 8);
					look.foam = FromSrgb8(255, 236, 140);
					look.depthScale = 1.5f;
					look.clarity = 0.0f;
					look.foamDepth = 0.3f;
					look.foamDistance = 1.6f;
					look.rippleStrength = 0.0f;
					look.bankDistance = 3.5f;
					look.bankDarkening = 0.35f;
					look.bankTint = FromSrgb8(120, 30, 0);
					break;
				case WaterType::Slime:
					look.shallow = FromSrgb8(150, 206, 62);
					look.deep = FromSrgb8(44, 104, 26);
					look.foam = FromSrgb8(206, 246, 128);
					look.depthScale = 2.5f;
					look.clarity = 0.25f;
					look.foamDepth = 0.3f;
					look.foamDistance = 1.0f;
					look.rippleStrength = 0.05f;
					look.bankDistance = 1.6f;
					look.bankDarkening = 0.72f;
					look.bankTint = FromSrgb8(20, 40, 0);
					break;
				case WaterType::None:
				case WaterType::Water:
				default:
					look.shallow = FromSrgb8(76, 164, 170);
					look.deep = FromSrgb8(24, 70, 120);
					look.foam = FromSrgb8(214, 236, 234);
					look.depthScale = 3.5f;
					look.clarity = 0.55f;
					look.foamDepth = 0.35f;
					look.foamDistance = 1.2f;
					look.rippleStrength = 0.08f;
					look.bankDistance = 1.6f;
					look.bankDarkening = 0.7f;
					break;
				}

				return look;
			}

			/// @brief Shades a pixel of liquid.
			/// @param look The liquid's look.
			/// @param floor The (already shaded) colour of whatever lies under the liquid.
			/// @param type The liquid type (lava and slime get their own surface pattern).
			/// @param depth Liquid depth over the floor in world units (0 at the shoreline).
			/// @param shoreDistance Distance to the nearest dry pixel in world units.
			/// @param worldX World X of the pixel, for the surface pattern.
			/// @param worldZ World Z of the pixel, for the surface pattern.
			inline Rgb ShadeWater(const WaterLook& look, const Rgb& floor, const WaterType type, const float depth,
				const float shoreDistance, const float worldX, const float worldZ)
			{
				const float d = std::max(depth, 0.0f);
				const float t = 1.0f - std::exp(-d / look.depthScale);

				Rgb body = Lerp(look.shallow, look.deep, t);

				// Very shallow liquid shows the floor, mostly desaturated and tinted by the liquid, so grass
				// under a flooded bank reads as a shallow, not as green patches in the water.
				const float floorLuminance = floor.r * 0.2126f + floor.g * 0.7152f + floor.b * 0.0722f;
				const Rgb mutedFloor = Lerp(floor, Rgb{ floorLuminance, floorLuminance, floorLuminance }, 0.65f);
				const float seeThrough = look.clarity * (1.0f - t) * (1.0f - t);
				body = Lerp(body, Multiply(mutedFloor, Scale(look.shallow, 1.7f)), seeThrough);

				if (type == WaterType::Lava)
				{
					// Dark cooling crust plates with glowing cracks between them; the crust thins toward
					// the rim where the lava is hottest.
					const float plates = Fbm(worldX * 0.16f, worldZ * 0.16f, 4);
					const float cracks = 1.0f - std::abs(Fbm(worldX * 0.11f + 40.0f, worldZ * 0.11f, 3) * 2.0f - 1.0f);
					const float crust = Smoothstep(0.42f, 0.62f, plates) * (1.0f - Smoothstep(0.82f, 0.95f, cracks));
					const float rim = 1.0f - Smoothstep(0.0f, 3.0f, shoreDistance);
					body = Lerp(body, FromSrgb8(46, 22, 18), crust * 0.85f * (1.0f - rim));
				}
				else if (type == WaterType::Slime)
				{
					const float bubbles = Smoothstep(0.72f, 0.8f, ValueNoise(worldX * 0.9f, worldZ * 0.9f));
					body = Lerp(body, look.foam, bubbles * 0.35f);
				}

				// Gentle ripples, stretched a little so open water does not look like noise.
				if (look.rippleStrength > 0.0f)
				{
					const float ripple = Fbm(worldX * 0.22f, worldZ * 0.45f, 3) * 2.0f - 1.0f;
					body = Scale(body, 1.0f + ripple * look.rippleStrength);
				}

				// Foam hugging the shoreline, broken up so it does not read as a stroked outline.
				const float byDepth = 1.0f - Smoothstep(0.0f, look.foamDepth, d);
				const float byDistance = 1.0f - Smoothstep(0.0f, look.foamDistance, shoreDistance);
				const float breakup = Smoothstep(0.25f, 0.65f, Fbm(worldX * 0.35f, worldZ * 0.35f, 2));
				const float wash = Clamp01(std::max(byDepth * 0.8f, byDistance) * (0.45f + 0.55f * breakup));
				// Plus a thin, nearly solid line right at the waterline, which is what makes the shore
				// read crisply at minimap scale.
				const float line = 1.0f - Smoothstep(0.35f, 1.0f, shoreDistance);
				const float foam = std::max(wash * 0.8f, line * 0.9f);
				return Lerp(body, look.foam, foam);
			}

			/// @brief Darkens (wet sand) or scorches (lava) land right next to the liquid.
			/// @param land The shaded land colour.
			/// @param look The look of the nearest liquid.
			/// @param distanceToWater Distance to the nearest liquid pixel in world units.
			inline Rgb ShadeBank(const Rgb& land, const WaterLook& look, const float distanceToWater)
			{
				const float s = 1.0f - Smoothstep(0.0f, look.bankDistance, distanceToWater);
				if (s <= 0.0f)
				{
					return land;
				}

				const Rgb darkened = Scale(land, 1.0f + (look.bankDarkening - 1.0f) * s);
				return Rgb{ darkened.r + look.bankTint.r * s, darkened.g + look.bankTint.g * s, darkened.b + look.bankTint.b * s };
			}

			/// @brief Fixed relief lighting: a sun from the north-west, normalised so flat ground keeps its
			///			albedo exactly. Slopes facing the sun brighten, slopes facing away darken.
			/// @param nx World-space normal X.
			/// @param ny World-space normal Y (up).
			/// @param nz World-space normal Z (south; north is -Z).
			/// @return The brightness factor to multiply the albedo with.
			inline float ReliefLight(const float nx, const float ny, const float nz)
			{
				constexpr float ambient = 0.32f;
				constexpr float diffuse = 0.68f;

				// Normalised (-1, 1.5, -1): north-west, about 47 degrees up.
				constexpr float lx = -0.5145f;
				constexpr float ly = 0.7717f;
				constexpr float lz = -0.5145f;

				const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
				if (length < 1.0e-4f)
				{
					return 1.0f;
				}

				const float nDotL = std::max((nx * lx + ny * ly + nz * lz) / length, 0.0f);
				const float factor = (ambient + diffuse * nDotL) / (ambient + diffuse * ly);
				return std::clamp(factor, 0.45f, 1.3f);
			}

			/// @brief The water-quad grid coordinate (0..127 per page axis) of a quad as mask lookup input.
			inline bool QuadHasWater(const water_lookup::PageWaterView& view, const int32 gx, const int32 gz)
			{
				constexpr int32 quadsPerSide = static_cast<int32>(constants::TilesPerPage * water_lookup::QuadsPerTileSide);
				const int32 cx = std::clamp(gx, 0, quadsPerSide - 1);
				const int32 cz = std::clamp(gz, 0, quadsPerSide - 1);
				return water_lookup::HasWaterAtQuad(view,
					static_cast<uint32>(cx) / water_lookup::QuadsPerTileSide, static_cast<uint32>(cz) / water_lookup::QuadsPerTileSide,
					static_cast<uint32>(cx) % water_lookup::QuadsPerTileSide, static_cast<uint32>(cz) % water_lookup::QuadsPerTileSide);
			}

			/// @brief Smoothed water presence at a page-local position: the quad mask bilinearly
			///			interpolated between quad centres. Its 0.5 iso-line runs along straight mask edges
			///			exactly and cuts the stair-step corners of diagonal edges, which is what turns the
			///			4 m-blocky mask into a rounded shoreline.
			/// @return Presence in [0, 1]; 0.5 is the shoreline.
			inline float WaterPresence(const water_lookup::PageWaterView& view, const float localX, const float localZ)
			{
				const float u = localX / water_lookup::QuadSize - 0.5f;
				const float v = localZ / water_lookup::QuadSize - 0.5f;
				const float fu = std::floor(u);
				const float fv = std::floor(v);
				const int32 gx = static_cast<int32>(fu);
				const int32 gz = static_cast<int32>(fv);
				const float tx = u - fu;
				const float tz = v - fv;

				const float a = QuadHasWater(view, gx, gz) ? 1.0f : 0.0f;
				const float b = QuadHasWater(view, gx + 1, gz) ? 1.0f : 0.0f;
				const float c = QuadHasWater(view, gx, gz + 1) ? 1.0f : 0.0f;
				const float d = QuadHasWater(view, gx + 1, gz + 1) ? 1.0f : 0.0f;
				return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
			}

			/// @brief Water surface height at a page-local position, bilinear over the page's water vertex grid.
			/// @remark Only meaningful where WaterPresence says there is water: the grid is zero elsewhere.
			inline float WaterSurfaceHeight(const water_lookup::PageWaterView& view, const float localX, const float localZ)
			{
				if (view.vertexHeights == nullptr)
				{
					return 0.0f;
				}

				constexpr int32 verticesPerSide = static_cast<int32>(constants::OuterVerticesPerPageSide);
				const float u = std::clamp(localX / water_lookup::QuadSize, 0.0f, static_cast<float>(verticesPerSide - 1));
				const float v = std::clamp(localZ / water_lookup::QuadSize, 0.0f, static_cast<float>(verticesPerSide - 1));
				const int32 x0 = std::min(static_cast<int32>(u), verticesPerSide - 2);
				const int32 z0 = std::min(static_cast<int32>(v), verticesPerSide - 2);
				const float tx = u - static_cast<float>(x0);
				const float tz = v - static_cast<float>(z0);

				const auto at = [&](const int32 x, const int32 z)
				{
					return view.vertexHeights[x + z * verticesPerSide];
				};

				const float top = at(x0, z0) + (at(x0 + 1, z0) - at(x0, z0)) * tx;
				const float bottom = at(x0, z0 + 1) + (at(x0 + 1, z0 + 1) - at(x0, z0 + 1)) * tx;
				return top + (bottom - top) * tz;
			}

			/// @brief Water surface height at a page-local position, averaged over only the water-carrying
			///			vertices around it. Use this near the shoreline: the plain bilinear blend pulls in the
			///			zero heights of dry vertices and sinks the surface along every mask edge.
			inline float WaterSurfaceHeightNearShore(const water_lookup::PageWaterView& view, const float localX, const float localZ)
			{
				if (view.vertexHeights == nullptr)
				{
					return 0.0f;
				}

				constexpr int32 verticesPerSide = static_cast<int32>(constants::OuterVerticesPerPageSide);
				const float u = std::clamp(localX / water_lookup::QuadSize, 0.0f, static_cast<float>(verticesPerSide - 1));
				const float v = std::clamp(localZ / water_lookup::QuadSize, 0.0f, static_cast<float>(verticesPerSide - 1));
				const int32 x0 = std::min(static_cast<int32>(u), verticesPerSide - 2);
				const int32 z0 = std::min(static_cast<int32>(v), verticesPerSide - 2);
				const float tx = u - static_cast<float>(x0);
				const float tz = v - static_cast<float>(z0);

				// A vertex carries water when any of the (up to four) quads sharing it does.
				const auto wet = [&](const int32 x, const int32 z)
				{
					return QuadHasWater(view, x - 1, z - 1) || QuadHasWater(view, x, z - 1) ||
						QuadHasWater(view, x - 1, z) || QuadHasWater(view, x, z);
				};

				float sum = 0.0f;
				float weightSum = 0.0f;
				const int32 xs[2] = { x0, x0 + 1 };
				const int32 zs[2] = { z0, z0 + 1 };
				const float wx[2] = { 1.0f - tx, tx };
				const float wz[2] = { 1.0f - tz, tz };
				for (int32 j = 0; j < 2; ++j)
				{
					for (int32 i = 0; i < 2; ++i)
					{
						if (!wet(xs[i], zs[j]))
						{
							continue;
						}

						// Keep a small floor on the weight so a corner sample still averages its wet neighbours.
						const float w = std::max(wx[i] * wz[j], 1.0e-3f);
						sum += view.vertexHeights[xs[i] + zs[j] * verticesPerSide] * w;
						weightSum += w;
					}
				}

				return weightSum > 0.0f ? sum / weightSum : WaterSurfaceHeight(view, localX, localZ);
			}

			/// @brief Distance in pixels from every pixel to the nearest pixel where `target` is true
			///			(0 on target pixels), using a two-pass 3-4 chamfer transform. Pixels that cannot reach
			///			any target get std::numeric_limits<float>::max().
			/// @param target size*size flags, row-major.
			/// @param size Edge length of the square image.
			/// @param nearest Optional: receives, per pixel, the index of the nearest target pixel (or -1).
			inline std::vector<float> ChamferDistance(const std::vector<uint8>& target, const uint32 size, std::vector<int32>* nearest = nullptr)
			{
				constexpr float infinity = std::numeric_limits<float>::max();
				std::vector<float> dist(static_cast<size_t>(size) * size, infinity);
				std::vector<int32> source(static_cast<size_t>(size) * size, -1);

				for (size_t i = 0; i < dist.size(); ++i)
				{
					if (target[i])
					{
						dist[i] = 0.0f;
						source[i] = static_cast<int32>(i);
					}
				}

				const auto relax = [&](const int32 x, const int32 y, const int32 nx, const int32 ny, const float cost)
				{
					if (nx < 0 || ny < 0 || nx >= static_cast<int32>(size) || ny >= static_cast<int32>(size))
					{
						return;
					}

					const size_t from = static_cast<size_t>(ny) * size + nx;
					const size_t to = static_cast<size_t>(y) * size + x;
					if (dist[from] != infinity && dist[from] + cost < dist[to])
					{
						dist[to] = dist[from] + cost;
						source[to] = source[from];
					}
				};

				// 3-4 chamfer weights scaled to pixel units.
				constexpr float straight = 1.0f;
				constexpr float diagonal = 4.0f / 3.0f;

				for (int32 y = 0; y < static_cast<int32>(size); ++y)
				{
					for (int32 x = 0; x < static_cast<int32>(size); ++x)
					{
						relax(x, y, x - 1, y, straight);
						relax(x, y, x, y - 1, straight);
						relax(x, y, x - 1, y - 1, diagonal);
						relax(x, y, x + 1, y - 1, diagonal);
					}
				}

				for (int32 y = static_cast<int32>(size) - 1; y >= 0; --y)
				{
					for (int32 x = static_cast<int32>(size) - 1; x >= 0; --x)
					{
						relax(x, y, x + 1, y, straight);
						relax(x, y, x, y + 1, straight);
						relax(x, y, x + 1, y + 1, diagonal);
						relax(x, y, x - 1, y + 1, diagonal);
					}
				}

				if (nearest)
				{
					*nearest = std::move(source);
				}

				return dist;
			}
		}
	}
}
