
#include "unit_handle.h"
#include "game_unit_c.h"
#include "log/default_log_levels.h"

namespace mmo
{
	UnitHandle::UnitHandle(GameUnitC& unit)
		: WeakHandle(unit, unit.removed)
	{
	}

	UnitHandle::UnitHandle() = default;

	int32 UnitHandle::GetHealth() const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetHealth();
	}

	int32 UnitHandle::GetMaxHealth() const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetMaxHealth();
	}

	int32 UnitHandle::GetLevel() const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetLevel();
	}

	bool UnitHandle::CheckNonNull() const
	{
		if (Get())
		{
			return true;
		}

		ELOG("Expected non-null unit handle!");
		return false;
	}
}
