// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/spells/aura_container.h"

#include "game_server/objects/game_player_s.h"
#include "game_server/objects/game_unit_s.h"
#include "game_server/spells/spell_cast.h"
#include "game_server/world/universe.h"
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

		m_procChance = spell.procchance();
		if (m_container.GetCaster())
		{
			m_container.GetCaster()->ApplySpellMod(spell_mod_op::ChanceOfSuccess, spell.id(), m_procChance);
			DLOG("Spell " << spell.id() << " has modified proc chance " << m_procChance);
		}

		if (m_procChance == 0)
		{
			return;
		}
		
		const uint32 procFlags = spell.procflags();
		const uint32 procSchool = spell.procschool();

		std::weak_ptr weakThis = shared_from_this();

		if (procFlags & (spell_proc_flags::Death | spell_proc_flags::Killed))
		{
			m_procEffects += m_container.GetOwner().killed.connect([weakThis, procSpell](GameUnitS* killer)
				{
					auto strongThis = weakThis.lock();
					if (!strongThis)
					{
						return;
					}

					if (!strongThis->m_container.GetCaster())
					{
						return;
					}

					if (strongThis->RollProcChance())
					{
						strongThis->ForEachProcTarget(strongThis->m_effect, killer, [strongThis, procSpell](const GameUnitS& unit) { return strongThis->ExecuteSpellProc(procSpell, unit); });
					}
				});
		}

		if (procFlags & (spell_proc_flags::TakenDamage |
			spell_proc_flags::TakenMeleeAutoAttack |
			spell_proc_flags::TakenSpellMagicDmgClassNeg |
			spell_proc_flags::TakenRangedAutoAttack |
			spell_proc_flags::TakenPeriodic))
		{
			m_procEffects += m_container.GetOwner().takenDamage.connect([weakThis, procFlags, procSchool, procSpell](GameUnitS* instigator, uint32 school, DamageType type)
				{
					auto strongThis = weakThis.lock();
					if (!strongThis)
					{
						return;
					}

					if (!strongThis->m_container.GetCaster())
					{
						return;
					}

					bool shouldProc = false;

					if (procFlags & spell_proc_flags::TakenDamage) shouldProc = true;
					else if (procFlags & spell_proc_flags::TakenMeleeAutoAttack && type == damage_type::AttackSwing) shouldProc = true;
					else if (procFlags & spell_proc_flags::TakenSpellMagicDmgClassNeg && type == damage_type::MagicalAbility && school == procSpell->procschool()) shouldProc = true;
					else if (procFlags & spell_proc_flags::TakenRangedAutoAttack && type == damage_type::RangedAttack) shouldProc = true;
					else if (procFlags & spell_proc_flags::TakenPeriodic && type == damage_type::Periodic && school == procSpell->procschool()) shouldProc = true;

					if (!shouldProc)
					{
						return;
					}

					if (strongThis->RollProcChance())
					{
						strongThis->ForEachProcTarget(strongThis->m_effect, instigator, [strongThis, procSpell](const GameUnitS& unit) { return strongThis->ExecuteSpellProc(procSpell, unit); });
					}
				});
		}

		if (spell.procflags() & spell_proc_flags::DoneMeleeAutoAttack)
		{
			m_procEffects += m_container.GetOwner().meleeAttackDone.connect([weakThis, procSpell](GameUnitS& victim)
				{
					auto strongThis = weakThis.lock();
					if (!strongThis)
					{
						return;
					}

					if (!strongThis->m_container.GetCaster())
					{
						return;
					}

					if (strongThis->RollProcChance())
					{
						strongThis->ForEachProcTarget(strongThis->m_effect, &strongThis->m_container.GetOwner(), [strongThis, procSpell](const GameUnitS& unit) { return strongThis->ExecuteSpellProc(procSpell, unit); });
					}
				});
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
		m_container.GetOwner().Damage(damage, school, m_container.GetCaster(), damage_type::Periodic);
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

		if (heal < 0)
		{
			heal = 0;
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
		int32 powerType = m_effect.miscvaluea();
		if (powerType < 0 || powerType > 2)
		{
			return;
		}

		uint32 power = GetBasePoints();

		uint32 curPower = m_container.GetOwner().Get<uint32>(object_fields::Mana + powerType);
		uint32 maxPower = m_container.GetOwner().Get<uint32>(object_fields::MaxMana + powerType);
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

	bool AuraEffect::RollProcChance() const
	{
		if (m_procChance == 0)
		{
			return false;
		}

		std::uniform_real_distribution chanceDistribution(0.0f, 100.0f);
		return chanceDistribution(randomGenerator) < m_procChance;
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
		, m_areaAuraTick(owner.GetTimers())
	{
		m_areaAuraTickConnection = m_areaAuraTick.ended.connect(this, &AuraContainer::HandleAreaAuraTick);
	}

	AuraContainer::~AuraContainer()
	{
		m_expirationCountdown.Cancel();

		if (m_applied)
		{
			SetApplied(false);
		}

		m_auras.clear();
	}

	void AuraContainer::AddAuraEffect(const proto::SpellEffect& effect, int32 basePoints)
	{
		// Check if this aura is an area aura and thus needs special handling
		if (effect.type() == spell_effects::ApplyAreaAura)
		{
			m_areaAura = true;
		}

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

		m_applied = apply;

		if (notify && m_owner.GetWorldInstance())
		{
			// TODO: Flag this aura as updated so we only sync changed auras to units which already know about this unit's auras instead
			// of having to sync ALL unit auras over and over again

			// Auras changed, flag object for next update loop
			m_owner.GetWorldInstance()->AddObjectUpdate(m_owner);
		}

		// Does this aura expire?
		if (apply)
		{
			// Start ticking area auras
			if (IsAreaAura())
			{
				// Do an initial tick
				m_areaAuraTick.SetEnd(GetAsyncTimeMs() + constants::OneSecond);
			}

			m_ownerEventConnections += m_owner.takenDamage.connect(this, &AuraContainer::OnOwnerDamaged);

			if (m_duration > 0)
			{
				if (!m_expiredConnection)
				{
					std::weak_ptr<AuraContainer> weakThis = weak_from_this();
					m_expiredConnection = m_expirationCountdown.ended += [weakThis]()
						{
							if (const auto strong = weakThis.lock())
							{
								strong->RemoveSelf();
							}
						};
				}

				m_expiration = GetAsyncTimeMs() + m_duration;
				m_expirationCountdown.SetEnd(m_expiration);
			}
		}
		else
		{
			m_ownerEventConnections.disconnect();
			m_expirationCountdown.Cancel();
			m_expiredConnection.disconnect();

			// Stop ticking area aura update
			if (IsAreaAura())
			{
				m_areaAuraTick.Cancel();
			}
		}

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

	void AuraContainer::NotifyOwnerMoved()
	{
		if (!m_applied)
		{
			return;
		}

		// Should aura be removed when moving?
		if (m_spell.aurainterruptflags() & spell_aura_interrupt_flags::Move)
		{
			RemoveSelf();
		}
	}

	void AuraContainer::RemoveSelf()
	{
		SetApplied(false);
		m_owner.RemoveAura(shared_from_this());
	}

	bool AuraContainer::ShouldRemoveAreaAuraDueToCasterConditions(const std::shared_ptr<GameUnitS>& caster,
		uint64 ownerGroupId, const Vector3& position, float range)
	{
		// If caster pointer is invalid
		if (!caster)
		{
			return true;
		}

		// Must be a player
		if (!caster->IsPlayer())
		{
			return true;
		}

		const GamePlayerS& casterPlayer = caster->AsPlayer();

		// Must be in same group; group must be non-zero
		if (casterPlayer.GetGroupId() != ownerGroupId || ownerGroupId == 0)
		{
			return true;
		}

		// Must be friendly
		if (!casterPlayer.UnitIsFriendly(m_owner))
		{
			return true;
		}

		// Must be in range
		const float distanceSq = casterPlayer.GetSquaredDistanceTo(position, /*allowZDiff=*/true);
		if (distanceSq > (range * range))
		{
			return true;
		}

		return false;
	}

	void AuraContainer::OnOwnerDamaged(GameUnitS* instigator, uint32 school, DamageType type)
	{
		if (m_spell.aurainterruptflags() & spell_aura_interrupt_flags::Damage)
		{
			RemoveSelf();
			return;
		}

		if (m_spell.aurainterruptflags() & spell_aura_interrupt_flags::DirectDamage)
		{
			if (type != damage_type::Periodic)
			{
				RemoveSelf();
				return;
			}
		}

		if (m_spell.aurainterruptflags() & spell_aura_interrupt_flags::HitBySpell)
		{
			if (type == damage_type::MagicalAbility)
			{
				RemoveSelf();
				return;
			}
		}
	}

	void AuraContainer::HandleAreaAuraTick()
	{
		// Should only ever be active for players right now!
		ASSERT(m_owner.IsPlayer());
		ASSERT(m_applied);

		GamePlayerS& owner = m_owner.AsPlayer();
		const uint64 groupId = owner.GetGroupId();

		const auto* rangeType = owner.GetProject().ranges.getById(m_spell.rangetype());
		ASSERT(rangeType);

		const float range = rangeType->range();
		const Vector3& position = m_owner.GetPosition();

		// Check if this is our area aura or if we got this from someone else
		if (GetCasterId() == m_owner.GetGuid())
		{
			// It's our own aura
			if (groupId != 0)
			{
				// Search for nearby party members and apply the aura to them
				m_owner.GetWorldInstance()->GetUnitFinder().FindUnits(Circle(position.x, position.z, range), [&owner, this](GameUnitS& unit) -> bool
					{
						// Skip ourselves!
						if (unit.GetGuid() == owner.GetGuid())
						{
							return true;
						}

						// Only players
						if (!unit.IsPlayer())
						{
							return true;
						}

						GamePlayerS& player = unit.AsPlayer();

						// Must be in the same group and friendly
						if (player.GetGroupId() == owner.GetGroupId() && player.UnitIsFriendly(owner))
						{
							// Aura already active from same caster?
							if (!player.HasAuraSpellFromCaster(m_spell.id(), m_casterId))
							{
								// Apply the aura
								auto container = std::make_shared<AuraContainer>(player, m_casterId, m_spell, m_duration, m_itemGuid);
								for (const auto& effect : m_auras)
								{
									container->AddAuraEffect(effect->GetEffect(), effect->GetBasePoints());
								}
								player.ApplyAura(std::move(container));
							}
						}

						return true;
					});
			}
		}
		else
		{
			// It's from someone else; check if we should remove it
			std::shared_ptr<GameUnitS> caster = m_caster.lock();
			if (ShouldRemoveAreaAuraDueToCasterConditions(caster, groupId, position, range))
			{
				RemoveSelf();
				return;
			}
		}

		m_areaAuraTick.SetEnd(GetAsyncTimeMs() + constants::OneSecond);
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

		const bool sameBaseSpellId = HasSameBaseSpellId(other.GetSpell());
		const bool sameSpellId = sameBaseSpellId || (other.GetSpellId() == GetSpellId());
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

	bool AuraContainer::HasSameBaseSpellId(const proto::SpellEntry& spell) const
	{
		if (spell.baseid() == 0)
		{
			return false;
		}

		const uint32 baseSpellId = GetBaseSpellId();
		if (baseSpellId == spell.baseid())
		{
			return true;
		}

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
