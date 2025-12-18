#pragma once
#include "base/non_copyable.h"

namespace mmo
{
	class GamePlayerC;

	/// @brief Base class for type-specific world object behavior on the client.
	class GameWorldObjectC_Type_Base : public NonCopyable
	{
	public:
		virtual ~GameWorldObjectC_Type_Base() = default;

		/// @brief Determines if this object type can ever be used.
		/// @return true if this type supports usage, false otherwise.
		virtual bool CanUse() const
		{
			return true;
		}

		/// @brief Determines if this object can be used right now by the given player.
		/// @param player The player attempting to use the object.
		/// @return true if the object can be used now, false otherwise.
		virtual bool CanUseNow(const GamePlayerC& player) const
		{
			return CanUse();
		}
	};

	/// @brief Chest-type world objects (lootable containers).
	class GameWorldObjectC_Type_Chest : public GameWorldObjectC_Type_Base
	{
	public:
		bool CanUse() const override
		{
			return true;
		}
	};

	/// @brief Door-type world objects (not typically usable).
	class GameWorldObjectC_Type_Door : public GameWorldObjectC_Type_Base
	{
	public:
		bool CanUse() const override
		{
			return false;
		}
	};
}
