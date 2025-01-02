#pragma once

#include <vector>

#include "base/typedefs.h"

namespace io
{
	class Reader;
	class Writer;
}

namespace mmo
{
	struct QuestRewardItem
	{
		uint32 itemId;
		uint32 count;
	};

	struct QuestRequiredItem
	{
		uint32 itemId;
		uint32 count;
	};

	struct QuestRequiredCreature
	{
		uint32 creatureId;
		uint32 count;
	};

	struct QuestInfo
	{
		uint64 id;
		String title;
		String summary;
		String description;
		uint32 questLevel;
		uint32 rewardXp;
		uint32 rewardMoney;
		uint32 rewardSpellId;
		std::vector<QuestRequiredItem> requiredItems;
		std::vector<QuestRequiredCreature> requiredCreatures;
		std::vector<QuestRewardItem> rewardItems;
		std::vector<QuestRewardItem> optionalItems;
	};

	io::Writer& operator<<(io::Writer& writer, const QuestInfo& itemInfo);
	io::Reader& operator>>(io::Reader& reader, QuestInfo& outItemInfo);
}
