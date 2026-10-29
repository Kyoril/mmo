
#include "item.h"

#include "base/macros.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

#include <cstring>
#include <iterator>

namespace mmo
{
	namespace
	{
		const char* s_inventoryTypeStrings[] = {
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
	}

	inventory_type::Type GetInventoryTypeByName(const char* name)
	{
		if (name == nullptr)
		{
			return inventory_type::NonEquip;
		}

		for (int32 type = 0; type < inventory_type::Count_; ++type)
		{
			if (std::strcmp(s_inventoryTypeStrings[type], name) == 0)
			{
				return static_cast<inventory_type::Type>(type);
			}
		}

		return inventory_type::NonEquip;
	}

	int32 GetComparisonEquipmentSlot(const inventory_type::Type type, const uint32 index)
	{
		// Mirrors the slot rules of the server's EquipmentManager::ValidateSlotCompatibility.
		int32 slots[2] = { -1, -1 };

		switch (type)
		{
		case inventory_type::Head:
			slots[0] = player_equipment_slots::Head;
			break;
		case inventory_type::Neck:
			slots[0] = player_equipment_slots::Neck;
			break;
		case inventory_type::Shoulders:
			slots[0] = player_equipment_slots::Shoulders;
			break;
		case inventory_type::Body:
			slots[0] = player_equipment_slots::Body;
			break;
		case inventory_type::Chest:
		case inventory_type::Robe:
			slots[0] = player_equipment_slots::Chest;
			break;
		case inventory_type::Waist:
			slots[0] = player_equipment_slots::Waist;
			break;
		case inventory_type::Legs:
			slots[0] = player_equipment_slots::Legs;
			break;
		case inventory_type::Feet:
			slots[0] = player_equipment_slots::Feet;
			break;
		case inventory_type::Wrists:
			slots[0] = player_equipment_slots::Wrists;
			break;
		case inventory_type::Hands:
			slots[0] = player_equipment_slots::Hands;
			break;
		case inventory_type::Finger:
			slots[0] = player_equipment_slots::Finger1;
			slots[1] = player_equipment_slots::Finger2;
			break;
		case inventory_type::Trinket:
			slots[0] = player_equipment_slots::Trinket1;
			slots[1] = player_equipment_slots::Trinket2;
			break;
		case inventory_type::Cloak:
			slots[0] = player_equipment_slots::Back;
			break;
		case inventory_type::Weapon:
			slots[0] = player_equipment_slots::Mainhand;
			slots[1] = player_equipment_slots::Offhand;
			break;
		case inventory_type::TwoHandedWeapon:
		case inventory_type::MainHandWeapon:
			slots[0] = player_equipment_slots::Mainhand;
			break;
		case inventory_type::OffHandWeapon:
		case inventory_type::Shield:
		case inventory_type::Holdable:
			slots[0] = player_equipment_slots::Offhand;
			break;
		case inventory_type::Ranged:
		case inventory_type::Thrown:
		case inventory_type::RangedRight:
			slots[0] = player_equipment_slots::Ranged;
			break;
		case inventory_type::Tabard:
			slots[0] = player_equipment_slots::Tabard;
			break;
		default:
			break;
		}

		if (index >= std::size(slots))
		{
			return -1;
		}

		return slots[index];
	}

	const char* ItemInfo::GetItemClassName() const
	{
		// TODO: Resolve item class name
		return nullptr;
	}

	const char* ItemInfo::GetItemSubClassName() const
	{
		// TODO: Resolve item subclass name
		return nullptr;
	}

	uint32 ItemInfo::GetProficiency() const
	{
		// Return the required proficiency ID (0 means no proficiency required)
		return requiredProficiency;
	}

	const char* ItemInfo::GetItemInventoryTypeName() const
	{
		ASSERT(inventoryType < inventory_type::Count_);
		return s_inventoryTypeStrings[inventoryType];
	}

	const char* ItemInfo::GetIcon() const
	{
		static const char* s_defaultIcon = "Interface/Icons/Spells/S_Attack.htex";
		return s_defaultIcon;
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
			nullptr,	// 2 - 6: former hard-coded attributes, see item_stat::AttributeStart
			nullptr,
			nullptr,
			nullptr,
			nullptr,
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

	int32 ItemInfo::GetStatAttribute(const int32 index) const
	{
		if (index < 0 || index >= static_cast<int32>(std::size(stats)))
		{
			return -1;
		}

		return item_stat::ToAttribute(stats[index].type);
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
			<< io::write<uint32>(itemInfo.skill)
			<< io::write<uint32>(itemInfo.requiredProficiency);

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
			>> io::read<uint32>(itemInfo.skill)
			>> io::read<uint32>(itemInfo.requiredProficiency);

		return reader;
	}
}
