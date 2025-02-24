
#include "aura_container.h"

#include "game_unit_s.h"
#include "spell_cast.h"
#include "base/clock.h"
#include "base/utilities.h"
#include "binary_io/vector_sink.h"
#include "log/default_log_levels.h"
#include "proto_data/project.h"

namespace mmo
{
	AuraEffect::AuraEffect(AuraContainer& container, const proto::SpellEffect& effect, TimerQueue& timers, int32 basePoints)
		: m_container(container)
		, m_basePoints(basePoints)
		, m_tickInterval(effect.amplitude())
		, m_effect(effect)
		, m_tickCountdown(timers)
	{
		m_casterSpellPower = m_container.GetCaster()->GetCalculatedModifierValue(unit_mods::SpellDamage);
		m_casterSpellHeal = m_container.GetCaster()->GetCalculatedModifierValue(unit_mods::Healing);

		m_onTick = m_tickCountdown.ended.connect(this, &AuraEffect::OnTick);

		if (m_effect.amplitude() > 0)
		{
			m_totalTicks = m_container.GetDuration() / m_effect.amplitude();
		}
	}

	void AuraEffect::HandleEffect(bool apply)
	{
		if (!apply && m_isPeriodic && m_container.IsExpired())
		{
			OnTick();
		}

		switch(GetType())
		{
		case AuraType::ModStat:
			HandleModStat(apply);
			break;
		case AuraType::ModHealth:
			break;
		case AuraType::ModMana:
			break;
		case AuraType::ProcTriggerSpell:
			HandleProcTriggerSpell(apply);
			break;
		case AuraType::ModDamageDone:
			HandleModDamageDone(apply);
			break;
		case AuraType::ModHealingDone:
			HandleModHealingDone(apply);
			break;
		case AuraType::ModDamageTaken:
			break;
		case AuraType::ModAttackSpeed:
			HandleModAttackSpeed(apply);
			break;
		case AuraType::ModAttackPower:
			HandleModAttackPower(apply);
			break;
		case AuraType::ModResistance:
			HandleModResistance(apply);
			break;
		case AuraType::ModSpeedAlways:
		case AuraType::ModIncreaseSpeed:
			HandleRunSpeedModifier(apply);
			break;
		case AuraType::ModDecreaseSpeed:
		case AuraType::ModSpeedNonStacking:
			HandleRunSpeedModifier(apply);
			HandleSwimSpeedModifier(apply);
			HandleFlySpeedModifier(apply);
			break;

		case AuraType::AddFlatModifier:
		case AuraType::AddPctModifier:
			HandleAddModifier(apply);
			break;

		case AuraType::PeriodicTriggerSpell:
		case AuraType::PeriodicHeal:
		case AuraType::PeriodicEnergize:
		case AuraType::PeriodicDamage:
			if (apply)
			{
				HandlePeriodicBase();
			}
			break;
		}
	}

	void AuraEffect::HandlePeriodicBase()
	{
		// Toggle periodic flag
		m_isPeriodic = true;

		// First tick at apply
		if (m_container.GetSpell().attributes(0) & spell_attributes::StartPeriodicAtApply)
		{
			OnTick();
		}
		else
		{
			StartPeriodicTimer();
		}
	}

	void AuraEffect::HandleModStat(const bool apply) const
	{
		const int32 stat = GetEffect().miscvaluea();

		if (stat < 0 || stat > 4)
		{
			ELOG("AURA_TYPE_MOD_STAT: Invalid stat index " << stat);
			return;
		}

		// Apply stat modifier
		m_container.GetOwner().UpdateModifierValue(GameUnitS::GetUnitModByStat(stat),
			unit_mod_type::TotalValue,
			GetBasePoints(),
			apply);
	}

	void AuraEffect::HandleProcTriggerSpell(const bool apply)
	{
		if (!apply)
		{
			m_procEffects.disconnect();
			return;
		}

		const proto::SpellEntry& spell = m_container.GetSpell();
		const proto::SpellEntry* procSpell = m_container.GetOwner().GetProject().spells.getById(m_effect.triggerspell());
		if (!procSpell)
		{
			ELOG("Unable to find proc trigger spell " << m_effect.triggerspell() << "!");
			return;
		}

		const uint32 procChance = spell.procchance();

		if (spell.procflags() & spell_proc_flags::DoneMeleeAutoAttack)
		{
			m_procEffects += m_container.GetOwner().meleeAttackDone.connect([this, procSpell, procChance](GameUnitS& victim)
				{
					std::uniform_real_distribution chanceDistribution(0.0f, 100.0f);
					if (chanceDistribution(randomGenerator) < procChance)
					{
						SpellTargetMap targetMap;
						targetMap.SetUnitTarget(victim.GetGuid());
						targetMap.SetTargetMap(spell_cast_target_flags::Unit);
						m_container.GetOwner().CastSpell(targetMap, *procSpell, 0, true, 0);
					}
				});
		}
	}

	void AuraEffect::HandleModDamageDone(const bool apply) const
	{
		m_container.GetOwner().UpdateModifierValue(unit_mods::SpellDamage,
			unit_mod_type::TotalValue,
			GetBasePoints(),
			apply);
	}

	void AuraEffect::HandleModHealingDone(const bool apply) const
	{
		m_container.GetOwner().UpdateModifierValue(unit_mods::Healing,
			unit_mod_type::TotalValue,
			GetBasePoints(),
			apply);
	}

	void AuraEffect::HandleModAttackPower(bool apply) const
	{
		m_container.GetOwner().UpdateModifierValue(unit_mods::AttackPower,
			unit_mod_type::TotalValue,
			GetBasePoints(),
			apply);
	}

	void AuraEffect::HandleModAttackSpeed(bool apply) const
	{
		m_container.GetOwner().UpdateModifierValue(unit_mods::AttackSpeed,
			unit_mod_type::TotalValue,
			GetBasePoints(),
			apply);
	}

	void AuraEffect::HandleModResistance(const bool apply) const
	{
		const int32 resistance = GetEffect().miscvaluea();

		if (resistance < 0 || resistance > 6)
		{
			ELOG("AURA_TYPE_MOD_RESISTANCE: Invalid resistance index " << resistance);
			return;
		}

		m_container.GetOwner().UpdateModifierValue(static_cast<UnitMods>(static_cast<uint32>(unit_mods::Armor) + resistance),
			unit_mod_type::TotalValue,
			GetBasePoints(),
			apply);
	}

	void AuraEffect::HandleRunSpeedModifier(bool apply) const
	{
		m_container.GetOwner().NotifySpeedChanged(movement_type::Run);
	}

	void AuraEffect::HandleSwimSpeedModifier(bool apply) const
	{
		m_container.GetOwner().NotifySpeedChanged(movement_type::Swim);
	}

	void AuraEffect::HandleFlySpeedModifier(bool apply) const
	{
		m_container.GetOwner().NotifySpeedChanged(movement_type::Flight);
	}

	void AuraEffect::HandleAddModifier(const bool apply) const
	{
		if (m_effect.miscvaluea() >= spell_mod_op::Count_)
		{
			ELOG("Invalid spell mod operation!");
			return;
		}

		SpellModifier mod;
		mod.op = static_cast<spell_mod_op::Type>(m_effect.miscvaluea());
		mod.value = m_basePoints;
		mod.type = GetType() == aura_type::AddFlatModifier ? spell_mod_type::Flat : spell_mod_type::Pct;
		mod.spellId = m_container.GetSpellId();
		mod.effectId = 0;	// TODO
		mod.charges = 0;	// TODO
		mod.mask = m_effect.affectmask();
		if (mod.mask == 0) mod.mask = m_effect.itemtype();
		if (mod.mask == 0)
		{
			WLOG("Invalid mod mask for spell " << m_container.GetSpellId());
		}

		m_container.GetOwner().ModifySpellMod(mod, apply);
	}

	void AuraEffect::HandlePeriodicDamage() const
	{
		const uint32 school = m_container.GetSpell().spellschool();
		int32 damage = m_basePoints;

		// Apply spell power bonus damage but divide it by number of ticks
		if (m_casterSpellPower > 0.0f && m_effect.powerbonusfactor() > 0.0f && m_totalTicks > 0)
		{
			damage += static_cast<int32>(m_casterSpellPower * m_effect.powerbonusfactor() / static_cast<float>(m_totalTicks));
		}

		// Apply damage bonus from casters spell power

		// Send event to all subscribers in sight
		std::vector<char> buffer;
		io::VectorSink sink(buffer);
		game::OutgoingPacket packet(sink);

		packet.Start(game::realm_client_packet::PeriodicAuraLog);
		packet
			<< io::write_packed_guid(m_container.GetOwner().GetGuid())
			<< io::write_packed_guid(m_container.GetCasterId())
			<< io::write<uint32>(m_container.GetSpell().id())
			<< io::write<uint32>(GetType())
			<< io::write<uint32>(damage)
			<< io::write<uint32>(school)
			<< io::write<uint32>(0) // Absorbed
			<< io::write<uint32>(0); // Resisted
		packet.Finish();

		m_container.GetOwner().ForEachSubscriberInSight([&packet, &buffer](TileSubscriber& subscriber)
		{
			subscriber.SendPacket(packet, buffer, true);
		});

		// Update health
		m_container.GetOwner().Damage(damage, school, m_container.GetCaster());
	}

	void AuraEffect::HandlePeriodicHeal() const
	{
		int32 heal = m_basePoints;

		// Apply spell power bonus damage but divide it by number of ticks
		if (m_casterSpellHeal > 0.0f && m_effect.powerbonusfactor() > 0.0f && m_totalTicks > 0)
		{
			heal += static_cast<int32>(m_casterSpellHeal * m_effect.powerbonusfactor() / static_cast<float>(m_totalTicks));
		}

		// Send event to all subscribers in sight
		std::vector<char> buffer;
		io::VectorSink sink(buffer);
		game::OutgoingPacket packet(sink);

		packet.Start(game::realm_client_packet::PeriodicAuraLog);
		packet
			<< io::write_packed_guid(m_container.GetOwner().GetGuid())
			<< io::write_packed_guid(m_container.GetCasterId())
			<< io::write<uint32>(m_container.GetSpell().id())
			<< io::write<uint32>(GetType())
			<< io::write<uint32>(heal);
		packet.Finish();

		m_container.GetOwner().ForEachSubscriberInSight([&packet, &buffer](TileSubscriber& subscriber)
			{
				subscriber.SendPacket(packet, buffer, true);
			});

		// Update health
		m_container.GetOwner().Heal(heal, m_container.GetCaster());
	}

	void AuraEffect::HandlePeriodicEnergize()
	{
		// TODO
	}

	void AuraEffect::HandlePeriodicTriggerSpell() const
	{
		SpellTargetMap targetMap;
		if (m_effect.targeta() == spell_effect_targets::Caster)
		{
			targetMap.SetTargetMap(spell_cast_target_flags::Self);
		}
		else
		{
			targetMap.SetUnitTarget(m_container.GetCasterId());
		}

		// Cast trigger spell if we know it
		if (const proto::SpellEntry* triggerSpell = m_container.GetOwner().GetProject().spells.getById(m_effect.triggerspell()))
		{
			m_container.GetOwner().CastSpell(targetMap, *triggerSpell, 0, true, 0);
		}
		else
		{
			WLOG("Failed to cast trigger spell: unknown spell id " << m_effect.triggerspell());
		}
	}

	void AuraEffect::StartPeriodicTimer() const
	{
		m_tickCountdown.SetEnd(GetAsyncTimeMs() + m_tickInterval);
	}

	void AuraEffect::OnTick()
	{
		// No more ticks
		if (m_totalTicks > 0 &&
			m_tickCount >= m_totalTicks) 
		{
			return;
		}

		// Prevent this aura from being deleted. This can happen in the
		// dealDamage-Methods, when a creature dies from that damage.
		auto strongThis = shared_from_this();

		// Increase tick counter
		if (m_totalTicks > 0)
		{
			m_tickCount++;
		}

		switch(GetType())
		{
		case aura_type::PeriodicDamage:
			HandlePeriodicDamage();
			break;
		case aura_type::PeriodicHeal:
			HandlePeriodicHeal();
			break;
		case aura_type::PeriodicEnergize:
			HandlePeriodicEnergize();
			break;
		case aura_type::PeriodicTriggerSpell:
			HandlePeriodicTriggerSpell();
			break;
		}

		// Start another tick
		if (m_tickCount < m_totalTicks)
		{
			StartPeriodicTimer();
		}
	}

	AuraContainer::AuraContainer(GameUnitS& owner, uint64 casterId, const proto::SpellEntry& spell, GameTime duration, uint64 itemGuid)
		: m_owner(owner)
		, m_casterId(casterId)
		, m_spell(spell)
		, m_duration(duration)
		, m_expirationCountdown(owner.GetTimers())
		, m_itemGuid(itemGuid)
	{
	}

	AuraContainer::~AuraContainer()
	{
		m_expirationCountdown.Cancel();

		if (m_applied)
		{
			SetApplied(false);
		}
	}

	void AuraContainer::AddAuraEffect(const proto::SpellEffect& effect, int32 basePoints)
	{
		// Add aura to the list of effective auras
		m_auras.emplace_back(std::make_shared<AuraEffect>(
			*this,
			effect,
			m_owner.GetTimers(),
			basePoints));
	}

	void AuraContainer::SetApplied(bool apply, bool notify)
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
				if (!m_expiredConnection)
				{
					std::weak_ptr<AuraContainer> weakThis = weak_from_this();
					m_expiredConnection = m_expirationCountdown.ended += [weakThis]()
						{
							if (const auto strong = weakThis.lock())
							{
								strong->SetApplied(false);
								strong->m_owner.RemoveAura(strong);
							}
						};
				}

				m_expiration = GetAsyncTimeMs() + m_duration;
				m_expirationCountdown.SetEnd(m_expiration);
			}
		}

		m_applied = apply;

		if (notify && m_owner.GetWorldInstance())
		{
			// TODO: Flag this aura as updated so we only sync changed auras to units which already know about this unit's auras instead
			// of having to sync ALL unit auras over and over again

			// Auras changed, flag object for next update loop
			m_owner.GetWorldInstance()->AddObjectUpdate(m_owner);
		}

		// TODO: Apply auras to the owning unit and notify others
		for(const auto& aura : m_auras)
		{
			aura->HandleEffect(m_applied);
		}
	}

	bool AuraContainer::IsExpired() const
	{
		return m_duration > 0 && m_expiration <= GetAsyncTimeMs();
	}

	void AuraContainer::WriteAuraUpdate(io::Writer& writer) const
	{
		const uint32 now = GetAsyncTimeMs();
		writer << io::write<uint32>(m_spell.id());
		writer << io::write<uint32>(m_expiration > now ? m_expiration - now : 0);
		writer << io::write_packed_guid(m_casterId);

		writer << io::write<uint8>(m_auras.size());
		for (uint32 i = 0; i < m_auras.size(); ++i)
		{
			const auto& aura = m_auras[i];
			writer << io::write<int32>(aura->GetBasePoints());
		}
	}

	bool AuraContainer::HasEffect(AuraType type) const
	{
		for (const auto& aura : m_auras)
		{
			if (aura->GetType() == type)
			{
				return true;
			}
		}

		return false;
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
			if (it->get()->GetType() != type)
			{
				continue;
			}

			if (it->get()->GetBasePoints() > threshold)
			{
				threshold = it->get()->GetBasePoints();
			}
		}

		return threshold;
	}

	int32 AuraContainer::GetMinimumBasePoints(AuraType type) const
	{
		int32 threshold = 0;

		for (auto it = m_auras.begin(); it != m_auras.end(); ++it)
		{
			if (it->get()->GetType() != type)
			{
				continue;
			}

			if (it->get()->GetBasePoints() < threshold)
			{
				threshold = it->get()->GetBasePoints();
			}
		}

		return threshold;
	}

	float AuraContainer::GetTotalMultiplier(const AuraType type) const
	{
		float multiplier = 1.0f;

		for (auto it = m_auras.begin(); it != m_auras.end(); ++it)
		{
			if (it->get()->GetType() != type)
			{
				continue;
			}

			multiplier *= (100.0f + static_cast<float>(it->get()->GetBasePoints())) / 100.0f;
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
		const bool sameItem = other.GetItemGuid() == GetItemGuid();

		// Right now, same caster and same spell id means we overwrite the old aura with this one
		// TODO: maybe add some settings here to explicitly allow stacking?
		if (sameCaster && sameSpellId && sameItem)
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

	GameUnitS* AuraContainer::GetCaster() const
	{
		std::shared_ptr<GameUnitS> strongCaster = m_caster.lock();
		if (!strongCaster)
		{
			if (!m_owner.GetWorldInstance())
			{
				return nullptr;
			}

			if (GameUnitS* caster = m_owner.GetWorldInstance()->FindByGuid<GameUnitS>(m_casterId))
			{
				m_caster = std::static_pointer_cast<GameUnitS>(caster->shared_from_this());
				return caster;
			}

			return nullptr;
		}
		
		return strongCaster.get();
	}
}
