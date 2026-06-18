// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "single_cast_state.h"

#include "game_server/objects/game_item_s.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/objects/game_world_object_s.h"
#include "game_server/spells/no_cast_state.h"
#include "game_server/spells/spell_effects.h"

#include "base/utilities.h"
#include "game_server/world/world_instance.h"
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
		, m_delayedCast(false)
		, m_itemGuid(itemGuid)
		, m_context(cast, spell, m_target)
		, m_targetResolver(m_context)
	{
		m_effectTargetsScratch.reserve(8);

		// Apply cast time modifier using a temporary so we avoid UB from reinterpret_cast on GameTime.
		auto& executor = m_cast.GetExecuter();
		int32 castTimeMod = static_cast<int32>(m_castTime);
		executor.ApplySpellMod<int32>(spell_mod_op::CastTime, spell.id(), castTimeMod);
		m_castTime = static_cast<GameTime>(std::max(0, castTimeMod));

		const Vector3& location = executor.GetPosition();
		m_x = location.x;
		m_y = location.y;
		m_z = location.z;

		m_countdown.ended.connect([this]()
			{
				this->OnCastFinished();
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

		// Trigger the global cooldown as soon as the cast begins. This covers both instant casts
		// (which finish immediately) and cast-time spells (where the GCD runs alongside the cast).
		const uint32 globalCooldownMs = static_cast<uint32>(TriggerGlobalCooldown());

		ConnectTargetSignals(ResolveUnitTarget());

		// Send SpellStart only after validation succeeds — avoids showing the cast bar for casts
		// that will immediately fail, which caused client-side flicker with the old constructor approach.
		if (m_context.GetWorldInstance() && !(m_spell.attributes(0) & spell_attributes::Passive) && !m_isProc && m_castTime > 0)
		{
			const uint64 casterId = m_cast.GetExecuter().GetGuid();
			// Determine the cooldown duration to communicate to the client at cast start.
			// For StartOnCastStart spells the cooldown is already ticking on the server, so send the raw duration.
			// For all other spells with a cooldown, send castTime + finalCooldown as a display preview: it starts
			// now and expires at exactly the same moment the post-cast cooldown will expire, so the UI shows the
			// spell's own sweep the whole time without a jarring GCD-then-swap visual glitch.
			uint32 startCooldownMs = 0;
			if (ShouldStartCooldownOnCastStart())
			{
				startCooldownMs = static_cast<uint32>(CalculateFinalCooldown());
			}
			else
			{
				const GameTime finalCd = CalculateFinalCooldown();
				if (finalCd > 0)
				{
					startCooldownMs = static_cast<uint32>(m_castTime + finalCd);
				}
			}
			m_context.SendPacketFromCaster(
				[casterId, startCooldownMs, globalCooldownMs, this](game::OutgoingPacket& out_packet)
				{
					out_packet.Start(game::realm_client_packet::SpellStart);
					out_packet
						<< io::write_packed_guid(casterId)
						<< io::write<uint32>(m_spell.id())
						<< io::write<GameTime>(m_castTime)
						<< m_target
						<< io::write<uint32>(startCooldownMs)
						<< io::write<uint32>(globalCooldownMs);
					out_packet.Finish();
				});
		}

		if (m_castTime > 0)
		{
			m_castEnd = GetAsyncTimeMs() + m_castTime;
			m_countdown.SetEnd(m_castEnd);

			// For channeled spells, apply effects immediately when the channel starts
			if (m_spell.attributes(0) & spell_attributes::Channeled)
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
					NotifyCastEnded(false);
					return;
				}

				if (!ConsumeReagents())
				{
					m_countdown.Cancel();
					NotifyCastEnded(false);
					return;
				}

				if (!ConsumeItem())
				{
					m_countdown.Cancel();
					NotifyCastEnded(false);
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

					// Channeled spells have a cast time, so the global cooldown was already sent
					// to the client via SpellStart. Avoid re-applying it here.
					m_context.SendPacketFromCaster(
						[chanCasterId, chanSpellId, &targetMap, cooldownMs](game::OutgoingPacket& out_packet)
						{
							out_packet.Start(game::realm_client_packet::SpellGo);
							out_packet
								<< io::write_packed_guid(chanCasterId)
								<< io::write<uint32>(chanSpellId)
								<< io::write<GameTime>(GetAsyncTimeMs())
								<< targetMap
								<< io::write<uint32>(cooldownMs)
								<< io::write<uint32>(0);
							out_packet.Finish();
						});
				}

				ApplyAllEffects();
				m_cast.GetExecuter().RaiseTrigger(trigger_event::OnSpellCast, { m_spell.id() }, &m_cast.GetExecuter());

				// Transition to ChannelingCastState which owns the countdown from here on.
				auto strongThis = shared_from_this();  // keep alive across SetState
				m_countdown.Cancel();                  // stop SingleCastState countdown
				const GameTime remaining = m_castEnd - GetAsyncTimeMs();
				m_cast.SetState(std::make_shared<ChannelingCastState>(
					m_cast, m_spell,
					m_context,
					remaining > 0 ? remaining : 0,
					std::move(m_onTargetDied),
					std::move(m_onTargetRemoved)));
				// strongThis drops here — SingleCastState dies after return
				return;
			}
		}
		else
		{
			OnCastFinished();
		}
	}

	SpellCastResult SingleCastState::StartCast(SpellCast& cast, const proto::SpellEntry& spell, const SpellTargetMap& target, const GameTime castTime, const bool doReplacePreviousCast, uint64 itemGuid)
	{
		if (!m_hasFinished && !doReplacePreviousCast)
		{
			return spell_cast_result::FailedSpellInProgress;
		}

		FinishChanneling();

		CastSpell(
			cast,
			spell,
			target,
			castTime,
			itemGuid,
			false);

		return spell_cast_result::CastOkay;
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

		m_countdown.Cancel();
		m_hasFinished = true;

		if (interruptCooldown)
		{
			ApplyCooldown(interruptCooldown, interruptCooldown);
		}

		// Notify client that the cast was cancelled. SendEndCast sends SpellFailure and
		// calls NotifyCastEnded (idempotent). This must happen before SetState so the
		// world instance and context are still reachable from this SingleCastState.
		SendEndCast(result);

		// Keep *this* alive across SetState: SetState activates the new NoCastState
		// which may drop the last external reference to this SingleCastState.
		auto strongThis = shared_from_this();
		m_cast.SetState(std::make_shared<NoCastState>());
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
		// No-op: FinishChanneling is now owned by ChannelingCastState after transition.
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

		if (m_cast.GetExecuter().IsStunned() && !(m_spell.attributes_size() >= 2 && (m_spell.attributes(1) & spell_attributes_b::UsableWhileStunned)))
		{
			SendEndCast(spell_cast_result::FailedInterrupted);
			return false;
		}

		if (m_cast.GetExecuter().IsSleeping() && !(m_spell.attributes_size() >= 2 && (m_spell.attributes(1) & spell_attributes_b::UsableWhileSleeping)))
		{
			SendEndCast(spell_cast_result::FailedInterrupted);
			return false;
		}

		if (m_cast.GetExecuter().IsFeared() && !(m_spell.attributes_size() >= 2 && (m_spell.attributes(1) & spell_attributes_b::UsableWhileFeared)))
		{
			SendEndCast(spell_cast_result::FailedInterrupted);
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

		// Line of sight check — skipped for self-targeted spells, passive spells,
		// and spells that explicitly carry the IgnoreLineOfSight attribute.
		const bool targetIsSelf = (unitTarget == &m_cast.GetExecuter());
		const bool isPassive    = (m_spell.attributes(0) & spell_attributes::Passive) != 0;
		const bool ignoresLos   = m_spell.attributes_size() >= 2 &&
		                          (m_spell.attributes(1) & spell_attributes_b::IgnoreLineOfSight) != 0;

		if (unitTarget && !targetIsSelf && !isPassive && !ignoresLos)
		{
			if (const WorldInstance* world = m_context.GetWorldInstance())
			{
				MapData* mapData = world->GetMapData();
				if (mapData && !mapData->IsInLineOfSight(m_cast.GetExecuter().GetPosition(),
				                                         unitTarget->GetPosition()))
				{
					SendEndCast(spell_cast_result::FailedLineOfSight);
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

			auto removeItem = [this, itemSlot]()
			{
				if (m_tookCastItem)
				{
					return;
				}

				// Re-look up the player and inventory here: the lambda may fire after ConsumeItem()
				// has returned, so capturing &inv by reference would be a dangling reference.
				GamePlayerS* owner = m_context.GetPlayerExecutor();
				if (!owner)
				{
					return;
				}

				auto& inv = owner->GetInventory();
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
							m_postEffectCallbacks.push_back(removeItem);
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

	bool SingleCastState::IsAffectedByGlobalCooldown() const
	{
		// Procs/triggered casts never trigger or respect the global cooldown. Every other spell
		// is affected unless it is explicitly flagged to opt out.
		return !m_isProc
			&& (m_spell.cooldownflags() & spell_cooldown_flags::NoGlobalCooldown) == 0;
	}

	GameTime SingleCastState::TriggerGlobalCooldown()
	{
		if (m_globalCooldownTriggered || !IsAffectedByGlobalCooldown())
		{
			return 0;
		}

		const GameTime gcd = m_cast.GetExecuter().GetGlobalCooldownDuration();
		if (gcd > 0)
		{
			m_cast.GetExecuter().SetGlobalCooldown(gcd);
		}

		m_globalCooldownTriggered = true;
		m_appliedGlobalCooldownMs = gcd;
		return gcd;
	}

	void SingleCastState::NotifyCastEnded(bool succeeded)
	{
		if (m_endNotified)
		{
			return;
		}

		m_endNotified = true;
		m_cast.ended(succeeded);
		m_selfHold.reset();
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
		
		// Apply aura containers to their respective owners. Lookup by GUID so stale raw
		// pointers from units that died during effect processing are handled gracefully.
		for (auto& [targetGuid, auraContainer] : m_targetAuraContainers)
		{
			GameUnitS* targetUnit = m_context.FindUnitByGuid(targetGuid);
			if (targetUnit)
			{
				targetUnit->ApplyAura(std::move(auraContainer));
			}
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

		{
			auto callbacks = std::move(m_postEffectCallbacks);
			for (auto& cb : callbacks) cb();
		}

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
		using FreeHandler = void(*)(SpellEffectContext&);
		static const std::unordered_map<uint32, FreeHandler> kHandlers
		{
			{se::Dummy,                  SpellEffects::HandleDummy},
			{se::InstantKill,            SpellEffects::HandleInstantKill},
			{se::PowerDrain,             SpellEffects::HandleDrainPower},
			{se::Heal,                   SpellEffects::HandleHeal},
			{se::Bind,                   SpellEffects::HandleBind},
			{se::QuestComplete,          SpellEffects::HandleQuestComplete},
			{se::WeaponDamageNoSchool,   SpellEffects::HandleWeaponDamageNoSchool},
			{se::CreateItem,             SpellEffects::HandleCreateItem},
			{se::WeaponDamage,           SpellEffects::HandleWeaponDamage},
			{se::TeleportUnits,          SpellEffects::HandleTeleportUnits},
			{se::Energize,               SpellEffects::HandleEnergize},
			{se::WeaponPercentDamage,    SpellEffects::HandleWeaponPercentDamage},
			{se::OpenLock,               SpellEffects::HandleOpenLock},
			{se::Dispel,                 SpellEffects::HandleDispel},
			{se::Summon,                 SpellEffects::HandleSummon},
			{se::SummonPet,              SpellEffects::HandleSummonPet},
			{se::LearnSpell,             SpellEffects::HandleLearnSpell},
			{se::Resurrect,              SpellEffects::HandleResurrect},
			{se::ApplyAura,              SpellEffects::HandleApplyAura},
			{se::ApplyAreaAura,          SpellEffects::HandleApplyAura},
			{se::PersistentAreaAura,     SpellEffects::HandlePersistentAreaAura},
			{se::SchoolDamage,           SpellEffects::HandleSchoolDamage},
			{se::EnvironmentalDamage,    SpellEffects::HandleEnvironmentalDamage},
			{se::ResetAttributePoints,   SpellEffects::HandleResetAttributePoints},
			{se::Parry,                  SpellEffects::HandleParry},
			{se::Block,                  SpellEffects::HandleBlock},
			{se::CriticalBlock,          SpellEffects::HandleCriticalBlock},
			{se::Dodge,                  SpellEffects::HandleDodge},
			{se::HealPct,                SpellEffects::HandleHealPct},
			{se::AddExtraAttacks,        SpellEffects::HandleAddExtraAttacks},
			{se::Charge,                 SpellEffects::HandleCharge},
			{se::InterruptSpellCast,     SpellEffects::HandleInterruptSpellCast},
			{se::ResetTalents,           SpellEffects::HandleResetTalents},
			{se::Proficiency,            SpellEffects::HandleProficiency},
			{se::TriggerSpell,           SpellEffects::HandleTriggerSpell},
		};

		const auto it = kHandlers.find(effect.type());
		if (it == kHandlers.end()) return;

		// Data-driven conditional gating: skip the effect entirely when its
		// condition (if any) is not satisfied. No targets are resolved and the
		// handler is never invoked.
		if (!EvaluateEffectCondition(effect))
		{
			return;
		}

		std::vector<GameObjectS*> targets;
		GetEffectTargets(effect, targets);
		SpellEffectContext ctx{
			m_context, effect, CalculateEffectBasePoints(effect), targets,
			[this](GameUnitS& u) -> AuraContainer& { return GetOrCreateAuraContainer(u); },
			[this](GameObjectS& o) { MarkAffectedTarget(o); }
		};
		it->second(ctx);
	}

	int32 SingleCastState::CalculateEffectBasePoints(const proto::SpellEffect& effect)
	{
		return static_cast<int32>(SpellEffects::CalculateBasePoints(m_context, effect));
	}

	uint32 SingleCastState::GetSpellPointsTotal(const proto::SpellEffect& effect, uint32 spellPower, uint32 bonusPct)
	{
		return 0;
	}

	void SingleCastState::MeleeSpecialAttack(const proto::SpellEffect& effect, bool basepointsArePct)
	{
		
	}


	bool SingleCastState::GetEffectTargets(const proto::SpellEffect& effect, std::vector<GameObjectS*>& targets)
	{
		return m_targetResolver.ResolveEffectTargets(effect, targets);
	}

	void SingleCastState::MarkAffectedTarget(GameObjectS& target)
	{
		m_affectedTargetGuids.insert(target.GetGuid());
	}

	bool SingleCastState::EvaluateEffectCondition(const proto::SpellEffect& effect)
	{
		const uint32 conditionType = effect.conditiontype();
		if (conditionType == spell_effect_condition::None)
		{
			return true;
		}

		// Resolve which unit the condition is evaluated against.
		GameUnitS* conditionUnit = nullptr;
		switch (effect.conditiontarget())
		{
		case spell_effect_condition_target::Caster:
			conditionUnit = &m_cast.GetExecuter();
			break;
		case spell_effect_condition_target::PrimaryTarget:
		default:
			conditionUnit = m_context.FindUnitByGuid(m_target.GetUnitTarget());
			break;
		}

		// A missing condition unit means the predicate cannot be satisfied; the
		// safest behaviour is to skip the gated effect.
		if (!conditionUnit)
		{
			return false;
		}

		const uint64 casterGuid = m_cast.GetExecuter().GetGuid();
		switch (conditionType)
		{
		case spell_effect_condition::TargetHasAuraFromCaster:
			return conditionUnit->HasAuraSpellFromCaster(effect.conditionvalue(), casterGuid);
		case spell_effect_condition::TargetMissingAuraFromCaster:
			return !conditionUnit->HasAuraSpellFromCaster(effect.conditionvalue(), casterGuid);
		default:
			// Unknown condition type - fail safe by not applying the effect so that
			// authored data which relies on an unimplemented predicate does not
			// silently behave as "always apply".
			WLOG("Spell " << m_spell.id() << " effect " << effect.index() << " uses unknown condition type " << conditionType);
			return false;
		}
	}

	void SingleCastState::InternalSpellEffectWeaponDamage(const proto::SpellEffect& effect, SpellSchool school)
	{
		std::vector<GameObjectS*> targets;
		GetEffectTargets(effect, targets);
		SpellEffectContext ctx{ m_context, effect, CalculateEffectBasePoints(effect), targets,
			[this](GameUnitS& u) -> AuraContainer& { return GetOrCreateAuraContainer(u); },
			[this](GameObjectS& o) { MarkAffectedTarget(o); } };
		SpellEffects::HandleInternalWeaponDamage(ctx, school);
	}

	AuraContainer& SingleCastState::GetOrCreateAuraContainer(GameUnitS& target)
	{
		const uint64 targetGuid = target.GetGuid();
		const auto it = m_targetAuraContainers.find(targetGuid);
		if (it != m_targetAuraContainers.end())
		{
			return *it->second;
		}

		GameTime duration = m_spell.duration();
		if (duration != 0)	// Infinite duration is infinite, nothing to modify here!
		{
			m_cast.GetExecuter().ApplySpellMod(spell_mod_op::Duration, m_spell.id(), duration);
		}

		auto& container = (m_targetAuraContainers[targetGuid] = std::make_unique<AuraContainer>(target, m_cast.GetExecuter().GetGuid(), m_spell, duration, m_itemGuid));
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

			// For cast-time spells the global cooldown was already sent via SpellStart; only
			// instant casts (no SpellStart) need to communicate it here.
			const uint32 globalCooldownMs = (m_castTime == 0) ? static_cast<uint32>(m_appliedGlobalCooldownMs) : 0;

			m_context.SendPacketFromCaster(
				[casterId, spellId, &targetMap, cooldownMs, globalCooldownMs](game::OutgoingPacket& out_packet)
				{
					out_packet.Start(game::realm_client_packet::SpellGo);
					out_packet
						<< io::write_packed_guid(casterId)
						<< io::write<uint32>(spellId)
						<< io::write<GameTime>(GetAsyncTimeMs())
						<< targetMap
						<< io::write<uint32>(cooldownMs)
						<< io::write<uint32>(globalCooldownMs);
					out_packet.Finish();
				});
		}
		else
		{
			// Rollback cast-start cooldown if the cast did not succeed.
			if (m_cooldownStartedOnCastStart)
			{
				executer.SetCooldown(m_spell.id(), 0);
				m_cooldownStartedOnCastStart = false;
				m_cooldownStartedAtCastStartMs = 0;
			}

			// Roll back the global cooldown that was triggered when the cast started.
			if (m_globalCooldownTriggered)
			{
				executer.SetGlobalCooldown(0);
				m_globalCooldownTriggered = false;
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
				// Notify here so m_selfHold is released; without this the cast state leaks.
				NotifyCastEnded(false);
				return;
			}

			// TODO: Range check etc.
		}

		m_hasFinished = true;

		// Re-validate only the conditions that can change during a cast: target liveness and range.
		// Player/caster requirements were already checked in Activate() and cannot change mid-cast.
		if (!ValidateTargetRequirements(ResolveUnitTarget()))
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
		auto strongThis = shared_from_this();
		StopCast(spell_interrupt_flags::Any);
	}

	void SingleCastState::OnTargetDespawned(GameObjectS&)
	{
		auto strongThis = shared_from_this();
		StopCast(spell_interrupt_flags::Any);
	}

	void SingleCastState::OnUserDamaged()
	{

	}

	void SingleCastState::ExecuteMeleeAttack()
	{
	}
}




