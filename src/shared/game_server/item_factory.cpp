// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "item_factory.h"

#include "game_server/objects/game_bag_s.h"
#include "game_server/objects/game_item_s.h"
#include "game_server/objects/game_object_s.h"
#include "inventory_types.h"
#include "shared/proto_data/items.pb.h"
#include "base/macros.h"

namespace mmo
{
    ItemFactory::ItemFactory(const IItemFactoryContext& context) noexcept
        : m_context(context)
    {
    }

    std::shared_ptr<GameItemS> ItemFactory::CreateItem(
        const proto::ItemEntry& entry,
        uint16 slot,
        uint16 stackCount) const noexcept
    {
        ASSERT(stackCount >= 1);

        // Step 1: Create the appropriate type instance
        auto item = CreateItemInstance(entry);
        ASSERT(item);

        // Step 2: Initialize field map (must be done before any field access)
        item->Initialize();

        // Step 3: Set up GUID, owner, and container relationships
        InitializeItemFields(item, entry, slot);

        // Step 4: Apply binding rules
        ApplyBindingRules(item, entry);

        // Step 5: Set stack count (Initialize() sets it to 1 by default)
        if (stackCount > 1)
        {
            SetStackCount(item, stackCount);
        }

        return item;
    }

    std::shared_ptr<GameItemS> ItemFactory::CreateItemInstance(
        const proto::ItemEntry& entry) const noexcept
    {
        const auto& project = m_context.GetProject();

        // Create bag for container/quiver items, regular item otherwise
        if (entry.itemclass() == item_class::Container ||
            entry.itemclass() == item_class::Quiver)
        {
            return std::make_shared<GameBagS>(project, entry);
        }

        return std::make_shared<GameItemS>(project, entry);
    }

	void ItemFactory::InitializeItemFields(
		std::shared_ptr<GameItemS>& item,
		const proto::ItemEntry& entry,
		uint16 slot) const noexcept
	{
		// Generate and assign unique GUID
		const uint64 newItemId = m_context.GenerateItemId();
		const uint64 itemGuid = CreateEntryGUID(newItemId, entry.id(), GuidType::Item);
		item->Set<uint64>(object_fields::Guid, itemGuid);

		// Assign owner
		const uint64 ownerGuid = m_context.GetOwnerGuid();
		item->Set<uint64>(object_fields::ItemOwner, ownerGuid);

		// Assign container (parent bag or owner)
		const InventorySlot invSlot = InventorySlot::FromAbsolute(slot);
		uint64 containerGuid = ownerGuid;

		if (invSlot.IsBag())
		{
			// Item is in an equipped bag - derive bag equip slot from bag ID
			// Bag ID (19-22) maps to bag equip slot in main inventory
			const uint8 bagId = invSlot.GetBag();
			const uint16 bagEquipSlot = InventorySlot::FromRelative(
				player_inventory_slots::Bag_0,
				bagId
			).GetAbsolute();

			auto bag = m_context.GetBagAtSlot(bagEquipSlot);
			if (bag)
			{
				containerGuid = bag->GetGuid();
			}
		}

		item->Set<uint64>(object_fields::Contained, containerGuid);
	}    void ItemFactory::ApplyBindingRules(
        std::shared_ptr<GameItemS>& item,
        const proto::ItemEntry& entry) const noexcept
    {
        // Apply Bind-on-Pickup binding
        if (entry.bonding() == item_binding::BindWhenPickedUp)
        {
            item->AddFlag<uint32>(object_fields::ItemFlags, item_flags::Bound);
        }
    }

    void ItemFactory::SetStackCount(
        std::shared_ptr<GameItemS>& item,
        uint16 stackCount) const noexcept
    {
        ASSERT(stackCount >= 1);

        // AddStacks adds to existing count (which is 1 after Initialize())
        if (stackCount > 1)
        {
            item->AddStacks(stackCount - 1);
        }
    }
}
