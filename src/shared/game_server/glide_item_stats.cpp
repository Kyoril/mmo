// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glide_item_stats.h"

#include "shared/proto_data/items.pb.h"

namespace mmo
{
	GlideModifiers GetGlideModifiers(const proto::ItemEntry& entry)
	{
		GlideModifiers modifiers;
		for (int i = 0; i < entry.stats_size(); ++i)
		{
			AddGlideStat(modifiers, entry.stats(i).type(), entry.stats(i).value());
		}

		return modifiers;
	}
}
