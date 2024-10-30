
#include "aura_container.h"

#include "game_unit_s.h"
#include "spell_cast.h"
#include "base/clock.h"
#include "log/default_log_levels.h"

namespace mmo
{
	AuraContainer::AuraContainer(GameUnitS& owner, uint64 casterId, const proto::SpellEntry& spell, GameTime duration)
		: m_owner(owner)
		, m_casterId(casterId)
		, m_spell(spell)
		, m_duration(duration)
		, m_expirationCountdown(owner.GetTimers())
	{
		m_expirationCountdown.ended += [this] { SetApplied(false); };
	}

	AuraContainer::~AuraContainer()
	{
		if (m_applied)
		{
			SetApplied(false);
		}
	}

	void AuraContainer::AddAuraEffect(const proto::SpellEffect& effect, int32 basePoints)
	{
		// Add aura to the list of effective auras
		m_auras.emplace_back(
			static_cast<AuraType>(effect.aura()),
			basePoints,
			effect.amplitude(),
			&effect);
	}

	void AuraContainer::SetApplied(bool apply)
	{
		if ((m_applied && apply) ||
			(!m_applied && !apply))
		{
			return;
		}

		// Does this aura expire?
		if (apply)
		{
			if (m_duration > 0)
			{
				m_expiration = GetAsyncTimeMs() + m_duration;
				m_expirationCountdown.SetEnd(m_expiration);
			}
		}

		m_applied = apply;

		// TODO: Apply auras to the owning unit and notify others
		for(const auto& aura : m_auras)
		{
			switch(aura.type)
			{
			case AuraType::ModStat:
				HandleModStat(aura, apply);
				break;
			case AuraType::ModHealth:
				break;
			case AuraType::ModMana:
				break;
			case AuraType::ModDamageDone:
				break;
			case AuraType::ModDamageTaken:
				break;
			case AuraType::ModAttackSpeed:
				break;
			case AuraType::ModResistance:
				HandleModResistance(aura, apply);
				break;
			case AuraType::ModSpeedAlways:
			case AuraType::ModIncreaseSpeed:
				HandleRunSpeedModifier(aura, apply);
				break;
			case AuraType::ModDecreaseSpeed:
			case AuraType::ModSpeedNonStacking:
				HandleRunSpeedModifier(aura, apply);
				HandleSwimSpeedModifier(aura, apply);
				HandleFlySpeedModifier(aura, apply);
				break;
			}
		}
	}

	bool AuraContainer::IsExpired() const
	{
		return m_duration > 0 && m_expiration <= GetAsyncTimeMs();
	}

	uint32 AuraContainer::GetSpellId() const
	{
		return m_spell.id();
	}

	int32 AuraContainer::GetMaximumBasePoints(const AuraType type) const
	{
		int32 threshold = 0;

		for (auto it = m_auras.begin(); it != m_auras.end(); ++it)
		{
			if (it->type != type)
			{
				continue;
			}

			if (it->basePoints > threshold)
			{
				threshold = it->basePoints;
			}
		}

		return threshold;
	}

	int32 AuraContainer::GetMinimumBasePoints(AuraType type) const
	{
		int32 threshold = 0;

		for (auto it = m_auras.begin(); it != m_auras.end(); ++it)
		{
			if (it->type != type)
			{
				continue;
			}

			if (it->basePoints < threshold)
			{
				threshold = it->basePoints;
			}
		}

		return threshold;
	}

	float AuraContainer::GetTotalMultiplier(const AuraType type) const
	{
		float multiplier = 1.0f;

		for (auto it = m_auras.begin(); it != m_auras.end(); ++it)
		{
			if (it->type != type)
			{
				continue;
			}

			multiplier *= (100.0f + static_cast<float>(it->basePoints)) / 100.0f;
		}

		return multiplier;
	}

	bool AuraContainer::ShouldOverwriteAura(AuraContainer& other) const
	{
		// NOTE: If we return true here, the other aura will be removed and replaced by this aura container instead

		if (&other == this)
		{
			return true;
		}

		const bool sameSpellId = other.GetSpellId() == GetSpellId();
		const bool onlyOneStackTotal = (m_spell.attributes(0) & spell_attributes::OnlyOneStackTotal) != 0;
		const bool sameCaster = other.GetCasterId() == GetCasterId();
		const bool sameItem = false;

		// Right now, same caster and same spell id means we overwrite the old aura with this one
		// TODO: maybe add some settings here to explicitly allow stacking?
		if (sameCaster && sameSpellId)
		{
			return true;
		}

		// Same spell but different casters: If we allow stacking for different casters
		if (sameSpellId && !sameCaster && onlyOneStackTotal)
		{
			return true;
		}

		// Should not overwrite, but create a whole new aura
		return false;
	}

	void AuraContainer::HandleModStat(const Aura& aura, bool apply)
	{
		const int32 stat = aura.effect->miscvaluea();

		if (stat < 0 || stat > 4)
		{
			ELOG("AURA_TYPE_MOD_STAT: Invalid stat index " << stat);
			return;
		}

		// Apply stat modifier
		m_owner.UpdateModifierValue(GameUnitS::GetUnitModByStat(stat), 
			unit_mod_type::TotalValue,
			aura.basePoints,
			apply);
	}

	void AuraContainer::HandleModResistance(const Aura& aura, bool apply)
	{
		const int32 resistance = aura.effect->miscvaluea();

		if (resistance < 0 || resistance > 6)
		{
			ELOG("AURA_TYPE_MOD_RESISTANCE: Invalid resistance index " << resistance);
			return;
		}

		m_owner.UpdateModifierValue(static_cast<UnitMods>(static_cast<uint32>(unit_mods::Armor) + resistance),
			unit_mod_type::TotalValue,
			aura.basePoints,
			apply);
	}

	void AuraContainer::HandleRunSpeedModifier(const Aura& aura, bool apply)
	{
		m_owner.NotifySpeedChanged(movement_type::Run);
	}

	void AuraContainer::HandleSwimSpeedModifier(const Aura& aura, bool apply)
	{
		m_owner.NotifySpeedChanged(movement_type::Swim);
	}

	void AuraContainer::HandleFlySpeedModifier(const Aura& aura, bool apply)
	{
		m_owner.NotifySpeedChanged(movement_type::Flight);
	}
}
