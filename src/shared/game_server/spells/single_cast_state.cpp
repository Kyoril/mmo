// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "single_cast_state.h"

#include "game_server/objects/game_item_s.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/objects/game_world_object_s.h"
#include "game_server/spells/no_cast_state.h"

#include "base/utilities.h"
#include "proto_data/project.h"

#include <unordered_map>

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
		, m_context(cast, spell, m_target)
		, m_targetResolver(m_context)
	{
		m_effectTargetsScratch.reserve(8);

		// Check if the executor is in the world
		auto& executor = m_cast.GetExecuter();
		const auto* worldInstance = m_context.GetWorldInstance();

		// Apply cast time modifier
		executor.ApplySpellMod<int32>(spell_mod_op::CastTime, spell.id(), reinterpret_cast<int32&>(m_castTime));

		// This is a hack because cast time might become actually negative by modifiers which would be bad here!
		if (static_cast<int32>(m_castTime) < 0)
		{
			m_castTime = 0;
		}
		
		auto const casterId = executor.GetGuid();
		const uint32 startCooldownMs = ShouldStartCooldownOnCastStart() ? static_cast<uint32>(CalculateFinalCooldown()) : 0;

		if (worldInstance && !(m_spell.attributes(0) & spell_attributes::Passive) && !m_isProc && m_castTime > 0 && !IsChanneled())
		{
			m_context.SendPacketFromCaster(
								[casterId, startCooldownMs, this](game::OutgoingPacket& out_packet)
				{
					out_packet.Start(game::realm_client_packet::SpellStart);
					out_packet
						<< io::write_packed_guid(casterId)
						<< io::write<uint32>(m_spell.id())
						<< io::write<GameTime>(m_castTime)
						<< m_target
						<< io::write<uint32>(startCooldownMs);
					out_packet.Finish();
				});
		}

		// Note: ChannelStart packet is sent in Activate() after validation succeeds,
		// to avoid sending it for spells that will fail range or other checks.

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
		m_selfHold = shared_from_this();

		if (!Validate())
		{
			ELOG("Validation failed");
			m_hasFinished = true;
			NotifyCastEnded(false);
			return;
		}

		if (ShouldStartCooldownOnCastStart())
		{
			m_cooldownStartedAtCastStartMs = CalculateFinalCooldown();
			if (m_cooldownStartedAtCastStartMs > 0)
			{
				ApplyCooldown(m_cooldownStartedAtCastStartMs, m_spell.categorycooldown());
				m_cooldownStartedOnCastStart = true;
			}
		}

		ConnectTargetSignals(ResolveUnitTarget());

		if (m_castTime > 0)
		{
			m_castEnd = GetAsyncTimeMs() + m_castTime;
			m_countdown.SetEnd(m_castEnd);

			// For channeled spells, apply effects immediately when the channel starts
			if (IsChanneled())
			{
				// Send ChannelStart now that validation has passed
				auto* worldInstance = m_context.GetWorldInstance();
				if (worldInstance)
				{
					const uint64 casterId = m_cast.GetExecuter().GetGuid();
					m_context.SendPacketFromCaster(
						[casterId, this](game::OutgoingPacket& out_packet)
						{
							out_packet.Start(game::realm_client_packet::ChannelStart);
							out_packet
								<< io::write_packed_guid(casterId)
								<< io::write<uint32>(m_spell.id())
								<< io::write<int32>(m_castTime);
							out_packet.Finish();
						}
					);
				}

				if (!ConsumePower())
				{
					m_countdown.Cancel();
					EndChanneling(false);
					return;
				}

				if (!ConsumeReagents())
				{
					m_countdown.Cancel();
					EndChanneling(false);
					return;
				}

				if (!ConsumeItem())
				{
					m_countdown.Cancel();
					EndChanneling(false);
					return;
				}

				// Send SpellGo packet without ending the cast (channel continues)
				auto& executer = m_cast.GetExecuter();
				auto* world = m_context.GetWorldInstance();
				if (world && !(m_spell.attributes(0) & spell_attributes::Passive))
				{
					const uint64 chanCasterId = executer.GetGuid();
					const uint32 chanSpellId = m_spell.id();
					SpellTargetMap targetMap = m_target;
					if (targetMap.GetTargetMap() == spell_cast_target_flags::Self)
					{
						targetMap.SetTargetMap(spell_cast_target_flags::Unit);
						targetMap.SetUnitTarget(executer.GetGuid());
					}

					const GameTime cooldownMs = m_cooldownStartedOnCastStart ? 0 : CalculateFinalCooldown();

					m_context.SendPacketFromCaster(
						[chanCasterId, chanSpellId, &targetMap, cooldownMs](game::OutgoingPacket& out_packet)
						{
							out_packet.Start(game::realm_client_packet::SpellGo);
							out_packet
								<< io::write_packed_guid(chanCasterId)
								<< io::write<uint32>(chanSpellId)
								<< io::write<GameTime>(GetAsyncTimeMs())
								<< targetMap
								<< io::write<uint32>(cooldownMs);
							out_packet.Finish();
						});
				}

				ApplyAllEffects();
				m_cast.GetExecuter().RaiseTrigger(trigger_event::OnSpellCast, { m_spell.id() }, &m_cast.GetExecuter());
			}
		}
		else
		{
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
			itemGuid,
			false);

		return std::make_pair(spell_cast_result::CastOkay, &casting);
	}

	void SingleCastState::StopCast(SpellInterruptFlags reason, const GameTime interruptCooldown)
	{
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

		if (IsChanneled())
		{
			EndChanneling(false);
		}

		m_countdown.Cancel();
		SendEndCast(result);
		m_hasFinished = true;

		if (interruptCooldown)
		{
			ApplyCooldown(interruptCooldown, interruptCooldown);
		}

		const std::weak_ptr weakThis = shared_from_this();

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
		EndChanneling(true);
	}

	bool SingleCastState::Validate()
	{
		// Risky: Is this really good?
		if (m_isProc)
		{
			return true;
		}

		if (!ValidatePlayerRequirements())
		{
			return false;
		}

		if (!ValidateCasterRequirements())
		{
			return false;
		}

		return ValidateTargetRequirements(ResolveUnitTarget());
	}

	bool SingleCastState::ValidatePlayerRequirements()
	{
		GamePlayerS* playerCaster = m_context.GetPlayerExecutor();
		if (!playerCaster)
		{
			return true;
		}

		if (m_spell.racemask() != 0 && !(m_spell.racemask() & (1 << (playerCaster->GetRaceEntry()->id() - 1))))
		{
			SendEndCast(spell_cast_result::FailedError);
			return false;
		}

		if (m_spell.classmask() != 0 && !(m_spell.classmask() & (1 << (playerCaster->GetClassEntry()->id() - 1))))
		{
			SendEndCast(spell_cast_result::FailedError);
			return false;
		}

		for (const auto& reagent : m_spell.reagents())
		{
			if (playerCaster->GetInventory().GetItemCount(reagent.item()) < reagent.count())
			{
				WLOG("Not enough items in inventory!");
				SendEndCast(spell_cast_result::FailedReagents);
				return false;
			}
		}

		return true;
	}

	bool SingleCastState::ValidateCasterRequirements()
	{
		if (m_spell.spelllevel() > 1 && static_cast<int32>(m_cast.GetExecuter().GetLevel()) < m_spell.spelllevel())
		{
			SendEndCast(spell_cast_result::FailedLevelRequirement);
			return false;
		}

		if (!m_cast.GetExecuter().IsAlive() && !HasAttributes(0, spell_attributes::CastableWhileDead))
		{
			SendEndCast(spell_cast_result::FailedCasterDead);
			return false;
		}

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

	bool SingleCastState::ValidateTargetRequirements(GameUnitS* unitTarget)
	{
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

		GamePlayerS* player = m_context.GetPlayerExecutor();
		if ((unitTarget || m_target.GetTargetMap() == spell_cast_target_flags::Self) && m_itemGuid && player)
		{
			GameUnitS* target = unitTarget ? unitTarget : &m_cast.GetExecuter();
			ASSERT(target);

			auto& inv = player->GetInventory();

			if (uint16 itemSlot; inv.FindItemByGUID(m_itemGuid, itemSlot))
			{
				const auto item = inv.GetItemAtSlot(itemSlot);
				ASSERT(item);

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
				m_cast.GetExecuter().ApplySpellMod(spell_mod_op::Range, m_spell.id(), range);

				if (m_cast.GetExecuter().GetSquaredDistanceTo(unitTarget->GetPosition(), true) > range * range)
				{
					SendEndCast(spell_cast_result::FailedOutOfRange);
					return false;
				}
			}
		}

		return true;
	}

	std::shared_ptr<GameUnitS> SingleCastState::GetEffectUnitTarget(const proto::SpellEffect& effect)
	{
		return m_targetResolver.ResolveEffectUnitTarget(effect);
	}

	bool SingleCastState::ConsumeItem(bool delayed)
	{
		if (m_tookCastItem && delayed)
		{
			return true;
		}
		
		GamePlayerS* character = m_context.GetPlayerExecutor();
		if (m_itemGuid != 0 && character)
		{
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
							m_completedEffectsExecution = completedEffects.connect(removeItem);
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

		if (m_tookReagents && delayed)
		{
			return true;
		}
		
		if (GamePlayerS* character = m_context.GetPlayerExecutor())
		{
			for (const auto& reagent : m_spell.reagents())
			{
				if (character->GetInventory().GetItemCount(reagent.item()) < reagent.count())
				{
					WLOG("Not enough items in inventory!");
					SendEndCast(spell_cast_result::FailedReagents);
					return false;
				}
			}

			// Now consume all reagents
			for (const auto& reagent : m_spell.reagents())
			{
				const auto* item = character->GetProject().items.getById(reagent.item());
				if (!item)
				{
					SendEndCast(spell_cast_result::FailedReagents);
					return false;
				}

				auto result = character->GetInventory().RemoveItems(*item, reagent.count());
				if (result != inventory_change_failure::Okay)
				{
					ELOG("Could not consume reagents: " << result);
					SendEndCast(spell_cast_result::FailedReagents);
					return false;
				}
			}
		}

		m_tookReagents = true;
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
		if (UsesGlobalCooldown())
		{
			if (cooldownTimeMs > 0)
			{
				m_cast.GetExecuter().SetGlobalCooldown(cooldownTimeMs);
			}
			return;
		}

		if (cooldownTimeMs > 0)
		{
			m_cast.GetExecuter().SetCooldown(m_spell.id(), cooldownTimeMs);
		}
		
		if (categoryCooldownTimeMs > 0)
		{
			m_cast.GetExecuter().SetSpellCategoryCooldown(m_spell.category(), categoryCooldownTimeMs);
		}
	}

	bool SingleCastState::ShouldStartCooldownOnCastStart() const
	{
		return !m_isProc
			&& m_castTime > 0
			&& (m_spell.cooldownflags() & spell_cooldown_flags::StartOnCastStart) != 0;
	}

	bool SingleCastState::UsesGlobalCooldown() const
	{
		return (m_spell.cooldownflags() & spell_cooldown_flags::UseGlobalCooldown) != 0;
	}

	void SingleCastState::NotifyCastEnded(bool succeeded)
	{
		if (m_endNotified)
		{
			return;
		}

		m_endNotified = true;
		m_casting.ended(succeeded);
		m_selfHold.reset();
	}

	void SingleCastState::EndChanneling(bool succeeded)
	{
		if (!IsChanneled())
		{
			return;
		}

		// Nothing to do twice.
		if (m_hasFinished)
		{
			return;
		}

		// Caster could have left the world
		const auto* world = m_context.GetWorldInstance();
		if (world)
		{
			const uint64 casterId = m_cast.GetExecuter().GetGuid();
			m_context.SendPacketFromCaster(
				[casterId](game::OutgoingPacket& out_packet)
				{
					out_packet.Start(game::realm_client_packet::ChannelUpdate);
					out_packet
						<< io::write_packed_guid(casterId)
						<< io::write<GameTime>(0);
					out_packet.Finish();
				});
		}

		//m_cast.GetExecuter().Set<uint64>(object_fields::ChannelObject, 0);
		//m_cast.GetExecuter().Set<uint32>(object_fields::ChannelSpell, 0);
		m_hasFinished = true;
		NotifyCastEnded(succeeded);
	}

	GameUnitS* SingleCastState::ResolveUnitTarget() const
	{
		return m_targetResolver.ResolveUnitTarget();
	}

	void SingleCastState::ConnectTargetSignals(GameUnitS* unitTarget)
	{
		if (!unitTarget)
		{
			return;
		}

		m_onTargetDied = unitTarget->killed.connect(this, &SingleCastState::OnTargetKilled);
		m_onTargetRemoved = unitTarget->despawned.connect(this, &SingleCastState::OnTargetDespawned);
	}

	GameTime SingleCastState::CalculateFinalCooldown() const
	{
		// No cooldown for procs
		if (m_isProc)
		{
			return 0;
		}

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
		}

		return finalCD;
	}

	void SingleCastState::ApplyAllEffects()
	{
		// Add spell cooldown if any - unless it was already started at cast start.
		const GameTime finalCD = CalculateFinalCooldown();
		if (finalCD && !m_cooldownStartedOnCastStart)
		{
			ApplyCooldown(finalCD, m_spell.categorycooldown());
		}

		m_canTrigger = true;

		// Make sure that the executer exists after all effects have been executed
		auto strongCaster = std::static_pointer_cast<GameUnitS>(m_cast.GetExecuter().shared_from_this());

		if (!m_delayedCast)
		{
			for (int index = 0; index < m_spell.effects_size(); ++index)
			{
				ApplyEffect(m_spell.effects(index));
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
			GameUnitS* targetUnit = m_context.FindUnitByGuid(m_target.GetUnitTarget());
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
			for (const uint64 targetGuid : m_affectedTargetGuids)
			{
				if (GameUnitS* targetUnit = m_context.FindUnitByGuid(targetGuid))
				{
					targetUnit->RaiseTrigger(trigger_event::OnSpellHit, { m_spell.id() }, &m_cast.GetExecuter());
				}
			}
		}
	}

	void SingleCastState::ApplyEffect(const proto::SpellEffect& effect)
	{
		namespace se = spell_effects;
		static const std::unordered_map<uint32, EffectHandler> kHandlers
		{
			{se::Dummy, &SingleCastState::SpellEffectDummy},
			{se::InstantKill, &SingleCastState::SpellEffectInstantKill},
			{se::PowerDrain, &SingleCastState::SpellEffectDrainPower},
			{se::Heal, &SingleCastState::SpellEffectHeal},
			{se::Bind, &SingleCastState::SpellEffectBind},
			{se::QuestComplete, &SingleCastState::SpellEffectQuestComplete},
			{se::WeaponDamageNoSchool, &SingleCastState::SpellEffectWeaponDamageNoSchool},
			{se::CreateItem, &SingleCastState::SpellEffectCreateItem},
			{se::WeaponDamage, &SingleCastState::SpellEffectWeaponDamage},
			{se::TeleportUnits, &SingleCastState::SpellEffectTeleportUnits},
			{se::Energize, &SingleCastState::SpellEffectEnergize},
			{se::WeaponPercentDamage, &SingleCastState::SpellEffectWeaponPercentDamage},
			{se::OpenLock, &SingleCastState::SpellEffectOpenLock},
			{se::Dispel, &SingleCastState::SpellEffectDispel},
			{se::Summon, &SingleCastState::SpellEffectSummon},
			{se::SummonPet, &SingleCastState::SpellEffectSummonPet},
			{se::LearnSpell, &SingleCastState::SpellEffectLearnSpell},
			{se::Resurrect, &SingleCastState::SpellEffectResurrect},
			{se::ApplyAura, &SingleCastState::SpellEffectApplyAura},
			{se::ApplyAreaAura, &SingleCastState::SpellEffectApplyAura},
			{se::PersistentAreaAura, &SingleCastState::SpellEffectPersistentAreaAura},
			{se::SchoolDamage, &SingleCastState::SpellEffectSchoolDamage},
			{se::EnvironmentalDamage, &SingleCastState::SpellEffectEnvironmentalDamage},
			{se::ResetAttributePoints, &SingleCastState::SpellEffectResetAttributePoints},
			{se::Parry, &SingleCastState::SpellEffectParry},
			{se::Block, &SingleCastState::SpellEffectBlock},
			{se::Dodge, &SingleCastState::SpellEffectDodge},
			{se::HealPct, &SingleCastState::SpellEffectHealPct},
			{se::AddExtraAttacks, &SingleCastState::SpellEffectAddExtraAttacks},
			{se::Charge, &SingleCastState::SpellEffectCharge},
			{se::InterruptSpellCast, &SingleCastState::SpellEffectInterruptSpellCast},
			{se::ResetTalents, &SingleCastState::SpellEffectResetTalents},
			{se::Proficiency, &SingleCastState::SpellEffectProficiency}
		};

		const auto it = kHandlers.find(effect.type());
		if (it == kHandlers.end())
		{
			return;
		}

		(this->*(it->second))(effect);
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
		auto& effectTargets = m_effectTargetsScratch;
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

			if (unitTarget.Damage(damageAmount, m_spell.spellschool(), &m_cast.GetExecuter(), damage_type::MagicalAbility) > 0)
			{
				float threat = static_cast<float>(damageAmount) * m_spell.threat_multiplier();
				m_cast.GetExecuter().ApplySpellMod(spell_mod_op::Threat, m_spell.id(), threat);
				unitTarget.threatened(m_cast.GetExecuter(), threat);
			}

			// Log spell damage to client
			m_cast.GetExecuter().SpellDamageLog(unitTarget.GetGuid(), damageAmount, m_spell.spellschool(), DamageFlags::None, m_spell);
			
			// Trigger proc events for spell damage
			m_cast.GetExecuter().TriggerProcEvent(spell_proc_flags::DoneSpellMagicDmgClassNeg, &unitTarget, damageAmount, proc_ex_flags::NormalHit, m_spell.spellschool(), false, m_spell.familyflags());
			unitTarget.TriggerProcEvent(spell_proc_flags::TakenSpellMagicDmgClassNeg, &m_cast.GetExecuter(), damageAmount, proc_ex_flags::NormalHit, m_spell.spellschool(), false, m_spell.familyflags());
		}
	}

	void SingleCastState::SpellEffectEnvironmentalDamage(const proto::SpellEffect& effect)
	{
		auto& effectTargets = m_effectTargetsScratch;
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

			// Environmental damage uses base points as a percentage of max HP
			const int32 basePoints = CalculateEffectBasePoints(effect);
			const uint32 maxHealth = unitTarget.GetMaxHealth();
			uint32 damageAmount = static_cast<uint32>(static_cast<float>(maxHealth) * static_cast<float>(basePoints) / 100.0f);
			if (damageAmount < 1)
			{
				damageAmount = 1;
			}

			// Apply as physical damage (school from spell)
			const uint32 actualDamage = unitTarget.Damage(damageAmount, m_spell.spellschool(), &m_cast.GetExecuter(), damage_type::PhysicalAbility);

			// Determine environmental damage type from miscvaluea (0=Fall, 1=Drowning, 2=Lava, 3=Fire)
			const auto envDamageType = static_cast<EnvironmentalDamageType>(effect.miscvaluea());

			// Send environmental damage log to the target's net watcher
			unitTarget.EnvironmentalDamageLog(unitTarget.GetGuid(), actualDamage, envDamageType);
		}
	}

	void SingleCastState::SpellEffectTeleportUnits(const proto::SpellEffect& effect)
	{
		auto& effectTargets = m_effectTargetsScratch;
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
				targetMap = m_context.GetWorldInstance()->GetMapId();
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

			if (targetMap != m_context.GetWorldInstance()->GetMapId())
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

		auto& effectTargets = m_effectTargetsScratch;
		if (!GetEffectTargets(effect, effectTargets) || effectTargets.empty())
		{
			ELOG("Failed to cast spell effect: Unable to resolve effect targets");
			return;
		}

		if (effectTargets.empty())
		{
			WLOG("No targets found to for spell " << m_spell.id() << " [" << m_spell.name() << "] effect " << effect.index());
			return;
		}

		for (auto* targetObject : effectTargets)
		{
			if (!targetObject->IsUnit())
			{
				continue;
			}

			MarkAffectedTarget(*targetObject);
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
		auto& effectTargets = m_effectTargetsScratch;
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

			MarkAffectedTarget(*targetObject);
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

			// Crit chance (TODO: Calculate crit chance)
			float critChance = 5.0f;		// Default crit chance for healing spells
			m_cast.GetExecuter().ApplySpellMod(spell_mod_op::CritChance, m_spell.id(), critChance);

			bool isCrit = false;
			if (critChance > 0.0f)
			{
				std::uniform_real_distribution<float> critDistribution(0.0f, 100.0f);
				if (critDistribution(randomGenerator) < critChance)
				{
					// Crit heal (double healing amount)
					healingAmount = static_cast<uint32>(healingAmount * 2.0f);
					isCrit = true;
				}
			}

			unitTarget.Heal(healingAmount, &m_cast.GetExecuter());

			uint32 procFlags = proc_ex_flags::NormalHit;
			if (isCrit)
			{
				procFlags |= proc_ex_flags::CriticalHit;
			}

			// Trigger proc events for healing
			m_cast.GetExecuter().TriggerProcEvent(spell_proc_flags::DoneSpellMagicDmgClassPos, &unitTarget, healingAmount, procFlags, m_spell.spellschool(), false, m_spell.familyflags());
			unitTarget.TriggerProcEvent(spell_proc_flags::TakenSpellMagicDmgClassPos, &m_cast.GetExecuter(), healingAmount, procFlags, m_spell.spellschool(), false, m_spell.familyflags());

			GameUnitS& caster = m_cast.GetExecuter();
			const uint32 spellId = m_spell.id();
			m_context.SendPacketFromCaster(
				[&unitTarget, &caster, spellId, healingAmount, isCrit](game::OutgoingPacket& out_packet)
				{
					out_packet.Start(game::realm_client_packet::SpellHealLog);
					out_packet
						<< io::write_packed_guid(unitTarget.GetGuid())
						<< io::write_packed_guid(caster.GetGuid())
						<< io::write<uint32>(spellId)
						<< io::write<uint32>(healingAmount)
						<< io::write<uint8>(isCrit);
					out_packet.Finish();
				});
		}
	}

	void SingleCastState::SpellEffectBind(const proto::SpellEffect& effect)
	{
		auto& effectTargets = m_effectTargetsScratch;
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

			MarkAffectedTarget(*targetObject);
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

		auto& effectTargets = m_effectTargetsScratch;
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

			MarkAffectedTarget(*targetObject);
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

		auto& effectTargets = m_effectTargetsScratch;
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

			MarkAffectedTarget(*targetObject);
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
			m_context.SendPacketFromCaster(
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
		GamePlayerS* playerCaster = m_context.GetPlayerExecutor();
		if (!playerCaster)
		{
			WLOG("Only players can open locks!");
			return;
		}

		auto& effectTargets = m_effectTargetsScratch;
		if (!GetEffectTargets(effect, effectTargets) || effectTargets.empty())
		{
			ELOG("Failed to cast spell effect: Unable to resolve effect targets");
			return;
		}

		for (auto* targetObject : effectTargets)
		{
			if (targetObject->IsWorldObject())
			{
				targetObject->AsObject().Use(*playerCaster);
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
		auto& effectTargets = m_effectTargetsScratch;
		if (!GetEffectTargets(effect, effectTargets) || effectTargets.empty())
		{
			ELOG("Failed to cast spell effect: Unable to resolve effect targets");
			return;
		}

		// Get proficiency ID from the effect's miscvaluea field
		const uint32 proficiencyId = effect.miscvaluea();
		if (proficiencyId == 0)
		{
			WLOG("Spell " << m_spell.id() << " has Proficiency effect with no proficiency ID");
			return;
		}

		for (auto* targetObject : effectTargets)
		{
			if (!targetObject->IsUnit())
			{
				continue;
			}

			MarkAffectedTarget(*targetObject);
			auto& unitTarget = targetObject->AsUnit();

			// Add the proficiency by ID
			unitTarget.AddProficiency(proficiencyId);
		}
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
		auto& effectTargets = m_effectTargetsScratch;
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

			MarkAffectedTarget(*targetObject);
			auto& unitTarget = targetObject->AsUnit();

			auto& mover = m_cast.GetExecuter().GetMover();

			const Radian orientation = unitTarget.GetAngle(m_cast.GetExecuter());

			const Vector3 target = unitTarget.GetMover().GetCurrentLocation().GetRelativePosition(orientation.GetValueRadians(), m_cast.GetExecuter().GetMeleeReach() * 0.5f);
			mover.MoveTo(target, 35.0f, m_cast.GetExecuter().GetMeleeReach() * 0.8f);
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

	void SingleCastState::SpellEffectInterruptSpellCast(const proto::SpellEffect& effect)
	{
		auto& effectTargets = m_effectTargetsScratch;
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

			// Interrupt spell cast (and apply cooldown)
			targetObject->AsUnit().CancelCast(spell_interrupt_flags::Interrupt, m_spell.duration());
		}
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

		auto& effectTargets = m_effectTargetsScratch;
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

			MarkAffectedTarget(*targetObject);
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

		MarkAffectedTarget(*unitTarget);

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
		auto& effectTargets = m_effectTargetsScratch;
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

			MarkAffectedTarget(*targetObject);
			auto& unitTarget = targetObject->AsUnit();
			unitTarget.NotifyCanParry(true);
		}
	}

	void SingleCastState::SpellEffectBlock(const proto::SpellEffect& effect)
	{
		auto& effectTargets = m_effectTargetsScratch;
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

			MarkAffectedTarget(*targetObject);
			auto& unitTarget = targetObject->AsUnit();
			unitTarget.NotifyCanBlock(true);
		}
	}

	void SingleCastState::SpellEffectDodge(const proto::SpellEffect& effect)
	{
		auto& effectTargets = m_effectTargetsScratch;
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

			MarkAffectedTarget(*targetObject);
			auto& unitTarget = targetObject->AsUnit();
			unitTarget.NotifyCanDodge(true);
		}
	}

	void SingleCastState::SpellEffectHealPct(const proto::SpellEffect& effect)
	{
		auto& effectTargets = m_effectTargetsScratch;
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

			MarkAffectedTarget(*targetObject);
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
			MarkAffectedTarget(m_cast.GetExecuter());
			m_cast.GetExecuter().OnAttackSwing();
		}
	}

	void SingleCastState::SpellEffectResetTalents(const proto::SpellEffect& effect)
	{
		auto& effectTargets = m_effectTargetsScratch;
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

			MarkAffectedTarget(*targetObject);

			if (targetObject->IsPlayer())
			{
				targetObject->AsPlayer().ResetTalents();
			}
			else
			{
				WLOG("Target is not a player character!");
			}
		}
	}

	bool SingleCastState::GetEffectTargets(const proto::SpellEffect& effect, std::vector<GameObjectS*>& targets)
	{
		return m_targetResolver.ResolveEffectTargets(effect, targets);
	}

	void SingleCastState::MarkAffectedTarget(GameObjectS& target)
	{
		m_affectedTargetGuids.insert(target.GetGuid());
	}

	void SingleCastState::InternalSpellEffectWeaponDamage(const proto::SpellEffect& effect, SpellSchool school)
	{
		auto unitTarget = GetEffectUnitTarget(effect);
		if (!unitTarget)
		{
			return;
		}

		MarkAffectedTarget(*unitTarget);

		auto& executer = m_cast.GetExecuter();
		const float minDamage = executer.Get<float>(object_fields::MinDamage);
		const float maxDamage = executer.Get<float>(object_fields::MaxDamage);
		const uint32 casterLevel = executer.Get<uint32>(object_fields::Level);
		const int32 bonus = CalculateEffectBasePoints(effect);
		const auto& combatSettings = executer.GetCombatSettings();

		// Auto-attack spells (AutoRepeat) use the full melee combat table and send
		// AttackerStateUpdate (white damage text) instead of SpellDamageLog (yellow).
		const bool isAutoAttack = (m_spell.attributes(1) & spell_attributes_b::AutoRepeat) != 0;

		if (isAutoAttack)
		{
			// Roll the full melee combat table
			const MeleeAttackOutcome outcome = executer.RollMeleeOutcomeAgainst(*unitTarget, weapon_attack::BaseAttack);

			// Calculate base weapon damage
			std::uniform_real_distribution distribution(minDamage + bonus, maxDamage + bonus + 1.0f);
			uint32 totalDamage = static_cast<uint32>(distribution(randomGenerator));

			uint32 hitInfo = hit_info::NormalSwing;
			uint32 victimState = victim_state::Normal;
			bool hit = true;

			switch (outcome)
			{
			case melee_attack_outcome::Crit:
				hitInfo |= hit_info::CriticalHit;
				totalDamage = static_cast<uint32>(totalDamage * combatSettings.melee_crit_multiplier());
				break;

			case melee_attack_outcome::Crushing:
				hitInfo |= hit_info::Crushing;
				totalDamage = static_cast<uint32>(totalDamage * combatSettings.crushing_damage_multiplier());
				break;

			case melee_attack_outcome::Glancing:
				hitInfo |= hit_info::Glancing;
				{
					const int32 skillDiff = unitTarget->GetMaxSkillValueForLevel(unitTarget->GetLevel()) - executer.GetMaxSkillValueForLevel(casterLevel);
					float damageReduction = std::min(combatSettings.glancing_max_reduction(), static_cast<float>(skillDiff) * combatSettings.glancing_reduction_per_skill_diff());
					float glancingMod = 1.0f - (damageReduction / 100.0f);
					totalDamage = static_cast<uint32>(totalDamage * glancingMod);
				}
				break;

			case melee_attack_outcome::Miss:
				hitInfo |= hit_info::Miss;
				victimState = victim_state::Normal;
				hit = false;
				totalDamage = 0;
				break;

			case melee_attack_outcome::Parry:
				hitInfo |= hit_info::Miss;
				victimState = victim_state::Parry;
				hit = false;
				totalDamage = 0;
				break;

			case melee_attack_outcome::Dodge:
				hitInfo |= hit_info::Miss;
				victimState = victim_state::Dodge;
				hit = false;
				totalDamage = 0;
				break;

			case melee_attack_outcome::Normal:
				break;

			default:
				break;
			}

			// Check for block after hit determination
			uint32 blockedDamage = 0;
			if (hit && unitTarget->CanBlock() && unitTarget->IsFacingTowards(executer))
			{
				std::uniform_real_distribution blockChanceDist(0.0f, 100.0f);
				if (blockChanceDist(randomGenerator) < unitTarget->BlockChance())
				{
					uint32 blockValue = combatSettings.default_block_value();
					blockedDamage = std::min(blockValue, totalDamage);
					totalDamage -= blockedDamage;

					hitInfo |= hit_info::Block;
					victimState = victim_state::Blocks;

					unitTarget->OnBlock();
				}
			}

			// Apply armor reduction for physical damage
			if (hit && totalDamage > 0 && school == spell_school::Normal)
			{
				totalDamage = unitTarget->CalculateArmorReducedDamage(casterLevel, totalDamage);
			}

			// Apply spell mods
			if (hit && totalDamage > 0)
			{
				executer.ApplySpellMod(spell_mod_op::Damage, m_spell.id(), totalDamage);
			}

			// Deal damage
			uint32 absorbedDamage = 0;
			if (hit && totalDamage > 0)
			{
				if (unitTarget->Damage(totalDamage, school, &executer, damage_type::AttackSwing) > 0)
				{
					unitTarget->threatened(executer, static_cast<float>(totalDamage));
				}
			}

			// Trigger defense events
			if (outcome == melee_attack_outcome::Parry)
			{
				unitTarget->OnParry();
			}
			else if (outcome == melee_attack_outcome::Dodge)
			{
				unitTarget->OnDodge();
			}

			// Send melee damage packet (white text) instead of SpellDamageLog (yellow text)
			executer.SendAttackerStateUpdate(unitTarget->GetGuid(), hitInfo, victimState, totalDamage, school, absorbedDamage, 0, blockedDamage);

			// Generate rage based on damage done
			if (totalDamage > 0 && executer.Get<uint32>(object_fields::PowerType) == power_type::Rage)
			{
				const float rageConversion = static_cast<float>(
					(combatSettings.rage_conversion_quadratic() * casterLevel * casterLevel) +
					combatSettings.rage_conversion_linear() * casterLevel) +
					combatSettings.rage_conversion_constant();
				const float addRage = (static_cast<float>(totalDamage) / rageConversion) * combatSettings.rage_damage_factor();
				executer.AddPower(power_type::Rage, addRage);
			}

			// Trigger melee auto-attack proc events
			if (hit)
			{
				uint32 procEx = 0;
				if (hitInfo & hit_info::CriticalHit)
				{
					procEx |= proc_ex_flags::CriticalHit;
				}
				else
				{
					procEx |= proc_ex_flags::NormalHit;
				}

				if (victimState == victim_state::Dodge)
				{
					procEx |= proc_ex_flags::Dodge;
				}
				else if (victimState == victim_state::Parry)
				{
					procEx |= proc_ex_flags::Parry;
				}
				else if (victimState == victim_state::Blocks)
				{
					procEx |= proc_ex_flags::Block;
				}

				executer.TriggerProcEvent(spell_proc_flags::DoneMeleeAutoAttack, unitTarget.get(), totalDamage, procEx, school, false);
				unitTarget->TriggerProcEvent(spell_proc_flags::TakenMeleeAutoAttack, &executer, totalDamage, procEx, school, false);
			}
		}
		else
		{
			// Regular weapon damage spell (e.g., Heroic Strike, Mortal Strike)
			// Uses simplified crit roll and SpellDamageLog

			// Calculate damage between minimum and maximum damage
			std::uniform_real_distribution distribution(minDamage + bonus, maxDamage + bonus + 1.0f);
			uint32 totalDamage = static_cast<uint32>(distribution(randomGenerator));

			// Physical damage is reduced by armor
			if (school == spell_school::Normal)
			{
				totalDamage = unitTarget->CalculateArmorReducedDamage(casterLevel, totalDamage);
			}

			executer.ApplySpellMod(spell_mod_op::Damage, m_spell.id(), totalDamage);

			// Get configurable crit chance and multiplier from combat settings
			float critChance = combatSettings.spell_weapon_default_crit_chance();
			std::uniform_real_distribution critDistribution(0.0f, 100.0f);

			executer.ApplySpellMod(spell_mod_op::CritChance, m_spell.id(), critChance);

			bool isCrit = false;
			if (critDistribution(randomGenerator) < critChance)
			{
				isCrit = true;
				totalDamage = static_cast<uint32>(totalDamage * combatSettings.spell_weapon_crit_multiplier());
			}

			// Log spell damage to client
			if (unitTarget->Damage(totalDamage, school, &executer, damage_type::PhysicalAbility) > 0)
			{
				float threat = static_cast<float>(totalDamage) * m_spell.threat_multiplier();
				executer.ApplySpellMod(spell_mod_op::Threat, m_spell.id(), threat);
				unitTarget->threatened(executer, threat);
			}
			executer.SpellDamageLog(unitTarget->GetGuid(), totalDamage, school, isCrit ? DamageFlags::Crit : DamageFlags::None, m_spell);

			// Trigger proc events for spell damage
			executer.TriggerProcEvent(spell_proc_flags::DoneSpellMeleeDmgClass, unitTarget.get(), totalDamage, proc_ex_flags::NormalHit, m_spell.spellschool(), false, m_spell.familyflags());
			unitTarget->TriggerProcEvent(spell_proc_flags::TakenSpellMeleeDmgClass, &executer, totalDamage, proc_ex_flags::NormalHit, m_spell.spellschool(), false, m_spell.familyflags());
		}
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
		auto* worldInstance = m_context.GetWorldInstance();

		NotifyCastEnded(result == spell_cast_result::CastOkay);

		if (!worldInstance || m_spell.attributes(0) & spell_attributes::Passive)
		{
			return;
		}

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

			// Cooldown has already been started at cast start for flagged spells.
			const GameTime cooldownMs = m_cooldownStartedOnCastStart ? 0 : CalculateFinalCooldown();

			m_context.SendPacketFromCaster(
				[casterId, spellId, &targetMap, cooldownMs](game::OutgoingPacket& out_packet)
				{
					out_packet.Start(game::realm_client_packet::SpellGo);
					out_packet
						<< io::write_packed_guid(casterId)
						<< io::write<uint32>(spellId)
						<< io::write<GameTime>(GetAsyncTimeMs())
						<< targetMap
						<< io::write<uint32>(cooldownMs);
					out_packet.Finish();
				});
		}
		else
		{
			// Rollback cast-start cooldown if the cast did not succeed.
			if (m_cooldownStartedOnCastStart)
			{
				if (UsesGlobalCooldown())
				{
					executer.SetGlobalCooldown(0);
				}
				else
				{
					executer.SetCooldown(m_spell.id(), 0);
				}
				m_cooldownStartedOnCastStart = false;
				m_cooldownStartedAtCastStartMs = 0;
			}

			m_context.SendPacketFromCaster(
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
			if (!m_context.GetWorldInstance())
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
				targetUnit = m_context.FindUnitByGuid(unitTargetGuid);
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




