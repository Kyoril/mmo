
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

	const std::string& UnitHandle::GetName() const
	{
		static const std::string s_unnamed = "Unnamed";

		if (!CheckNonNull()) return s_unnamed;
		return Get()->GetName();
	}

	int32 UnitHandle::GetPowerType() const
	{
		if (!CheckNonNull()) return -1;
		return Get()->GetPowerType();
	}

	float UnitHandle::GetMinDamage() const
	{
		if (!CheckNonNull()) return 0.0f;
		return Get()->GetMinDamage();
	}

	float UnitHandle::GetMaxDamage() const
	{
		if (!CheckNonNull()) return 0.0f;
		return Get()->GetMaxDamage();
	}

	uint32 UnitHandle::GetAttackTime() const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetAttackTime();
	}

	float UnitHandle::GetAttackPower() const
	{
		if (!CheckNonNull()) return 0.0f;
		return Get()->GetAttackPower();
	}

	int32 UnitHandle::GetStat(int32 statId) const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetStat(statId);
	}

	int32 UnitHandle::GetPosStat(int32 statId) const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetPosStat(statId);
	}

	int32 UnitHandle::GetNegStat(int32 statId) const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetNegStat(statId);
	}

	int32 UnitHandle::GetArmor() const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetArmor();
	}

	float UnitHandle::GetArmorReductionFactor() const
	{
		if (!CheckNonNull()) return 0.0f;
		return Get()->GetArmorReductionFactor();
	}

	int32 UnitHandle::GetAvailableAttributePoints() const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetAvailableAttributePoints();
	}

	bool UnitHandle::IsAlive() const
	{
		if (!CheckNonNull()) return false;
		return Get()->IsAlive();
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
