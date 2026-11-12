// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "grid_layout.h"

namespace mmo::pcg
{
	/// Inclusive integer rectangle in grid index space.
	struct IntRect
	{
		int32 minX = 0, minZ = 0, maxX = 0, maxZ = 0;
	};

	/// Global-index access to terrain data. Implemented over terrain::Terrain by the editor.
	class ITerrainAccess
	{
	public:
		virtual ~ITerrainAccess() = default;

		/// Gets the world layout of the terrain grids.
		virtual const GridLayout& GetLayout() const = 0;

		virtual float GetOuterHeight(int32 vx, int32 vz) const = 0;
		virtual void SetOuterHeight(int32 vx, int32 vz, float h) = 0;
		virtual float GetInnerHeight(int32 ix, int32 iz) const = 0;
		virtual void SetInnerHeight(int32 ix, int32 iz, float h) = 0;

		/// Packed splat weights, one byte per layer (layer 0 in the low byte).
		virtual uint32 GetSplat(int32 px, int32 pz) const = 0;
		virtual void SetSplat(int32 px, int32 pz, uint32 packed) = 0;

		/// Quad indices are global (tile * quadsPerTile + local). Type 0 = none.
		virtual uint8 GetWaterQuad(int32 qx, int32 qz) const = 0;
		virtual void SetWaterQuad(int32 qx, int32 qz, uint8 waterType) = 0;
		virtual float GetWaterHeight(int32 vx, int32 vz) const = 0;
		virtual void SetWaterHeight(int32 vx, int32 vz, float h) = 0;

		virtual String GetTileMaterial(int32 tx, int32 tz) const = 0;
		virtual void SetTileMaterial(int32 tx, int32 tz, const String& material) = 0;

		/// Called once after a batch of writes with the touched outer vertex rect (normals, meshes).
		virtual void FinishEdits(const IntRect& outerVertexRect) = 0;
	};
}
