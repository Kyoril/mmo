#pragma once

#include "item.h"
#include "quest.h"

namespace mmo
{
	namespace game_world_object_type
	{
		enum Type
		{
			Chest = 0,

			Door,

			// TODO: Add additional types here

			Count_,
		};
	}

	typedef game_world_object_type::Type GameWorldObjectType;

	namespace unit_flags
	{
		enum Type
		{
			None = 0x00000000,

			InCombat = 0x00000001,

			Lootable = 0x00000002,

			Looting = 0x00000004,

			Attacking = 0x00000008,
		};
	}

	namespace npc_flags
	{
		enum Type
		{
			None = 0x00000000,

			/// This npc wants to talk.
			Gossip = 0x00000001,

			/// This npc has a quest to accept or turn in.
			QuestGiver = 0x00000002,

			/// This npc is a trainer.
			Trainer = 0x00000004,

			/// This npc is a class trainer.
			TrainerClass = 0x00000008,

			/// This npc is a profession trainer.
			TrainerProfession = 0x00000010,

			/// This npc is a vendor.
			Vendor = 0x00000020,

			/// This npc offers repair services.
			Repair = 0x00000040,

			/// This npc is an inn keeper and offers inn services such as setting the bind location to return to.
			InnKeeper = 0x00000080,
		};
	}

	namespace unit_stand_state
	{
		enum Type
		{
			Stand = 0,

			Sit,

			Sleep,

			Dead,

			Kneel,

			Count_
		};
	}

	namespace object_update_flags
	{
		enum Type
		{
			None = 0,

			HasMovementInfo = 1 << 0
		};
	}

	namespace game_world_object_flags
	{
		enum Type
		{
			None = 0,

			InUse = 1 << 0,

			Locked = 1 << 1,

			NotInteractable = 1 << 2
		};
	}

	// Enumerates available object type ids.
	enum class ObjectTypeId
	{
		/// Default type. Generic object.
		Object = 0,

		/// The object is an item.
		Item = 1,

		/// An item container object.
		Container = 2,

		/// A living unit with health etc.
		Unit = 3,

		/// A player character, which is also a unit.
		Player = 4,

		/// A dynamic object which is temporarily spawned.
		DynamicObject = 5,

		/// A player corpse.
		Corpse = 6
	};

	
	// Enumerates available object fields
	namespace object_fields
	{
		enum ObjectFields
		{
			/// @brief 64 bit object guid.
			Guid = 0,
			/// @brief 32 bit object id
			Type = 2,
			/// @brief 32 bit object entry
			Entry = 3,
			/// @brief 32 bit object scale
			Scale = 4,
			/// @brief 64 bit object guid
			Owner = 5,

			/// @brief Number of object fields
			ObjectFieldCount = Owner + 2,
		};

		enum UnitFields
		{
			/// @brief 32 bit object field
			Level = ObjectFieldCount,

			Bytes,

			FactionTemplate,
			DisplayId,

			/// @brief 32 bit unit flags
			Flags,

			/// @brief 32 bit object field
			MaxHealth,
			/// @brief 32 bit object field
			Health,

			/// @brief 32 bit object field
			Mana,
			/// @brief 32 bit object field
			Rage,
			/// @brief 32 bit object field
			Energy,

			/// @brief 32 bit object field
			MaxMana,
			/// @brief 32 bit object field
			MaxRage,
			/// @brief 32 bit object field
			MaxEnergy,

			/// @brief 32 bit object field
			PowerType,

			StatStamina,
			StatStrength,
			StatAgility,
			StatIntellect,
			StatSpirit,

			PosStatStamina,
			PosStatStrength,
			PosStatAgility,
			PosStatIntellect,
			PosStatSpirit,

			NegStatStamina,
			NegStatStrength,
			NegStatAgility,
			NegStatIntellect,
			NegStatSpirit,

			Armor,

			PosStatArmor,
			NegStatArmor,

			AttackPower,

			/// @brief 64 bit object guid
			TargetUnit,

			/// @brief 32 bit time
			BaseAttackTime = TargetUnit + 2,

			/// @brief 32 bit time
			MinDamage,
			MaxDamage,

			NpcFlags,
			StandState,

			UnitFieldCount = StandState + 1,
		};

#define VISIBLE_ITEM_FIELDS(index, offset) \
	VisibleItem##index##_CREATOR = offset, \
	VisibleItem##index##_0 = VisibleItem##index##_CREATOR + 2, \
	VisibleItem##index##_PROPERTIES = VisibleItem##index##_0 + 12

#define VISIBLE_ITEM_FIELDS_NEXT(index, prev) \
	VisibleItem##index##_CREATOR = VisibleItem##prev##_PROPERTIES + 1, \
	VisibleItem##index##_0 = VisibleItem##index##_CREATOR + 2, \
	VisibleItem##index##_PROPERTIES = VisibleItem##index##_0 + 12

		enum PlayerFields
		{
			Xp = UnitFieldCount,

			NextLevelXp,

			MaxLevel,

			Class,

			Race,

			CharacterBytes,

			Money,

			Guild,

			AvailableAttributePoints = Guild + 2,

			AttributePointCost,

			// Visible item fields
			VISIBLE_ITEM_FIELDS(1, AttributePointCost + 2),
			VISIBLE_ITEM_FIELDS_NEXT(2, 1),
			VISIBLE_ITEM_FIELDS_NEXT(3, 2),
			VISIBLE_ITEM_FIELDS_NEXT(4, 3),
			VISIBLE_ITEM_FIELDS_NEXT(5, 4),
			VISIBLE_ITEM_FIELDS_NEXT(6, 5),
			VISIBLE_ITEM_FIELDS_NEXT(7, 6),
			VISIBLE_ITEM_FIELDS_NEXT(8, 7),
			VISIBLE_ITEM_FIELDS_NEXT(9, 8),
			VISIBLE_ITEM_FIELDS_NEXT(10, 9),
			VISIBLE_ITEM_FIELDS_NEXT(11, 10),
			VISIBLE_ITEM_FIELDS_NEXT(12, 11),
			VISIBLE_ITEM_FIELDS_NEXT(13, 12),
			VISIBLE_ITEM_FIELDS_NEXT(14, 13),
			VISIBLE_ITEM_FIELDS_NEXT(15, 14),
			VISIBLE_ITEM_FIELDS_NEXT(16, 15),
			VISIBLE_ITEM_FIELDS_NEXT(17, 16),
			VISIBLE_ITEM_FIELDS_NEXT(18, 17),
			VISIBLE_ITEM_FIELDS_NEXT(19, 18),

			// Each of the beneath fields contains a uint64 value (so 2 fields actually) with an item object guid or 0 if no item is located at the given slot!

			// 23 equippable slots (19 visible + 4 bags)
			InvSlotHead = VisibleItem19_PROPERTIES + 1,

			// 16 backpack slots
			PackSlot_1 = InvSlotHead + (player_inventory_pack_slots::Start) * 2,

			// 28 bank slots
			BankSlot_1 = PackSlot_1 + (player_inventory_pack_slots::End - player_inventory_pack_slots::Start) * 2,

			BankBagSlot_1 = BankSlot_1 + 56,
			VendorBuybackSlot_1 = BankBagSlot_1 + 14,
			BuybackPrice_1 = VendorBuybackSlot_1 + 12,
			BuybackTimestamp_1 = BuybackPrice_1 + 12,

			// Questlog
			QuestLogSlot_1 = BuybackTimestamp_1 + 12,

			// Field count
			PlayerFieldCount = QuestLogSlot_1 + (sizeof(QuestField) / sizeof(uint32)) * MaxQuestLogSize + 1,
		};

		enum ItemFields
		{
			/// 64 bit guid of owning player.
			ItemOwner = ObjectFieldCount,

			/// 64 bit guid of containing object.
			Contained = ItemOwner + 2,

			/// 64 bit guid of creating player or 0 in most cases if no creator. Creator name will be displayed in tooltip to players.
			Creator = Contained + 2,

			/// 32 bit stack count.
			StackCount = Creator + 2,

			/// 32 bit duration.
			Duration,

			/// 32 bit * 5
			SpellCharges,

			ItemFlags = SpellCharges + 5,

			Enchantment,

			PropertySeed,

			RandomPropertiesID,

			ItemTextID,

			Durability,

			MaxDurability,

			ItemFieldCount
		};

		enum BagFields
		{
			/// 32 bit value representing the number of actual slots the bag supports. Maximum is 36.
			NumSlots = ItemFieldCount,

			// 36x 64 bit slots (item guids).
			Slot_1,

			BagFieldCount = Slot_1 + (36 * 2),
		};

		enum WorldObjectFields
		{
			ObjectDisplayId = ObjectFieldCount,

			ObjectTypeId,

			CreatedBy,

			ObjectFlags = CreatedBy + 2,

			RotationW,

			RotationX,

			RotationY,

			RotationZ,

			AnimProgress,

			State,

			WorldObjectFieldCount = State + 1,
		};
	}
}
