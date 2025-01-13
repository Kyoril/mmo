
#include "item_handle.h"
#include "game_item_c.h"
#include "log/default_log_levels.h"

namespace mmo
{
	ItemHandle::ItemHandle(GameItemC& item)
		: WeakHandle(item, item.removed)
	{
	}

	ItemHandle::ItemHandle() = default;

	int32 ItemHandle::GetStackCount() const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetStackCount();
	}

	bool ItemHandle::IsBag() const
	{
		if (!CheckNonNull()) return false;
		return Get()->IsBag();
	}

	int32 ItemHandle::GetBagSlots() const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetBagSlots();
	}

	const char* ItemHandle::GetItemClass() const
	{
		if (!CheckNonNull()) return nullptr;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return nullptr;

		static const char* s_itemClassStrings[] = {
			"CONSUMABLE",
			"CONTAINER",
			"WEAPON",
			"GEM",
			"ARMOR",
			"REAGENT",
			"PROJECTILE",
			"TRADEGOODS",
			"GENERIC",
			"RECIPE",
			"MONEY",
			"QUIVER",
			"QUEST",
			"KEY",
			"PERMANENT",
			"JUNK",
		};

		static_assert(std::size(s_itemClassStrings) == item_class::Count_, "Item class strings mismatch");

		ASSERT(info->itemClass < item_class::Count_);
		return s_itemClassStrings[info->itemClass];
	}

	const char* ItemHandle::GetItemSubClass() const
	{
		if (!CheckNonNull()) return nullptr;

		// TODO: Subclass string depends on class string

		return nullptr;
	}

	const char* ItemHandle::GetInventoryType() const
	{
		if (!CheckNonNull()) return nullptr;


		const ItemInfo* info = Get()->GetEntry();
		if (!info) return nullptr;

		static const char* s_inventoryTypeStrings[] = {
			"NON_EQUIP",
			"HEAD",
			"NECK",
			"SHOULDERS",
			"BODY",
			"CHEST",
			"WAIST",
			"LEGS",
			"FEET",
			"WRISTS",
			"HANDS",
			"FINGER",
			"TRINKET",
			"WEAPON",
			"SHIELD",
			"RANGED",
			"CLOAK",
			"TWO_HANDED_WEAPON",
			"BAG",
			"TABARD",
			"ROBE",
			"MAIN_HAND_WEAPON",
			"OFF_HAND_WEAPON",
			"HOLDABLE",
			"AMMO",
			"THROWN",
			"RANGED_RIGHT",
			"QUIVER",
			"RELIC",
		};

		static_assert(std::size(s_inventoryTypeStrings) == inventory_type::Count_, "InventoryType strings mismatch");

		ASSERT(info->inventoryType < item_class::Count_);
		return s_inventoryTypeStrings[info->inventoryType];
	}

	bool ItemHandle::CheckNonNull() const
	{
		if (Get())
		{
			return true;
		}

		ELOG("Expected non-null item handle!");
		return false;
	}
}
