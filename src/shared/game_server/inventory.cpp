// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "inventory.h"

#include "game_server/objects/game_bag_s.h"
#include "game_server/objects/game_item_s.h"
#include "game_server/objects/game_player_s.h"
#include "inventory_repository.h"
#include "base/linear_set.h"
#include "base/utilities.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"
#include "proto_data/project.h"
#include "shared/proto_data/items.pb.h"

namespace mmo
{

	io::Writer &operator<<(io::Writer &w, ItemData const &object)
	{
		w.WritePOD(object);
		return w;
	}

	io::Reader &operator>>(io::Reader &r, ItemData &object)
	{
		r.readPOD(object);
		return r;
	}

	Inventory::Inventory(GamePlayerS &owner)
		: m_owner(owner), m_freeSlots(player_inventory_pack_slots::End - player_inventory_pack_slots::Start) // Default slot count with only a backpack
		  ,
		  m_nextBuyBackSlot(player_buy_back_slots::Start), m_repository(nullptr), m_isDirty(false), m_validator(std::make_unique<ItemValidator>(owner)), m_slotManager(std::make_unique<SlotManager>(*this)), m_commandFactory(std::make_unique<InventoryCommandFactory>(*this, *this, *this)), m_commandLogger(std::make_unique<InventoryCommandLogger>())
	{
		// Connect to item change signals to automatically mark inventory as dirty
		m_inventoryConnections += itemInstanceCreated.connect([this](std::shared_ptr<GameItemS>, uint16)
															  { MarkDirty(); });

		m_inventoryConnections += itemInstanceUpdated.connect([this](std::shared_ptr<GameItemS>, uint16)
															  { MarkDirty(); });

		m_inventoryConnections += itemInstanceDestroyed.connect([this](std::shared_ptr<GameItemS>, uint16)
																{ MarkDirty(); });
	}

	// Helper method to validate item count limits
	InventoryChangeFailure Inventory::ValidateItemLimits(const proto::ItemEntry &entry, uint16 amount) const
	{
		const uint16 itemCount = GetItemCount(entry.id());
		if (entry.maxcount() > 0 && static_cast<uint32>(itemCount + amount) > entry.maxcount())
		{
			return inventory_change_failure::CantCarryMoreOfThis;
		}

		// Quick check if there are enough free slots (only works if we don't have an item of this type yet)
		const uint16 requiredSlots = (amount - 1) / entry.maxstack() + 1;
		if ((itemCount == 0 || entry.maxstack() <= 1) && requiredSlots > m_freeSlots)
		{
			return inventory_change_failure::InventoryFull;
		}

		return inventory_change_failure::Okay;
	}

	// Helper method to find available slots for item creation
	InventoryChangeFailure Inventory::FindAvailableSlots(const proto::ItemEntry &entry, uint16 amount, ItemSlotInfo &slotInfo) const
	{
		const uint16 itemCount = GetItemCount(entry.id());
		const uint16 requiredSlots = (amount - 1) / entry.maxstack() + 1;
		uint16 itemsProcessed = 0;

		ForEachBag([&](uint8 bag, uint8 slotStart, uint8 slotEnd) -> bool
				   {
			for (uint8 slot = slotStart; slot < slotEnd; ++slot)
			{
				const uint16 absoluteSlot = GetAbsoluteSlot(bag, slot);
				auto it = m_itemsBySlot.find(absoluteSlot);

				if (it == m_itemsBySlot.end())
				{
					// Empty slot found
					slotInfo.availableStacks += entry.maxstack();
					slotInfo.emptySlots.add(absoluteSlot);

					if (itemsProcessed >= itemCount && slotInfo.emptySlots.size() >= requiredSlots)
					{
						break;
					}
					continue;
				}

				// Check if item matches entry
				if (it->second->GetEntry().id() != entry.id())
				{
					continue;
				}

				const uint32 stackCount = it->second->GetStackCount();
				itemsProcessed += stackCount;

				// Check if stack can accept more items
				if (stackCount < entry.maxstack())
				{
					slotInfo.availableStacks += (entry.maxstack() - stackCount);
					slotInfo.usedCapableSlots.add(absoluteSlot);
				}

				if (itemsProcessed >= itemCount && slotInfo.emptySlots.size() >= requiredSlots)
				{
					break;
				}
			}

			return itemsProcessed < itemCount || slotInfo.emptySlots.size() < requiredSlots; });

		return amount > slotInfo.availableStacks ? inventory_change_failure::InventoryFull : inventory_change_failure::Okay;
	}

	// Helper method to add items to existing stacks
	uint16 Inventory::AddToExistingStacks(const proto::ItemEntry &entry, uint16 amount, const LinearSet<uint16> &usedCapableSlots, std::map<uint16, uint16> *out_addedBySlot)
	{
		uint16 amountLeft = amount;

		for (const auto &slot : usedCapableSlots)
		{
			auto item = m_itemsBySlot[slot];
			const uint16 added = item->AddStacks(amountLeft);
			amountLeft -= added;

			if (added > 0)
			{
				UpdateItemStack(entry, item, slot, added, out_addedBySlot);
			}

			if (amountLeft == 0)
			{
				break;
			}
		}

		return amountLeft;
	}

	// Helper method to update item stack and notify systems
	void Inventory::UpdateItemStack(const proto::ItemEntry &entry, std::shared_ptr<GameItemS> item, uint16 slot, uint16 added, std::map<uint16, uint16> *out_addedBySlot)
	{
		m_itemCounter[entry.id()] += added;

		if (out_addedBySlot)
		{
			out_addedBySlot->insert(std::make_pair(slot, added));
		}

		itemInstanceUpdated(item, slot);
		NotifySlotUpdate(slot);
	}

	// Helper method to notify about slot updates
	void Inventory::NotifySlotUpdate(uint16 slot)
	{
		if (IsInventorySlot(slot) || IsEquipmentSlot(slot) || IsBagPackSlot(slot))
		{
			m_owner.Invalidate(object_fields::InvSlotHead + ((slot & 0xFF) * 2));
			m_owner.Invalidate(object_fields::InvSlotHead + ((slot & 0xFF) * 2) + 1);
		}
		else if (IsBagSlot(slot))
		{
			auto bag = GetBagAtSlot(slot);
			if (bag)
			{
				bag->Invalidate(object_fields::Slot_1 + (slot & 0xFF) * 2);
				bag->Invalidate(object_fields::Slot_1 + (slot & 0xFF) * 2 + 1);
				itemInstanceUpdated(bag, slot);
			}
		}
	}

	// Helper method to create new item instances
	uint16 Inventory::CreateNewItems(const proto::ItemEntry &entry, uint16 amount, const LinearSet<uint16> &emptySlots, std::map<uint16, uint16> *out_addedBySlot)
	{
		uint16 amountLeft = amount;

		for (const auto &slot : emptySlots)
		{
			auto item = CreateSingleItem(entry, slot);
			if (!item)
			{
				continue;
			}

			// One stack has been created by initializing the item
			amountLeft--;

			// Add additional stacks
			const uint16 added = item->AddStacks(amountLeft);
			amountLeft -= added;

			// Update counters and tracking
			const uint16 totalAdded = added + 1;
			m_itemCounter[entry.id()] += totalAdded;

			if (out_addedBySlot)
			{
				out_addedBySlot->insert(std::make_pair(slot, totalAdded));
			}

			// Add to inventory
			AddItemToSlot(item, slot);

			if (amountLeft == 0)
			{
				break;
			}
		}

		return amountLeft;
	}

	// Helper method to create a single item instance
	std::shared_ptr<GameItemS> Inventory::CreateSingleItem(const proto::ItemEntry &entry, uint16 slot)
	{
		// Create appropriate item type
		std::shared_ptr<GameItemS> item;
		if (entry.itemclass() == item_class::Container || entry.itemclass() == item_class::Quiver)
		{
			item = std::make_shared<GameBagS>(m_owner.GetProject(), entry);
		}
		else
		{
			item = std::make_shared<GameItemS>(m_owner.GetProject(), entry);
		}

		item->Initialize();
		SetupNewItem(item, entry, slot);
		return item;
	}

	// Helper method to setup a newly created item
	void Inventory::SetupNewItem(std::shared_ptr<GameItemS> item, const proto::ItemEntry &entry, uint16 slot)
	{
		auto *world = m_owner.GetWorldInstance();
		ASSERT(world);

		// Generate new ID and setup basic properties
		auto newItemId = world->GetItemIdGenerator().GenerateId();
		item->Set<uint64>(object_fields::Guid, CreateEntryGUID(newItemId, entry.id(), GuidType::Item));
		item->Set<uint64>(object_fields::ItemOwner, m_owner.GetGuid());

		// Set container based on slot type
		if (IsBagSlot(slot))
		{
			auto bagInst = GetBagAtSlot(slot);
			item->Set<uint64>(object_fields::Contained, bagInst ? bagInst->GetGuid() : m_owner.GetGuid());
		}
		else
		{
			item->Set<uint64>(object_fields::Contained, m_owner.GetGuid());
		}

		// Apply binding
		if (entry.bonding() == item_binding::BindWhenPickedUp)
		{
			item->AddFlag<uint32>(object_fields::ItemFlags, item_flags::Bound);
		}
	}

	// Helper method to add an item to a specific slot and update all related systems
	void Inventory::AddItemToSlot(std::shared_ptr<GameItemS> item, uint16 slot)
	{
		// Add to slot map and update free slot count
		m_itemsBySlot[slot] = item;
		m_freeSlots--;

		// Setup despawn signal watching
		m_itemDespawnSignals[item->GetGuid()] = item->despawned.connect(
			std::bind(&Inventory::OnItemDespawned, this, std::placeholders::_1));

		// Notify about item creation
		itemInstanceCreated(item, slot);

		// Update player fields based on slot type
		UpdatePlayerFieldsForNewItem(item, slot);
	}

	// Helper method to update player fields when adding a new item
	void Inventory::UpdatePlayerFieldsForNewItem(std::shared_ptr<GameItemS> item, uint16 slot)
	{
		uint8 bag = 0, subslot = 0;
		GetRelativeSlots(slot, bag, subslot);

		if (bag == player_inventory_slots::Bag_0)
		{
			m_owner.Set<uint64>(object_fields::InvSlotHead + (subslot * 2), item->GetGuid());

			if (IsBagBarSlot(slot))
			{
				m_owner.ApplyItemStats(*item, true);
			}

			if (IsEquipmentSlot(slot))
			{
				UpdateEquipmentVisuals(item, subslot);
				m_owner.ApplyItemStats(*item, true);
			}
		}
		else if (IsBagSlot(slot))
		{
			UpdateBagSlot(item, bag, subslot);
		}
	}

	// Helper method to update equipment visuals
	void Inventory::UpdateEquipmentVisuals(std::shared_ptr<GameItemS> item, uint8 subslot)
	{
		constexpr uint32 slotSize = object_fields::VisibleItem2_CREATOR - object_fields::VisibleItem1_CREATOR;
		m_owner.Set<uint32>(object_fields::VisibleItem1_0 + (subslot * slotSize), item->GetEntry().id());
		m_owner.Set<uint64>(object_fields::VisibleItem1_CREATOR + (subslot * slotSize), item->Get<uint64>(object_fields::Creator));
	}

	// Helper method to update bag slot
	void Inventory::UpdateBagSlot(std::shared_ptr<GameItemS> item, uint8 bag, uint8 subslot)
	{
		auto packSlot = GetAbsoluteSlot(player_inventory_slots::Bag_0, bag);
		auto bagInst = GetBagAtSlot(packSlot);
		if (bagInst)
		{
			bagInst->Set<uint64>(object_fields::Slot_1 + (subslot * 2), item->GetGuid());
			itemInstanceUpdated(bagInst, packSlot);
		}
	}

	InventoryChangeFailure Inventory::CreateItems(const proto::ItemEntry &entry, uint16 amount /* = 1*/, std::map<uint16, uint16> *out_addedBySlot /* = nullptr*/)
	{
		if (amount == 0)
		{
			amount = 1;
		}

		// Validate item count limits
		if (auto result = ValidateItemLimits(entry, amount); result != inventory_change_failure::Okay)
		{
			return result;
		}

		// Find available slots for items
		ItemSlotInfo slotInfo;
		if (auto result = FindAvailableSlots(entry, amount, slotInfo); result != inventory_change_failure::Okay)
		{
			return result;
		}

		// Add items to existing stacks first
		uint16 amountLeft = amount;
		amountLeft = AddToExistingStacks(entry, amountLeft, slotInfo.usedCapableSlots, out_addedBySlot);

		// Create new items for remaining amount
		if (amountLeft > 0)
		{
			amountLeft = CreateNewItems(entry, amountLeft, slotInfo.emptySlots, out_addedBySlot);
		}

		// Validate that all items were created successfully
		if (amountLeft > 0)
		{
			ELOG("Could not add all items, something went really wrong! " << __FUNCTION__);
			return inventory_change_failure::InventoryFull;
		}

		// Quest check
		m_owner.OnQuestItemAddedCredit(entry, amount);
		return inventory_change_failure::Okay;
	}

	InventoryChangeFailure Inventory::AddItem(std::shared_ptr<GameItemS> item, uint16 *out_slot)
	{
		ASSERT(item);

		const auto &entry = item->GetEntry();

		// Validate item count limits
		const uint16 itemCount = GetItemCount(entry.id());
		if (entry.maxcount() > 0 &&
			static_cast<uint32>(itemCount + item->GetStackCount()) > entry.maxcount())
		{
			return inventory_change_failure::CantCarryMoreOfThis;
		}

		if (m_freeSlots < 1)
		{
			return inventory_change_failure::InventoryFull;
		}

		// Find empty slot
		uint16 targetSlot = FindEmptySlot();
		if (targetSlot == 0)
		{
			return inventory_change_failure::InventoryFull;
		}

		// Setup item properties and add to slot
		SetupExistingItem(item, entry, targetSlot);
		AddExistingItemToSlot(item, targetSlot, out_slot);

		return inventory_change_failure::Okay;
	}

	// Helper method to find an empty slot
	uint16 Inventory::FindEmptySlot() const
	{
		uint16 targetSlot = 0;
		ForEachBag([this, &targetSlot](uint8 bag, uint8 slotStart, uint8 slotEnd) -> bool
				   {
			for (uint8 slot = slotStart; slot < slotEnd; ++slot)
			{
				const uint16 absoluteSlot = GetAbsoluteSlot(bag, slot);

				// Check if this slot is empty
				auto it = m_itemsBySlot.find(absoluteSlot);
				if (it == m_itemsBySlot.end())
				{
					// Slot is empty - use it!
					targetSlot = absoluteSlot;
					return false;
				}
			}

			// Continue with the next bag
			return true; });

		return targetSlot;
	}

	// Helper method to setup properties for an existing item
	void Inventory::SetupExistingItem(std::shared_ptr<GameItemS> item, const proto::ItemEntry &entry, uint16 targetSlot)
	{
		// Fix item properties like container guid and owner guid
		if (IsBagSlot(targetSlot))
		{
			auto bagInst = GetBagAtSlot(targetSlot);
			ASSERT(bagInst);
			item->Set<uint64>(object_fields::Contained, bagInst ? bagInst->GetGuid() : m_owner.GetGuid());
		}
		else
		{
			item->Set<uint64>(object_fields::Contained, m_owner.GetGuid());
		}
		item->Set<uint64>(object_fields::ItemOwner, m_owner.GetGuid());

		// Bind this item
		if (entry.bonding() == item_binding::BindWhenPickedUp)
		{
			item->AddFlag<uint32>(object_fields::ItemFlags, item_flags::Bound);
		}
	}

	// Helper method to add an existing item to a slot
	void Inventory::AddExistingItemToSlot(std::shared_ptr<GameItemS> item, uint16 targetSlot, uint16 *out_slot)
	{
		// Increase cached counter
		m_itemCounter[item->GetEntry().id()] += item->GetStackCount();
		if (out_slot)
		{
			*out_slot = targetSlot;
		}

		// Add this item to the inventory slot and reduce our free slot cache
		m_itemsBySlot[targetSlot] = item;
		m_freeSlots--;

		// Watch for item despawn packet
		m_itemDespawnSignals[item->GetGuid()] = item->despawned.connect(
			std::bind(&Inventory::OnItemDespawned, this, std::placeholders::_1));

		// Create the item instance
		itemInstanceCreated(item, targetSlot);

		// Update player fields based on slot type
		UpdatePlayerFieldsForNewItem(item, targetSlot);
	}

	InventoryChangeFailure Inventory::RemoveItems(const proto::ItemEntry &entry, uint16 amount)
	{
		// If amount equals 0, remove ALL items of that entry.
		const uint16 itemCount = GetItemCount(entry.id());
		if (amount == 0)
		{
			amount = itemCount;
		}

		// We don't have enough items, so we don't need to bother iterating through
		if (itemCount < amount)
		{
			// Maybe use a different result
			return inventory_change_failure::ItemNotFound;
		}

		// Counter used to know when to stop iteration
		uint16 itemsToDelete = amount;

		ForEachBag([this, &itemCount, &entry, &itemsToDelete](uint8 bag, uint8 slotStart, uint8 slotEnd) -> bool
				   {
				for (uint8 slot = slotStart; slot < slotEnd; ++slot)
				{
					const uint16 absoluteSlot = GetAbsoluteSlot(bag, slot);

					// Check if this slot is empty
					auto it = m_itemsBySlot.find(absoluteSlot);
					if (it == m_itemsBySlot.end())
					{
						// Empty slot
						continue;
					}

					// It is not empty, so check if the item is of the same entry
					if (it->second->GetEntry().id() != entry.id())
					{
						// Different item
						continue;
					}

					// Get the items stack count
					const uint32 stackCount = it->second->GetStackCount();
					if (stackCount <= itemsToDelete)
					{
						// Remove item at this slot
						auto result = RemoveItem(absoluteSlot);
						if (result != inventory_change_failure::Okay)
						{
							ELOG("Could not remove item at slot " << absoluteSlot);
						}
						else
						{
							// Reduce counter
							itemsToDelete -= stackCount;
						}
					}
					else
					{
						// Reduce stack count
						it->second->Set<uint32>(object_fields::StackCount, stackCount - itemsToDelete);
						m_itemCounter[entry.id()] -= itemsToDelete;
						itemsToDelete = 0;

						// Notify client about this update
						itemInstanceUpdated(it->second, slot);
					}

					// All items processed, we can stop here
					if (itemsToDelete == 0) {
						return false;
					}
				}

				return true; });

		// WARNING: There should never be any items left here!
		ASSERT(itemsToDelete == 0);
		ASSERT(m_itemCounter[entry.id()] == itemCount - amount);

		return inventory_change_failure::Okay;
	}

	InventoryChangeFailure Inventory::RemoveItem(uint16 absoluteSlot, uint16 stacks /* = 0*/, bool sold /* = false*/)
	{
		// Try to find item
		auto it = m_itemsBySlot.find(absoluteSlot);
		if (it == m_itemsBySlot.end())
		{
			return inventory_change_failure::ItemNotFound;
		}

		// Updated cached item counter
		const uint32 stackCount = it->second->GetStackCount();
		if (stacks == 0 || stacks > stackCount)
		{
			stacks = stackCount;
		}
		m_itemCounter[it->second->GetEntry().id()] -= stacks;

		// Increase ref count since the item instance might be destroyed below
		auto item = it->second;
		if (stackCount == stacks)
		{
			// No longer watch for item despawn
			m_itemDespawnSignals.erase(item->GetGuid());

			// Remove item from slot
			m_itemsBySlot.erase(it);
			m_freeSlots++;

			uint8 bag = 0, subslot = 0;
			GetRelativeSlots(absoluteSlot, bag, subslot);
			if (bag == player_inventory_slots::Bag_0)
			{
				m_owner.Set<uint64>(object_fields::InvSlotHead + (subslot * 2), 0);
				if (IsBagBarSlot(absoluteSlot))
				{
					m_owner.ApplyItemStats(*item, false);
				}

				if (IsEquipmentSlot(absoluteSlot))
				{
					constexpr uint32 slotSize = object_fields::VisibleItem2_CREATOR - object_fields::VisibleItem1_CREATOR;
					m_owner.Set<uint32>(object_fields::VisibleItem1_0 + (subslot * slotSize), item->GetEntry().id());
					m_owner.Set<uint64>(object_fields::VisibleItem1_CREATOR + (subslot * slotSize), item->Get<uint64>(object_fields::Creator));
					m_owner.ApplyItemStats(*item, false);
				}
			}
			else if (IsBagSlot(absoluteSlot))
			{
				auto packSlot = GetAbsoluteSlot(player_inventory_slots::Bag_0, bag);
				auto bagInst = GetBagAtSlot(packSlot);
				if (bagInst)
				{
					bagInst->Set<uint64>(object_fields::Slot_1 + (subslot * 2), 0);
					itemInstanceUpdated(bagInst, packSlot);
				}
			}

			// Notify about destruction
			// Note: Even if sold=true (moved to buyback), we fire the signal to mark inventory dirty
			if (!sold)
			{
				itemInstanceDestroyed(item, absoluteSlot);
			}
			else
			{
				// Item was sold and moved to buyback - still an inventory change that needs saving
				itemInstanceUpdated(item, absoluteSlot);
			}
		}
		else
		{
			item->Set<uint32>(object_fields::StackCount, stackCount - stacks);
			itemInstanceUpdated(it->second, absoluteSlot);
		} // If the item has been sold...
		if (sold)
		{
			// Find the next free buyback slot
			uint16 oldestFieldSlot = 0;
			uint16 slot = player_buy_back_slots::Start;
			uint32 oldestSlotTime = std::numeric_limits<uint32>::max();

			do
			{
				const uint16 fieldSlot = slot - player_buy_back_slots::Start;
				const uint16 buyBackGuid = m_owner.Get<uint64>(object_fields::VendorBuybackSlot_1 + (fieldSlot * 2));
				if (buyBackGuid == 0)
				{
					// Found a free slot
					break;
				}

				const uint32 slotTime = m_owner.Get<uint32>(object_fields::BuybackTimestamp_1 + fieldSlot);
				if (slotTime < oldestSlotTime)
				{
					oldestSlotTime = slotTime;
					oldestFieldSlot = slot;
				}
			} while (++slot < player_buy_back_slots::End);

			// Check if there is a slot available
			if (slot >= player_buy_back_slots::End)
			{
				// No free slot available, discard the oldest one
				if (const auto itemInst = m_itemsBySlot[oldestFieldSlot])
				{
					itemInstanceDestroyed(std::cref(itemInst), oldestFieldSlot);
					m_itemsBySlot.erase(oldestFieldSlot);
				}

				slot = oldestFieldSlot;
			}

			const uint32 slotTime = ::time(nullptr);
			const uint16 fieldSlot = slot - player_buy_back_slots::Start;

			// Save in new slot location
			m_itemsBySlot[slot] = item;

			// Update slots
			m_owner.Set<uint64>(object_fields::VendorBuybackSlot_1 + (fieldSlot * 2), item->GetGuid());
			m_owner.Set<uint32>(object_fields::BuybackPrice_1 + fieldSlot, item->GetEntry().sellprice() * stacks);
			m_owner.Set<uint32>(object_fields::BuybackTimestamp_1 + fieldSlot, slotTime + 30 * 3600);
		}

		// Quest check
		m_owner.OnQuestItemRemovedCredit(item->GetEntry(), stacks);

		return inventory_change_failure::Okay;
	}

	InventoryChangeFailure Inventory::RemoveItemByGUID(uint64 guid, uint16 stacks)
	{
		uint16 slot = 0;
		if (!FindItemByGUID(guid, slot))
		{
			return inventory_change_failure::InternalBagError;
		}

		return RemoveItem(slot, stacks);
	}

	InventoryChangeFailure Inventory::SwapItems(uint16 slotA, uint16 slotB)
	{
		auto srcItem = GetItemAtSlot(slotA);
		auto dstItem = GetItemAtSlot(slotB);

		// Validate swap prerequisites
		if (auto result = ValidateSwapPrerequisites(srcItem, dstItem, slotA, slotB); result != inventory_change_failure::Okay)
		{
			return result;
		}

		// Try to merge stacks if possible
		if (dstItem && CanMergeItems(srcItem, dstItem))
		{
			return MergeItemStacks(srcItem, dstItem, slotA, slotB);
		}

		// Validate bag constraints
		if (auto result = ValidateBagConstraints(srcItem, slotB); result != inventory_change_failure::Okay)
		{
			return result;
		}

		// Perform the actual swap
		PerformItemSwap(srcItem, dstItem, slotA, slotB);
		return inventory_change_failure::Okay;
	}

	namespace
	{
		weapon_prof::Type weaponProficiency(uint32 subclass)
		{
			switch (subclass)
			{
			case item_subclass_weapon::OneHandedAxe:
				return weapon_prof::OneHandAxe;
			case item_subclass_weapon::TwoHandedAxe:
				return weapon_prof::TwoHandAxe;
			case item_subclass_weapon::Bow:
				return weapon_prof::Bow;
			case item_subclass_weapon::CrossBow:
				return weapon_prof::Crossbow;
			case item_subclass_weapon::Dagger:
				return weapon_prof::Dagger;
			case item_subclass_weapon::Fist:
				return weapon_prof::Fist;
			case item_subclass_weapon::Gun:
				return weapon_prof::Gun;
			case item_subclass_weapon::OneHandedMace:
				return weapon_prof::OneHandMace;
			case item_subclass_weapon::TwoHandedMace:
				return weapon_prof::TwoHandMace;
			case item_subclass_weapon::Polearm:
				return weapon_prof::Polearm;
			case item_subclass_weapon::Staff:
				return weapon_prof::Staff;
			case item_subclass_weapon::OneHandedSword:
				return weapon_prof::OneHandSword;
			case item_subclass_weapon::TwoHandedSword:
				return weapon_prof::TwoHandSword;
			case item_subclass_weapon::Thrown:
				return weapon_prof::Throw;
			case item_subclass_weapon::Wand:
				return weapon_prof::Wand;
			}

			return weapon_prof::None;
		}

		armor_prof::Type armorProficiency(uint32 subclass)
		{
			switch (subclass)
			{
			case item_subclass_armor::Misc:
				return armor_prof::Common;
			case item_subclass_armor::Buckler:
			case item_subclass_armor::Shield:
				return armor_prof::Shield;
			case item_subclass_armor::Cloth:
				return armor_prof::Cloth;
			case item_subclass_armor::Leather:
				return armor_prof::Leather;
			case item_subclass_armor::Mail:
				return armor_prof::Mail;
			case item_subclass_armor::Plate:
				return armor_prof::Plate;
			}

			return armor_prof::None;
		}
	}

	InventoryChangeFailure Inventory::IsValidSlot(uint16 slot, const proto::ItemEntry &entry) const
	{
		// Split the absolute slot
		uint8 bag = 0, subslot = 0;
		if (!GetRelativeSlots(slot, bag, subslot))
		{
			return inventory_change_failure::InternalBagError;
		}

		// Check if it is a special bag....
		if (IsEquipmentSlot(slot))
		{
			auto armorProf = m_owner.GetArmorProficiency();
			auto weaponProf = m_owner.GetWeaponProficiency();

			// Determine whether the provided inventory type can go into the slot
			if (entry.itemclass() == item_class::Weapon)
			{
				if ((weaponProf & (1 << entry.subclass())) == 0)
				{
					return inventory_change_failure::NoRequiredProficiency;
				}
			}
			else if (entry.itemclass() == item_class::Armor)
			{
				if ((armorProf & (1 << entry.subclass())) == 0)
				{
					return inventory_change_failure::NoRequiredProficiency;
				}
			}

			if (entry.requiredlevel() > 0 &&
				entry.requiredlevel() > m_owner.GetLevel())
			{
				return inventory_change_failure::CantEquipLevel;
			}

			/*
			if (entry.requiredskill() != 0 &&
				!m_owner.HasSkill(entry.requiredskill()))
			{
				return inventory_change_failure::CantEquipSkill;
			}
			*/

			// Validate equipment slots
			auto srcInvType = entry.inventorytype();
			switch (subslot)
			{
			case player_equipment_slots::Head:
				if (srcInvType != inventory_type::Head)
				{
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Body:
				if (srcInvType != inventory_type::Body)
				{
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Chest:
				if (srcInvType != inventory_type::Chest &&
					srcInvType != inventory_type::Robe)
				{
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Feet:
				if (srcInvType != inventory_type::Feet)
				{
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Neck:
				if (srcInvType != inventory_type::Neck)
				{
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Ranged:
				if (srcInvType != inventory_type::Ranged &&
					srcInvType != inventory_type::Thrown &&
					srcInvType != inventory_type::RangedRight)
				{
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Finger1:
			case player_equipment_slots::Finger2:
				if (srcInvType != inventory_type::Finger)
				{
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Trinket1:
			case player_equipment_slots::Trinket2:
				if (srcInvType != inventory_type::Trinket)
				{
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Hands:
				if (srcInvType != inventory_type::Hands)
				{
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Legs:
				if (srcInvType != inventory_type::Legs)
				{
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Mainhand:
				if (srcInvType != inventory_type::MainHandWeapon &&
					srcInvType != inventory_type::TwoHandedWeapon &&
					srcInvType != inventory_type::Weapon)
				{
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}

				if (srcInvType == inventory_type::TwoHandedWeapon)
				{
					auto offhand = GetItemAtSlot(GetAbsoluteSlot(player_inventory_slots::Bag_0, player_equipment_slots::Offhand));
					if (offhand)
					{
						// We need to be able to store the offhand weapon in the inventory
						auto result = CanStoreItems(offhand->GetEntry());
						if (result != inventory_change_failure::Okay)
						{
							return result;
						}
					}
				}
				break;
			case player_equipment_slots::Offhand:
			{
				if (srcInvType != inventory_type::OffHandWeapon &&
					srcInvType != inventory_type::Shield &&
					srcInvType != inventory_type::Weapon &&
					srcInvType != inventory_type::Holdable)
				{
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				if (srcInvType != inventory_type::Shield &&
					srcInvType != inventory_type::Holdable &&
					!m_owner.CanDualWield())
				{
					return inventory_change_failure::CantDualWield;
				}

				const auto item = GetItemAtSlot(GetAbsoluteSlot(player_inventory_slots::Bag_0, player_equipment_slots::Mainhand));
				if (item &&
					item->GetEntry().inventorytype() == inventory_type::TwoHandedWeapon)
				{
					return inventory_change_failure::CantEquipWithTwoHanded;
				}
			}
			break;
			case player_equipment_slots::Shoulders:
				if (srcInvType != inventory_type::Shoulders)
				{
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Tabard:
				if (srcInvType != inventory_type::Tabard)
				{
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Waist:
				if (srcInvType != inventory_type::Waist)
				{
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Wrists:
				if (srcInvType != inventory_type::Wrists)
				{
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Back:
				if (srcInvType != inventory_type::Cloak)
				{
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			default:
				return inventory_change_failure::ItemDoesNotGoToSlot;
			}

			return inventory_change_failure::Okay;
		}

		if (IsInventorySlot(slot))
		{
			// TODO: Inventory slot validation? However, isInventorySlot already
			// performs some checks
			return inventory_change_failure::Okay;
		}

		if (IsBagSlot(slot))
		{
			// Validate bag
			auto bag = GetBagAtSlot(slot);
			if (!bag)
			{
				return inventory_change_failure::ItemDoesNotGoToSlot;
			}

			if (subslot >= bag->GetSlotCount())
			{
				return inventory_change_failure::ItemDoesNotGoToSlot;
			}

			if (bag->GetEntry().itemclass() == item_class::Quiver &&
				entry.inventorytype() != inventory_type::Ammo)
			{
				return inventory_change_failure::OnlyAmmoCanGoHere;
			}

			return inventory_change_failure::Okay;
		}

		if (IsBagPackSlot(slot))
		{
			if (entry.itemclass() != item_class::Container &&
				entry.itemclass() != item_class::Quiver)
			{
				return inventory_change_failure::NotABag;
			}

			// Make sure that we have only up to one quiver equipped at a time
			if (entry.itemclass() == item_class::Quiver)
			{
				if (HasEquippedQuiver())
					return inventory_change_failure::CanEquipOnlyOneQuiver;
			}

			auto bagItem = GetItemAtSlot(slot);
			if (bagItem)
			{
				if (bagItem->GetTypeId() != ObjectTypeId::Container)
				{
					// Return code valid? ...
					return inventory_change_failure::NotABag;
				}

				auto castedBag = std::static_pointer_cast<GameBagS>(bagItem);
				ASSERT(castedBag);

				if (!castedBag->IsEmpty())
				{
					return inventory_change_failure::CanOnlyDoWithEmptyBags;
				}
			}

			return inventory_change_failure::Okay;
		}

		return inventory_change_failure::InternalBagError;
	}

	InventoryChangeFailure Inventory::CanStoreItems(const proto::ItemEntry &entry, uint16 amount) const
	{
		// Incorrect value used, so give at least one item
		if (amount == 0)
		{
			amount = 1;
		}

		// Limit the total amount of items
		const uint16 itemCount = GetItemCount(entry.id());
		if (entry.maxcount() > 0 &&
			uint32(itemCount + amount) > entry.maxcount())
		{
			return inventory_change_failure::CantCarryMoreOfThis;
		}

		// Quick check if there are enough free slots (only works if we don't have an item of this type yet)
		const uint16 requiredSlots = (amount - 1) / entry.maxstack() + 1;
		if ((itemCount == 0 || entry.maxstack() <= 1) &&
			requiredSlots > m_freeSlots)
		{
			return inventory_change_failure::InventoryFull;
		}

		// TODO

		return inventory_change_failure::Okay;
	}

	uint16 Inventory::GetItemCount(const uint32 itemId) const noexcept
	{
		const auto it = m_itemCounter.find(itemId);
		return (it != m_itemCounter.end() ? it->second : 0);
	}

	bool Inventory::HasEquippedQuiver() const
	{
		for (uint8 slot = player_inventory_slots::Start; slot < player_inventory_slots::End; ++slot)
		{
			const auto testBag = GetItemAtSlot(GetAbsoluteSlot(player_inventory_slots::Bag_0, slot));
			if (testBag && testBag->GetEntry().itemclass() == item_class::Quiver)
			{
				return true;
			}
		}

		return false;
	}

	uint16 Inventory::GetAbsoluteSlot(uint8 bag, uint8 slot)
	{
		return (bag << 8) | slot;
	}

	bool Inventory::GetRelativeSlots(uint16 absoluteSlot, uint8 &out_bag, uint8 &out_slot)
	{
		out_bag = static_cast<uint8>(absoluteSlot >> 8);
		out_slot = static_cast<uint8>(absoluteSlot & 0xFF);
		return true;
	}

	std::shared_ptr<GameItemS> Inventory::GetItemAtSlot(uint16 absoluteSlot) const noexcept
	{
		if (const auto it = m_itemsBySlot.find(absoluteSlot); it != m_itemsBySlot.end())
		{
			return it->second;
		}

		return nullptr;
	}

	std::shared_ptr<GameBagS> Inventory::GetBagAtSlot(uint16 absolute_slot) const noexcept
	{
		if (!IsBagPackSlot(absolute_slot))
		{
			// Convert bag slot to bag pack slot which is 0xFFXX where XX is the bag slot id
			absolute_slot = GetAbsoluteSlot(player_inventory_slots::Bag_0, static_cast<uint8>(absolute_slot >> 8));
		}

		auto it = m_itemsBySlot.find(absolute_slot);
		if (it != m_itemsBySlot.end() && it->second->GetTypeId() == ObjectTypeId::Container)
		{
			return std::dynamic_pointer_cast<GameBagS>(it->second);
		}

		return nullptr;
	}

	std::shared_ptr<GameItemS> Inventory::GetWeaponByAttackType(WeaponAttack attackType, bool nonbroken, bool useable) const
	{
		uint8 slot;

		switch (attackType)
		{
		case weapon_attack::BaseAttack:
			slot = player_equipment_slots::Mainhand;
			break;
		case weapon_attack::OffhandAttack:
			slot = player_equipment_slots::Offhand;
			break;
		case weapon_attack::RangedAttack:
			slot = player_equipment_slots::Ranged;
			break;
		}

		std::shared_ptr<GameItemS> item = GetItemAtSlot(GetAbsoluteSlot(player_inventory_slots::Bag_0, slot));

		if (!item || item->GetEntry().itemclass() != item_class::Weapon)
		{
			return nullptr;
		}

		if ((m_owner.GetWeaponProficiency() & (1 << item->GetEntry().subclass())) == 0)
		{
			return nullptr; // No proficiency for this weapon type
		}

		if (nonbroken && item->IsBroken())
		{
			return nullptr;
		}

		if (useable && !m_owner.CanUseWeapon(attackType))
		{
			return nullptr;
		}

		return item;
	}

	bool Inventory::FindItemByGUID(uint64 guid, uint16 &out_slot) const
	{
		for (auto &item : m_itemsBySlot)
		{
			if (item.second->GetGuid() == guid)
			{
				out_slot = item.first;
				return true;
			}
		}

		return false;
	}

	bool Inventory::IsEquipmentSlot(uint16 absoluteSlot)
	{
		return (
			absoluteSlot >> 8 == player_inventory_slots::Bag_0 &&
			(absoluteSlot & 0xFF) < player_equipment_slots::End);
	}

	bool Inventory::IsBagPackSlot(uint16 absoluteSlot)
	{
		return (
			absoluteSlot >> 8 == player_inventory_slots::Bag_0 &&
			(absoluteSlot & 0xFF) >= player_inventory_slots::Start &&
			(absoluteSlot & 0xFF) < player_inventory_slots::End);
	}

	bool Inventory::IsInventorySlot(uint16 absoluteSlot)
	{
		return (
			absoluteSlot >> 8 == player_inventory_slots::Bag_0 &&
			(absoluteSlot & 0xFF) >= player_inventory_pack_slots::Start &&
			(absoluteSlot & 0xFF) < player_inventory_pack_slots::End);
	}

	bool Inventory::IsBagSlot(uint16 absoluteSlot)
	{
		return (
			absoluteSlot >> 8 >= player_inventory_slots::Start &&
			absoluteSlot >> 8 < player_inventory_slots::End);
	}

	bool Inventory::IsBagBarSlot(uint16 absoluteSlot)
	{
		return (
			(absoluteSlot >> 8) == player_inventory_slots::Bag_0 &&
			(absoluteSlot & 0xFF) >= player_inventory_slots::Start &&
			(absoluteSlot & 0xFF) < player_inventory_slots::End);
	}

	bool Inventory::IsBuyBackSlot(uint16 absoluteSlot)
	{
		return (absoluteSlot >= player_buy_back_slots::Start && absoluteSlot < player_buy_back_slots::End);
	}

	uint32 Inventory::RepairAllItems()
	{
		uint32 totalCost = 0;

		// Repair everything equipped and in main bag
		for (uint8 slot = player_equipment_slots::Start; slot < player_inventory_pack_slots::End; ++slot)
		{
			totalCost += RepairItem(GetAbsoluteSlot(player_inventory_slots::Bag_0, slot));
		}

		// Also check equipped bag contents
		for (uint8 bagSlot = player_inventory_slots::Start; bagSlot < player_inventory_slots::End; ++bagSlot)
		{
			const uint16 absoluteBagSlot = GetAbsoluteSlot(player_inventory_slots::Bag_0, bagSlot);

			// Check if bag exists
			auto bag = std::static_pointer_cast<GameBagS>(GetItemAtSlot(absoluteBagSlot));
			if (!bag)
			{
				continue;
			}

			// Iterate bag items
			for (uint8 bagItemSlot = 0; bagItemSlot < static_cast<uint8>(bag->GetSlotCount()); ++bagItemSlot)
			{
				totalCost += RepairItem(GetAbsoluteSlot(bagSlot, bagItemSlot));
			}
		}

		return totalCost;
	}

	uint32 Inventory::RepairItem(uint16 absoluteSlot)
	{
		uint32 totalCost = 0;

		// Look for item instance
		auto item = GetItemAtSlot(absoluteSlot);
		if (!item)
		{
			return totalCost;
		}

		// Get max durability
		const uint32 maxDurability = item->GetEntry().durability();
		if (maxDurability == 0)
		{
			return totalCost;
		}

		// Get current durability
		const uint32 durability = item->Get<uint32>(object_fields::Durability);
		if (durability >= maxDurability)
		{
			return totalCost;
		}

		// Okay, we have something to repair
		// TODO: Calculate repair cost and try to consume money

		// Repair the item and notify the owner
		item->Set<uint32>(object_fields::Durability, maxDurability);
		itemInstanceUpdated(std::cref(item), 0);

		// Reapply item stats if needed
		if (durability == 0 && IsEquipmentSlot(absoluteSlot))
		{
			m_owner.ApplyItemStats(*item, true);
		}

		return totalCost;
	}

	void Inventory::AddRealmData(const ItemData &data)
	{
		m_realmData.push_back(data);
	}

	void Inventory::ConstructFromRealmData(std::vector<GameObjectS *> &out_items)
	{
		// Reconstruct realm data if available
		if (!m_realmData.empty())
		{
			// World instance has to be ready
			auto *world = m_owner.GetWorldInstance();
			if (!world)
			{
				return;
			}

			// We need to store bag items for later, since we first need to create all bags
			std::map<uint16, std::shared_ptr<GameItemS>> bagItems;

			// Iterate through all entries
			for (auto &data : m_realmData)
			{
				auto *entry = m_owner.GetProject().items.getById(data.entry);
				if (!entry)
				{
					ELOG("Could not find item " << data.entry);
					continue;
				}

				// Create a new item instance
				std::shared_ptr<GameItemS> item;
				if (entry->itemclass() == item_class::Container ||
					entry->itemclass() == item_class::Quiver)
				{
					item = std::make_shared<GameBagS>(m_owner.GetProject(), *entry);
				}
				else
				{
					item = std::make_shared<GameItemS>(m_owner.GetProject(), *entry);
				}

				auto newItemId = world->GetItemIdGenerator().GenerateId();
				item->Initialize();
				item->Set<uint64>(object_fields::Guid, CreateEntryGUID(newItemId, entry->id(), GuidType::Item));
				item->Set<uint64>(object_fields::ItemOwner, m_owner.GetGuid());
				item->Set<uint64>(object_fields::Creator, data.creator);
				item->Set<uint64>(object_fields::Contained, m_owner.GetGuid());
				item->Set<uint32>(object_fields::Durability, data.durability);
				if (entry->bonding() == item_binding::BindWhenPickedUp)
				{
					item->AddFlag<uint32>(object_fields::ItemFlags, item_flags::Bound);
				}

				// Add this item to the inventory slot
				ASSERT(!m_itemsBySlot.contains(data.slot) && "Item slot already in use by another item - duplicate slot assignment!");
				m_itemsBySlot[data.slot] = item;

				// Determine slot
				uint8 bag = 0, subslot = 0;
				GetRelativeSlots(data.slot, bag, subslot);
				if (bag == player_inventory_slots::Bag_0)
				{
					if (IsBagBarSlot(data.slot))
					{
						m_owner.ApplyItemStats(*item, true);
					}

					if (IsEquipmentSlot(data.slot))
					{
						// Ensure slot size is valid
						constexpr uint32 slotSize = object_fields::VisibleItem2_CREATOR - object_fields::VisibleItem1_CREATOR;

						m_owner.Set<uint64>(object_fields::InvSlotHead + (subslot * 2), item->GetGuid());
						m_owner.Set<uint32>(object_fields::VisibleItem1_0 + (subslot * slotSize), item->GetEntry().id());
						m_owner.Set<uint64>(object_fields::VisibleItem1_CREATOR + (subslot * slotSize), item->Get<uint64>(object_fields::Creator));
						m_owner.ApplyItemStats(*item, true);
						if (item->GetEntry().itemset() != 0)
						{
							OnSetItemEquipped(item->GetEntry().itemset());
						}

						// Apply bonding
						if (entry->bonding() == item_binding::BindWhenEquipped)
						{
							item->AddFlag<uint32>(object_fields::ItemFlags, item_flags::Bound);
						}
					}
					else if (IsInventorySlot(data.slot))
					{
						m_owner.Set<uint64>(object_fields::InvSlotHead + (subslot * 2), item->GetGuid());
					}
					else if (IsBagPackSlot(data.slot) && item->GetTypeId() == ObjectTypeId::Container)
					{
						m_owner.Set<uint64>(object_fields::InvSlotHead + (subslot * 2), item->GetGuid());

						// Increase slot count since this is an equipped bag
						m_freeSlots += reinterpret_cast<GameBagS *>(item.get())->GetSlotCount();

						// Apply bonding
						if (entry->bonding() == item_binding::BindWhenEquipped)
						{
							item->AddFlag<uint32>(object_fields::ItemFlags, item_flags::Bound);
						}
					}
				}
				else if (IsBagSlot(data.slot))
				{
					bagItems[data.slot] = item;
				}

				// Modify stack count
				(void)item->AddStacks(data.stackCount - 1);
				m_itemCounter[data.entry] += data.stackCount;

				// Watch for item despawn packet
				m_itemDespawnSignals[item->GetGuid()] = item->despawned.connect(std::bind(&Inventory::OnItemDespawned, this, std::placeholders::_1));

				item->ClearFieldChanges();

				// Notify
				out_items.push_back(item.get());

				// Quest check
				// m_owner.onQuestItemAddedCredit(item->GetEntry(), data.stackCount);

				// Inventory slot used
				if (IsInventorySlot(data.slot) || IsBagSlot(data.slot))
				{
					m_freeSlots--;
				}
			}

			// Clear realm data since we don't need it any more
			m_realmData.clear();

			// Store items in bags
			for (auto &pair : bagItems)
			{
				auto bag = GetBagAtSlot(GetAbsoluteSlot(player_inventory_slots::Bag_0, pair.first >> 8));
				if (!bag)
				{
					ELOG("Could not find bag at slot " << pair.first << ": Maybe this bag is sent after the item");
				}
				else
				{
					pair.second->Set<uint64>(object_fields::Contained, bag->GetGuid());
					bag->Set<uint64>(object_fields::Slot_1 + ((pair.first & 0xFF) * 2), pair.second->GetGuid());
					bag->ClearFieldChanges();
				}
			}
		}
	}

	void Inventory::ForEachBag(const BagCallbackFunc &callback) const
	{
		// Enumerates all possible bags
		static std::array<uint8, 5> bags =
			{
				player_inventory_slots::Bag_0,
				player_inventory_slots::Start + 0,
				player_inventory_slots::Start + 1,
				player_inventory_slots::Start + 2,
				player_inventory_slots::Start + 3};

		for (const auto &bag : bags)
		{
			uint8 slotStart = 0, slotEnd = 0;
			if (bag == player_inventory_slots::Bag_0)
			{
				slotStart = player_inventory_pack_slots::Start;
				slotEnd = player_inventory_pack_slots::End;
			}
			else
			{
				auto bagInst = GetBagAtSlot(GetAbsoluteSlot(player_inventory_slots::Bag_0, bag));
				if (!bagInst)
				{
					// Skip this bag
					continue;
				}

				slotStart = 0;
				slotEnd = bagInst->GetSlotCount();
			}

			if (slotEnd <= slotStart)
			{
				continue;
			}

			if (!callback(bag, slotStart, slotEnd))
			{
				break;
			}
		}
	}

	void Inventory::OnItemDespawned(GameObjectS &object)
	{
		if (object.GetTypeId() != ObjectTypeId::Item &&
			object.GetTypeId() != ObjectTypeId::Container)
		{
			return;
		}

		// Find item slot by guid
		uint16 slot = 0;
		if (!FindItemByGUID(object.GetGuid(), slot))
		{
			WLOG("Could not find item by slot!");
			return;
		}

		// Destroy this item
		RemoveItem(slot);
	}

	void Inventory::OnSetItemEquipped(uint32 set)
	{
	}

	void Inventory::OnSetItemUnequipped(uint32 set)
	{
	}

	io::Writer &operator<<(io::Writer &w, Inventory const &object)
	{
		if (object.m_realmData.empty())
		{
			// Inventory has actual item instances, so we serialize this object for realm usage
			w << io::write<uint16>(object.m_itemsBySlot.size());
			for (const auto &pair : object.m_itemsBySlot)
			{
				ItemData data;
				data.entry = pair.second->GetEntry().id();
				data.slot = pair.first;
				data.stackCount = pair.second->GetStackCount();
				data.creator = pair.second->Get<uint64>(object_fields::Creator);
				data.contained = pair.second->Get<uint64>(object_fields::Contained);
				data.durability = pair.second->Get<uint32>(object_fields::Durability);
				data.randomPropertyIndex = 0;
				data.randomSuffixIndex = 0;
				w << data;
			}
		}
		else
		{
			// Inventory has realm data left, and no item instances
			w << io::write<uint16>(object.m_realmData.size());
			for (const auto &data : object.m_realmData)
			{
				w << data;
			}
		}

		return w;
	}

	io::Reader &operator>>(io::Reader &r, Inventory &object)
	{
		object.m_itemsBySlot.clear();
		object.m_freeSlots = player_inventory_pack_slots::End - player_inventory_pack_slots::Start;
		object.m_itemCounter.clear();
		object.m_realmData.clear();

		// Read amount of items
		uint16 itemCount = 0;
		r >> io::read<uint16>(itemCount);

		// Read realm data
		object.m_realmData.resize(itemCount);
		for (uint16 i = 0; i < itemCount; ++i)
		{
			r >> object.m_realmData[i];
		}

		return r;
	}

	// Helper method to validate swap prerequisites
	InventoryChangeFailure Inventory::ValidateSwapPrerequisites(std::shared_ptr<GameItemS> srcItem, std::shared_ptr<GameItemS> dstItem, uint16 slotA, uint16 slotB)
	{
		if (!srcItem)
		{
			return inventory_change_failure::ItemNotFound;
		}

		if (!m_owner.IsAlive())
		{
			return inventory_change_failure::YouAreDead;
		}

		// TODO: QOL improvement: When swapping one bag with another bag, we can try to merge the bags if possible

		// If source slot is a bag slot and bag is not empty
		if (srcItem && srcItem->IsContainer())
		{
			if (!std::static_pointer_cast<GameBagS>(srcItem)->IsEmpty())
			{
				return inventory_change_failure::CanOnlyDoWithEmptyBags;
			}
		}

		// Destination bag must be empty as well
		if (dstItem && dstItem->IsContainer())
		{
			if (!std::static_pointer_cast<GameBagS>(dstItem)->IsEmpty())
			{
				return inventory_change_failure::CanOnlyDoWithEmptyBags;
			}
		}

		// Trying to put an equipped bag out of the bag bar slots?
		if (IsBagBarSlot(slotA) && !IsBagBarSlot(slotB))
		{
			// Check that the new slot is not inside the bag itself
			const auto &bag = GetBagAtSlot(slotB);
			if (bag && bag == srcItem)
			{
				return inventory_change_failure::BagsCantBeWrapped;
			}
		}

		// Can't change equipment while in combat (except weapons)
		if (m_owner.IsInCombat() && IsEquipmentSlot(slotA))
		{
			const uint8 equipSlot = slotA & 0xFF;
			if (equipSlot != player_equipment_slots::Mainhand &&
				equipSlot != player_equipment_slots::Offhand &&
				equipSlot != player_equipment_slots::Ranged)
			{
				return inventory_change_failure::NotInCombat;
			}
		}

		// Verify destination slot for source item
		auto result = IsValidSlot(slotB, srcItem->GetEntry());
		if (result != inventory_change_failure::Okay)
		{
			return result;
		}

		// If there is an item in the destination slot, also verify the source slot
		if (dstItem)
		{
			result = IsValidSlot(slotA, dstItem->GetEntry());
			if (result != inventory_change_failure::Okay)
			{
				return result;
			}
		}

		return inventory_change_failure::Okay;
	}

	// Helper method to check if two items can be merged
	bool Inventory::CanMergeItems(std::shared_ptr<GameItemS> srcItem, std::shared_ptr<GameItemS> dstItem) const
	{
		if (!srcItem || !dstItem)
		{
			return false;
		}

		// Must be same item type
		if (srcItem->GetEntry().id() != dstItem->GetEntry().id())
		{
			return false;
		}

		const uint32 maxStack = srcItem->GetEntry().maxstack();
		return maxStack > 1 && maxStack > dstItem->GetStackCount();
	}

	// Helper method to merge item stacks
	InventoryChangeFailure Inventory::MergeItemStacks(std::shared_ptr<GameItemS> srcItem, std::shared_ptr<GameItemS> dstItem, uint16 slotA, uint16 slotB)
	{
		const uint32 maxStack = srcItem->GetEntry().maxstack();
		const uint32 availableDstStacks = maxStack - dstItem->GetStackCount();

		if (availableDstStacks == 0)
		{
			return inventory_change_failure::InventoryFull;
		}

		if (availableDstStacks >= srcItem->GetStackCount())
		{
			// Merge all source stacks into destination
			dstItem->AddStacks(srcItem->GetStackCount());
			itemInstanceUpdated(dstItem, slotB);

			// Remove source item completely
			RemoveItemFromSlot(slotA);
		}
		else
		{
			// Partial merge
			dstItem->AddStacks(availableDstStacks);
			srcItem->Set<uint32>(object_fields::StackCount, srcItem->GetStackCount() - availableDstStacks);
			itemInstanceUpdated(dstItem, slotB);
			itemInstanceUpdated(srcItem, slotA);
		}

		return inventory_change_failure::Okay;
	}

	// Helper method to remove an item from a slot
	void Inventory::RemoveItemFromSlot(uint16 slot)
	{
		auto item = m_itemsBySlot[slot];
		if (!item)
		{
			return;
		}

		// Update player fields
		if (IsEquipmentSlot(slot) || IsInventorySlot(slot) || IsBagPackSlot(slot))
		{
			m_owner.Set<uint64>(object_fields::InvSlotHead + (slot & 0xFF) * 2, 0);
		}

		// Remove from tracking
		m_itemDespawnSignals.erase(item->GetGuid());
		itemInstanceDestroyed(item, slot);
		m_itemsBySlot.erase(slot);
		m_freeSlots++;
	}

	// Helper method to validate bag constraints
	InventoryChangeFailure Inventory::ValidateBagConstraints(std::shared_ptr<GameItemS> srcItem, uint16 slotB) const
	{
		if (srcItem->GetTypeId() == ObjectTypeId::Container && !IsBagPackSlot(slotB))
		{
			auto bag = std::dynamic_pointer_cast<GameBagS>(srcItem);
			if (bag && !bag->IsEmpty())
			{
				return inventory_change_failure::CanOnlyDoWithEmptyBags;
			}
		}
		return inventory_change_failure::Okay;
	}

	// Helper method to perform the actual item swap
	void Inventory::PerformItemSwap(std::shared_ptr<GameItemS> srcItem, std::shared_ptr<GameItemS> dstItem, uint16 slotA, uint16 slotB)
	{
		// Handle 2-handed weapon equipping in mainhand slot
		if (IsEquipmentSlot(slotB) && (slotB & 0xFF) == player_equipment_slots::Mainhand &&
			srcItem && srcItem->GetEntry().inventorytype() == inventory_type::TwoHandedWeapon)
		{
			auto offhandSlot = GetAbsoluteSlot(player_inventory_slots::Bag_0, player_equipment_slots::Offhand);
			auto offhandItem = GetItemAtSlot(offhandSlot);

			if (offhandItem)
			{
				// Find an empty inventory slot for the offhand item
				uint16 emptySlot = FindEmptySlot();
				if (emptySlot != 0)
				{
					// Move offhand item to inventory
					UpdateSlotContents(offhandSlot, nullptr);
					UpdateSlotContents(emptySlot, offhandItem);

					// Update internal map
					m_itemsBySlot.erase(offhandSlot);
					m_itemsBySlot[emptySlot] = offhandItem;

					// Update free slot count
					m_freeSlots--;

					// Apply equipment effects for unequipping offhand
					ApplyEquipmentEffects(offhandItem, nullptr, offhandSlot, emptySlot);

					// Notify about the offhand item move
					itemInstanceUpdated(offhandItem, emptySlot);
				}
				else
				{
					// This should not happen as IsValidSlot should have prevented this,
					// but adding a safeguard just in case
					ELOG("Failed to find empty slot for offhand item when equipping 2-handed weapon");
				}
			}
		}

		// Update slot A with destination item
		UpdateSlotContents(slotA, dstItem);

		// Update slot B with source item
		UpdateSlotContents(slotB, srcItem);

		// Update bag slot counts
		UpdateBagSlotCounts(srcItem, dstItem, slotA, slotB);

		// Swap items in internal map
		std::swap(m_itemsBySlot[slotA], m_itemsBySlot[slotB]);

		// Handle empty slot case
		if (!dstItem)
		{
			m_itemsBySlot.erase(slotA);
			UpdateFreeSlotCount(slotA, slotB, false);
		}

		// Apply item stats and visuals
		ApplySwapEffects(srcItem, dstItem, slotA, slotB);

		// Fire signals for the swap - these need to fire unconditionally
		// UpdateSlotContents only fires if container ownership changes, which isn't always the case
		if (srcItem)
		{
			itemInstanceUpdated(srcItem, slotB);
		}
		if (dstItem)
		{
			itemInstanceUpdated(dstItem, slotA);
		}
	} // Helper method to update slot contents
	void Inventory::UpdateSlotContents(uint16 slot, std::shared_ptr<GameItemS> item)
	{
		if (IsEquipmentSlot(slot) || IsInventorySlot(slot) || IsBagPackSlot(slot))
		{
			m_owner.Set<uint64>(object_fields::InvSlotHead + (slot & 0xFF) * 2, item ? item->GetGuid() : 0);

			if (item && item->Get<uint64>(object_fields::Contained) != m_owner.GetGuid())
			{
				item->Set<uint64>(object_fields::Contained, m_owner.GetGuid());
				itemInstanceUpdated(item, slot);
			}
		}
		else if (IsBagSlot(slot))
		{
			auto bag = GetBagAtSlot(slot);
			if (bag)
			{
				bag->Set<uint64>(object_fields::Slot_1 + (slot & 0xFF) * 2, item ? item->GetGuid() : 0);
				itemInstanceUpdated(bag, GetAbsoluteSlot(player_inventory_slots::Bag_0, slot >> 8));

				if (item && item->Get<uint64>(object_fields::Contained) != bag->GetGuid())
				{
					item->Set<uint64>(object_fields::Contained, bag->GetGuid());
					itemInstanceUpdated(item, slot);
				}
			}
		}
	}

	// Helper method to update bag slot counts
	void Inventory::UpdateBagSlotCounts(std::shared_ptr<GameItemS> srcItem, std::shared_ptr<GameItemS> dstItem, uint16 slotA, uint16 slotB)
	{
		const bool isBagPackA = IsBagPackSlot(slotA);
		const bool isBagPackB = IsBagPackSlot(slotB);

		if (isBagPackA && !isBagPackB)
		{
			if (!dstItem && srcItem->GetTypeId() == ObjectTypeId::Container)
			{
				m_freeSlots -= srcItem->Get<uint32>(object_fields::NumSlots);
			}
			else if (dstItem &&
					 dstItem->GetTypeId() == ObjectTypeId::Container &&
					 srcItem->GetTypeId() == ObjectTypeId::Container)
			{
				m_freeSlots -= srcItem->Get<uint32>(object_fields::NumSlots);
				m_freeSlots += dstItem->Get<uint32>(object_fields::NumSlots);
			}
		}
		else if (isBagPackB && !isBagPackA)
		{
			if (!dstItem && srcItem->GetTypeId() == ObjectTypeId::Container)
			{
				m_freeSlots += srcItem->Get<uint32>(object_fields::NumSlots);
			}
			else if (dstItem &&
					 dstItem->GetTypeId() == ObjectTypeId::Container &&
					 srcItem->GetTypeId() == ObjectTypeId::Container)
			{
				m_freeSlots -= dstItem->Get<uint32>(object_fields::NumSlots);
				m_freeSlots += srcItem->Get<uint32>(object_fields::NumSlots);
			}
		}
	}

	// Helper method to update free slot count
	void Inventory::UpdateFreeSlotCount(uint16 slotA, uint16 slotB, bool dstItemExists)
	{
		const bool isInventoryA = IsInventorySlot(slotA) || IsBagSlot(slotA);
		const bool isInventoryB = IsInventorySlot(slotB) || IsBagSlot(slotB);

		if (isInventoryA && !isInventoryB)
		{
			m_freeSlots++;
		}
		else if (isInventoryB && !isInventoryA)
		{
			ASSERT(m_freeSlots >= 1);
			m_freeSlots--;
		}
	}

	// Helper method to apply swap effects (stats, visuals, bonding)
	void Inventory::ApplySwapEffects(std::shared_ptr<GameItemS> srcItem, std::shared_ptr<GameItemS> dstItem, uint16 slotA, uint16 slotB)
	{
		// Apply bag stats
		if (IsBagBarSlot(slotA))
		{
			m_owner.ApplyItemStats(*srcItem, false);
			if (dstItem)
			{
				m_owner.ApplyItemStats(*dstItem, true);
			}
		}

		if (IsBagBarSlot(slotB))
		{
			m_owner.ApplyItemStats(*srcItem, true);
			if (dstItem)
			{
				m_owner.ApplyItemStats(*dstItem, false);
			}
		}

		// Update equipment visuals and stats
		ApplyEquipmentEffects(srcItem, dstItem, slotA, slotB);
	}

	// Helper method to apply equipment effects
	void Inventory::ApplyEquipmentEffects(std::shared_ptr<GameItemS> srcItem, std::shared_ptr<GameItemS> dstItem, uint16 slotA, uint16 slotB)
	{
		constexpr uint32 slotSize = object_fields::VisibleItem2_CREATOR - object_fields::VisibleItem1_CREATOR;

		if (IsEquipmentSlot(slotA))
		{
			const uint8 equipSlotA = slotA & 0xFF;
			m_owner.Set<uint32>(object_fields::VisibleItem1_0 + (equipSlotA * slotSize), dstItem ? dstItem->GetEntry().id() : 0);
			m_owner.Set<uint64>(object_fields::VisibleItem1_CREATOR + (equipSlotA * slotSize), dstItem ? dstItem->Get<uint64>(object_fields::Creator) : 0);

			m_owner.ApplyItemStats(*srcItem, false);
			HandleItemSetEffects(srcItem, false);

			if (dstItem)
			{
				m_owner.ApplyItemStats(*dstItem, true);
				HandleItemSetEffects(dstItem, true);
			}
		}

		if (IsEquipmentSlot(slotB))
		{
			const uint8 equipSlotB = slotB & 0xFF;
			m_owner.Set<uint32>(object_fields::VisibleItem1_0 + (equipSlotB * slotSize), srcItem->GetEntry().id());
			m_owner.Set<uint64>(object_fields::VisibleItem1_CREATOR + (equipSlotB * slotSize), srcItem->Get<uint64>(object_fields::Creator));

			m_owner.ApplyItemStats(*srcItem, true);
			HandleItemSetEffects(srcItem, true);

			if (dstItem)
			{
				m_owner.ApplyItemStats(*dstItem, false);
				HandleItemSetEffects(dstItem, false);
			}

			// Handle binding when equipped
			if (srcItem->GetEntry().bonding() == item_binding::BindWhenEquipped)
			{
				srcItem->AddFlag<uint32>(object_fields::ItemFlags, item_flags::Bound);
				itemInstanceUpdated(srcItem, slotB);
			}
		}
		else if (IsBagPackSlot(slotB))
		{
			// Handle binding for bag pack slots
			if (srcItem->GetEntry().bonding() == item_binding::BindWhenEquipped)
			{
				srcItem->AddFlag<uint32>(object_fields::ItemFlags, item_flags::Bound);
			}
		}
	}

	// Helper method to handle item set effects
	void Inventory::HandleItemSetEffects(std::shared_ptr<GameItemS> item, bool equipped)
	{
		if (item->GetEntry().itemset() != 0)
		{
			if (equipped)
			{
				OnSetItemEquipped(item->GetEntry().itemset());
			}
			else
			{
				OnSetItemUnequipped(item->GetEntry().itemset());
			}
		}
	}

	void Inventory::SetRepository(IInventoryRepository *repository) noexcept
	{
		m_repository = repository;
	}

	bool Inventory::SaveToRepository()
	{
		if (!m_repository)
		{
			// No repository configured - this is normal on Realm Server
			return false;
		}

		if (!m_isDirty)
		{
			// No changes to save
			return true;
		}

		ILOG("Saving inventory to repository (dirty flag set)");
		// Convert current inventory state to InventoryItemData
		std::vector<InventoryItemData> items;
		items.reserve(m_itemsBySlot.size());

		for (const auto &[slot, item] : m_itemsBySlot)
		{
			// Skip buyback slots - these are temporary
			if (IsBuyBackSlot(slot))
			{
				continue;
			}

			InventoryItemData data;
			data.entry = item->GetEntry().id();
			data.slot = slot;
			data.stackCount = static_cast<uint16>(item->Get<int32>(object_fields::StackCount));
			data.creator = item->Get<uint64>(object_fields::Creator);
			data.contained = item->Get<uint64>(object_fields::Contained);
			data.durability = item->Get<uint32>(object_fields::Durability);
			data.randomPropertyIndex = 0; // TODO: Implement if needed
			data.randomSuffixIndex = 0;	  // TODO: Implement if needed

			items.push_back(data);
		}

		ILOG("Prepared " << items.size() << " items to save (filtered from " << m_itemsBySlot.size() << " total slots)");

		// Use transaction for atomicity
		if (!m_repository->BeginTransaction())
		{
			ELOG("Failed to begin inventory transaction");
			return false;
		}

		// Save all items (Realm Server will delete-then-insert for clean state)
		const bool success = m_repository->SaveAllItems(m_owner.GetGuid(), items);
		if (success)
		{
			// Commit transaction
			if (!m_repository->Commit())
			{
				ELOG("Failed to commit inventory transaction");
				return false;
			}

			// Clear dirty flag
			m_isDirty = false;
			return true;
		}
		else
		{
			// Rollback on failure
			m_repository->Rollback();
			ELOG("Failed to save inventory items");
			return false;
		}
	}

	void Inventory::MarkDirty() noexcept
	{
		m_isDirty = true;
	}

	bool Inventory::IsDirty() const noexcept
	{
		return m_isDirty;
	}

	// ============================================================================
	// IAddItemCommandContext Implementation
	// ============================================================================

	ItemValidator &Inventory::GetValidator()
	{
		return *m_validator;
	}

	SlotManager &Inventory::GetSlotManager()
	{
		return *m_slotManager;
	}

	// Note: AddItemToSlot is already implemented as a private helper method
	// and is now public with override keyword

	// ============================================================================
	// IRemoveItemCommandContext Implementation
	// ============================================================================

	void Inventory::RemoveItemFromSlot(uint16 slot, uint16 stacks)
	{
		// Delegate to existing RemoveItem method
		RemoveItem(slot, stacks, false);
	}

	// Note: GetItemAtSlot is already implemented and now has override keyword

	// ============================================================================
	// ISwapItemsCommandContext Implementation
	// ============================================================================

	void Inventory::SwapItemSlots(uint16 slot1, uint16 slot2)
	{
		// Delegate to existing SwapItems method
		SwapItems(slot1, slot2);
	}

	bool Inventory::SplitStack(uint16 sourceSlot, uint16 destSlot, uint16 count)
	{
		// Get the source item
		auto sourceItem = GetItemAtSlot(sourceSlot);
		if (!sourceItem)
		{
			return false;
		}

		// Check if destination slot is empty
		auto destItem = GetItemAtSlot(destSlot);
		if (destItem)
		{
			return false; // Destination must be empty for split
		}

		// Check if we have enough stacks to split
		const uint16 currentStacks = sourceItem->Get<uint32>(object_fields::StackCount);
		if (count >= currentStacks)
		{
			return false; // Can't split all or more than we have
		}

		// Get item entry for validation (returns reference, not pointer)
		const auto &entry = sourceItem->GetEntry();

		// Validate destination slot
		if (IsValidSlot(destSlot, entry) != inventory_change_failure::Okay)
		{
			return false;
		}

		// Create a new item with the split count (following existing pattern)
		auto newItem = std::make_shared<GameItemS>(m_owner.GetProject(), entry);
		newItem->Initialize();

		// Get world instance for ID generation
		auto *world = m_owner.GetWorldInstance();
		if (!world)
		{
			return false;
		}

		// Generate GUID (following existing pattern in SetupNewItem)
		auto newItemId = world->GetItemIdGenerator().GenerateId();
		newItem->Set<uint64>(object_fields::Guid, CreateEntryGUID(newItemId, entry.id(), GuidType::Item));
		newItem->Set<uint64>(object_fields::ItemOwner, m_owner.GetGuid());
		newItem->Set(object_fields::StackCount, static_cast<int32>(count));
		newItem->Set(object_fields::Creator, sourceItem->Get<uint64>(object_fields::Creator));
		newItem->Set(object_fields::Durability, sourceItem->Get<uint32>(object_fields::Durability));

		// Reduce source stack
		sourceItem->Set(object_fields::StackCount, static_cast<int32>(currentStacks - count));

		// Add new item to destination
		AddItemToSlot(newItem, destSlot);

		// Fire update signal for source
		itemInstanceUpdated(sourceItem, sourceSlot);

		return true;
	}

	bool Inventory::MergeStacks(uint16 sourceSlot, uint16 destSlot)
	{
		// Get both items
		auto sourceItem = GetItemAtSlot(sourceSlot);
		auto destItem = GetItemAtSlot(destSlot);

		if (!sourceItem || !destItem)
		{
			return false;
		}

		// Check if they're the same item type (GetEntry returns reference)
		const auto &sourceEntry = sourceItem->GetEntry();
		const auto &destEntry = destItem->GetEntry();

		if (sourceEntry.id() != destEntry.id())
		{
			return false;
		}

		// Get current stack counts
		const uint16 sourceStacks = sourceItem->Get<uint32>(object_fields::StackCount);
		const uint16 destStacks = destItem->Get<uint32>(object_fields::StackCount);
		const uint16 maxStack = sourceEntry.maxstack();

		// Check if destination is already full
		if (destStacks >= maxStack)
		{
			return false;
		}

		// Calculate how much we can merge
		const uint16 spaceInDest = maxStack - destStacks;
		const uint16 amountToMerge = std::min(spaceInDest, sourceStacks);

		// Update destination stack
		destItem->Set(object_fields::StackCount, static_cast<int32>(destStacks + amountToMerge));
		itemInstanceUpdated(destItem, destSlot);

		// Update or remove source
		if (amountToMerge >= sourceStacks)
		{
			// Remove source item completely
			RemoveItem(sourceSlot, 0, false);
		}
		else
		{
			// Reduce source stack
			sourceItem->Set(object_fields::StackCount, static_cast<int32>(sourceStacks - amountToMerge));
			itemInstanceUpdated(sourceItem, sourceSlot);
		}

		return true;
	}
}
