// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "export_command.h"

#include "image_io.h"
#include "terrain_pages.h"
#include "zone_meta.h"

#include "log/default_log_levels.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	int32 RunExport(const ExportArgs &args)
	{
		ZoneGrid grid;
		if (!LoadZoneGrid(args.world, args.pageX0, args.pageZ0, args.pageX1, args.pageZ1, grid))
		{
			ELOG("No terrain pages could be loaded for world '" << args.world << "' in the given page rect!");
			return 1;
		}

		const auto [minIt, maxIt] = std::minmax_element(grid.heights.begin(), grid.heights.end());
		float minY = *minIt;
		float maxY = *maxIt;
		if (!(maxY > minY))
		{
			// Perfectly flat terrain: widen the range so the quantization stays well defined
			maxY = minY + 1.0f;
		}

		GrayImage16 image;
		image.width = grid.width;
		image.height = grid.height;
		image.pixels.resize(grid.heights.size());
		for (size_t i = 0; i < grid.heights.size(); ++i)
		{
			const float t = (grid.heights[i] - minY) / (maxY - minY);
			image.pixels[i] = static_cast<uint16>(std::lround(t * 65535.0f));
		}

		if (!SaveGray16Png(args.outPath, image))
		{
			return 1;
		}

		ZoneMeta meta;
		meta.world = args.world;
		meta.pageX0 = args.pageX0;
		meta.pageZ0 = args.pageZ0;
		meta.pageX1 = args.pageX1;
		meta.pageZ1 = args.pageZ1;
		meta.minY = minY;
		meta.maxY = maxY;

		if (!SaveZoneMeta(args.metaOutPath, meta))
		{
			return 1;
		}

		ILOG("Exported " << grid.loadedPageCount << " terrain pages to '" << args.outPath.string() << "' (height range "
			<< minY << " to " << maxY << ")");
		return 0;
	}
}
