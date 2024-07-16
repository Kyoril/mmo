
#include "single_cast_state.h"

#include "no_cast_state.h"

#include "base/utilities.h"

namespace mmo
{
	SingleCastState::SingleCastState(SpellCast& cast, const proto::SpellEntry& spell, const SpellTargetMap& target, const GameTime castTime, const bool isProc)
		: m_cast(cast)
		, m_spell(spell)
		, m_target(target)
		, m_hasFinished(false)
		, m_countdown(cast.GetTimerQueue())
		, m_impactCountdown(cast.GetTimerQueue())
		, m_castTime(castTime)
		, m_castEnd(0)
		, m_isProc(isProc)
		, m_projectileStart(0)
		, m_projectileEnd(0)
		, m_connectedMeleeSignal(false)
		, m_delayCounter(0)
		, m_tookCastItem(false)
		, m_tookReagents(false)
		, m_attackerProc(0)
		, m_victimProc(0)
		, m_canTrigger(false)
		, m_instantsCast(false)
		, m_delayedCast(false)
	{
		// Check if the executor is in the world
		auto& executor = m_cast.GetExecuter();
		const auto* worldInstance = executor.GetWorldInstance();

		//m_castTime *= executor.Get<float>(object_fields::ModCastSpeed);

		auto const casterId = executor.GetGuid();

		if (worldInstance && !(m_spell.attributes(0) & spell_attributes::Passive) && !m_isProc && m_castTime > 0)
		{
			SendPacketFromCaster(
				executor,
				[casterId, this](game::OutgoingPacket& out_packet)
				{
					out_packet.Start(game::realm_client_packet::SpellStart);
					out_packet
						<< io::write_packed_guid(casterId)
						<< io::write<uint32>(m_spell.id())
						<< io::write<GameTime>(m_castTime)
						<< m_target;
					out_packet.Finish();
				});
		}

		if (worldInstance && IsChanneled())
		{
			SendPacketFromCaster(
				executor,
				[casterId, this](game::OutgoingPacket& out_packet)
				{
					out_packet.Start(game::realm_client_packet::ChannelStart);
					out_packet
						<< io::write_packed_guid(casterId)
						<< io::write<uint32>(m_spell.id())
						<< io::write<int32>(m_spell.duration());
					out_packet.Finish();
				}
			);

			//executor.Set<uint64>(object_fields::ChannelObject, m_target.getUnitTarget());
			//executor.Set<uint32>(object_fields::ChannelSpell, m_spell.id());
		}

		const Vector3& location = m_cast.GetExecuter().GetPosition();
		m_x = location.x, m_y = location.y, m_z = location.z;

		m_countdown.ended.connect([this]()
			{
				if (IsChanneled())
				{
					this->FinishChanneling();
				}
				else
				{
					this->OnCastFinished();
				}
			});
	}

	void SingleCastState::Activate()
	{
		if (!Validate())
		{
			m_hasFinished = true;
			return;
		}

		if (m_castTime > 0)
		{
			m_castEnd = GetAsyncTimeMs() + m_castTime;
			m_countdown.SetEnd(m_castEnd);

			WorldInstance* world = m_cast.GetExecuter().GetWorldInstance();
			ASSERT(world);

			GameUnitS* unitTarget = nullptr;
			if (m_target.HasUnitTarget())
			{
				unitTarget = dynamic_cast<GameUnitS*>(world->FindObjectByGuid(m_target.GetUnitTarget()));
			}
			
			if (unitTarget)
			{
				m_onTargetDied = unitTarget->killed.connect(this, &SingleCastState::OnTargetKilled);
				m_onTargetRemoved = unitTarget->despawned.connect(this, &SingleCastState::OnTargetDespawned);
			}
		}
		else
		{
			WorldInstance* world = m_cast.GetExecuter().GetWorldInstance();
			ASSERT(world);

			GameUnitS* unitTarget = nullptr;
			if (m_target.HasUnitTarget())
			{
				unitTarget = dynamic_cast<GameUnitS*>(world->FindObjectByGuid(m_target.GetUnitTarget()));
			}

			if (unitTarget)
			{
				m_onTargetDied = unitTarget->killed.connect(this, &SingleCastState::OnTargetKilled);
				m_onTargetRemoved = unitTarget->despawned.connect(this, &SingleCastState::OnTargetDespawned);
			}

			OnCastFinished();
		}
	}

	std::pair<SpellCastResult, SpellCasting*> SingleCastState::StartCast(SpellCast& cast, const proto::SpellEntry& spell, const SpellTargetMap& target, const GameTime castTime, const bool doReplacePreviousCast)
	{
		if (!m_hasFinished && !doReplacePreviousCast)
		{
			return std::make_pair(spell_cast_result::FailedSpellInProgress, &m_casting);
		}

		FinishChanneling();

		SpellCasting& casting = CastSpell(
			cast,
			spell,
			target,
			castTime);

		return std::make_pair(spell_cast_result::CastOkay, &casting);
	}

	void SingleCastState::StopCast(const GameTime interruptCooldown)
	{
		FinishChanneling();

		// Nothing to cancel
		if (m_hasFinished)
		{
			return;
		}

		m_countdown.Cancel();
		SendEndCast(spell_cast_result::FailedBadTargets);
		m_hasFinished = true;

		if (interruptCooldown)
		{
			ApplyCooldown(interruptCooldown, interruptCooldown);
		}

		const std::weak_ptr weakThis = shared_from_this();
		m_casting.ended(false);

		if (weakThis.lock())
		{
			m_cast.SetState(std::make_shared<NoCastState>());
		}
	}

	void SingleCastState::OnUserStartsMoving()
	{
		if (m_hasFinished)
		{
			return;
		}

		// Interrupt spell cast if moving
		const Vector3 location = m_cast.GetExecuter().GetPosition();
		if (location.x != m_x || location.y != m_y || location.z != m_z)
		{
			StopCast();
		}
	}

	void SingleCastState::FinishChanneling()
	{
		if (!IsChanneled())
		{
			return;
		}

		// Caster could have left the world
		const auto* world = m_cast.GetExecuter().GetWorldInstance();
		if (!world)
		{
			return;
		}

		const uint64 casterId = m_cast.GetExecuter().GetGuid();
		SendPacketFromCaster(m_cast.GetExecuter(),
			[casterId](game::OutgoingPacket& out_packet)
			{
				out_packet.Start(game::realm_client_packet::ChannelUpdate);
				out_packet
					<< io::write_packed_guid(casterId)
					<< io::write<GameTime>(0);
				out_packet.Finish();
			});

		//m_cast.GetExecuter().Set<uint64>(object_fields::ChannelObject, 0);
		//m_cast.GetExecuter().Set<uint32>(object_fields::ChannelSpell, 0);
		m_casting.ended(true);
	}

	bool SingleCastState::Validate()
	{
		// Caster either has to be alive or spell has to be castable while dead
		if (!m_cast.GetExecuter().IsAlive() && !HasAttributes(0, spell_attributes::CastableWhileDead))
		{
			SendEndCast(spell_cast_result::FailedCasterDead);
			return false;
		}

		GameUnitS* unitTarget = nullptr;
		if (m_target.HasUnitTarget())
		{
			unitTarget = reinterpret_cast<GameUnitS*>(m_cast.GetExecuter().GetWorldInstance()->FindObjectByGuid(m_target.GetUnitTarget()));
		}

		if (unitTarget != nullptr && !m_cast.GetExecuter().IsFacingTowards(*unitTarget))
		{
			SendEndCast(spell_cast_result::FailedUnitNotInfront);
			return false;
		}

		// If only castable on daytime, check the current time of day
		if (HasAttributes(0, spell_attributes::DaytimeOnly) && !HasAttributes(0, spell_attributes::NightOnly))
		{
			// TODO
		}

		if (HasAttributes(0, spell_attributes::NightOnly) && !HasAttributes(0, spell_attributes::DaytimeOnly))
		{
			// TODO
		}

		if (HasAttributes(0, spell_attributes::IndoorOnly) && !HasAttributes(0, spell_attributes::OutdoorOnly))
		{
			// TODO: Check whether we are indoor. For now, caster is always considered to be outdoor
			SendEndCast(spell_cast_result::FailedOnlyIndoors);
			return false;
		}

		if (HasAttributes(0, spell_attributes::OutdoorOnly) && !HasAttributes(0, spell_attributes::IndoorOnly))
		{
			// TODO: Check whether we are indoor. For now, caster is always considered to be outdoor
		}

		if (HasAttributes(0, spell_attributes::OnlyStealthed))
		{
			// TODO: Check whether we are stealthed. For now, caster is never stealthed
			SendEndCast(spell_cast_result::FailedOnlyStealthed);
			return false;
		}

		if (HasAttributes(0, spell_attributes::NotInCombat) && m_cast.GetExecuter().IsInCombat())
		{
			SendEndCast(spell_cast_result::FailedAffectingCombat);
			return false;
		}

		return true;
	}

	bool SingleCastState::ConsumeItem(bool delayed)
	{
		return true;
	}

	bool SingleCastState::ConsumeReagents(bool delayed)
	{
		return true;
	}

	bool SingleCastState::ConsumePower()
	{
		const int32 totalCost = m_cast.CalculatePowerCost(m_spell);
		if (totalCost > 0)
		{
			if (m_spell.powertype() == power_type::Health)
			{
				uint32 currentHealth = m_cast.GetExecuter().Get<uint32>(object_fields::Health);
				if (totalCost > 0 && currentHealth < static_cast<uint32>(totalCost))
				{
					SendEndCast(spell_cast_result::FailedNoPower);
					m_hasFinished = true;
					return false;
				}

				currentHealth -= totalCost;
				m_cast.GetExecuter().Set<uint32>(object_fields::Health, currentHealth);
			}
			else
			{
				int32 currentPower = m_cast.GetExecuter().Get<uint32>(object_fields::Mana + m_spell.powertype());
				if (totalCost > 0 && currentPower < uint32(totalCost))
				{
					SendEndCast(spell_cast_result::FailedNoPower);
					m_hasFinished = true;
					return false;
				}

				currentPower -= totalCost;
				m_cast.GetExecuter().Set<uint32>(object_fields::Mana + m_spell.powertype(), currentPower);
			}
		}

		return true;
	}

	auto SingleCastState::ApplyCooldown(const GameTime cooldownTimeMs, const GameTime categoryCooldownTimeMs) -> void
	{
		if (cooldownTimeMs > 0)
		{
			m_cast.GetExecuter().SetCooldown(m_spell.id(), cooldownTimeMs);
		}
		
		if (categoryCooldownTimeMs > 0)
		{
			m_cast.GetExecuter().SetCooldown(m_spell.id(), cooldownTimeMs);
		}
	}

	void SingleCastState::ApplyAllEffects()
	{
		// Add spell cooldown if any
		if (!m_instantsCast && !m_delayedCast)
		{
			const uint64 spellCatCD = m_spell.categorycooldown();
			const uint64 spellCD = m_spell.cooldown();

			GameTime finalCD = spellCD;
			if (!finalCD) 
			{
				finalCD = spellCatCD;
			}

			if (finalCD)
			{
				ApplyCooldown(finalCD, spellCatCD);
			}
		}

		// Make sure that this isn't destroyed during the effects
		auto strong = shared_from_this();

		std::vector<uint32> effects;
		for (int i = 0; i < m_spell.effects_size(); ++i)
		{
			effects.push_back(m_spell.effects(i).type());
		}

		m_canTrigger = true;

		namespace se = spell_effects;

		std::vector<std::pair<uint32, EffectHandler>> effectMap{
			{se::Dummy,				std::bind(&SingleCastState::SpellEffectDummy, this, std::placeholders::_1) },
			{se::InstantKill,			std::bind(&SingleCastState::SpellEffectInstantKill, this, std::placeholders::_1)},
			{se::PowerDrain,			std::bind(&SingleCastState::SpellEffectDrainPower, this, std::placeholders::_1)},
			{se::Heal,				std::bind(&SingleCastState::SpellEffectHeal, this, std::placeholders::_1)},
			{se::Bind,				std::bind(&SingleCastState::SpellEffectBind, this, std::placeholders::_1)},
			{se::QuestComplete,		std::bind(&SingleCastState::SpellEffectQuestComplete, this, std::placeholders::_1)},
			{se::Proficiency,			std::bind(&SingleCastState::SpellEffectProficiency, this, std::placeholders::_1)},
			{se::AddComboPoints,		std::bind(&SingleCastState::SpellEffectAddComboPoints, this, std::placeholders::_1)},
			{se::Duel,					std::bind(&SingleCastState::SpellEffectDuel, this, std::placeholders::_1)},
			{se::WeaponDamageNoSchool,	std::bind(&SingleCastState::SpellEffectWeaponDamageNoSchool, this, std::placeholders::_1)},
			{se::CreateItem,			std::bind(&SingleCastState::SpellEffectCreateItem, this, std::placeholders::_1)},
			{se::WeaponDamage,			std::bind(&SingleCastState::SpellEffectWeaponDamage, this, std::placeholders::_1)},
			{se::TeleportUnits,			std::bind(&SingleCastState::SpellEffectTeleportUnits, this, std::placeholders::_1)},
			{se::TriggerSpell,			std::bind(&SingleCastState::SpellEffectTriggerSpell, this, std::placeholders::_1)},
			{se::Energize,				std::bind(&SingleCastState::SpellEffectEnergize, this, std::placeholders::_1)},
			{se::WeaponPercentDamage,	std::bind(&SingleCastState::SpellEffectWeaponPercentDamage, this, std::placeholders::_1)},
			{se::PowerBurn,				std::bind(&SingleCastState::SpellEffectPowerBurn, this, std::placeholders::_1)},
			{se::OpenLock,				std::bind(&SingleCastState::SpellEffectOpenLock, this, std::placeholders::_1)},
			{se::OpenLockItem,			std::bind(&SingleCastState::SpellEffectOpenLock, this, std::placeholders::_1) },
			{se::ApplyAreaAuraParty,	std::bind(&SingleCastState::SpellEffectApplyAreaAuraParty, this, std::placeholders::_1)},
			{se::Dispel,				std::bind(&SingleCastState::SpellEffectDispel, this, std::placeholders::_1)},
			{se::Summon,				std::bind(&SingleCastState::SpellEffectSummon, this, std::placeholders::_1)},
			{se::SummonPet,				std::bind(&SingleCastState::SpellEffectSummonPet, this, std::placeholders::_1) },
			{se::ScriptEffect,			std::bind(&SingleCastState::SpellEffectScript, this, std::placeholders::_1)},
			{se::AttackMe,				std::bind(&SingleCastState::SpellEffectAttackMe, this, std::placeholders::_1)},
			{se::NormalizedWeaponDmg,	std::bind(&SingleCastState::SpellEffectNormalizedWeaponDamage, this, std::placeholders::_1)},
			{se::StealBeneficialBuff,	std::bind(&SingleCastState::SpellEffectStealBeneficialBuff, this, std::placeholders::_1)},
			{se::InterruptCast,			std::bind(&SingleCastState::SpellEffectInterruptCast, this, std::placeholders::_1) },
			{se::LearnSpell,			std::bind(&SingleCastState::SpellEffectLearnSpell, this, std::placeholders::_1) },
			{se::ScriptEffect,			std::bind(&SingleCastState::SpellEffectScriptEffect, this, std::placeholders::_1) },
			{se::DispelMechanic,		std::bind(&SingleCastState::SpellEffectDispelMechanic, this, std::placeholders::_1) },
			{se::Resurrect,				std::bind(&SingleCastState::SpellEffectResurrect, this, std::placeholders::_1) },
			{se::ResurrectNew,			std::bind(&SingleCastState::SpellEffectResurrectNew, this, std::placeholders::_1) },
			{se::KnockBack,				std::bind(&SingleCastState::SpellEffectKnockBack, this, std::placeholders::_1) },
			{se::TransDoor,				std::bind(&SingleCastState::SpellEffectTransDoor, this, std::placeholders::_1) },
			{se::ApplyAura,				std::bind(&SingleCastState::SpellEffectApplyAura, this, std::placeholders::_1)},
			{se::PersistentAreaAura,	std::bind(&SingleCastState::SpellEffectPersistentAreaAura, this, std::placeholders::_1) },
			{se::ApplyAreaAuraParty,	std::bind(&SingleCastState::SpellEffectApplyAura, this, std::placeholders::_1)},
			{se::SchoolDamage,			std::bind(&SingleCastState::SpellEffectSchoolDamage, this, std::placeholders::_1)}
		};

		// Make sure that the executer exists after all effects have been executed
		auto strongCaster = std::static_pointer_cast<GameUnitS>(m_cast.GetExecuter().shared_from_this());

		if (!m_delayedCast)
		{
			for (auto& effect : effectMap)
			{
				for (int k = 0; k < effects.size(); ++k)
				{
					if (effect.first == effects[k])
					{
						ASSERT(effect.second);
						effect.second(m_spell.effects(k));
					}
				}
			}

			m_delayedCast = true;
		}
		
		if (!m_instantsCast || !m_delayedCast)
		{
			return;
		}

		completedEffects();
	}

	int32 SingleCastState::CalculateEffectBasePoints(const proto::SpellEffect& effect)
	{
		// TODO
		const int32 comboPoints = 0;

		int32 level = static_cast<int32>(m_cast.GetExecuter().Get<uint32>(object_fields::Level));
		if (level > m_spell.maxlevel() && m_spell.maxlevel() > 0)
		{
			level = m_spell.maxlevel();
		}
		else if (level < m_spell.baselevel())
		{
			level = m_spell.baselevel();
		}
		level -= m_spell.spelllevel();

		// Calculate the damage done
		const float basePointsPerLevel = effect.pointsperlevel();
		const float randomPointsPerLevel = effect.diceperlevel();
		const int32 basePoints = effect.basepoints() + level * basePointsPerLevel;
		const int32 randomPoints = effect.diesides() + level * randomPointsPerLevel;
		const int32 comboDamage = effect.pointspercombopoint() * comboPoints;

		std::uniform_int_distribution<int> distribution(effect.basedice(), randomPoints);
		const int32 randomValue = (effect.basedice() >= randomPoints ? effect.basedice() : distribution(randomGenerator));

		return basePoints + randomValue + comboDamage;
	}

	uint32 SingleCastState::GetSpellPointsTotal(const proto::SpellEffect& effect, uint32 spellPower, uint32 bonusPct)
	{
		return 0;
	}

	void SingleCastState::MeleeSpecialAttack(const proto::SpellEffect& effect, bool basepointsArePct)
	{
		
	}

	void SingleCastState::SpellEffectInstantKill(const proto::SpellEffect& effect)
	{
		GameUnitS* unitTarget = nullptr;
		if (m_target.HasUnitTarget())
		{
			unitTarget = reinterpret_cast<GameUnitS*>(m_cast.GetExecuter().GetWorldInstance()->FindObjectByGuid(m_target.GetUnitTarget()));
		}

		if (unitTarget == nullptr)
		{
			return;
		}

		unitTarget->Kill(&m_cast.GetExecuter());
	}

	void SingleCastState::SpellEffectDummy(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectSchoolDamage(const proto::SpellEffect& effect)
	{
		GameUnitS* unitTarget = nullptr;
		if (m_target.HasUnitTarget())
		{
			unitTarget = reinterpret_cast<GameUnitS*>(m_cast.GetExecuter().GetWorldInstance()->FindObjectByGuid(m_target.GetUnitTarget()));
		}

		if (unitTarget == nullptr)
		{
			return;
		}

		// TODO: Do real calculation including crit chance, miss chance, resists, etc.
		const uint32 damageAmount = std::max<int32>(0, CalculateEffectBasePoints(effect));
		unitTarget->Damage(damageAmount, m_spell.spellschool(), &m_cast.GetExecuter());

		// Log spell damage to client
		m_cast.GetExecuter().SpellDamageLog(unitTarget->GetGuid(), damageAmount, m_spell.spellschool(), DamageFlags::None, m_spell);
	}

	void SingleCastState::SpellEffectTeleportUnits(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectApplyAura(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectPersistentAreaAura(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectDrainPower(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectHeal(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectBind(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectQuestComplete(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectWeaponDamageNoSchool(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectCreateItem(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectEnergize(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectWeaponPercentDamage(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectOpenLock(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectApplyAreaAuraParty(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectDispel(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectSummon(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectSummonPet(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectWeaponDamage(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectProficiency(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectPowerBurn(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectTriggerSpell(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectScript(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectAddComboPoints(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectDuel(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectCharge(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectAttackMe(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectNormalizedWeaponDamage(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectStealBeneficialBuff(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectInterruptCast(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectLearnSpell(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectScriptEffect(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectDispelMechanic(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectResurrect(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectResurrectNew(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectKnockBack(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectSkill(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectTransDoor(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SendEndCast(SpellCastResult result)
	{
		auto& executer = m_cast.GetExecuter();
		auto* worldInstance = executer.GetWorldInstance();

		if (!worldInstance || m_spell.attributes(0) & spell_attributes::Passive)
		{
			return;
		}

		// Raise event
		GetCasting().ended(result == spell_cast_result::CastOkay);

		const uint64 casterId = executer.GetGuid();
		const uint32 spellId = m_spell.id();

		if (result == spell_cast_result::CastOkay)
		{
			// Instead of self-targeting, use unit target
			SpellTargetMap targetMap = m_target;
			if (targetMap.GetTargetMap() == spell_cast_target_flags::Self)
			{
				targetMap.SetTargetMap(spell_cast_target_flags::Unit);
				targetMap.SetUnitTarget(executer.GetGuid());
			}

			SendPacketFromCaster(executer,
				[casterId, spellId, &targetMap](game::OutgoingPacket& out_packet)
				{
					out_packet.Start(game::realm_client_packet::SpellGo);
					out_packet
						<< io::write_packed_guid(casterId)
						<< io::write<uint32>(spellId)
						<< io::write<GameTime>(GetAsyncTimeMs())
						<< targetMap;
					out_packet.Finish();
				});
		}
		else
		{
			SendPacketFromCaster(executer,
				[casterId, spellId, result](game::OutgoingPacket& out_packet)
				{
					out_packet.Start(game::realm_client_packet::SpellFailure);
					out_packet
						<< io::write_packed_guid(casterId)
						<< io::write<uint32>(spellId)
						<< io::write<GameTime>(GetAsyncTimeMs())
						<< io::write<uint8>(result);
					out_packet.Finish();
				});
		}
	}

	void SingleCastState::OnCastFinished()
	{
		auto strongThis = shared_from_this();

		if (m_castTime > 0)
		{
			if (!m_cast.GetExecuter().GetWorldInstance())
			{
				m_hasFinished = true;
				return;
			}

			// TODO: Range check etc.
		}

		m_hasFinished = true;

		if (!Validate())
		{
			return;
		}

		if (!ConsumePower()) 
		{
			return;
		}

		if (!ConsumeReagents()) 
		{
			return;
		}

		if (!ConsumeItem()) 
		{
			return;
		}

		SendEndCast(spell_cast_result::CastOkay);

		if (m_spell.speed() > 0.0f)
		{
			uint64 unitTargetGuid = 0;
			GameUnitS* targetUnit = nullptr;
			if (m_target.HasUnitTarget() && (unitTargetGuid = m_target.GetUnitTarget()) != 0)
			{
				targetUnit = dynamic_cast<GameUnitS*>(m_cast.GetExecuter().GetWorldInstance()->FindObjectByGuid(unitTargetGuid));
				if (targetUnit != nullptr)
				{
					const float distance = ::sqrtf(m_cast.GetExecuter().GetSquaredDistanceTo(targetUnit->GetPosition(), true));
					const GameTime travelTimeMs = static_cast<GameTime>(distance / m_spell.speed() * 1000.0f);

					// Calculate spell impact delay
					auto strongTarget = std::static_pointer_cast<GameUnitS>(targetUnit->shared_from_this());

					// This will be executed on the impact
					m_impactCountdown.ended.connect(
						[this, strongThis, strongTarget]() mutable
						{
							const auto currentTime = GetAsyncTimeMs();
							const auto& targetLoc = strongTarget->GetPosition();

							// If end quals start time, we are at 100% progress
							const float percentage = (m_projectileEnd == m_projectileStart) ? 1.0f : static_cast<float>(currentTime - m_projectileStart) / static_cast<float>(m_projectileEnd - m_projectileStart);
							const Vector3 projectilePos = m_projectileOrigin.Lerp(m_projectileDest, percentage);
							const float dist = (targetLoc - projectilePos).GetLength();
							const GameTime timeMS = (dist / m_spell.speed()) * 1000;

							m_projectileOrigin = projectilePos;
							m_projectileDest = targetLoc;
							m_projectileStart = currentTime;
							m_projectileEnd = currentTime + timeMS;

							if (timeMS >= 50)
							{
								m_impactCountdown.SetEnd(currentTime + std::min<GameTime>(timeMS, 200));
							}
							else
							{
								strongThis->ApplyAllEffects();
								strongTarget.reset();
								strongThis.reset();
							}
						});

					m_projectileStart = GetAsyncTimeMs();
					m_projectileEnd = m_projectileStart + travelTimeMs;
					m_projectileOrigin = m_cast.GetExecuter().GetPosition();
					m_projectileDest = targetUnit->GetPosition();
					m_impactCountdown.SetEnd(m_projectileStart + std::min<GameTime>(travelTimeMs, 200));
				}
			}
		}
		else
		{
			ApplyAllEffects();
		}

		if (!IsChanneled())
		{
			// may destroy this, too
			m_casting.ended(true);
		}
	}

	void SingleCastState::OnTargetKilled(GameUnitS*)
	{
		StopCast();
	}

	void SingleCastState::OnTargetDespawned(GameObjectS&)
	{
		StopCast();
	}

	void SingleCastState::OnUserDamaged()
	{

	}

	void SingleCastState::ExecuteMeleeAttack()
	{
	}
}
