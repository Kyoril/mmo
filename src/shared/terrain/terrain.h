
#pragma once

#include "base/typedefs.h"

namespace mmo
{
	typedef uint16 TileId;
	
	
	class Terrain
	{
	public:
		explicit Terrain();

	public:

		void PreparePage(TileId x, TileId y);

		void LoadPage(TileId x, TileId y);

		void UnloadPage(TileId x, TileId y);

		uint32 GetWidth() const;
		
		uint32 GetHeight() const;
	};
}