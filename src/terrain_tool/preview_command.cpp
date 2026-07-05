// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "preview_command.h"

#include "image_io.h"
#include "terrain_pages.h"

#include "log/default_log_levels.h"
#include "math/vector3.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		struct RampStop
		{
			float t;
			float r, g, b;
		};

		// Hypsometric tint from lowland green over tan and brown to bright peaks
		constexpr RampStop HeightRamp[] = {
			{ 0.00f, 0.13f, 0.35f, 0.16f },
			{ 0.35f, 0.42f, 0.58f, 0.28f },
			{ 0.60f, 0.66f, 0.58f, 0.36f },
			{ 0.80f, 0.52f, 0.42f, 0.30f },
			{ 1.00f, 0.93f, 0.93f, 0.93f }
		};

		void SampleRamp(const float t, float &r, float &g, float &b)
		{
			const size_t stopCount = std::size(HeightRamp);
			if (t <= HeightRamp[0].t)
			{
				r = HeightRamp[0].r; g = HeightRamp[0].g; b = HeightRamp[0].b;
				return;
			}

			for (size_t i = 1; i < stopCount; ++i)
			{
				if (t <= HeightRamp[i].t)
				{
					const float f = (t - HeightRamp[i - 1].t) / (HeightRamp[i].t - HeightRamp[i - 1].t);
					r = HeightRamp[i - 1].r + (HeightRamp[i].r - HeightRamp[i - 1].r) * f;
					g = HeightRamp[i - 1].g + (HeightRamp[i].g - HeightRamp[i - 1].g) * f;
					b = HeightRamp[i - 1].b + (HeightRamp[i].b - HeightRamp[i - 1].b) * f;
					return;
				}
			}

			r = HeightRamp[stopCount - 1].r; g = HeightRamp[stopCount - 1].g; b = HeightRamp[stopCount - 1].b;
		}
	}

	int32 RunPreview(const PreviewArgs &args)
	{
		ZoneGrid grid;
		if (!LoadZoneGrid(args.world, args.pageX0, args.pageZ0, args.pageX1, args.pageZ1, grid))
		{
			ELOG("No terrain pages could be loaded for world '" << args.world << "' in the given page rect!");
			return 1;
		}

		const auto [minIt, maxIt] = std::minmax_element(grid.heights.begin(), grid.heights.end());
		const float minY = *minIt;
		const float range = std::max(*maxIt - minY, 0.001f);

		// Sun from the northwest (image left/top), matching the Python preview convention
		Vector3 toSun(-1.0f, 1.0f, -1.0f);
		toSun.Normalize();

		RgbImage8 image;
		image.width = grid.width;
		image.height = grid.height;
		image.pixels.resize(static_cast<size_t>(grid.width) * grid.height * 3);

		for (uint32 z = 0; z < grid.height; ++z)
		{
			for (uint32 x = 0; x < grid.width; ++x)
			{
				const size_t index = static_cast<size_t>(z) * grid.width + x;

				// Shade using the normals as stored in the page files
				float nx, ny, nz;
				DecodeNormalSNorm8(grid.normals[index], nx, ny, nz);
				Vector3 normal(nx, ny, nz);
				normal.Normalize();

				const float diffuse = std::max(0.0f, normal.Dot(toSun));
				const float lighting = 0.25f + 0.75f * diffuse;

				const float heightT = (grid.heights[index] - minY) / range;
				float r, g, b;
				SampleRamp(heightT, r, g, b);

				float shade = 1.0f;
				if (args.contourInterval > 0.0f)
				{
					const auto band = [&](const size_t i) { return static_cast<int64>(std::floor(grid.heights[i] / args.contourInterval)); };
					const int64 center = band(index);
					const bool contour =
						(x + 1 < grid.width && band(index + 1) != center) ||
						(z + 1 < grid.height && band(index + grid.width) != center);
					if (contour)
					{
						shade = 0.55f;
					}
				}

				image.pixels[index * 3] = static_cast<uint8>(std::clamp(r * lighting * shade, 0.0f, 1.0f) * 255.0f);
				image.pixels[index * 3 + 1] = static_cast<uint8>(std::clamp(g * lighting * shade, 0.0f, 1.0f) * 255.0f);
				image.pixels[index * 3 + 2] = static_cast<uint8>(std::clamp(b * lighting * shade, 0.0f, 1.0f) * 255.0f);
			}
		}

		// Optional integer upscale (nearest neighbor) for easier visual inspection
		if (args.scale > 1)
		{
			RgbImage8 scaled;
			scaled.width = image.width * args.scale;
			scaled.height = image.height * args.scale;
			scaled.pixels.resize(static_cast<size_t>(scaled.width) * scaled.height * 3);

			for (uint32 z = 0; z < scaled.height; ++z)
			{
				const size_t srcRow = static_cast<size_t>(z / args.scale) * image.width;
				for (uint32 x = 0; x < scaled.width; ++x)
				{
					const size_t src = (srcRow + x / args.scale) * 3;
					const size_t dst = (static_cast<size_t>(z) * scaled.width + x) * 3;
					scaled.pixels[dst] = image.pixels[src];
					scaled.pixels[dst + 1] = image.pixels[src + 1];
					scaled.pixels[dst + 2] = image.pixels[src + 2];
				}
			}

			image = std::move(scaled);
		}

		if (!SaveRgb8Png(args.outPath, image))
		{
			return 1;
		}

		ILOG("Rendered preview of " << grid.loadedPageCount << " terrain pages to '" << args.outPath.string() << "'");
		return 0;
	}
}
