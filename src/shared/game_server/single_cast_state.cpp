
#include "single_cast_state.h"

#include "game_item_s.h"
#include "game_player_s.h"
#include "game_world_object_s.h"
#include "no_cast_state.h"

#include "base/utilities.h"
#include "proto_data/project.h"

namespace mmo
{
	SingleCastState::SingleCastState(SpellCast& cast, const proto::SpellEntry& spell, const SpellTargetMap& target, const GameTime castTime, const bool isProc, uint64 itemGuid)
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
		, m_attackerProc(0)
		, m_victimProc(0)
		, m_canTrigger(false)
		, m_instantsCast(false)
		, m_delayedCast(false)
		, m_itemGuid(itemGuid)
	{
		// Check if the executor is in the world
		auto& executor = m_cast.GetExecuter();
		const auto* worldInstance = executor.GetWorldInstance();

		// Apply cast time modifier
		executor.ApplySpellMod<int32>(spell_mod_op::CastTime, spell.id(), reinterpret_cast<int32&>(m_castTime));

		// This is a hack because cast time might become actually negative by modifiers which would be bad here!
		if (static_cast<int32>(m_castTime) < 0)
		{
			m_castTime = 0;
		}
		
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
			ELOG("Validation failed");
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

	std::pair<SpellCastResult, SpellCasting*> SingleCastState::StartCast(SpellCast& cast, const proto::SpellEntry& spell, const SpellTargetMap& target, const GameTime castTime, const bool doReplacePreviousCast, uint64 itemGuid)
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
			castTime,
			itemGuid);

		return std::make_pair(spell_cast_result::CastOkay, &casting);
	}

	void SingleCastState::StopCast(SpellInterruptFlags reason, const GameTime interruptCooldown)
	{
		FinishChanneling();

		// Nothing to cancel
		if (m_hasFinished)
		{
			return;
		}

		// Check whether the spell can be interrupted by this action
		if (reason != spell_interrupt_flags::Any &&
			(m_spell.interruptflags() & reason) == 0)
		{
			return;
		}

		SpellCastResult result = spell_cast_result::FailedBadTargets;
		switch(reason)
		{
		case spell_interrupt_flags::Interrupt:
		case spell_interrupt_flags::Damage:
		case spell_interrupt_flags::AutoAttack:
			result = spell_cast_result::FailedInterrupted;
			break;
		case spell_interrupt_flags::Movement:
			result = spell_cast_result::FailedMoving;
			break;
		}

		m_countdown.Cancel();
		SendEndCast(result);
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
			StopCast(spell_interrupt_flags::Movement);
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
		// Risky: Is this really good?
		if (m_isProc)
		{
			return true;
		}

		// Caster level too low?
		if (m_spell.spelllevel() > 1 && static_cast<int32>(m_cast.GetExecuter().GetLevel()) < m_spell.spelllevel())
		{
			SendEndCast(spell_cast_result::FailedLevelRequirement);
			return false;
		}

		// Check race requirement
		if (m_cast.GetExecuter().IsPlayer())
		{
			GamePlayerS& playerCaster = m_cast.GetExecuter().AsPlayer();
			if (m_spell.racemask() != 0 && !(m_spell.racemask() & (1 << (playerCaster.GetRaceEntry()->id() - 1))))
			{
				SendEndCast(spell_cast_result::FailedError);
				return false;
			}

			// Check class requirement
			if (m_spell.classmask() != 0 && !(m_spell.classmask() & (1 << (playerCaster.GetClassEntry()->id() - 1))))
			{
				SendEndCast(spell_cast_result::FailedError);
				return false;
			}
		}

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

		if (unitTarget != nullptr)
		{
			if ((m_spell.facing() & spell_facing_flags::TargetInFront) != 0 && !m_cast.GetExecuter().IsFacingTowards(*unitTarget))
			{
				SendEndCast(spell_cast_result::FailedUnitNotInfront);
				return false;
			}

			if ((m_spell.facing() & spell_facing_flags::BehindTarget) != 0 && !unitTarget->IsFacingAwayFrom(m_cast.GetExecuter()))
			{
				SendEndCast(spell_cast_result::FailedUnitNotBehind);
				return false;
			}
		}

		// If this is an item and targets a unit target, check if it's a potion
		if ((unitTarget || m_target.GetTargetMap() == spell_cast_target_flags::Self) && m_itemGuid && m_cast.GetExecuter().IsPlayer())
		{
			GameUnitS* target = unitTarget ? unitTarget : &m_cast.GetExecuter();
			ASSERT(target);

			auto& player = m_cast.GetExecuter().AsPlayer();
			auto& inv = player.GetInventory();

			if (uint16 itemSlot; inv.FindItemByGUID(m_itemGuid, itemSlot))
			{
				const auto item = inv.GetItemAtSlot(itemSlot);
				ASSERT(item);

				if (item->GetEntry().itemclass() == item_class::Consumable)
				{
					if (SpellHasEffect(m_spell, spell_effects::Heal) && target->GetHealth() >= target->GetMaxHealth())
					{
						SendEndCast(spell_cast_result::FailedAlreadyAtFullHealth);
						return false;
					}

					if (SpellHasEffect(m_spell, spell_effects::Energize) && target->GetPower() >= target->GetMaxPower())
					{
						SendEndCast(spell_cast_result::FailedAlreadyAtFullPower);
						return false;
					}
				}
			}

		}

		// Check if we are trying to cast a spell on a dead target which is not allowed
		if (unitTarget && !unitTarget->IsAlive() && !HasAttributes(0, spell_attributes::CanTargetDead))
		{
			SendEndCast(spell_cast_result::FailedTargetNotDead);
			return false;
		}

		if (unitTarget && m_spell.has_rangetype())
		{
			const proto::RangeType* rangeType = unitTarget->GetProject().ranges.getById(m_spell.rangetype());
			if (rangeType && rangeType->range() > 0.0f)
			{
				float range = rangeType->range();

				// Modify spell range by spell mods
				m_cast.GetExecuter().ApplySpellMod(spell_mod_op::Range, m_spell.id(), range);

				// If distance is too big, cancel casting. Note we use squared distance check as distance involves sqrt which is more expansive
				if (m_cast.GetExecuter().GetSquaredDistanceTo(unitTarget->GetPosition(), true) > range * range)
				{
					SendEndCast(spell_cast_result::FailedOutOfRange);
					return false;
				}
			}
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

	std::shared_ptr<GameUnitS> SingleCastState::GetEffectUnitTarget(const proto::SpellEffect& effect)
	{
		switch (effect.targeta())
		{
		case spell_effect_targets::Caster:
		case spell_effect_targets::NearbyParty:
			// TODO: Nearby party
			return std::static_pointer_cast<GameUnitS>(m_cast.GetExecuter().shared_from_this());
		case spell_effect_targets::TargetAlly:
		case spell_effect_targets::TargetEnemy:
		case spell_effect_targets::TargetAny:
			{
				GameObjectS* targetObject = m_cast.GetExecuter().GetWorldInstance()->FindObjectByGuid(m_target.GetUnitTarget());
				if (!targetObject)
				{
					return nullptr;
				}

				auto unit = std::dynamic_pointer_cast<GameUnitS>(targetObject->shared_from_this());
				if (unit)
				{
					switch (effect.targeta())
					{
					case spell_effect_targets::TargetAlly:
						// For now we consider all non-hostile units as allies
						if (m_cast.GetExecuter().UnitIsEnemy(*unit))
						{
							// Target has to be an ally but is not
							return nullptr;
						}
						break;
					case spell_effect_targets::TargetEnemy:
						if (!m_cast.GetExecuter().UnitIsEnemy(*unit))
						{
							// Target has to be an enemy but is not
							return nullptr;
						}
						break;
					}
				}

				return unit;
			}
		default:
			return nullptr;
		}
	}

	bool SingleCastState::ConsumeItem(bool delayed)
	{
		if (m_tookCastItem && delayed)
		{
			return true;
		}
		
		if (m_itemGuid != 0 && m_cast.GetExecuter().GetTypeId() == ObjectTypeId::Player)
		{
			auto* character = reinterpret_cast<GamePlayerS*>(&m_cast.GetExecuter());
			auto& inv = character->GetInventory();

			uint16 itemSlot = 0;
			if (!inv.FindItemByGUID(m_itemGuid, itemSlot))
			{
				return false;
			}

			auto item = inv.GetItemAtSlot(itemSlot);
			if (!item)
			{
				return false;
			}

			auto removeItem = [this, item, itemSlot, &inv]() {
				if (m_tookCastItem)
				{
					return;
				}

				auto result = inv.RemoveItem(itemSlot, 1);
				if (result != inventory_change_failure::Okay)
				{
					//inv.GetOwner().InventoryChangeFailure(result, item.get(), nullptr);
				}
				else
				{
					m_tookCastItem = true;
				}
			};

			for (auto& spell : item->GetEntry().spells())
			{
				// OnUse spell cast
				if (spell.spell() == m_spell.id() &&
					(spell.trigger() == item_spell_trigger::OnUse))
				{
					// Item is removed on use
					if (spell.charges() == static_cast<uint32>(-1))
					{
						if (delayed)
						{
							m_completedEffectsExecution[m_cast.GetExecuter().GetGuid()] = completedEffects.connect(removeItem);
						}
						else
						{
							removeItem();
						}
					}
					break;
				}
			}
		}

		return true;
	}

	bool SingleCastState::ConsumeReagents(bool delayed)
	{
		// Nothing to consume when proccing
		if (m_isProc)
		{
			return true;
		}

		return true;
	}

	bool SingleCastState::ConsumePower()
	{
		// Nothing to consume when proccing
		if (m_isProc)
		{
			return true;
		}

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
				if (totalCost > 0 && currentPower < totalCost)
				{
					SendEndCast(spell_cast_result::FailedNoPower);
					m_hasFinished = true;
					return false;
				}

				currentPower -= totalCost;
				m_cast.GetExecuter().Set<uint32>(object_fields::Mana + m_spell.powertype(), currentPower);

				// Mana has been used, modify mana regeneration counter
				if (m_spell.powertype() == power_type::Mana)
				{
					m_cast.GetExecuter().NotifyManaUsed();
				}
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
		const uint64 spellCatCD = m_spell.categorycooldown();
		const uint64 spellCD = m_spell.cooldown();

		GameTime finalCD = spellCD;
		if (finalCD == 0)
		{
			finalCD = spellCatCD;
		}

		if (finalCD)
		{
			// Modify spell cooldown by spell mods
			m_cast.GetExecuter().ApplySpellMod(spell_mod_op::Cooldown, m_spell.id(), finalCD);

			ApplyCooldown(finalCD, spellCatCD);
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
			{se::Dummy,					std::bind(&SingleCastState::SpellEffectDummy, this, std::placeholders::_1) },
			{se::InstantKill,				std::bind(&SingleCastState::SpellEffectInstantKill, this, std::placeholders::_1)},
			{se::PowerDrain,				std::bind(&SingleCastState::SpellEffectDrainPower, this, std::placeholders::_1)},
			{se::Heal,					std::bind(&SingleCastState::SpellEffectHeal, this, std::placeholders::_1)},
			{se::Bind,					std::bind(&SingleCastState::SpellEffectBind, this, std::placeholders::_1)},
			{se::QuestComplete,			std::bind(&SingleCastState::SpellEffectQuestComplete, this, std::placeholders::_1)},
			{se::WeaponDamageNoSchool,	std::bind(&SingleCastState::SpellEffectWeaponDamageNoSchool, this, std::placeholders::_1)},
			{se::CreateItem,				std::bind(&SingleCastState::SpellEffectCreateItem, this, std::placeholders::_1)},
			{se::WeaponDamage,			std::bind(&SingleCastState::SpellEffectWeaponDamage, this, std::placeholders::_1)},
			{se::TeleportUnits,			std::bind(&SingleCastState::SpellEffectTeleportUnits, this, std::placeholders::_1)},
			{se::Energize,				std::bind(&SingleCastState::SpellEffectEnergize, this, std::placeholders::_1)},
			{se::WeaponPercentDamage,		std::bind(&SingleCastState::SpellEffectWeaponPercentDamage, this, std::placeholders::_1)},
			{se::OpenLock,				std::bind(&SingleCastState::SpellEffectOpenLock, this, std::placeholders::_1)},
			{se::Dispel,					std::bind(&SingleCastState::SpellEffectDispel, this, std::placeholders::_1)},
			{se::Summon,					std::bind(&SingleCastState::SpellEffectSummon, this, std::placeholders::_1)},
			{se::SummonPet,				std::bind(&SingleCastState::SpellEffectSummonPet, this, std::placeholders::_1) },
			{se::LearnSpell,				std::bind(&SingleCastState::SpellEffectLearnSpell, this, std::placeholders::_1) },
			{se::Resurrect,				std::bind(&SingleCastState::SpellEffectResurrect, this, std::placeholders::_1) },
			{se::ApplyAura,				std::bind(&SingleCastState::SpellEffectApplyAura, this, std::placeholders::_1)},
			{se::ApplyAreaAura,			std::bind(&SingleCastState::SpellEffectApplyAura, this, std::placeholders::_1)},	// Area Auras are auras too, but have a special handling in the resulting aura container
			{se::PersistentAreaAura,		std::bind(&SingleCastState::SpellEffectPersistentAreaAura, this, std::placeholders::_1) },
			{se::SchoolDamage,			std::bind(&SingleCastState::SpellEffectSchoolDamage, this, std::placeholders::_1)},
			{se::ResetAttributePoints,	std::bind(&SingleCastState::SpellEffectResetAttributePoints, this, std::placeholders::_1)},
			{se::Parry,					std::bind(&SingleCastState::SpellEffectParry, this, std::placeholders::_1)},
			{se::Block,					std::bind(&SingleCastState::SpellEffectBlock, this, std::placeholders::_1)},
			{se::Dodge,					std::bind(&SingleCastState::SpellEffectDodge, this, std::placeholders::_1)},
			{se::HealPct,					std::bind(&SingleCastState::SpellEffectHealPct, this, std::placeholders::_1)},
			{se::AddExtraAttacks,			std::bind(&SingleCastState::SpellEffectAddExtraAttacks, this, std::placeholders::_1)},
			{se::Charge,					std::bind(&SingleCastState::SpellEffectCharge, this, std::placeholders::_1)}
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
		
		// Apply aura containers to their respective owners
		for(auto& [targetUnit, auraContainer] : m_targetAuraContainers)
		{
			targetUnit->ApplyAura(std::move(auraContainer));
		}

		ASSERT(m_spell.attributes_size() >= 2);
		if (m_spell.attributes(1) & spell_attributes_b::MeleeCombatStart)
		{
			GameUnitS* targetUnit = m_cast.GetExecuter().GetWorldInstance()->FindByGuid<GameUnitS>(m_target.GetUnitTarget());
			if (targetUnit && !m_cast.GetExecuter().UnitIsFriendly(*targetUnit) && targetUnit->IsAlive())
			{
				m_cast.GetExecuter().StartAttack(std::static_pointer_cast<GameUnitS>(targetUnit->shared_from_this()));
			}
		}

		// Clear auras
		m_targetAuraContainers.clear();

		completedEffects();

		if (strongCaster->IsUnit())
		{
			for (const auto& target : m_affectedTargets)
			{
				auto strongTarget = target.lock();
				if (strongTarget)
				{
					if (strongTarget->IsUnit())
					{
						std::static_pointer_cast<GameUnitS>(strongTarget)->RaiseTrigger(
							trigger_event::OnSpellHit, { m_spell.id() }, &m_cast.GetExecuter());
					}
				}
			}
		}
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

		int32 outBasePoints = basePoints + randomValue + comboDamage;

		// Apply spell base point modifications
		m_cast.GetExecuter().ApplySpellMod(spell_mod_op::AllEffects, m_spell.id(), outBasePoints);

		if (effect.type() == spell_effects::ApplyAura)
		{
			if (effect.aura() == aura_type::PeriodicDamage ||
				effect.aura() == aura_type::PeriodicHeal)
			{
				m_cast.GetExecuter().ApplySpellMod(spell_mod_op::PeriodicBasePoints, m_spell.id(), outBasePoints);

				// Also apply damage done bonus for now
				if (effect.aura() == aura_type::PeriodicDamage)
				{
					m_cast.GetExecuter().ApplySpellMod(spell_mod_op::Damage, m_spell.id(), outBasePoints);
				}
			}
		}
		
		return outBasePoints;
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
		auto unitTarget = GetEffectUnitTarget(effect);
		if (!unitTarget)
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
		std::vector<GameObjectS*> effectTargets;
		if (!GetEffectTargets(effect, effectTargets) || effectTargets.empty())
		{
			ELOG("Failed to cast spell effect: Unable to resolve effect targets");
			return;
		}

		for (auto* targetObject : effectTargets)
		{
			if (!targetObject->IsUnit())
			{
				continue;
			}

			auto& unitTarget = targetObject->AsUnit();

			// TODO: Do real calculation including crit chance, miss chance, resists, etc.
			uint32 damageAmount = std::max<int32>(0, CalculateEffectBasePoints(effect));
			m_cast.GetExecuter().ApplySpellMod(spell_mod_op::Damage, m_spell.id(), damageAmount);

			// Add spell power to damage
			const float spellDamage = m_cast.GetExecuter().GetCalculatedModifierValue(unit_mods::SpellDamage);
			if (spellDamage > 0.0f && effect.powerbonusfactor() > 0.0f)
			{
				damageAmount += static_cast<uint32>(spellDamage * effect.powerbonusfactor());
			}

			unitTarget.Damage(damageAmount, m_spell.spellschool(), &m_cast.GetExecuter(), damage_type::MagicalAbility);

			// Log spell damage to client
			m_cast.GetExecuter().SpellDamageLog(unitTarget.GetGuid(), damageAmount, m_spell.spellschool(), DamageFlags::None, m_spell);
		}
	}

	void SingleCastState::SpellEffectTeleportUnits(const proto::SpellEffect& effect)
	{
		std::vector<GameObjectS*> effectTargets;
		if (!GetEffectTargets(effect, effectTargets) || effectTargets.empty())
		{
			ELOG("Failed to cast spell effect: Unable to resolve effect targets");
			return;
		}

		for (auto* targetObject : effectTargets)
		{
			if (!targetObject->IsUnit())
			{
				continue;
			}

			auto& unitTarget = targetObject->AsUnit();
			uint32 targetMap;
			Vector3 targetLocation;
			Radian targetRotation;

			switch (effect.targetb())
			{
			case spell_effect_targets::DatabaseLocation:
				targetMap = m_spell.targetmap();
				targetLocation = { m_spell.targetx(), m_spell.targety(), m_spell.targetz() };
				targetRotation = { m_spell.targeto() };
				break;
			case spell_effect_targets::CasterLocation:
				targetMap = m_cast.GetExecuter().GetWorldInstance()->GetMapId();
				targetLocation = m_cast.GetExecuter().GetPosition();
				targetRotation = m_cast.GetExecuter().GetFacing();
				break;
			case spell_effect_targets::Home:
				targetMap = m_cast.GetExecuter().GetBindMap();
				targetLocation = m_cast.GetExecuter().GetBindPosition();
				targetRotation = m_cast.GetExecuter().GetBindFacing();
				break;
			default:
				WLOG("Unsupported teleport target location value for spell " << m_spell.id());
				return;
			}

			if (targetMap != m_cast.GetExecuter().GetWorldInstance()->GetMapId())
			{
				WLOG("TODO: Teleport to different map is not yet implemented!");
			}
			else
			{
				unitTarget.TeleportOnMap(targetLocation, targetRotation);
			}
		}
	}

	void SingleCastState::SpellEffectApplyAura(const proto::SpellEffect& effect)
	{
		// Okay, so we have an ApplyAura effect here. Here comes the thing:

		// One spell can apply multiple auras, either on the same target or even on multiple targets.
		// For example, a buff can apply two auras to the casting unit, which for example buffs the armor value and adds an on hit trigger effect to slow a potential attacker.

		// Both these "auras" would be displayed as one aura icon on the casting unit in the UI because its the same spell that triggered both these auras.
		// However, the game can also apply multiple auras. For example, a spell could apply one aura to the target and a second aura to the caster at the same time.
		// These would be considered two auras in the UI and on the client, because it's on two targets.

		// The same goes for auras that are applied to multiple targets at once, like an AoE HOT effect or something like that.

		// So what we want to do in this ApplyAuraEffect is to create one AuraContainer for each target that is affected and add an aura to that container.
		// After all effects have been processed, we then want to apply all created aura containers for their respective targets.

		std::vector<GameObjectS*> effectTargets;
		if (!GetEffectTargets(effect, effectTargets) || effectTargets.empty())
		{
			ELOG("Failed to cast spell effect: Unable to resolve effect targets");
			return;
		}

		for (auto* targetObject : effectTargets)
		{
			if (!targetObject->IsUnit())
			{
				continue;
			}

			m_affectedTargets.insert(targetObject->shared_from_this());
			auto& unitTarget = targetObject->AsUnit();

			// Threaten target on aura application if the target is not ourself
			if (unitTarget.GetGuid() != m_cast.GetExecuter().GetGuid())
			{
				unitTarget.threatened(m_cast.GetExecuter(), 0.0f);
			}

			auto& container = GetOrCreateAuraContainer(unitTarget);
			container.AddAuraEffect(effect, CalculateEffectBasePoints(effect));
		}
	}

	void SingleCastState::SpellEffectPersistentAreaAura(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectDrainPower(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectHeal(const proto::SpellEffect& effect)
	{
		std::vector<GameObjectS*> effectTargets;
		if (!GetEffectTargets(effect, effectTargets) || effectTargets.empty())
		{
			ELOG("Failed to cast spell effect: Unable to resolve effect targets");
			return;
		}

		for (auto* targetObject : effectTargets)
		{
			if (!targetObject->IsUnit())
			{
				continue;
			}

			m_affectedTargets.insert(targetObject->shared_from_this());
			auto& unitTarget = targetObject->AsUnit();

			// TODO: Do real calculation including crit chance, miss chance, resists, etc.
			uint32 healingAmount = std::max<int32>(0, CalculateEffectBasePoints(effect));

			// Add spell power to heal
			const float spellHealing = m_cast.GetExecuter().GetCalculatedModifierValue(unit_mods::HealingDone);
			if (spellHealing > 0.0f && effect.powerbonusfactor() > 0.0f)
			{
				healingAmount += static_cast<uint32>(spellHealing * effect.powerbonusfactor());
			}

			const float healingTakenBonus = unitTarget.GetCalculatedModifierValue(unit_mods::HealingTaken);
			if (healingTakenBonus > 0.0f || -healingTakenBonus < healingAmount)
			{
				healingAmount += static_cast<uint32>(healingTakenBonus);
			}
			else
			{
				healingAmount = 0;
			}

			unitTarget.Heal(healingAmount, &m_cast.GetExecuter());

			GameUnitS& caster = m_cast.GetExecuter();
			const uint32 spellId = m_spell.id();
			SendPacketFromCaster(m_cast.GetExecuter(),
				[&unitTarget, &caster, spellId, healingAmount](game::OutgoingPacket& out_packet)
				{
					out_packet.Start(game::realm_client_packet::SpellHealLog);
					out_packet
						<< io::write_packed_guid(unitTarget.GetGuid())
						<< io::write_packed_guid(caster.GetGuid())
						<< io::write<uint32>(spellId)
						<< io::write<uint32>(healingAmount)
						<< io::write<uint8>(false);
					out_packet.Finish();
				});
		}
	}

	void SingleCastState::SpellEffectBind(const proto::SpellEffect& effect)
	{
		std::vector<GameObjectS*> effectTargets;
		if (!GetEffectTargets(effect, effectTargets) || effectTargets.empty())
		{
			ELOG("Failed to cast spell effect: Unable to resolve effect targets");
			return;
		}

		for (auto* targetObject : effectTargets)
		{
			if (!targetObject->IsUnit())
			{
				continue;
			}

			m_affectedTargets.insert(targetObject->shared_from_this());
			auto& unitTarget = targetObject->AsUnit();
			unitTarget.SetBinding(
				unitTarget.GetWorldInstance()->GetMapId(),
				unitTarget.GetPosition(),
				unitTarget.GetFacing());
		}
	}

	void SingleCastState::SpellEffectQuestComplete(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectWeaponDamageNoSchool(const proto::SpellEffect& effect)
	{
		InternalSpellEffectWeaponDamage(effect, SpellSchool::Normal);
	}

	void SingleCastState::SpellEffectCreateItem(const proto::SpellEffect& effect)
	{
		// Get item entry
		const auto* item = m_cast.GetExecuter().GetProject().items.getById(effect.itemtype());
		if (!item)
		{
			ELOG("Could not find item by id " << effect.itemtype());
			return;
		}

		std::vector<GameObjectS*> effectTargets;
		if (!GetEffectTargets(effect, effectTargets) || effectTargets.empty())
		{
			ELOG("Failed to cast spell effect: Unable to resolve effect targets");
			return;
		}

		for (auto* targetObject : effectTargets)
		{
			if (!targetObject->IsPlayer())
			{
				continue;
			}

			m_affectedTargets.insert(targetObject->shared_from_this());
			auto& playerTarget = targetObject->AsPlayer();

			const int32 itemCount = CalculateEffectBasePoints(effect);
			if (itemCount <= 0)
			{
				WLOG("Effect base points of spell " << m_spell.id() << " resulted in <= 0, so no items could be created");
				return;
			}

			const InventoryChangeFailure result = playerTarget.GetInventory().CreateItems(*item, itemCount);
			if (result != inventory_change_failure::Okay)
			{
				ELOG("Failed to add item: " << result);
				return;
			}
		}
	}

	void SingleCastState::SpellEffectEnergize(const proto::SpellEffect& effect)
	{
		int32 powerType = effect.miscvaluea();
		if (powerType < 0 || powerType > 2) 
		{
			return;
		}

		std::vector<GameObjectS*> effectTargets;
		if (!GetEffectTargets(effect, effectTargets) || effectTargets.empty())
		{
			ELOG("Failed to cast spell effect: Unable to resolve effect targets");
			return;
		}

		for (auto* targetObject : effectTargets)
		{
			if (!targetObject->IsUnit())
			{
				continue;
			}

			m_affectedTargets.insert(targetObject->shared_from_this());
			auto& unitTarget = targetObject->AsUnit();
			uint32 power = CalculateEffectBasePoints(effect);

			uint32 curPower = unitTarget.Get<uint32>(object_fields::Mana + powerType);
			uint32 maxPower = unitTarget.Get<uint32>(object_fields::MaxMana + powerType);
			if (curPower + power > maxPower)
			{
				power = maxPower - curPower;
				curPower = maxPower;
			}
			else
			{
				curPower += power;
			}

			unitTarget.Set<uint32>(object_fields::Mana + powerType, curPower);

			GameUnitS& caster = m_cast.GetExecuter();
			const uint32 spellId = m_spell.id();
			SendPacketFromCaster(m_cast.GetExecuter(),
				[&unitTarget, &caster, spellId, powerType, power](game::OutgoingPacket& out_packet)
				{
					out_packet.Start(game::realm_client_packet::SpellEnergizeLog);
					out_packet
						<< io::write_packed_guid(unitTarget.GetGuid())
						<< io::write_packed_guid(caster.GetGuid())
						<< io::write<uint32>(spellId)
						<< io::write<uint32>(powerType)
						<< io::write<uint32>(power);
					out_packet.Finish();
				});
		}
	}

	void SingleCastState::SpellEffectWeaponPercentDamage(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectOpenLock(const proto::SpellEffect& effect)
	{
		if (!m_cast.GetExecuter().IsPlayer())
		{
			WLOG("Only players can open locks!");
			return;
		}

		std::vector<GameObjectS*> effectTargets;
		if (!GetEffectTargets(effect, effectTargets) || effectTargets.empty())
		{
			ELOG("Failed to cast spell effect: Unable to resolve effect targets");
			return;
		}

		for (auto* targetObject : effectTargets)
		{
			if (targetObject->IsWorldObject())
			{
				targetObject->AsObject().Use(m_cast.GetExecuter().AsPlayer());
			}
		}
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
		InternalSpellEffectWeaponDamage(effect, static_cast<SpellSchool>(m_spell.spellschool()));
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
		std::vector<GameObjectS*> effectTargets;
		if (!GetEffectTargets(effect, effectTargets) || effectTargets.empty())
		{
			ELOG("Failed to cast spell effect: Unable to resolve effect targets");
			return;
		}

		for (auto* targetObject : effectTargets)
		{
			if (!targetObject->IsUnit())
			{
				continue;
			}

			m_affectedTargets.insert(targetObject->shared_from_this());
			auto& unitTarget = targetObject->AsUnit();

			auto& mover = m_cast.GetExecuter().GetMover();

			const Radian orientation = unitTarget.GetAngle(m_cast.GetExecuter());

			const Vector3 target = unitTarget.GetMover().GetCurrentLocation().GetRelativePosition(orientation.GetValueRadians(), m_cast.GetExecuter().GetMeleeReach() * 0.5f);
			mover.MoveTo(target, 35.0f);
		}
	}

	void SingleCastState::SpellEffectAttackMe(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectNormalizedWeaponDamage(const proto::SpellEffect& effect)
	{
		InternalSpellEffectWeaponDamage(effect, static_cast<SpellSchool>(m_spell.spellschool()));
	}

	void SingleCastState::SpellEffectStealBeneficialBuff(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectInterruptCast(const proto::SpellEffect& effect)
	{
	}

	void SingleCastState::SpellEffectLearnSpell(const proto::SpellEffect& effect)
	{
		uint32 spellId = effect.triggerspell();
		if (!spellId)
		{
			ELOG("No spell index to learn set for spell id " << m_spell.id());
			return;
		}

		// Look for spell
		const auto* spell = m_cast.GetExecuter().GetProject().spells.getById(spellId);
		if (!spell)
		{
			ELOG("Unknown spell index to learn set for spell id " << m_spell.id() << ": " << spellId);
			return;
		}

		std::vector<GameObjectS*> effectTargets;
		if (!GetEffectTargets(effect, effectTargets) || effectTargets.empty())
		{
			ELOG("Failed to cast spell effect: Unable to resolve effect targets");
			return;
		}

		for (auto* targetObject : effectTargets)
		{
			if (!targetObject->IsUnit())
			{
				continue;
			}

			m_affectedTargets.insert(targetObject->shared_from_this());
			auto& unitTarget = targetObject->AsUnit();
			unitTarget.AddSpell(spellId);
		}
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

	void SingleCastState::SpellEffectResetAttributePoints(const proto::SpellEffect& effect)
	{
		auto unitTarget = GetEffectUnitTarget(effect);
		if (!unitTarget)
		{
			WLOG("Unable to resolve effect unit target!");
			return;
		}

		m_affectedTargets.insert(unitTarget->shared_from_this());

		if (const std::shared_ptr<GamePlayerS> playerTarget = std::dynamic_pointer_cast<GamePlayerS>(unitTarget))
		{
			DLOG("Resetting attribute points for player!");
			playerTarget->ResetAttributePoints();
		}
		else
		{
			WLOG("Target is not a player character!");
		}
	}

	void SingleCastState::SpellEffectParry(const proto::SpellEffect& effect)
	{
		std::vector<GameObjectS*> effectTargets;
		if (!GetEffectTargets(effect, effectTargets) || effectTargets.empty())
		{
			ELOG("Failed to cast spell effect: Unable to resolve effect targets");
			return;
		}

		for (auto* targetObject : effectTargets)
		{
			if (!targetObject->IsUnit())
			{
				continue;
			}

			m_affectedTargets.insert(targetObject->shared_from_this());
			auto& unitTarget = targetObject->AsUnit();
			unitTarget.NotifyCanParry(true);
		}
	}

	void SingleCastState::SpellEffectBlock(const proto::SpellEffect& effect)
	{
		std::vector<GameObjectS*> effectTargets;
		if (!GetEffectTargets(effect, effectTargets) || effectTargets.empty())
		{
			ELOG("Failed to cast spell effect: Unable to resolve effect targets");
			return;
		}

		for (auto* targetObject : effectTargets)
		{
			if (!targetObject->IsUnit())
			{
				continue;
			}

			m_affectedTargets.insert(targetObject->shared_from_this());
			auto& unitTarget = targetObject->AsUnit();
			unitTarget.NotifyCanBlock(true);
		}
	}

	void SingleCastState::SpellEffectDodge(const proto::SpellEffect& effect)
	{
		std::vector<GameObjectS*> effectTargets;
		if (!GetEffectTargets(effect, effectTargets) || effectTargets.empty())
		{
			ELOG("Failed to cast spell effect: Unable to resolve effect targets");
			return;
		}

		for (auto* targetObject : effectTargets)
		{
			if (!targetObject->IsUnit())
			{
				continue;
			}

			m_affectedTargets.insert(targetObject->shared_from_this());
			auto& unitTarget = targetObject->AsUnit();
			unitTarget.NotifyCanDodge(true);
		}
	}

	void SingleCastState::SpellEffectHealPct(const proto::SpellEffect& effect)
	{
		std::vector<GameObjectS*> effectTargets;
		if (!GetEffectTargets(effect, effectTargets) || effectTargets.empty())
		{
			ELOG("Failed to cast spell effect: Unable to resolve effect targets");
			return;
		}

		for (auto* targetObject : effectTargets)
		{
			if (!targetObject->IsUnit())
			{
				continue;
			}

			m_affectedTargets.insert(targetObject->shared_from_this());
			auto& unitTarget = targetObject->AsUnit();

			// TODO: Do real calculation including crit chance, miss chance, resists, etc.
			int32 basePoints = CalculateEffectBasePoints(effect);
			if (basePoints <= 0 || basePoints > 100)
			{
				WLOG("Spell " << m_spell.id() << " has invalid base points for spell Effect HealPct: " << basePoints << ". Will be clamped to 1-100.");
				return;
			}

			basePoints = Clamp(basePoints, 1, 100);

			const uint32 healAmount = static_cast<uint32>(floor(static_cast<float>(unitTarget.GetMaxHealth()) * (static_cast<float>(basePoints) / 100.0f)));
			unitTarget.Heal(healAmount, &m_cast.GetExecuter());

			// TODO: Heal log to show healing numbers at the clients
		}
	}

	void SingleCastState::SpellEffectAddExtraAttacks(const proto::SpellEffect& effect)
	{
		const int32 numAttacks = CalculateEffectBasePoints(effect);
		if (numAttacks <= 0)
		{
			WLOG("Unable to perform extra attacks, because base points of spell " << m_spell.id() << " rolled for " << numAttacks << " but have to be >= 1");
			return;
		}

		for (int32 i = 0; i < numAttacks; ++i)
		{
			m_affectedTargets.insert(m_cast.GetExecuter().shared_from_this());
			m_cast.GetExecuter().OnAttackSwing();
		}
	}

	bool SingleCastState::GetEffectTargets(const proto::SpellEffect& effect, std::vector<GameObjectS*>& targets)
	{
		const proto::RangeType* range = m_cast.GetExecuter().GetProject().ranges.getById(m_spell.rangetype());

		if (effect.targeta() == spell_effect_targets::Caster)
		{
			targets.push_back(&m_cast.GetExecuter());
			return true;
		}

		if (effect.targeta() == spell_effect_targets::ObjectTarget)
		{
			if (!m_target.HasGOTarget())
			{
				return false;
			}

			GameObjectS* target = m_cast.GetExecuter().GetWorldInstance()->FindObjectByGuid(m_target.GetGOTarget());
			if (!target)
			{
				return false;
			}

			targets.push_back(target);
			return true;
		}

		if (effect.targeta() == spell_effect_targets::CasterAreaParty ||
			effect.targeta() == spell_effect_targets::NearbyParty ||
			effect.targeta() == spell_effect_targets::NearbyAlly || 
			effect.targeta() == spell_effect_targets::NearbyEnemy)
		{
			// For these effects, the spell needs to have a range set!
			const auto& position = m_cast.GetExecuter().GetPosition();
			if (effect.radius() <= 0.0f)
			{
				ELOG("Spell " << m_spell.id() << " (" << m_spell.name() << ") effect has no radius >= 0 set");
				return false;
			}

			// Fast exit if looking for party members and caster is not in a party
			if (effect.targeta() == spell_effect_targets::CasterAreaParty ||
				effect.targeta() == spell_effect_targets::NearbyParty)
			{
				// Only players can be in a party
				if (!m_cast.GetExecuter().IsPlayer() || m_cast.GetExecuter().AsPlayer().GetGroupId() == 0)
				{
					targets.push_back(&m_cast.GetExecuter());
					return true;
				}
			}

			m_cast.GetExecuter().GetWorldInstance()->GetUnitFinder().FindUnits(Circle(position.x, position.z, effect.radius()), [this, &effect, &targets](GameUnitS& unit)
			{
				// Already too many targets
				if (m_spell.maxtargets() > 0 && targets.size() >= m_spell.maxtargets())
				{
					return true;
				}

				if (!(m_spell.attributes(0) & spell_attributes::CanTargetDead) && !unit.IsAlive())
				{
					return true;
				}

				// Looking for party members?
				if (effect.targeta() == spell_effect_targets::CasterAreaParty ||
					effect.targeta() == spell_effect_targets::NearbyParty)
				{
					// Only players can be in a party
					if (!unit.IsPlayer())
					{
						return true;
					}

					// In same party?
					if (unit.AsPlayer().GetGroupId() == m_cast.GetExecuter().AsPlayer().GetGroupId())
					{
						targets.push_back(&unit);
					}
				}
				else if (effect.targeta() == spell_effect_targets::NearbyAlly)
				{
					if (!m_cast.GetExecuter().UnitIsFriendly(unit))
					{
						return true;
					}
				}
				else if (effect.targeta() == spell_effect_targets::NearbyEnemy)
				{
					if (m_cast.GetExecuter().UnitIsFriendly(unit))
					{
						return true;
					}
				}

				return true;
			});

			return true;
		}

		if (effect.targeta() == spell_effect_targets::TargetAreaEnemy)
		{
			GameObjectS* targetObject = m_cast.GetExecuter().GetWorldInstance()->FindObjectByGuid(m_target.GetUnitTarget());
			if (!targetObject)
			{
				return false;
			}

			// For these effects, the spell needs to have a range set!
			const auto& position = targetObject->GetPosition();
			if (effect.radius() <= 0.0f)
			{
				ELOG("Spell " << m_spell.id() << " (" << m_spell.name() << ") effect has no radius >= 0 set");
				return false;
			}

			m_cast.GetExecuter().GetWorldInstance()->GetUnitFinder().FindUnits(Circle(position.x, position.z, effect.radius()), [this, &effect, &targets](GameUnitS& unit)
				{
					// Already too many targets
					if (m_spell.maxtargets() > 0 && targets.size() >= m_spell.maxtargets())
					{
						return true;
					}

					if (!(m_spell.attributes(0) & spell_attributes::CanTargetDead) && !unit.IsAlive())
					{
						return true;
					}

					if (m_cast.GetExecuter().UnitIsFriendly(unit))
					{
						return true;
					}

					targets.push_back(&unit);
					return true;
				});

			return true;
		}

		if (effect.targeta() == spell_effect_targets::TargetAlly || 
			effect.targeta() == spell_effect_targets::TargetAny ||
			effect.targeta() == spell_effect_targets::TargetEnemy)
		{
			GameObjectS* targetObject = m_cast.GetExecuter().GetWorldInstance()->FindObjectByGuid(m_target.GetUnitTarget());
			if (!targetObject)
			{
				return false;
			}

			const auto unit = std::dynamic_pointer_cast<GameUnitS>(targetObject->shared_from_this());
			if (unit)
			{
				switch (effect.targeta())
				{
				case spell_effect_targets::TargetAlly:
					// For now we consider all non-hostile units as allies
					if (m_cast.GetExecuter().UnitIsEnemy(*unit))
					{
						// Target has to be an ally but is not
						return false;
					}
					break;
				case spell_effect_targets::TargetEnemy:
					if (!m_cast.GetExecuter().UnitIsEnemy(*unit))
					{
						// Target has to be an enemy but is not
						return false;
					}
					break;
				}
			}

			targets.push_back(unit.get());
			return true;
		}

		return false;
	}

	void SingleCastState::InternalSpellEffectWeaponDamage(const proto::SpellEffect& effect, SpellSchool school)
	{
		auto unitTarget = GetEffectUnitTarget(effect);
		if (!unitTarget)
		{
			return;
		}

		m_affectedTargets.insert(unitTarget->shared_from_this());
		const float minDamage = m_cast.GetExecuter().Get<float>(object_fields::MinDamage);
		const float maxDamage = m_cast.GetExecuter().Get<float>(object_fields::MaxDamage);
		const uint32 casterLevel = m_cast.GetExecuter().Get<uint32>(object_fields::Level);

		const int32 bonus = CalculateEffectBasePoints(effect);

		// Calculate damage between minimum and maximum damage
		std::uniform_real_distribution distribution(minDamage + bonus, maxDamage + bonus + 1.0f);
		uint32 totalDamage = static_cast<uint32>(distribution(randomGenerator));

		// Physical damage is reduced by armor
		if (school == spell_school::Normal)
		{
			totalDamage = unitTarget->CalculateArmorReducedDamage(casterLevel, totalDamage);
		}

		m_cast.GetExecuter().ApplySpellMod(spell_mod_op::Damage, m_spell.id(), totalDamage);

		// TODO: Add stuff like immunities, miss chance, dodge, parry, glancing, crushing, crit, block, absorb etc.
		float critChance = 5.0f;			// 5% crit chance hard coded for now
		std::uniform_real_distribution critDistribution(0.0f, 100.0f);

		m_cast.GetExecuter().ApplySpellMod(spell_mod_op::CritChance, m_spell.id(), critChance);

		bool isCrit = false;
		if (critDistribution(randomGenerator) < critChance)
		{
			isCrit = true;
			totalDamage *= 2;
		}

		// Log spell damage to client
		unitTarget->Damage(totalDamage, school, &m_cast.GetExecuter(), damage_type::PhysicalAbility);
		m_cast.GetExecuter().SpellDamageLog(unitTarget->GetGuid(), totalDamage, school, isCrit ? DamageFlags::Crit : DamageFlags::None, m_spell);
	}

	AuraContainer& SingleCastState::GetOrCreateAuraContainer(GameUnitS& target)
	{
		if (m_targetAuraContainers.contains(&target))
		{
			return *m_targetAuraContainers[&target];
		}

		GameTime duration = m_spell.duration();
		if (duration != 0)	// Infinite duration is infinite, nothing to modify here!
		{
			m_cast.GetExecuter().ApplySpellMod(spell_mod_op::Duration, m_spell.id(), duration);
		}
		
		auto& container = (m_targetAuraContainers[&target] = std::make_unique<AuraContainer>(target, m_cast.GetExecuter().GetGuid(), m_spell, duration, m_itemGuid));
		return *container;
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

		m_cast.GetExecuter().RaiseTrigger(trigger_event::OnSpellCast, { m_spell.id() }, &m_cast.GetExecuter());

		if (!IsChanneled())
		{
			// may destroy this, too
			m_casting.ended(true);
		}
	}

	void SingleCastState::OnTargetKilled(GameUnitS*)
	{
		StopCast(spell_interrupt_flags::Any);
	}

	void SingleCastState::OnTargetDespawned(GameObjectS&)
	{
		StopCast(spell_interrupt_flags::Any);
	}

	void SingleCastState::OnUserDamaged()
	{

	}

	void SingleCastState::ExecuteMeleeAttack()
	{
	}
}
