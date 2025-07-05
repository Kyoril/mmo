
#include "unit_handle.h"

#include "game_player_c.h"
#include "game_unit_c.h"
#include "base/clock.h"
#include "log/default_log_levels.h"
#include "shared/client_data/proto_client/classes.pb.h"

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

	uint64 UnitHandle::GetGuid() const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetGuid();
	}

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

	const char* UnitHandle::GetClass() const
	{
		if (!CheckNonNull()) return nullptr;

		if (Get()->IsPlayer())
		{
			const auto* classEntry = Get()->AsPlayer().GetClass();
			if (!classEntry)
			{
				return nullptr;
			}

			return classEntry->name().c_str();
		}

		return nullptr;
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

	int32 UnitHandle::GetTalentPoints() const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetTalentPoints();
	}

	bool UnitHandle::IsAlive() const
	{
		if (!CheckNonNull()) return false;
		return Get()->IsAlive();
	}

	bool UnitHandle::IsFriendly() const
	{
		if (!CheckNonNull()) return false;
		return Get()->IsFriendly();
	}

	bool UnitHandle::IsHostile() const
	{
		if (!CheckNonNull()) return false;
		return Get()->IsHostile();
	}

	const char* UnitHandle::GetType() const
	{
		if (!CheckNonNull()) return nullptr;

		static const char* s_unitTypeStrings[] = {
			"CREATURE",
			"PLAYER"
		};

		if (Get()->IsPlayer())
		{
			return s_unitTypeStrings[1];
		}

		return s_unitTypeStrings[0];
	}

	int32 UnitHandle::GetHealthFromStat(int32 statId) const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetHealthFromStat(statId);
	}

	int32 UnitHandle::GetManaFromStat(int32 statId) const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetManaFromStat(statId);
	}

	int32 UnitHandle::GetAttackPowerFromStat(int32 statId) const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetAttackPowerFromStat(statId);
	}

	uint8 UnitHandle::GetAttributeCost(uint32 attribute) const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetAttributeCost(attribute);
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
