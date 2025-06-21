// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "aura_effect.h"

#include <algorithm>

#include "aura_container.h"
#include "spell_cast.h"
#include "vector_sink.h"
#include "base/utilities.h"
#include "log/default_log_levels.h"
#include "objects/game_player_s.h"
#include "objects/game_unit_s.h"
#include "world/universe.h"

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
		m_casterSpellHeal = m_container.GetCaster()->GetCalculatedModifierValue(unit_mods::HealingDone);

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

		switch (GetType())
		{
		case AuraType::ModStat:
			HandleModStat(apply);
			break;
		case AuraType::ModStatPct:
			HandleModStat(apply);
			break;
		case AuraType::ModHealth:
			break;
		case AuraType::ModMana:
			break;
		case AuraType::ProcTriggerSpell:
			// Nothing to be done here
			break;
		case AuraType::ModDamageDone:
		case AuraType::ModDamageDonePct:
			HandleModDamageDone(apply);
			break;
		case AuraType::ModHealingDone:
			HandleModHealingDone(apply);
			break;
		case AuraType::ModHealingTaken:
			HandleModHealingTaken(apply);
			break;
		case AuraType::ModDamageTaken:
			HandleModDamageTaken(apply);
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
		case AuraType::ModResistancePct:
			HandleModResistancePct(apply);
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

		case AuraType::ModRoot:
			HandleModRoot(apply);
			break;
		case AuraType::ModSleep:
			HandleModSleep(apply);
			break;
		case AuraType::ModStun:
			HandleModStun(apply);
			break;
		case AuraType::ModFear:
			HandleModFear(apply);
			break;

		case AuraType::ModVisibility:
			HandleModVisibility(apply);
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

	void AuraEffect::HandleModStatPct(bool apply) const
	{
		const int32 stat = GetEffect().miscvaluea();

		if (stat < 0 || stat > 4)
		{
			ELOG("AURA_TYPE_MOD_STAT_PCT: Invalid stat index " << stat);
			return;
		}

		// Apply stat modifier
		m_container.GetOwner().UpdateModifierValue(GameUnitS::GetUnitModByStat(stat),
			unit_mod_type::TotalPct,
			GetBasePoints(),
			apply);
	}

	bool AuraEffect::ExecuteSpellProc(const proto::SpellEntry* procSpell, const GameUnitS& unit) const
	{
		// Check if castable on dead unit
		if (!(procSpell->attributes(0) & spell_attributes::CanTargetDead) && !unit.IsAlive())
		{
			return false;
		}

		SpellTargetMap targetMap;
		targetMap.SetUnitTarget(unit.GetGuid());
		targetMap.SetTargetMap(spell_cast_target_flags::Unit);

		if (GameUnitS* caster = m_container.GetCaster())
		{
			auto strongCaster = std::static_pointer_cast<GameUnitS>(caster->shared_from_this());
			caster->GetWorldInstance()->GetUniverse().Post([strongCaster, targetMap, procSpell]()
				{
					const auto result = strongCaster->CastSpell(targetMap, *procSpell, 0, true, 0);
				});
		}

		return true;
	}

	void AuraEffect::HandleProcEffect(GameUnitS* instigator)
	{
		switch (m_effect.aura())
		{
		case aura_type::ProcTriggerSpell:
			{
				const proto::SpellEntry& spell = m_container.GetSpell();
				const proto::SpellEntry* procSpell = m_container.GetOwner().GetProject().spells.getById(m_effect.triggerspell());
				if (!procSpell)
				{
					ELOG("Unable to find proc trigger spell " << m_effect.triggerspell() << "!");
					return;
				}

				// Apply to all targets
				ForEachProcTarget(m_effect,
					instigator,
					[&](const GameUnitS& unit)
					{
						return ExecuteSpellProc(procSpell, unit);
					});
			}
			break;
		}

	}

	void AuraEffect::HandleModDamageDone(const bool apply) const
	{
		m_container.GetOwner().UpdateModifierValue(unit_mods::SpellDamage,
			m_effect.aura() == aura_type::ModDamageDone ? unit_mod_type::TotalValue : unit_mod_type::TotalPct,
			GetBasePoints(),
			apply);
	}

	void AuraEffect::HandleModDamageTaken(bool apply) const
	{

	}

	void AuraEffect::HandleModHealingDone(const bool apply) const
	{
		m_container.GetOwner().UpdateModifierValue(unit_mods::HealingDone,
			unit_mod_type::TotalValue,
			GetBasePoints(),
			apply);
	}

	void AuraEffect::HandleModHealingTaken(bool apply) const
	{
		m_container.GetOwner().UpdateModifierValue(unit_mods::HealingTaken,
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

	void AuraEffect::HandleModResistancePct(bool apply) const
	{
		const int32 resistance = GetEffect().miscvaluea();

		if (resistance < 0 || resistance > 6)
		{
			ELOG("AURA_TYPE_MOD_RESISTANCE_PCT: Invalid resistance index " << resistance);
			return;
		}

		m_container.GetOwner().UpdateModifierValue(static_cast<UnitMods>(static_cast<uint32>(unit_mods::Armor) + resistance),
			unit_mod_type::TotalPct,
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

	void AuraEffect::HandleModRoot(bool apply) const
	{
		std::weak_ptr weakOwner = std::static_pointer_cast<GameUnitS>(m_container.GetOwner().shared_from_this());
		m_container.GetOwner().GetWorldInstance()->GetUniverse().Post([weakOwner]()
			{
				if (const auto owner = weakOwner.lock())
				{
					owner->NotifyRootChanged();
				}
			});
	}

	void AuraEffect::HandleModStun(bool apply) const
	{
		std::weak_ptr weakOwner = std::static_pointer_cast<GameUnitS>(m_container.GetOwner().shared_from_this());
		m_container.GetOwner().GetWorldInstance()->GetUniverse().Post([weakOwner]()
			{
				if (const auto owner = weakOwner.lock())
				{
					owner->NotifyStunChanged();
				}
			});
	}

	void AuraEffect::HandleModFear(bool apply) const
	{
		std::weak_ptr weakOwner = std::static_pointer_cast<GameUnitS>(m_container.GetOwner().shared_from_this());
		m_container.GetOwner().GetWorldInstance()->GetUniverse().Post([weakOwner]()
			{
				if (const auto owner = weakOwner.lock())
				{
					owner->NotifyFearChanged();
				}
			});
	}

	void AuraEffect::HandleModSleep(bool apply) const
	{
		std::weak_ptr weakOwner = std::static_pointer_cast<GameUnitS>(m_container.GetOwner().shared_from_this());
		m_container.GetOwner().GetWorldInstance()->GetUniverse().Post([weakOwner]()
			{
				if (const auto owner = weakOwner.lock())
				{
					owner->NotifySleepChanged();
				}
			});
	}

	void AuraEffect::HandleModVisibility(bool apply) const
	{
		std::weak_ptr weakOwner = std::static_pointer_cast<GameUnitS>(m_container.GetOwner().shared_from_this());
		m_container.GetOwner().GetWorldInstance()->GetUniverse().Post([weakOwner]()
			{
				if (const auto owner = weakOwner.lock())
				{
					owner->NotifyVisibilityChanged();
				}
			});
	}

	void AuraEffect::HandlePeriodicDamage() const
	{
		auto strongContainer = m_container.shared_from_this();

		const uint32 school = strongContainer->GetSpell().spellschool();
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
			<< io::write_packed_guid(strongContainer->GetOwner().GetGuid())
			<< io::write_packed_guid(strongContainer->GetCasterId())
			<< io::write<uint32>(strongContainer->GetSpell().id())
			<< io::write<uint32>(GetType())
			<< io::write<uint32>(damage)
			<< io::write<uint32>(school)
			<< io::write<uint32>(0) // Absorbed
			<< io::write<uint32>(0); // Resisted
		packet.Finish();

		strongContainer->GetOwner().ForEachSubscriberInSight([&packet, &buffer](TileSubscriber& subscriber)
			{
				subscriber.SendPacket(packet, buffer, true);
			});

		// Update health
		strongContainer->GetOwner().Damage(damage, school, strongContainer->GetCaster(), damage_type::Periodic);
		
		// Trigger proc events for periodic damage
		if (strongContainer->GetCaster())
		{
			strongContainer->GetCaster()->TriggerProcEvent(spell_proc_flags::DonePeriodicDamage, &strongContainer->GetOwner(), damage, proc_ex_flags::NormalHit, school, false, strongContainer->GetSpell().familyflags());
		}

		strongContainer->GetOwner().TriggerProcEvent(spell_proc_flags::TakenPeriodicDamage, strongContainer->GetCaster(), damage, proc_ex_flags::NormalHit, school, false, strongContainer->GetSpell().familyflags());
	}

	void AuraEffect::HandlePeriodicHeal() const
	{
		int32 heal = m_basePoints;

		// Apply spell power bonus damage but divide it by number of ticks
		if (m_casterSpellHeal > 0.0f && m_effect.powerbonusfactor() > 0.0f && m_totalTicks > 0)
		{
			heal += static_cast<int32>(m_casterSpellHeal * m_effect.powerbonusfactor() / static_cast<float>(m_totalTicks));
		}

		const float healingTakenBonus = m_container.GetOwner().GetCalculatedModifierValue(unit_mods::HealingTaken);
		if (m_totalTicks > 0)
		{
			heal += static_cast<int32>(healingTakenBonus / static_cast<float>(m_totalTicks));
		}

		heal = std::max(heal, 0);

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
		
		// Trigger proc events for periodic healing
		if (m_container.GetCaster())
		{
			m_container.GetCaster()->TriggerProcEvent(spell_proc_flags::DonePeriodicHeal, &m_container.GetOwner(), heal, proc_ex_flags::NormalHit, m_container.GetSpell().spellschool(), false, m_container.GetSpell().familyflags());
		}
		m_container.GetOwner().TriggerProcEvent(spell_proc_flags::TakenPeriodicHeal, m_container.GetCaster(), heal, proc_ex_flags::NormalHit, m_container.GetSpell().spellschool(), false, m_container.GetSpell().familyflags());
	}

	void AuraEffect::HandlePeriodicEnergize()
	{
		int32 powerType = m_effect.miscvaluea();
		if (powerType < 0 || powerType > 2)
		{
			return;
		}

		uint32 power = GetBasePoints();

		uint32 curPower = m_container.GetOwner().Get<uint32>(object_fields::Mana + powerType);
		const uint32 maxPower = m_container.GetOwner().Get<uint32>(object_fields::MaxMana + powerType);
		if (curPower + power > maxPower)
		{
			power = maxPower - curPower;
			curPower = maxPower;
		}
		else
		{
			curPower += power;
		}

		m_container.GetOwner().Set<uint32>(object_fields::Mana + powerType, curPower);

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
			<< io::write<uint32>(power);
		packet.Finish();

		m_container.GetOwner().ForEachSubscriberInSight([&packet, &buffer](TileSubscriber& subscriber)
			{
				subscriber.SendPacket(packet, buffer, true);
			});

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

	void AuraEffect::HandleProcForUnitTarget(GameUnitS& unit)
	{

	}

	void AuraEffect::ForEachProcTarget(const proto::SpellEffect& effect, GameUnitS* instigator, const std::function<bool(GameUnitS&)>& proc)
	{
		uint32 targets = 0;

		switch (effect.targetb())
		{
		case spell_effect_targets::Caster:
			if (m_container.GetCaster())
			{
				proc(*m_container.GetCaster());
			}
			break;

		case spell_effect_targets::Instigator:
			if (instigator)
			{
				proc(*instigator);
			}
			break;

		case spell_effect_targets::TargetAny:
		case spell_effect_targets::TargetEnemy:
		case spell_effect_targets::TargetAlly:
			proc(m_container.GetOwner());
			break;

		case spell_effect_targets::TargetArea:
		case spell_effect_targets::TargetAreaEnemy:
		{
			const float range = effect.radius();
			const Vector3& position = m_container.GetOwner().GetPosition();
			m_container.GetOwner().GetWorldInstance()->GetUnitFinder().FindUnits(Circle(position.x, position.z, range), [&effect, &proc, &targets, this](GameUnitS& unit) -> bool
				{
					// Limit hit targets
					if (m_container.GetSpell().maxtargets() != 0 && targets >= m_container.GetSpell().maxtargets())
					{
						return true;
					}

					if (effect.targetb() == spell_effect_targets::TargetAreaEnemy)
					{
						if (!m_container.GetCaster())
						{
							return true;
						}

						if (m_container.GetCaster()->UnitIsFriendly(unit))
						{
							return true;
						}
					}

					if (proc(unit))
					{
						targets++;
					}
					return true;
				});
		}
		break;
		case spell_effect_targets::NearbyAlly:
		case spell_effect_targets::NearbyEnemy:
		case spell_effect_targets::CasterAreaParty:
		case spell_effect_targets::NearbyParty:
		{
			const float range = effect.radius();
			const Vector3& position = m_container.GetOwner().GetPosition();
			m_container.GetOwner().GetWorldInstance()->GetUnitFinder().FindUnits(Circle(position.x, position.z, range), [&proc, &targets, this](GameUnitS& unit) -> bool
				{
					// Limit hit targets
					if (m_container.GetSpell().maxtargets() != 0 && targets >= m_container.GetSpell().maxtargets())
					{
						return true;
					}

					if (unit.GetGuid() == m_container.GetCasterId())
					{
						if (proc(unit))
						{
							targets++;
						}
						return true;
					}

					if (!m_container.GetCaster())
					{
						return true;
					}

					if (m_container.GetCaster()->IsPlayer())
					{
						auto& casterPlayer = m_container.GetCaster()->AsPlayer();
						if (casterPlayer.GetGroupId() == 0)
						{
							return true;
						}

						if (!unit.IsPlayer() || unit.AsPlayer().GetGroupId() != casterPlayer.GetGroupId())
						{
							return true;
						}
					}

					if (proc(unit))
					{
						targets++;
					}
					return true;
				});
		}
		break;
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

		switch (GetType())
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

	AuraType AuraEffect::GetType() const
	{
		return static_cast<AuraType>(m_effect.aura());
	}
}
