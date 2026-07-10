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
		/// Class mask (1 << (classId - 1), 0 = no restriction) of classes this quest is intended
		/// for. Used by the client to render log quests as disabled while a non-matching class is
		/// active (see IsQuestClassAllowed in game/quest.h).
		uint32 requiredClasses = 0;
		std::vector<QuestRequiredItem> requiredItems;
		std::vector<QuestRequiredCreature> requiredCreatures;
		std::vector<QuestRewardItem> rewardItems;
		std::vector<QuestRewardItem> optionalItems;
	};

	io::Writer& operator<<(io::Writer& writer, const QuestInfo& itemInfo);
	io::Reader& operator>>(io::Reader& reader, QuestInfo& outItemInfo);
}
