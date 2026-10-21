// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_gear.h"

#include "game/item.h"
#include "proto_data/project.h"

namespace mmo
{
	namespace
	{
		/// Class ids from the class data.
		constexpr uint32 WarriorClass = 1;
		constexpr uint32 ScoutClass = 4;

		uint32 primaryStatFor(const uint32 classId)
		{
			switch (classId)
			{
			case WarriorClass:
				return item_stat::Strength;
			case ScoutClass:
				return item_stat::Agility;
			default:
				return item_stat::Intellect;
			}
		}

		bool fightsInMelee(const uint32 classId)
		{
			return classId == WarriorClass || classId == ScoutClass;
		}
	}

	std::vector<uint8> EquipmentSlotsFor(const uint32 inventoryType)
	{
		using namespace player_equipment_slots;

		switch (inventoryType)
		{
		case inventory_type::Head: return { Head };
		case inventory_type::Neck: return { Neck };
		case inventory_type::Shoulders: return { Shoulders };
		case inventory_type::Body: return { Body };
		case inventory_type::Chest:
		case inventory_type::Robe: return { Chest };
		case inventory_type::Waist: return { Waist };
		case inventory_type::Legs: return { Legs };
		case inventory_type::Feet: return { Feet };
		case inventory_type::Wrists: return { Wrists };
		case inventory_type::Hands: return { Hands };
		case inventory_type::Finger: return { Finger1, Finger2 };
		case inventory_type::Trinket: return { Trinket1, Trinket2 };
		case inventory_type::Cloak: return { Back };
		case inventory_type::Weapon:
		case inventory_type::MainHandWeapon:
		case inventory_type::TwoHandedWeapon: return { Mainhand };
		case inventory_type::Shield:
		case inventory_type::OffHandWeapon:
		case inventory_type::Holdable: return { Offhand };
		case inventory_type::Ranged:
		case inventory_type::Thrown:
		case inventory_type::RangedRight: return { Ranged };
		case inventory_type::Tabard: return { Tabard };
		default: return {};
		}
	}

	float ScoreItem(const proto::ItemEntry& item, const uint32 classId)
	{
		const uint32 primary = primaryStatFor(classId);

		float score = 0.0f;
		for (int i = 0; i < item.stats_size(); ++i)
		{
			const proto::ItemStats& stat = item.stats(i);
			const auto value = static_cast<float>(stat.value());
			if (stat.type() == primary)
			{
				score += value * 2.0f;
			}
			else if (stat.type() == item_stat::Stamina)
			{
				score += value * 1.0f;
			}
			else
			{
				score += value * 0.3f;
			}
		}

		score += static_cast<float>(item.armor()) * 0.05f;

		if (item.has_damage() && item.delay() > 0)
		{
			const float averageDamage = (item.damage().mindmg() + item.damage().maxdmg()) * 0.5f;
			const float damagePerSecond = averageDamage / (static_cast<float>(item.delay()) / 1000.0f);
			score += damagePerSecond * (fightsInMelee(classId) ? 3.0f : 0.5f);
		}

		// Tie-breaker, and the only thing separating two items without stats.
		score += static_cast<float>(item.itemlevel()) * 0.1f;
		return score;
	}

	bool IsJunk(const proto::ItemEntry& item)
	{
		return item.quality() == item_quality::Poor;
	}

	bool IsWearable(const proto::ItemEntry& item)
	{
		return !EquipmentSlotsFor(item.inventorytype()).empty();
	}
}
