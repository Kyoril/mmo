
#include "item.h"

#include "base/macros.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo
{
	const char* ItemInfo::GetItemClassName() const
	{
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

		ASSERT(itemClass < item_class::Count_);
		return s_itemClassStrings[itemClass];
	}

	const char* ItemInfo::GetItemSubClassName() const
	{
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

		switch (itemClass)
		{
		case item_class::Consumable:	return s_consumableSubclassStrings[itemSubclass];
		case item_class::Container:		return s_containerSubclassStrings[itemSubclass];
		case item_class::Weapon:		return s_weaponSubclassStrings[itemSubclass];
		case item_class::Gem:			return s_gemSubclassStrings[itemSubclass];
		case item_class::Armor:			return s_armorSubclassStrings[itemSubclass];
		case item_class::Projectile:	return s_projectileSubclassStrings[itemSubclass];
		case item_class::TradeGoods:	return s_tradeGoodsSubclassStrings[itemSubclass];
		}

		return nullptr;
	}

	const char* ItemInfo::GetItemInventoryTypeName() const
	{
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

		ASSERT(inventoryType < inventory_type::Count_);
		return s_inventoryTypeStrings[inventoryType];
	}

	const char* ItemInfo::GetIcon() const
	{
		// TODO!
		return nullptr;
	}

	float ItemInfo::GetMinDamage() const
	{
		return damage.min;
	}

	float ItemInfo::GetMaxDamage() const
	{
		return damage.max;
	}

	float ItemInfo::GetAttackSpeed() const
	{
		return static_cast<float>(attackTime) / 1000.0f;
	}

	float ItemInfo::GetDps() const
	{
		return (damage.min + damage.max) / 2.0f / GetAttackSpeed();
	}

	const char* ItemInfo::GetStatType(int32 index) const
	{
		if (index < 0 || index >= static_cast<int32>(std::size(stats)))
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

		if (stats[index].type >= std::size(s_statTypeStrings))
		{
			return nullptr;
		}

		return s_statTypeStrings[stats[index].type];
	}

	int32 ItemInfo::GetStatValue(int32 index) const
	{
		if (index < 0 || index >= static_cast<int32>(std::size(stats)))
		{
			return 0;
		}

		return static_cast<int32>(stats[index].value);
	}

	uint32 ItemInfo::GetSpellId(int32 index) const
	{
		if (index < 0 || index >= static_cast<int32>(std::size(spells)))
		{
			return 0;
		}

		return spells[index].spellId;
	}

	const char* ItemInfo::GetSpellTriggerType(int32 index) const
	{
		if (index < 0 || index >= std::size(spells))
		{
			return nullptr;
		}

		static const char* s_spellTriggerTypes[] = {
			"ON_USE",
			"ON_EQUIP",
			"HIT_CHANCE",
		};

		static_assert(std::size(s_spellTriggerTypes) == item_spell_trigger::Count_, "Spell trigger type strings mismatch");

		ASSERT(spells[index].triggertype < item_spell_trigger::Count_);
		return s_spellTriggerTypes[spells[index].triggertype];
	}

	io::Writer& operator<<(io::Writer& writer, const ItemInfo& itemInfo)
	{
		writer
			<< io::write<uint64>(itemInfo.id)
			<< io::write_dynamic_range<uint8>(itemInfo.name)
			<< io::write_dynamic_range<uint8>(itemInfo.description)
			<< io::write<uint32>(itemInfo.itemClass)
			<< io::write<uint32>(itemInfo.itemSubclass)
			<< io::write<uint32>(itemInfo.inventoryType)
			<< io::write<uint32>(itemInfo.displayId)
			<< io::write<uint32>(itemInfo.quality)
			<< io::write<uint32>(itemInfo.flags)
			<< io::write<uint32>(itemInfo.buyCount)
			<< io::write<uint32>(itemInfo.buyPrice)
			<< io::write<uint32>(itemInfo.sellPrice)
			<< io::write<int32>(itemInfo.allowedClasses)
			<< io::write<int32>(itemInfo.allowedRaces)
			<< io::write<uint32>(itemInfo.itemlevel)
			<< io::write<uint32>(itemInfo.requiredlevel)
			<< io::write<uint32>(itemInfo.requiredskill)
			<< io::write<uint32>(itemInfo.requiredskillrank)
			<< io::write<uint32>(itemInfo.requiredspell)
			<< io::write<uint32>(itemInfo.requiredhonorrank)
			<< io::write<uint32>(itemInfo.requiredcityrank)
			<< io::write<uint32>(itemInfo.requiredrep)
			<< io::write<uint32>(itemInfo.requiredreprank)
			<< io::write<uint32>(itemInfo.maxcount)
			<< io::write<uint32>(itemInfo.maxstack)
			<< io::write<uint32>(itemInfo.containerslots);
		for (auto stat : itemInfo.stats)
		{
			writer.WritePOD(stat);
		}
		writer.WritePOD(itemInfo.damage);
		writer << io::write<uint32>(itemInfo.attackTime);

		writer
			<< io::write<uint32>(itemInfo.armor);
		for (const auto& resistance : itemInfo.resistance)
		{
			writer << io::write<uint32>(resistance);
		}

		writer
			<< io::write<uint32>(itemInfo.ammotype);
		for (const auto& spell : itemInfo.spells)
		{
			writer.WritePOD(spell);
		}
		writer
			<< io::write<uint32>(itemInfo.bonding)
			<< io::write<uint32>(itemInfo.lockid)
			<< io::write<uint32>(itemInfo.sheath)
			<< io::write<uint32>(itemInfo.randomproperty)
			<< io::write<uint32>(itemInfo.randomsuffix)
			<< io::write<uint32>(itemInfo.block)
			<< io::write<uint32>(itemInfo.itemset)
			<< io::write<uint32>(itemInfo.maxdurability)
			<< io::write<uint32>(itemInfo.area)
			<< io::write<int32>(itemInfo.map)
			<< io::write<int32>(itemInfo.bagfamily)
			<< io::write<int32>(itemInfo.material)
			<< io::write<int32>(itemInfo.totemcategory);
		for (const auto& socket : itemInfo.sockets)
		{
			writer.WritePOD(socket);
		}
		writer
			<< io::write<uint32>(itemInfo.socketbonus)
			<< io::write<uint32>(itemInfo.gemproperties)
			<< io::write<uint32>(itemInfo.disenchantskillval)
			<< io::write<uint32>(itemInfo.foodtype)
			<< io::write<uint32>(itemInfo.duration)
			<< io::write<uint32>(itemInfo.extraflags)
			<< io::write<uint32>(itemInfo.startquestid)
			<< io::write<float>(itemInfo.rangedrangepercent)
			<< io::write<uint32>(itemInfo.skill);

		return writer;
	}

	io::Reader& operator>>(io::Reader& reader, ItemInfo& itemInfo)
	{
		reader
			>> io::read<uint64>(itemInfo.id)
			>> io::read_container<uint8>(itemInfo.name)
			>> io::read_container<uint8>(itemInfo.description)
			>> io::read<uint32>(itemInfo.itemClass)
			>> io::read<uint32>(itemInfo.itemSubclass)
			>> io::read<uint32>(itemInfo.inventoryType)
			>> io::read<uint32>(itemInfo.displayId)
			>> io::read<uint32>(itemInfo.quality)
			>> io::read<uint32>(itemInfo.flags)
			>> io::read<uint32>(itemInfo.buyCount)
			>> io::read<uint32>(itemInfo.buyPrice)
			>> io::read<uint32>(itemInfo.sellPrice)
			>> io::read<int32>(itemInfo.allowedClasses)
			>> io::read<int32>(itemInfo.allowedRaces)
			>> io::read<uint32>(itemInfo.itemlevel)
			>> io::read<uint32>(itemInfo.requiredlevel)
			>> io::read<uint32>(itemInfo.requiredskill)
			>> io::read<uint32>(itemInfo.requiredskillrank)
			>> io::read<uint32>(itemInfo.requiredspell)
			>> io::read<uint32>(itemInfo.requiredhonorrank)
			>> io::read<uint32>(itemInfo.requiredcityrank)
			>> io::read<uint32>(itemInfo.requiredrep)
			>> io::read<uint32>(itemInfo.requiredreprank)
			>> io::read<uint32>(itemInfo.maxcount)
			>> io::read<uint32>(itemInfo.maxstack)
			>> io::read<uint32>(itemInfo.containerslots);
		for (auto& stat : itemInfo.stats)
		{
			reader.readPOD(stat);
		}
		reader.readPOD(itemInfo.damage);
		reader >> io::read<uint32>(itemInfo.attackTime);

		reader
			>> io::read<uint32>(itemInfo.armor);
		for (auto& resistance : itemInfo.resistance)
		{
			reader >> io::read<uint32>(resistance);
		}

		reader
			>> io::read<uint32>(itemInfo.ammotype);
		for (auto& spell : itemInfo.spells)
		{
			reader.readPOD(spell);
		}
		reader
			>> io::read<uint32>(itemInfo.bonding)
			>> io::read<uint32>(itemInfo.lockid)
			>> io::read<uint32>(itemInfo.sheath)
			>> io::read<uint32>(itemInfo.randomproperty)
			>> io::read<uint32>(itemInfo.randomsuffix)
			>> io::read<uint32>(itemInfo.block)
			>> io::read<uint32>(itemInfo.itemset)
			>> io::read<uint32>(itemInfo.maxdurability)
			>> io::read<uint32>(itemInfo.area)
			>> io::read<int32>(itemInfo.map)
			>> io::read<int32>(itemInfo.bagfamily)
			>> io::read<int32>(itemInfo.material)
			>> io::read<int32>(itemInfo.totemcategory);
		for (auto& socket : itemInfo.sockets)
		{
			reader.readPOD(socket);
		}
		reader
			>> io::read<uint32>(itemInfo.socketbonus)
			>> io::read<uint32>(itemInfo.gemproperties)
			>> io::read<uint32>(itemInfo.disenchantskillval)
			>> io::read<uint32>(itemInfo.foodtype)
			>> io::read<uint32>(itemInfo.duration)
			>> io::read<uint32>(itemInfo.extraflags)
			>> io::read<uint32>(itemInfo.startquestid)
			>> io::read<float>(itemInfo.rangedrangepercent)
			>> io::read<uint32>(itemInfo.skill);

		return reader;
	}
}
