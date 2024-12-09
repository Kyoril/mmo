
#include "unit_handle.h"
#include "game_unit_c.h"
#include "base/clock.h"
#include "log/default_log_levels.h"

namespace mmo
{
	AuraHandle::AuraHandle(GameAuraC& aura)
		: WeakHandle(aura, aura.removed)
	{
	}

	AuraHandle::AuraHandle()
	{
	}

	bool AuraHandle::CanExpire() const
	{
		if (!CheckNonNull()) return false;
		return Get()->CanExpire();
	}

	bool AuraHandle::IsExpired() const
	{
		if (!CheckNonNull()) return false;
		return Get()->IsExpired();
	}

	uint32 AuraHandle::GetDuration() const
	{
		if (!CheckNonNull()) return 0;

		if (Get()->CanExpire())
		{
			return Get()->GetExpiration() - GetAsyncTimeMs();
		}

		return 0;
	}

	const proto_client::SpellEntry* AuraHandle::GetSpell() const
	{
		if (!CheckNonNull()) return nullptr;
		return Get()->GetSpell();
	}

	bool AuraHandle::CheckNonNull() const
	{
		if (Get())
		{
			return true;
		}

		ELOG("Expected non-null aura handle!");
		return false;
	}

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

	int32 UnitHandle::GetPower(const int32 powerType) const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetPower(powerType);
	}

	int32 UnitHandle::GetMaxPower(const int32 powerType) const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetMaxPower(powerType);
	}

	uint32 UnitHandle::GetAuraCount() const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetAuraCount();
	}

	std::shared_ptr<AuraHandle> UnitHandle::GetAura(const uint32 index) const
	{
		if (!CheckNonNull()) return nullptr;

		if (GameAuraC* aura = Get()->GetAura(index))
		{
			return std::make_shared<AuraHandle>(*aura);
		}

		return nullptr;
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
