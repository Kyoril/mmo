// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/signal.h"
#include "game/item.h"
#include "base/linear_set.h"

#include <memory>
#include <map>

namespace io
{
	class Writer;
	class Reader;
}

namespace mmo
{
	class GameObjectS;

	namespace proto
	{
		class ItemEntry;
	}

	class GameBagS;
	class GameItemS;
	class GamePlayerS;

	/// Contains item data.
	struct ItemData
	{
		uint32 entry;
		uint16 slot;
		uint8 stackCount;
		uint64 creator;
		uint64 contained;
		uint16 durability;
		uint16 randomPropertyIndex;
		uint16 randomSuffixIndex;

		ItemData()
			: entry(0)
			, slot(0)
			, stackCount(0)
			, creator(0)
			, contained(0)
			, durability(0)
			, randomPropertyIndex(0)
			, randomSuffixIndex(0)
		{
		}
	};

	io::Writer& operator << (io::Writer& w, ItemData const& object);
	io::Reader& operator >> (io::Reader& r, ItemData& object);


	/// Represents a characters inventory and provides functionalities like
	/// adding and organizing items.
	class Inventory
	{
		friend io::Writer& operator << (io::Writer& w, Inventory const& object);
		friend io::Reader& operator >> (io::Reader& r, Inventory& object);

	private:

		Inventory(const Inventory& Other) = delete;
		Inventory& operator=(const Inventory& Other) = delete;

	public:

		// NOTE: We provide shared_ptr<> because these signals could be used to collect changed items
		// and send a bundled package to the client, so these item instances may need to stay alive
		// until then.

		/// Fired when a new item instance was created.
		signal<void(std::shared_ptr<GameItemS>, uint16)> itemInstanceCreated;

		/// Fired when an item instance was updated (stack count changed for example).
		signal<void(std::shared_ptr<GameItemS>, uint16)> itemInstanceUpdated;

		/// Fired when an item instance is about to be destroyed.
		signal<void(std::shared_ptr<GameItemS>, uint16)> itemInstanceDestroyed;

	public:

		/// Initializes a new instance of the inventory class.
		/// @param owner The owner of this inventory.
		explicit Inventory(GamePlayerS& owner);

		/// Tries to add multiple items of the same entry to the inventory.
		/// @param entry The item template to be used for creating new items.
		/// @param amount The amount of items to create, has to be greater than 0.
		/// @param out_slots If not nullptr, a map, containing all slots and counters will be filled.
		/// @returns game::inventory_change_failure::Okay if succeeded.
		InventoryChangeFailure CreateItems(const proto::ItemEntry& entry, uint16 amount = 1, std::map<uint16, uint16>* out_addedBySlot = nullptr);

		/// Tries to add multiple existing items of the same entry to the inventory.
		/// @param entry The item template to be used for creating new items.
		/// @param out_slot If not nullptr, a slot number that will be used by the item.
		/// @returns game::inventory_change_failure::Okay if succeeded.
		InventoryChangeFailure AddItem(std::shared_ptr<GameItemS> item, uint16* out_slot = nullptr);

		/// Tries to remove multiple items of the same entry.
		/// @param entry The item template to delete.
		/// @param amount The amount of items to delete. If 0, ALL items of that entry are deleted.
		/// @returns game::inventory_change_failure::Okay if succeeded.
		InventoryChangeFailure RemoveItems(const proto::ItemEntry& entry, uint16 amount = 1);

		/// Tries to remove an item at a specified slot.
		/// @param absoluteSlot The item slot to delete from.
		/// @param stacks The number of stacks to remove. If 0, ALL stacks will be removed. Stacks are capped automatically,
		///               so that if stacks >= actual item stacks, simply all stacks will be removed as well.
		/// @param sold If set to true, the item will be moved to the list of buyback items.
		/// @returns game::inventory_change_failure::Okay if succeeded.
		InventoryChangeFailure RemoveItem(uint16 absoluteSlot, uint16 stacks = 0, bool sold = false);

		/// Tries to remove an item by it's guid. This is basically just a shortcut to findItemByGUID() and removeItem().
		/// @param guid The item guid.
		/// @param stacks The number of stacks to remove. If 0, ALL stacks will be removed. Stacks are capped automatically,
		///               so that if stacks >= actual item stacks, simply all stacks will be removed as well.
		/// @returns game::inventory_change_failure::Okay if succeeded.
		InventoryChangeFailure RemoveItemByGUID(uint64 guid, uint16 stacks = 0);

		/// Tries to swap two slots. Can also be used to move items, if one of the slots is
		/// empty.
		/// @param slotA The first (source) slot.
		/// @param slotB The second (destination) slot.
		/// @returns game::inventory_change_failure::Okay if succeeded.
		InventoryChangeFailure SwapItems(uint16 slotA, uint16 slotB);

		/// Determines whether a slot is valid for the given item entry and this character.
		/// This also validates equipment slots and bags.
		/// @param slot The absolute slot index to check.
		/// @param entry The item prototype to check.
		/// @return game::inventory_change_failure::Okay if the slot is valid.
		InventoryChangeFailure IsValidSlot(uint16 slot, const proto::ItemEntry& entry) const;

		/// Determines whether the specified items can be stored.
		InventoryChangeFailure CanStoreItems(const proto::ItemEntry& entry, uint16 amount = 1) const;
		
		/// Gets a reference of the owner of this inventory.
		GamePlayerS& GetOwner() {
			return m_owner;
		}

		/// Gets the total amount of a specific item in the inventory. This method uses a cache,
		/// so you don't need to worry about performance too much here.
		/// @param itemId The entry id of the searched item.
		/// @returns Number of items that the player has.
		uint16 GetItemCount(uint32 itemId) const;

		/// Determines whether the player has an item in his inventory. This method is merely
		/// syntactic sugar, it simply checks if getItemCount(itemId) is greater that 0.
		/// @param itemid The entry id of the searched item.
		/// @returns true if the player has the item.
		bool HasItem(uint32 itemId) const {
			return GetItemCount(itemId) > 0;
		}

		/// Determines whether the character has equipped a quiver. This is useful because
		/// players can only equip one quiver at a time, so this check might be used at multiple
		/// locations.
		/// @returns true if the player has equipped a quiver.
		bool HasEquippedQuiver() const;

		/// Gets an absolute slot position from a bag index and a bag slot.
		static uint16 GetAbsoluteSlot(uint8 bag, uint8 slot);

		/// Splits an absolute slot into a bag index and a bag slot.
		static bool GetRelativeSlots(uint16 absoluteSlot, uint8& out_bag, uint8& out_slot);

		/// Gets the amount of free inventory slots.
		uint16 GetFreeSlotCount() const {
			return m_freeSlots;
		}

		/// Returns an item at a specified absolute slot.
		std::shared_ptr<GameItemS> GetItemAtSlot(uint16 absoluteSlot) const;

		/// Returns a bag at a specified absolute slot.
		std::shared_ptr<GameBagS> GetBagAtSlot(uint16 absolute_slot) const;

		/// Returns the weapon at a specified slot.
		std::shared_ptr<GameItemS> GetWeaponByAttackType(WeaponAttack attackType, bool nonbroken, bool useable) const;

		/// Finds an item by it's guid.
		/// @param guid The GUID of the searched item.
		/// @param out_slot The absolute item slot will be stored there.
		/// @returns false if the item could not be found.
		bool FindItemByGUID(uint64 guid, uint16& out_slot) const;

		/// Determines whether the given slot is an equipment slot.
		static bool IsEquipmentSlot(uint16 absoluteSlot);

		/// Determines whether the given slot is a custom bag slot (not in a bag, but a bag equipment slot).
		static bool IsBagPackSlot(uint16 absoluteSlot);

		/// Determines whether the given slot is a slot in the default bag.
		static bool IsInventorySlot(uint16 absoluteSlot);

		/// Determines whether the given slot is a slot in a bag (not the default bag).
		static bool IsBagSlot(uint16 absoluteSlot);

		/// Determines whether the given slot is a slot in a bag bar.
		static bool IsBagBarSlot(uint16 absoluteSlot);

		/// Determines whether the given slot is a buyback slot.
		static bool IsBuyBackSlot(uint16 absoluteSlot);

		/// Tries to repair all items in the players bags and also consumes money from the owner.
		/// @returns Repair cost that couldn't be consumed, so if this is greater than 0, there was an error.
		uint32 RepairAllItems();

		/// Tries to repair an item at the given slot and also consume money from the owner.
		/// @returns Repair cost that couldn't be consumed, so if this is greater than 0, there was an error.
		uint32 RepairItem(uint16 absoluteSlot);

	public:

		/// Adds a new realm data entry to the inventory. Note that this method should only be called
		/// on the realm, when the inventory is loaded from the database.
		void AddRealmData(const ItemData& data);

		/// Gets the inventory's realm data, if any. Should only be used on the realm when saving the
		/// inventory into the database.
		const std::vector<ItemData>& GetItemData() const { return m_realmData; }

		/// Constructs actual items from realm data and actually "spawns" the items objects.
		void ConstructFromRealmData(std::vector<GameObjectS*>& out_items);

	private:

		/// Parameters: Bag-ID, Start-Slot, End-Slot
		typedef std::function<bool(uint8, uint8, uint8)> BagCallbackFunc;

		/// Executes a callback function for every bag the player has (including the default bag).
		void ForEachBag(const BagCallbackFunc& callback) const;

		/// Fired when an item despawns (this signal is used to force item removal, currently used
		/// when lootable item no longer has loot).
		void OnItemDespawned(GameObjectS& object);

		void OnSetItemEquipped(uint32 set);

		void OnSetItemUnequipped(uint32 set);

	private:

		GamePlayerS& m_owner;

		/// Stores the actual item instances by slot.
		std::map<uint16, std::shared_ptr<GameItemS>> m_itemsBySlot;

		/// Used to cache count of items. This is needed to performantly check for total amount
		/// of items in some checks (so we don't have to iterate through the whole inventory).
		std::map<uint32, uint16> m_itemCounter;

		/// Cached amount of free slots. Used for performance
		uint16 m_freeSlots;

		/// This is needed for serialization/deserialization, as item instances are generated only
		/// on the world node. If m_realmData is not empty on initialization time, the item instances
		/// will be created and this map will be cleared.
		std::vector<ItemData> m_realmData;

		std::map<uint64, scoped_connection> m_itemDespawnSignals;

		std::map<uint32, uint8> m_setItems;

		/// The next buyback slot to be used.
		uint8 m_nextBuyBackSlot;

		/// Helper structure for organizing slot information during item creation
		struct ItemSlotInfo
		{
			LinearSet<uint16> emptySlots;
			LinearSet<uint16> usedCapableSlots;
			uint16 availableStacks = 0;
		};

		/// Helper method to validate item count limits
		InventoryChangeFailure ValidateItemLimits(const proto::ItemEntry& entry, uint16 amount) const;
		
		/// Helper method to find available slots for item creation
		InventoryChangeFailure FindAvailableSlots(const proto::ItemEntry& entry, uint16 amount, ItemSlotInfo& slotInfo) const;
		
		/// Helper method to add items to existing stacks
		uint16 AddToExistingStacks(const proto::ItemEntry& entry, uint16 amount, const LinearSet<uint16>& usedCapableSlots, std::map<uint16, uint16>* out_addedBySlot);
		
		/// Helper method to update item stack and notify systems
		void UpdateItemStack(const proto::ItemEntry& entry, std::shared_ptr<GameItemS> item, uint16 slot, uint16 added, std::map<uint16, uint16>* out_addedBySlot);
		
		/// Helper method to notify about slot updates
		void NotifySlotUpdate(uint16 slot);
		
		/// Helper method to create new item instances
		uint16 CreateNewItems(const proto::ItemEntry& entry, uint16 amount, const LinearSet<uint16>& emptySlots, std::map<uint16, uint16>* out_addedBySlot);
		
		/// Helper method to create a single item instance
		std::shared_ptr<GameItemS> CreateSingleItem(const proto::ItemEntry& entry, uint16 slot);
		
		/// Helper method to setup a newly created item
		void SetupNewItem(std::shared_ptr<GameItemS> item, const proto::ItemEntry& entry, uint16 slot);
		
		/// Helper method to add an item to a specific slot and update all related systems
		void AddItemToSlot(std::shared_ptr<GameItemS> item, uint16 slot);
		
		/// Helper method to update player fields when adding a new item
		void UpdatePlayerFieldsForNewItem(std::shared_ptr<GameItemS> item, uint16 slot);
		
		/// Helper method to update equipment visuals
		void UpdateEquipmentVisuals(std::shared_ptr<GameItemS> item, uint8 subslot);
		
		/// Helper method to update bag slot
		void UpdateBagSlot(std::shared_ptr<GameItemS> item, uint8 bag, uint8 subslot);

		/// Helper method to validate swap prerequisites
		InventoryChangeFailure ValidateSwapPrerequisites(std::shared_ptr<GameItemS> srcItem, std::shared_ptr<GameItemS> dstItem, uint16 slotA, uint16 slotB);
		
		/// Helper method to check if two items can be merged
		bool CanMergeItems(std::shared_ptr<GameItemS> srcItem, std::shared_ptr<GameItemS> dstItem) const;
		
		/// Helper method to merge item stacks
		InventoryChangeFailure MergeItemStacks(std::shared_ptr<GameItemS> srcItem, std::shared_ptr<GameItemS> dstItem, uint16 slotA, uint16 slotB);
		
		/// Helper method to remove an item from a slot
		void RemoveItemFromSlot(uint16 slot);
		
		/// Helper method to validate bag constraints
		InventoryChangeFailure ValidateBagConstraints(std::shared_ptr<GameItemS> srcItem, uint16 slotB) const;
		
		/// Helper method to perform the actual item swap
		/// Automatically moves offhand items to inventory when equipping 2-handed weapons
		void PerformItemSwap(std::shared_ptr<GameItemS> srcItem, std::shared_ptr<GameItemS> dstItem, uint16 slotA, uint16 slotB);
		
		/// Helper method to update slot contents
		void UpdateSlotContents(uint16 slot, std::shared_ptr<GameItemS> item);
		
		/// Helper method to update bag slot counts
		void UpdateBagSlotCounts(std::shared_ptr<GameItemS> srcItem, std::shared_ptr<GameItemS> dstItem, uint16 slotA, uint16 slotB);
		
		/// Helper method to update free slot count
		void UpdateFreeSlotCount(uint16 slotA, uint16 slotB, bool dstItemExists);
		
		/// Helper method to apply swap effects (stats, visuals, bonding)
		void ApplySwapEffects(std::shared_ptr<GameItemS> srcItem, std::shared_ptr<GameItemS> dstItem, uint16 slotA, uint16 slotB);
		
		/// Helper method to apply equipment effects
		void ApplyEquipmentEffects(std::shared_ptr<GameItemS> srcItem, std::shared_ptr<GameItemS> dstItem, uint16 slotA, uint16 slotB);
		
		/// Helper method to handle item set effects
		void HandleItemSetEffects(std::shared_ptr<GameItemS> item, bool equipped);
		
		/// Helper method to find an empty slot
		uint16 FindEmptySlot() const;
		
		/// Helper method to setup properties for an existing item
		void SetupExistingItem(std::shared_ptr<GameItemS> item, const proto::ItemEntry& entry, uint16 targetSlot);
		
		/// Helper method to add an existing item to a slot
		void AddExistingItemToSlot(std::shared_ptr<GameItemS> item, uint16 targetSlot, uint16* out_slot);
	};

	/// Serializes this inventory.
	io::Writer& operator << (io::Writer& w, Inventory const& object);
	/// Deserializes this inventory.
	io::Reader& operator >> (io::Reader& r, Inventory& object);
}