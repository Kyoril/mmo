#pragma once

#include "base/typedefs.h"
#include "base/signal.h"
#include "game/item.h"

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
		InventoryChangeFailure createItems(const proto::ItemEntry& entry, uint16 amount = 1, std::map<uint16, uint16>* out_addedBySlot = nullptr);

		/// Tries to add multiple existing items of the same entry to the inventory.
		/// @param entry The item template to be used for creating new items.
		/// @param out_slot If not nullptr, a slot number that will be used by the item.
		/// @returns game::inventory_change_failure::Okay if succeeded.
		InventoryChangeFailure addItem(std::shared_ptr<GameItemS> item, uint16* out_slot = nullptr);

		/// Tries to remove multiple items of the same entry.
		/// @param entry The item template to delete.
		/// @param amount The amount of items to delete. If 0, ALL items of that entry are deleted.
		/// @returns game::inventory_change_failure::Okay if succeeded.
		InventoryChangeFailure removeItems(const proto::ItemEntry& entry, uint16 amount = 1);

		/// Tries to remove an item at a specified slot.
		/// @param absoluteSlot The item slot to delete from.
		/// @param stacks The number of stacks to remove. If 0, ALL stacks will be removed. Stacks are capped automatically,
		///               so that if stacks >= actual item stacks, simply all stacks will be removed as well.
		/// @param sold If set to true, the item will be moved to the list of buyback items.
		/// @returns game::inventory_change_failure::Okay if succeeded.
		InventoryChangeFailure removeItem(uint16 absoluteSlot, uint16 stacks = 0, bool sold = false);

		/// Tries to remove an item by it's guid. This is basically just a shortcut to findItemByGUID() and removeItem().
		/// @param guid The item guid.
		/// @param stacks The number of stacks to remove. If 0, ALL stacks will be removed. Stacks are capped automatically,
		///               so that if stacks >= actual item stacks, simply all stacks will be removed as well.
		/// @returns game::inventory_change_failure::Okay if succeeded.
		InventoryChangeFailure removeItemByGUID(uint64 guid, uint16 stacks = 0);

		/// Tries to swap two slots. Can also be used to move items, if one of the slots is
		/// empty.
		/// @param slotA The first (source) slot.
		/// @param slotB The second (destination) slot.
		/// @returns game::inventory_change_failure::Okay if succeeded.
		InventoryChangeFailure swapItems(uint16 slotA, uint16 slotB);

		/// Determines whether a slot is valid for the given item entry and this character.
		/// This also validates equipment slots and bags.
		/// @param slot The absolute slot index to check.
		/// @param entry The item prototype to check.
		/// @return game::inventory_change_failure::Okay if the slot is valid.
		InventoryChangeFailure isValidSlot(uint16 slot, const proto::ItemEntry& entry) const;

		/// Determines whether the specified items can be stored.
		InventoryChangeFailure canStoreItems(const proto::ItemEntry& entry, uint16 amount = 1) const;
		
		/// Gets a reference of the owner of this inventory.
		GamePlayerS& getOwner() {
			return m_owner;
		}

		/// Gets the total amount of a specific item in the inventory. This method uses a cache,
		/// so you don't need to worry about performance too much here.
		/// @param itemId The entry id of the searched item.
		/// @returns Number of items that the player has.
		uint16 getItemCount(uint32 itemId) const;

		/// Determines whether the player has an item in his inventory. This method is merely
		/// syntactic sugar, it simply checks if getItemCount(itemId) is greater that 0.
		/// @param itemid The entry id of the searched item.
		/// @returns true if the player has the item.
		bool hasItem(uint32 itemId) const {
			return getItemCount(itemId) > 0;
		}

		/// Determines whether the character has equipped a quiver. This is useful because
		/// players can only equip one quiver at a time, so this check might be used at multiple
		/// locations.
		/// @returns true if the player has equipped a quiver.
		bool hasEquippedQuiver() const;

		/// Gets an absolute slot position from a bag index and a bag slot.
		static uint16 getAbsoluteSlot(uint8 bag, uint8 slot);

		/// Splits an absolute slot into a bag index and a bag slot.
		static bool getRelativeSlots(uint16 absoluteSlot, uint8& out_bag, uint8& out_slot);

		/// Gets the amount of free inventory slots.
		uint16 getFreeSlotCount() const {
			return m_freeSlots;
		}

		/// Returns an item at a specified absolute slot.
		std::shared_ptr<GameItemS> getItemAtSlot(uint16 absoluteSlot) const;

		/// Returns a bag at a specified absolute slot.
		std::shared_ptr<GameBagS> getBagAtSlot(uint16 absolute_slot) const;

		/// Returns the weapon at a specified slot.
		std::shared_ptr<GameItemS> getWeaponByAttackType(WeaponAttack attackType, bool nonbroken, bool useable) const;

		/// Finds an item by it's guid.
		/// @param guid The GUID of the searched item.
		/// @param out_slot The absolute item slot will be stored there.
		/// @returns false if the item could not be found.
		bool findItemByGUID(uint64 guid, uint16& out_slot) const;

		/// Determines whether the given slot is an equipment slot.
		static bool isEquipmentSlot(uint16 absoluteSlot);

		/// Determines whether the given slot is a custom bag slot (not in a bag, but a bag equipment slot).
		static bool isBagPackSlot(uint16 absoluteSlot);

		/// Determines whether the given slot is a slot in the default bag.
		static bool isInventorySlot(uint16 absoluteSlot);

		/// Determines whether the given slot is a slot in a bag (not the default bag).
		static bool isBagSlot(uint16 absoluteSlot);

		/// Determines whether the given slot is a slot in a bag bar.
		static bool isBagBarSlot(uint16 absoluteSlot);

		/// Determines whether the given slot is a buyback slot.
		static bool isBuyBackSlot(uint16 absoluteSlot);

		/// Tries to repair all items in the players bags and also consumes money from the owner.
		/// @returns Repair cost that couldn't be consumed, so if this is greater than 0, there was an error.
		uint32 repairAllItems();

		/// Tries to repair an item at the given slot and also consume money from the owner.
		/// @returns Repair cost that couldn't be consumed, so if this is greater than 0, there was an error.
		uint32 repairItem(uint16 absoluteSlot);

	public:

		/// Adds a new realm data entry to the inventory. Note that this method should only be called
		/// on the realm, when the inventory is loaded from the database.
		void addRealmData(const ItemData& data);

		/// Gets the inventory's realm data, if any. Should only be used on the realm when saving the
		/// inventory into the database.
		const std::vector<ItemData> getItemData() const {
			return m_realmData;
		}

		/// Adds spawn blocks for every item in the inventory. These blocks will be included in
		/// the players spawn packet, so that all items that are available to the player will be sent
		/// at once.
		/// @param out_blocks A reference to the array of update blocks to send.
		void addSpawnBlocks(std::vector<std::vector<char>>& out_blocks);

	private:

		/// Parameters: Bag-ID, Start-Slot, End-Slot
		typedef std::function<bool(uint8, uint8, uint8)> BagCallbackFunc;

		/// Executes a callback function for every bag the player has (including the default bag).
		void forEachBag(BagCallbackFunc callback);

		/// Fired when an item despawns (this signal is used to force item removal, currently used
		/// when lootable item no longer has loot).
		void onItemDespawned(GameObjectS& object);

		void onSetItemEquipped(uint32 set);

		void onSetItemUnequipped(uint32 set);

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
	};

	/// Serializes this inventory.
	io::Writer& operator << (io::Writer& w, Inventory const& object);
	/// Deserializes this inventory.
	io::Reader& operator >> (io::Reader& r, Inventory& object);
}