
#include "quest_info.h"

#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo
{
	io::Writer& operator<<(io::Writer& writer, const QuestInfo& itemInfo)
	{
		writer
			<< io::write<uint64>(itemInfo.id)
			<< io::write_dynamic_range<uint8>(itemInfo.title)
			<< io::write_dynamic_range<uint16>(itemInfo.summary)
			<< io::write_dynamic_range<uint16>(itemInfo.description);

		writer
			<< io::write<uint32>(itemInfo.questLevel)
			<< io::write<uint32>(itemInfo.rewardXp)
			<< io::write<uint32>(itemInfo.rewardMoney)
			<< io::write<uint32>(itemInfo.rewardSpellId);

		writer << io::write<uint8>(itemInfo.requiredItems.size());
		for (const auto& requiredItem : itemInfo.requiredItems)
		{
			writer
				<< io::write<uint32>(requiredItem.itemId)
				<< io::write<uint32>(requiredItem.count);
		}

		writer << io::write<uint8>(itemInfo.requiredCreatures.size());
		for (const auto& requiredCreature : itemInfo.requiredCreatures)
		{
			writer
				<< io::write<uint32>(requiredCreature.creatureId)
				<< io::write<uint32>(requiredCreature.count);
		}

		writer << io::write<uint8>(itemInfo.rewardItems.size());
		for (const auto& rewardItem : itemInfo.rewardItems)
		{
			writer
				<< io::write<uint32>(rewardItem.itemId)
				<< io::write<uint32>(rewardItem.count);
		}

		writer << io::write<uint8>(itemInfo.optionalItems.size());
		for (const auto& optionalItem : itemInfo.optionalItems)
		{
			writer
				<< io::write<uint32>(optionalItem.itemId)
				<< io::write<uint32>(optionalItem.count);
		}

		return writer;
	}

	io::Reader& operator>>(io::Reader& reader, QuestInfo& outItemInfo)
	{
		reader >> io::read<uint64>(outItemInfo.id)
			>> io::read_container<uint8>(outItemInfo.title)
			>> io::read_container<uint16>(outItemInfo.summary)
			>> io::read_container<uint16>(outItemInfo.description);

		reader
			>> io::read<uint32>(outItemInfo.questLevel)
			>> io::read<uint32>(outItemInfo.rewardXp)
			>> io::read<uint32>(outItemInfo.rewardMoney)
			>> io::read<uint32>(outItemInfo.rewardSpellId);

		uint8 num = 0;
		reader >> io::read<uint8>(num);
		for (uint8 i = 0; i < num; ++i)
		{
			QuestRequiredItem item;
			reader
				>> io::read<uint32>(item.itemId)
				>> io::read<uint32>(item.count);
			outItemInfo.requiredItems.emplace_back(item);
		}

		reader >> io::read<uint8>(num);
		for (uint8 i = 0; i < num; ++i)
		{
			QuestRequiredCreature creature;
			reader
				>> io::read<uint32>(creature.creatureId)
				>> io::read<uint32>(creature.count);
			outItemInfo.requiredCreatures.emplace_back(creature);
		}

		reader >> io::read<uint8>(num);
		for (uint8 i = 0; i < num; ++i)
		{
			QuestRewardItem item;
			reader
				>> io::read<uint32>(item.itemId)
				>> io::read<uint32>(item.count);
			outItemInfo.rewardItems.emplace_back(item);
		}

		reader >> io::read<uint8>(num);
		for (uint8 i = 0; i < num; ++i)
		{
			QuestRewardItem item;
			reader
				>> io::read<uint32>(item.itemId)
				>> io::read<uint32>(item.count);
			outItemInfo.optionalItems.emplace_back(item);
		}

		return reader;
	}
}
