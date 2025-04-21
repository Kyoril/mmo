
#include "game_server/loot_instance.h"

#include "game_server/objects/game_player_s.h"
#include "base/utilities.h"

namespace mmo
{
	LootInstance::LootInstance(const proto::ItemManager& items, uint64 lootGuid)
		: m_itemManager(items)
		, m_lootGuid(lootGuid)
		, m_gold(0)
	{
	}

	LootInstance::LootInstance(const proto::ItemManager& items, uint64 lootGuid, const proto::LootEntry* entry,
		uint32 minGold, uint32 maxGold, const std::vector<std::weak_ptr<GamePlayerS>>& lootRecipients)
		: m_itemManager(items)
		, m_lootGuid(lootGuid)
		, m_gold(0)
	{
		m_recipients.reserve(lootRecipients.size());
		for (const auto& character : lootRecipients)
		{
			if (const auto strongChar = character.lock())
			{
				m_recipients.push_back(strongChar->GetGuid());
			}
		}
		
		if (entry)
		{
			for (int i = 0; i < entry->groups_size(); ++i)
			{
				const auto& group = entry->groups(i);

				std::uniform_real_distribution<float> lootDistribution(0.0f, 100.0f);
				float groupRoll = lootDistribution(randomGenerator);

				//auto shuffled = group;
				//TODO std::shuffle(shuffled.definitions().begin(), shuffled.definitions().end(), randomGenerator);

				std::vector<const proto::LootDefinition*> equalChanced;
				std::vector<const proto::LootDefinition*> nonEqualChanced;
				for (int i = 0; i < group.definitions_size(); ++i)
				{
					const auto& def = group.definitions(i);

					// Is quest item?
					if (def.conditiontype() == 9)
					{
						uint32 questItemCount = 0;
						uint32 questId = def.conditionvala();
						for (const auto& recipient : lootRecipients)
						{
							/*if (recipient &&
								recipient->needsQuestItem(def.item()))
							{
								if (recipient->getQuestStatus(questId) == game::quest_status::Incomplete)
								{
									questItemCount++;
									break;		// Later...
								}
							}*/
						}

						// Skip this quest item
						if (questItemCount == 0)
						{
							continue;
						}
					}

					if (def.dropchance() == 0.0f)
					{
						equalChanced.push_back(&def);
						continue;
					}

					if (def.dropchance() > 0.0f &&
						def.dropchance() >= groupRoll)
					{
						nonEqualChanced.push_back(&def);
					}
				}

				if (nonEqualChanced.empty() && !equalChanced.empty())
				{
					std::uniform_int_distribution<uint32> equalDistribution(0, equalChanced.size() - 1);
					uint32 index = equalDistribution(randomGenerator);
					AddLootItem(*equalChanced[index]);
				}
				else if (!nonEqualChanced.empty())
				{
					std::uniform_int_distribution<uint32> nonEqualDistribution(0, nonEqualChanced.size() - 1);
					uint32 index = nonEqualDistribution(randomGenerator);
					AddLootItem(*nonEqualChanced[index]);
				}
			}
		}

		// Generate gold
		std::uniform_int_distribution goldDistribution(minGold, maxGold);
		m_gold = goldDistribution(randomGenerator);
	}

	bool LootInstance::IsEmpty() const
	{
		// Determine if there is gold to loot
		if (HasGold())
		{
			return false;
		}

		// There is no gold, but there are items available that we need to check one by one
		for (const auto& item : m_items)
		{
			// If the item hasn't been looted yet...
			if (!item.isLooted)
			{
				// Check if this is a party-shared loot item
				const auto* entry = m_itemManager.getById(item.definition.item());
				if (!entry)
					continue;

				if (entry->flags() & item_flags::PartyLoot)
				{
					// It is shared by the party, so we need to determine if at least one loot receiver 
					// can still loot this item
					for (const auto& guid : m_recipients)
					{
						auto dataIt = m_playerLootData.find(guid);
						if (dataIt == m_playerLootData.end())
						{
							// No shared loots for this player yet
							return false;
						}

						auto counterIt = dataIt->second.find(entry->id());
						if (counterIt == dataIt->second.end())
						{
							// No loots of this item by this player yet
							return false;
						}

						if (counterIt->second < item.count)
						{
							// Item not looted often enough by this player yet
							return false;
						}
					}
				}
				else
				{
					// This item is not shared, so it is available and we can stop here
					return false;
				}
			}
		}

		// No lootable items found - loot is empty
		return true;
	}

	bool LootInstance::ContainsLootFor(uint64 receiver)
	{
		// Gold available?
		if (HasGold())
		{
			return true;
		}

		// Now check all items...
		for (auto& item : m_items)
		{
			// Item not looted?
			if (!item.isLooted)
			{
				const auto* entry = m_itemManager.getById(item.definition.item());
				if (!entry)
					continue;

				// Check if item is a shared item...
				if (entry->flags() & item_flags::PartyLoot)
				{
					auto it = m_playerLootData.find(receiver);
					if (it == m_playerLootData.end())
					{
						// Not looted yet, so loot is available
						return true;
					}

					auto it2 = it->second.find(entry->id());
					if (it2 == it->second.end())
					{
						// This item hasn't been looted yet, so loot is available
						return true;
					}

					if (it2->second < item.count)
					{
						// Not enough items looted - so loot is available
						return true;
					}
				}
				else
				{
					// Item isn't looted and is not sharable - so loot is available
					return true;
				}
			}
		}

		// No lootable item is available for this player
		return false;
	}

	void LootInstance::TakeGold()
	{
		if (!HasGold())
		{
			return;
		}

		// Remove gold
		m_gold = 0;

		// Notify all looting players
		goldRemoved();

		// Notify loot source object
		if (IsEmpty())
		{
			cleared();
		}
	}

	const LootItem* LootInstance::GetLootDefinition(uint8 slot) const
	{
		if (slot >= m_items.size()) {
			return nullptr;
		}

		return &m_items[slot];
	}

	void LootInstance::TakeItem(uint8 slot, uint64 receiver)
	{
		// Check if slot is valid
		if (slot >= m_items.size()) {
			return;
		}

		// Check if item was already looted
		if (m_items[slot].isLooted)
		{
			return;
		}

		// Request item entry for more data
		const auto* entry = m_itemManager.getById(m_items[slot].definition.item());
		if (!entry)
		{
			return;
		}

		// If this item is a party-shared item...
		if (entry->flags() & item_flags::PartyLoot)
		{
			// We will simply increment the counter
			auto& data = m_playerLootData[receiver];
			data[entry->id()] = m_items[slot].count;
		}
		else
		{
			// This item is not shared, so it is looted now
			m_items[slot].isLooted = true;
		}

		// Notify all watching players
		itemRemoved(slot);

		// If everything has been looted, we will call the signal. This will most likely
		// update the corpse / game object, that owns this loot instance
		if (IsEmpty())
		{
			cleared();
		}
	}

	void LootInstance::Serialize(io::Writer& writer, uint64 receiver) const
	{
		// Write gold
		writer
			<< io::write<uint32>(m_gold);

		// Write placeholder item count (real value will be overwritten later)
		const size_t itemCountpos = writer.Sink().Position();
		writer
			<< io::write<uint8>(m_items.size());

		auto dataIt = m_playerLootData.find(receiver);

		// Iterate through all loot items...
		uint8 realCount = 0;
		uint8 slot = 0;
		for (const auto& def : m_items)
		{
			const auto* itemEntry = m_itemManager.getById(def.definition.item());
			if (itemEntry)
			{
				bool isLooted = def.isLooted;
				if (!isLooted &&					// Not yet looted
					(itemEntry->flags() & item_flags::PartyLoot) && // Shared item
					dataIt != m_playerLootData.end())		// Player loot data available
				{
					auto counterIt = dataIt->second.find(itemEntry->id());
					if (counterIt != dataIt->second.end())
					{
						isLooted = (counterIt->second >= def.count);
					}
				}

				// Only write item entry if the item hasn't been looted yet
				if (!isLooted)
				{
					writer
						<< io::write<uint8>(slot)
						<< io::write<uint32>(def.definition.item())
						<< io::write<uint32>(def.count)
						<< io::write<uint32>(itemEntry->displayid())
						<< io::write<uint32>(0)	// RandomSuffixIndex TODO
						<< io::write<uint32>(0)	// RandomPropertyId TODO
						<< io::write<uint8>(loot_slot_type::AllowLoot)
						;
					realCount++;
				}
			}

			slot++;
		}

		// Overwrite real item count
		writer.WritePOD(itemCountpos, realCount);
	}

	void LootInstance::AddLootItem(const proto::LootDefinition& def)
	{
		const auto* lootItem = m_itemManager.getById(def.item());
		if (!lootItem) {
			return;
		}

		uint32 dropCount = def.mincount();
		if (def.maxcount() > def.mincount())
		{
			std::uniform_int_distribution<uint32> dropDistribution(def.mincount(), def.maxcount());
			dropCount = dropDistribution(randomGenerator);
		}

		if (dropCount > lootItem->maxstack())
		{
			WLOG("Loot entry: Item's " << def.item() << " drop count was " << dropCount << " but max item stack count is " << lootItem->maxstack());
			dropCount = lootItem->maxstack();
		}

		// Always at least 1 item
		if (dropCount == 0) {
			dropCount = 1;
		}

		m_items.emplace_back(LootItem(dropCount, def));
	}
}
