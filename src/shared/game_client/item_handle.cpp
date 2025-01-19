
#include "item_handle.h"
#include "game_item_c.h"
#include "log/default_log_levels.h"

namespace mmo
{
	ItemHandle::ItemHandle(GameItemC& item, const proto_client::SpellManager& spells)
		: WeakHandle(item, item.removed)
		, m_spells(&spells)
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

	const char* ItemHandle::GetName() const
	{
		if (!CheckNonNull()) return nullptr;
		if (!Get()->GetEntry()) return nullptr;

		return Get()->GetEntry()->name.c_str();
	}

	const char* ItemHandle::GetDescription() const
	{
		if (!CheckNonNull()) return nullptr;
		if (!Get()->GetEntry()) return nullptr;

		return Get()->GetEntry()->description.c_str();
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

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return nullptr;

		static const char* s_consumableSubclassStrings[] = {
			"CONSUMABLE",
			"POTION",
			"ELIXIR",
			"FLASK",
			"SCROLL",
			"FOOD",
			"ITEM_ENHANCEMENT",
			"BANDAGE"
		};

		static_assert(std::size(s_consumableSubclassStrings) == item_subclass_consumable::Count_, "Consumable subclass strings array size mismatch");

		static const char* s_containerSubclassStrings[] = {
			"CONTAINER"
		};

		static_assert(std::size(s_containerSubclassStrings) == item_subclass_container::Count_, "Container subclass strings array size mismatch");

		static const char* s_weaponSubclassStrings[] = {
			"ONE_HANDED_AXE",
			"TWO_HANDED_AXE",
			"BOW",
			"GUN",
			"ONE_HANDED_MACE",
			"TWO_HANDED_MACE",
			"POLEARM",
			"ONE_HANDED_SWORD",
			"TWO_HANDED_SWORD",
			"STAFF",
			"FIST",
			"DAGGER",
			"THROWN",
			"SPEAR",
			"CROSS_BOW",
			"WAND",
			"FISHING_POLE"
		};

		static_assert(std::size(s_weaponSubclassStrings) == item_subclass_weapon::Count_, "Weapon subclass strings array size mismatch");

		static const char* s_gemSubclassStrings[] = {
			"RED",
			"BLUE",
			"YELLOW",
			"PURPLE",
			"GREEN",
			"ORANGE",
			"PRISMATIC"
		};

		static_assert(std::size(s_gemSubclassStrings) == item_subclass_gem::Count_, "Gem subclass strings array size mismatch");

		static const char* s_armorSubclassStrings[] = {
			"MISC",
			"CLOTH",
			"LEATHER",
			"MAIL",
			"PLATE",
			"BUCKLER",
			"SHIELD",
			"LIBRAM",
			"IDOL",
			"TOTEM"
		};

		static_assert(std::size(s_armorSubclassStrings) == item_subclass_armor::Count_, "Armor subclass strings array size mismatch");

		static const char* s_projectileSubclassStrings[] = {
			"WAND",
			"BOLT",
			"ARROW",
			"BULLET",
			"THROWN"
		};

		static_assert(std::size(s_projectileSubclassStrings) == item_subclass_projectile::Count_, "Projectile subclass strings array size mismatch");

		static const char* s_tradeGoodsSubclassStrings[] = {
			"TRADE_GOODS",
			"PARTS",
			"EXPLOSIVES",
			"DEVICES",
			"JEWELCRAFTING",
			"CLOTH",
			"LEATHER",
			"METAL_STONE",
			"MEAT",
			"HERB",
			"ELEMENTAL",
			"TRADE_GOODS_OTHER",
			"ENCHANTING",
			"MATERIAL"
		};

		static_assert(std::size(s_tradeGoodsSubclassStrings) == item_subclass_trade_goods::Count_, "Trade goods subclass strings array size mismatch");

		switch (info->itemClass)
		{
		case item_class::Consumable:	return s_consumableSubclassStrings[info->itemSubclass];
		case item_class::Container:		return s_containerSubclassStrings[info->itemSubclass];
		case item_class::Weapon:		return s_weaponSubclassStrings[info->itemSubclass];
		case item_class::Gem:			return s_gemSubclassStrings[info->itemSubclass];
		case item_class::Armor:			return s_armorSubclassStrings[info->itemSubclass];
		case item_class::Projectile:	return s_projectileSubclassStrings[info->itemSubclass];
		case item_class::TradeGoods:	return s_tradeGoodsSubclassStrings[info->itemSubclass];
		}

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

		ASSERT(info->inventoryType < inventory_type::Count_);
		return s_inventoryTypeStrings[info->inventoryType];
	}

	const char* ItemHandle::GetIcon() const
	{
		static const String s_defaultItemIcon = "Interface\\Icons\\Spells\\S_Attack.htex";

		if (!CheckNonNull()) 
		{
			return nullptr;
		}

		const ItemInfo* info = Get()->GetEntry();
		if (!info)
		{
			return nullptr;
		}

		if (!info->icon.empty())
		{
			return info->icon.c_str();
		}

		return s_defaultItemIcon.c_str();
	}

	int32 ItemHandle::GetQuality() const
	{
		if (!CheckNonNull()) return 0;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return 0;

		return info->quality;
	}

	int32 ItemHandle::GetMinDamage() const
	{
		if (!CheckNonNull()) return 0;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return 0;
		if (info->itemClass != item_class::Weapon) return 0;

		return static_cast<int32>(floor(info->damage.min));
	}

	int32 ItemHandle::GetMaxDamage() const
	{
		if (!CheckNonNull()) return 0;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return 0;
		if (info->itemClass != item_class::Weapon) return 0;

		return static_cast<int32>(floor(info->damage.max));
	}

	float ItemHandle::GetAttackSpeed() const
	{
		if (!CheckNonNull()) return 0;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return 0.0f;
		if (info->itemClass != item_class::Weapon) return 0.0f;

		return static_cast<float>(info->attackTime) / 1000.0f;
	}

	float ItemHandle::GetDps() const
	{
		const float minDamage = GetMinDamage();
		const float maxDamage = GetMaxDamage();
		const float attackTime = GetAttackSpeed();

		if (attackTime <= 0.0f)
		{
			return 0.0f;
		}

		if (minDamage == maxDamage)
		{
			return minDamage / attackTime;
		}

		return (maxDamage + minDamage) * 0.5f / attackTime;
	}

	int32 ItemHandle::GetArmor() const
	{
		if (!CheckNonNull()) return 0;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return 0;

		return static_cast<int32>(info->armor);
	}

	const ItemInfo* ItemHandle::GetEntry() const
	{
		if (!CheckNonNull()) return nullptr;
		return Get()->GetEntry();
	}

	int32 ItemHandle::GetBlock() const
	{
		if (!CheckNonNull()) return 0;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return 0;

		return static_cast<int32>(info->block);
	}

	int32 ItemHandle::GetDurability() const
	{
		if (!CheckNonNull()) return 0;
		return Get()->Get<uint32>(object_fields::Durability);
	}

	int32 ItemHandle::GetMaxDurability() const
	{
		if (!CheckNonNull()) return 0;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return 0;

		return static_cast<int32>(info->maxdurability);
	}

	uint32 ItemHandle::GetSellPrice() const
	{
		if (!CheckNonNull()) return 0;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return 0;

		return info->sellPrice;
	}

	const proto_client::SpellEntry* ItemHandle::GetSpell(const int32 index) const
	{
		if (!CheckNonNull()) return nullptr;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return nullptr;

		if (!m_spells) return nullptr;
		return m_spells->getById(info->spells[index].spellId);
	}

	const char* ItemHandle::GetSpellTriggerType(int32 index) const
	{
		if (!CheckNonNull()) return nullptr;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return nullptr;

		if (index < 0 || index > 4)
		{
			return nullptr;
		}

		static const char* s_spellTriggerTypes[] = {
			"ON_USE",
			"ON_EQUIP",
			"HIT_CHANCE",
		};

		static_assert(std::size(s_spellTriggerTypes) == item_spell_trigger::Count_, "Spell trigger type strings mismatch");

		ASSERT(info->spells[index].triggertype < item_spell_trigger::Count_);
		return s_spellTriggerTypes[info->spells[index].triggertype];
	}

	const char* ItemHandle::GetStatType(const int32 index) const
	{
		if (!CheckNonNull()) return nullptr;

		if (index < 0 || index > 9)
		{
			return nullptr;
		}

		static const char* s_statTypeStrings[] = {
			"MANA",
			"HEALTH",
			"AGILITY",
			"STRENGTH",
			"INTELLECT",
			"SPIRIT",
			"STAMINA",
			"DEFENSE",
			"DODGE",
			"PARRY",
			"BLOCK",
			"HIT_MELEE",
			"HIT_RANGED",
			"HIT_SPELL",
			"CRIT_MELEE",
			"CRIT_RANGED",
			"CRIT_SPELL",
			"HIT_TAKEN_MELEE",
			"HIT_TAKEN_RANGED",
			"HIT_TAKEN_SPELL",
			"CRIT_TAKEN_MELEE",
			"CRIT_TAKEN_RANGED",
			"CRIT_TAKEN_SPELL",
			"HASTE_MELEE",
			"HASTE_RANGED",
			"HASTE_SPELL",
			"HIT",
			"CRIT",
			"HIT_TAKEN",
			"CRIT_TAKEN",
			"HASTE",
			"EXPERTISE"
		};

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return nullptr;

		if (info->stats[index].type >= std::size(s_statTypeStrings))
		{
			return nullptr;
		}

		return s_statTypeStrings[info->stats[index].type];
	}

	int32 ItemHandle::GetStatValue(const int32 index) const
	{
		if (!CheckNonNull()) return 0;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return 0;

		if (index < 0 || index > std::size(info->stats))
		{
			return 0;
		}

		return static_cast<int32>(info->stats[index].value);
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
