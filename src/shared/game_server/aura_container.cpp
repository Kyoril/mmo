
#include "aura_container.h"

#include "game_unit_s.h"
#include "spell_cast.h"
#include "base/clock.h"
#include "log/default_log_levels.h"

namespace mmo
{
	AuraContainer::AuraContainer(GameUnitS& owner, uint64 casterId, uint32 spellId, GameTime duration)
		: m_owner(owner)
		, m_casterId(casterId)
		, m_spellId(spellId)
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
				break;
				
			}
		}

		m_applied = apply;
	}

	bool AuraContainer::IsExpired() const
	{
		return m_duration > 0 && m_expiration <= GetAsyncTimeMs();
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
}
