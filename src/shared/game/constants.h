#pragma once

#include "base/typedefs.h"

namespace mmo
{
	namespace constants
	{
		/// Width or height of a ADT page in world units (Yards).
		static const float MapWidth = 533.33333f;

		/// Number of adt pages per map (one side).
		static const uint32 PagesPerMap = 64;

		/// Number of tiles per adt page (one side).
		static const uint32 TilesPerPage = 16;

		/// Total number of tiles per adt page (both sides).
		static const uint32 TilesPerPageSquared = TilesPerPage * TilesPerPage;

		/// Total number of vertices per tile (both sides).
		static const uint32 VertsPerTile = 9 * 9 + 8 * 8;
		
		/// Maximum number of action buttons the client knows.
		static const uint32 ActionButtonLimit = 132;
	}
}
