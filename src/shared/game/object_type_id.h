#pragma once

namespace mmo
{
	namespace object_update_flags
	{
		enum Type
		{
			None = 0,

			HasMovementInfo = 1 << 0
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
			ObjectFieldCount = Owner + 1,
		};

		enum UnitFields
		{
			/// @brief 32 bit object field
			Level = ObjectFieldCount,

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

			/// @brief 64 bit object guid
			TargetUnit,

			UnitFieldCount = TargetUnit + 2,
		};

		enum PlayerFields
		{
			Xp = UnitFieldCount,
			NextLevelXp,

			PlayerFieldCount
		};
	}
}