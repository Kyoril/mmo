// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game/glide_stats.h"

namespace mmo
{
	namespace proto
	{
		class ItemEntry;
	}

	/// @brief Gets the glide modifiers of a glider item entry (the sum of its glide stats).
	GlideModifiers GetGlideModifiers(const proto::ItemEntry& entry);
}
