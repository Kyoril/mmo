// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo::pcg
{
	/// World layout of the terrain grids, supplied by the editor (tests use small layouts).
	struct GridLayout
	{
		/// World X position of global vertex (0,0).
		double originX = 0.0;
		/// World Z position of global vertex (0,0).
		double originZ = 0.0;
		/// Outer vertex spacing; inner vertices sit at +0.5 of it.
		double vertexSpacing = 1.0;
		/// Splat pixel spacing (pixel 0 at origin).
		double pixelSpacing = 1.0;
		/// Water quads per tile side.
		uint32 quadsPerTile = 8;
		/// Outer vertex steps per tile.
		uint32 verticesPerTile = 8;
		/// Tiles of the terrain along x (global tile indices 0 .. tilesX - 1); 0 = not known (no clamping).
		uint32 tilesX = 0;
		/// Tiles of the terrain along z; 0 = not known (no clamping).
		uint32 tilesZ = 0;
	};
}
