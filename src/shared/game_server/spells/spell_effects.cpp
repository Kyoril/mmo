// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Real implementations of all spell-effect free functions.
// Handlers access SingleCastState state exclusively through SpellEffectContext.

#include "spell_effects.h"

#include "game_server/objects/game_item_s.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/objects/game_world_object_s.h"
#include "game_server/spells/aura_container.h"
#include "base/utilities.h"

#include "proto_data/project.h"
#include "world/universe.h"

#include <algorithm>

namespace mmo
{
	namespace SpellEffects
	{
		int32_t CalculateBasePoints(SpellCastContext& castCtx, const proto::SpellEffect& effect)
		{
			// TODO
			const int32_t comboPoints = 0;

			const proto::SpellEntry& spell = castCtx.GetSpell();
			GameUnitS& executer = castCtx.GetExecutor();

			int32_t level = static_cast<int32_t>(executer.Get<uint32>(object_fields::Level));
			if (level > spell.maxlevel() && spell.maxlevel() > 0)
			{
				level = spell.maxlevel();
			}
			else if (level < spell.baselevel())
			{
				level = spell.baselevel();
			}
			level -= spell.spelllevel();

			// Calculate the damage done
			const float basePointsPerLevel = effect.pointsperlevel();
			const float randomPointsPerLevel = effect.diceperlevel();
			const int32_t basePoints = effect.basepoints() + level * static_cast<int32_t>(basePointsPerLevel);
			const int32_t randomPoints = effect.diesides() + level * static_cast<int32_t>(randomPointsPerLevel);
			const int32_t comboDamage = static_cast<int32_t>(effect.pointspercombopoint()) * comboPoints;

			std::uniform_int_distribution<int> distribution(effect.basedice(), randomPoints);
			const int32_t randomValue = (effect.basedice() >= randomPoints ? effect.basedice() : distribution(randomGenerator));

			int32_t outBasePoints = basePoints + randomValue + comboDamage;

			// Apply spell base point modifications
			executer.ApplySpellMod(spell_mod_op::AllEffects, spell.id(), outBasePoints);

			if (effect.type() == spell_effects::ApplyAura)
			{
				if (effect.aura() == aura_type::PeriodicDamage ||
					effect.aura() == aura_type::PeriodicHeal)
				{
					executer.ApplySpellMod(spell_mod_op::PeriodicBasePoints, spell.id(), outBasePoints);

					// Also apply damage done bonus for now
					if (effect.aura() == aura_type::PeriodicDamage)
					{
						executer.ApplySpellMod(spell_mod_op::Damage, spell.id(), outBasePoints);
					}
				}
			}

			return outBasePoints;
		}

		void HandleInstantKill(SpellEffectContext& ctx)
		{
			if (ctx.effectTargets.empty())
			{
				return;
			}

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				targetObject->AsUnit().Kill(&ctx.castContext.GetExecutor());
			}
		}

		void HandleDummy(SpellEffectContext& /*ctx*/) {}

		void HandleSchoolDamage(SpellEffectContext& ctx)
		{
			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			const proto::SpellEntry& spell = ctx.castContext.GetSpell();
			GameUnitS& executer = ctx.castContext.GetExecutor();

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				auto& unitTarget = targetObject->AsUnit();

				// If the target is immune to this spell's damage school, report the immunity to
				// clients (so the floating combat text shows "Immune") and skip damage entirely.
				if (unitTarget.IsImmuneToSchool(spell.spellschool()))
				{
					executer.SpellDamageLog(unitTarget.GetGuid(), 0, spell.spellschool(), DamageFlags::Immune, spell);
					continue;
				}

				// TODO: Do real calculation including crit chance, miss chance, resists, etc.
				uint32 damageAmount = std::max<int32_t>(0, ctx.basePoints);
				executer.ApplySpellMod(spell_mod_op::Damage, spell.id(), damageAmount);

				// Add spell power to damage
				const float spellDamage = executer.GetCalculatedModifierValue(unit_mods::SpellDamage);
				if (spellDamage > 0.0f && ctx.effect.powerbonusfactor() > 0.0f)
				{
					damageAmount += static_cast<uint32>(spellDamage * ctx.effect.powerbonusfactor());
				}

				// Apply both Damage and SpellDamage mods to the damage amount!
				damageAmount = executer.CalculateModifiedValue(unit_mods::Damage, damageAmount);
				damageAmount = executer.CalculateModifiedValue(unit_mods::SpellDamage, damageAmount);

				// Roll for a block. Only physical damage from the front can be blocked; the helper
				// returns no block for other schools, so this is a no-op for magical spells.
				uint32 blockedDamage = 0;
				const BlockResult block = unitTarget.RollMeleeBlock(executer, static_cast<SpellSchool>(spell.spellschool()), damageAmount);
				if (block.blocked)
				{
					blockedDamage = block.blockedAmount;
					damageAmount -= blockedDamage;
					unitTarget.OnBlock();
				}

				damageAmount = static_cast<uint32>(static_cast<float>(damageAmount) * unitTarget.GetIncomingDamageTakenMultiplier(&executer, static_cast<SpellDmgClass>(spell.dmgclass())));

				if (unitTarget.Damage(damageAmount, spell.spellschool(), &executer, damage_type::MagicalAbility) > 0)
				{
					float threat = static_cast<float>(damageAmount) * spell.threat_multiplier();
					executer.ApplySpellMod(spell_mod_op::Threat, spell.id(), threat);
					unitTarget.threatened(executer, threat);
				}

				// Log spell damage to client
				executer.SpellDamageLog(unitTarget.GetGuid(), damageAmount, spell.spellschool(), block.blocked ? DamageFlags::Block : DamageFlags::None, spell, blockedDamage);

				// Trigger proc events for spell damage
				executer.TriggerProcEvent(spell_proc_flags::DoneSpellMagicDmgClassNeg, &unitTarget, damageAmount, proc_ex_flags::NormalHit, spell.spellschool(), false, spell.familyflags());
				unitTarget.TriggerProcEvent(spell_proc_flags::TakenSpellMagicDmgClassNeg, &executer, damageAmount, proc_ex_flags::NormalHit, spell.spellschool(), false, spell.familyflags());
			}
		}

		void HandleEnvironmentalDamage(SpellEffectContext& ctx)
		{
			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			const proto::SpellEntry& spell = ctx.castContext.GetSpell();
			GameUnitS& executer = ctx.castContext.GetExecutor();

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				auto& unitTarget = targetObject->AsUnit();

				// Environmental damage uses base points as a percentage of max HP
				const uint32 maxHealth = unitTarget.GetMaxHealth();
				uint32 damageAmount = static_cast<uint32>(static_cast<float>(maxHealth) * static_cast<float>(ctx.basePoints) / 100.0f);
				if (damageAmount < 1)
				{
					damageAmount = 1;
				}

				// Apply as physical damage (school from spell)
				const uint32 actualDamage = unitTarget.Damage(damageAmount, spell.spellschool(), &executer, damage_type::PhysicalAbility);

				// Determine environmental damage type from miscvaluea (0=Fall, 1=Drowning, 2=Lava, 3=Fire)
				const auto envDamageType = static_cast<EnvironmentalDamageType>(ctx.effect.miscvaluea());

				// Send environmental damage log to the target's net watcher
				unitTarget.EnvironmentalDamageLog(unitTarget.GetGuid(), actualDamage, envDamageType);
			}
		}

		void HandleTeleportUnits(SpellEffectContext& ctx)
		{
			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			const proto::SpellEntry& spell = ctx.castContext.GetSpell();
			GameUnitS& executer = ctx.castContext.GetExecutor();

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				auto& unitTarget = targetObject->AsUnit();
				uint32 targetMap;
				Vector3 targetLocation;
				Radian targetRotation;

				switch (ctx.effect.targetb())
				{
				case spell_effect_targets::DatabaseLocation:
					targetMap = spell.targetmap();
					targetLocation = { spell.targetx(), spell.targety(), spell.targetz() };
					targetRotation = { spell.targeto() };
					break;
				case spell_effect_targets::CasterLocation:
					targetMap = ctx.castContext.GetWorldInstance()->GetMapId();
					targetLocation = executer.GetPosition();
					targetRotation = executer.GetFacing();
					break;
				case spell_effect_targets::Home:
					targetMap = executer.GetBindMap();
					targetLocation = executer.GetBindPosition();
					targetRotation = executer.GetBindFacing();
					break;
				default:
					WLOG("Unsupported teleport target location value for spell " << spell.id());
					return;
				}

				if (targetMap != ctx.castContext.GetWorldInstance()->GetMapId())
				{
					WLOG("TODO: Teleport to different map is not yet implemented!");
				}
				else
				{
					unitTarget.TeleportOnMap(targetLocation, targetRotation);
				}
			}
		}

		void HandleApplyAura(SpellEffectContext& ctx)
		{
			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			const proto::SpellEntry& spell = ctx.castContext.GetSpell();
			GameUnitS& executer = ctx.castContext.GetExecutor();

			if (ctx.effectTargets.empty())
			{
				WLOG("No targets found for spell " << spell.id() << " [" << spell.name() << "] effect " << ctx.effect.index());
				return;
			}

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				ctx.markAffectedTarget(*targetObject);
				auto& unitTarget = targetObject->AsUnit();

				// Threaten target on aura application if the target is not ourself
				if (unitTarget.GetGuid() != executer.GetGuid())
				{
					unitTarget.threatened(executer, 0.0f);
				}

				auto& container = ctx.getOrCreateAuraContainer(unitTarget);
				container.AddAuraEffect(ctx.effect, ctx.basePoints);
			}
		}

		void HandlePersistentAreaAura(SpellEffectContext& /*ctx*/) {}

		void HandleDrainPower(SpellEffectContext& /*ctx*/) {}

		void HandleHeal(SpellEffectContext& ctx)
		{
			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			const proto::SpellEntry& spell = ctx.castContext.GetSpell();
			GameUnitS& executer = ctx.castContext.GetExecutor();

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				ctx.markAffectedTarget(*targetObject);
				auto& unitTarget = targetObject->AsUnit();

				// TODO: Do real calculation including crit chance, miss chance, resists, etc.
				uint32 healingAmount = std::max<int32_t>(0, ctx.basePoints);

				// Add spell power to heal
				const float spellHealing = executer.GetCalculatedModifierValue(unit_mods::HealingDone);
				if (spellHealing > 0.0f && ctx.effect.powerbonusfactor() > 0.0f)
				{
					healingAmount += static_cast<uint32>(spellHealing * ctx.effect.powerbonusfactor());
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
				executer.ApplySpellMod(spell_mod_op::CritChance, spell.id(), critChance);

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

				const int32 effectiveHealing = unitTarget.Heal(healingAmount, &executer);

				// Healing a unit that is under attack generates threat on its attackers and pulls
				// the healer into combat, just like dealing damage would.
				unitTarget.GenerateHealingThreat(executer, static_cast<uint32>(std::max<int32>(0, effectiveHealing)));

				uint32 procFlags = proc_ex_flags::NormalHit;
				if (isCrit)
				{
					procFlags |= proc_ex_flags::CriticalHit;
				}

				// Trigger proc events for healing
				executer.TriggerProcEvent(spell_proc_flags::DoneSpellMagicDmgClassPos, &unitTarget, healingAmount, procFlags, spell.spellschool(), false, spell.familyflags());
				unitTarget.TriggerProcEvent(spell_proc_flags::TakenSpellMagicDmgClassPos, &executer, healingAmount, procFlags, spell.spellschool(), false, spell.familyflags());

				const uint32 spellId = spell.id();
				ctx.castContext.SendPacketFromCaster(
					[&unitTarget, &executer, spellId, healingAmount, isCrit](game::OutgoingPacket& out_packet)
					{
						out_packet.Start(game::realm_client_packet::SpellHealLog);
						out_packet
							<< io::write_packed_guid(unitTarget.GetGuid())
							<< io::write_packed_guid(executer.GetGuid())
							<< io::write<uint32>(spellId)
							<< io::write<uint32>(healingAmount)
							<< io::write<uint8>(isCrit);
						out_packet.Finish();
					});
			}
		}

		void HandleBind(SpellEffectContext& ctx)
		{
			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				ctx.markAffectedTarget(*targetObject);
				auto& unitTarget = targetObject->AsUnit();
				unitTarget.SetBinding(
					unitTarget.GetWorldInstance()->GetMapId(),
					unitTarget.GetPosition(),
					unitTarget.GetFacing());
			}
		}

		void HandleQuestComplete(SpellEffectContext& /*ctx*/) {}

		void HandleWeaponDamageNoSchool(SpellEffectContext& ctx)
		{
			HandleInternalWeaponDamage(ctx, SpellSchool::Normal);
		}

		void HandleCreateItem(SpellEffectContext& ctx)
		{
			// Get item entry
			const auto* item = ctx.castContext.GetExecutor().GetProject().items.getById(ctx.effect.itemtype());
			if (!item)
			{
				ELOG("Could not find item by id " << ctx.effect.itemtype());
				return;
			}

			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			const proto::SpellEntry& spell = ctx.castContext.GetSpell();

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsPlayer())
				{
					continue;
				}

				ctx.markAffectedTarget(*targetObject);
				auto& playerTarget = targetObject->AsPlayer();

				const int32_t itemCount = ctx.basePoints;
				if (itemCount <= 0)
				{
					WLOG("Effect base points of spell " << spell.id() << " resulted in <= 0, so no items could be created");
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

		void HandleEnergize(SpellEffectContext& ctx)
		{
			int32_t powerType = ctx.effect.miscvaluea();
			if (powerType < 0 || powerType > 2)
			{
				return;
			}

			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			GameUnitS& executer = ctx.castContext.GetExecutor();
			const uint32 spellId = ctx.castContext.GetSpell().id();

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				ctx.markAffectedTarget(*targetObject);
				auto& unitTarget = targetObject->AsUnit();
				uint32 power = static_cast<uint32>(ctx.basePoints);

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

				const uint32 finalPower = power;
				const int32_t finalPowerType = powerType;
				ctx.castContext.SendPacketFromCaster(
					[&unitTarget, &executer, spellId, finalPowerType, finalPower](game::OutgoingPacket& out_packet)
					{
						out_packet.Start(game::realm_client_packet::SpellEnergizeLog);
						out_packet
							<< io::write_packed_guid(unitTarget.GetGuid())
							<< io::write_packed_guid(executer.GetGuid())
							<< io::write<uint32>(spellId)
							<< io::write<uint32>(finalPowerType)
							<< io::write<uint32>(finalPower);
						out_packet.Finish();
					});
			}
		}

		void HandleWeaponPercentDamage(SpellEffectContext& ctx)
		{
			HandleInternalWeaponDamage(ctx, static_cast<SpellSchool>(ctx.castContext.GetSpell().spellschool()), true);
		}

		void HandleOpenLock(SpellEffectContext& ctx)
		{
			GamePlayerS* playerCaster = ctx.castContext.GetPlayerExecutor();
			if (!playerCaster)
			{
				WLOG("Only players can open locks!");
				return;
			}

			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			const uint32 spellLockTypeId = static_cast<uint32>(ctx.effect.miscvaluea());

			for (auto* targetObject : ctx.effectTargets)
			{
				if (targetObject->IsWorldObject())
				{
					const uint32 objectLockTypeId = targetObject->AsObject().Get<uint32>(object_fields::LockEntry);
					if (objectLockTypeId != 0 && objectLockTypeId != spellLockTypeId)
					{
						WLOG("OpenLock: lock type mismatch — spell requires " << spellLockTypeId << ", object has " << objectLockTypeId);
						continue;
					}

					targetObject->AsObject().Use(*playerCaster);

					// One-time door unlock: if data[1] is set, change the active lock type
					auto* worldObj = static_cast<GameWorldObjectS*>(targetObject);
					const uint32 postUnlockLockType = worldObj->GetPostUnlockLockType();
					if (postUnlockLockType != 0)
					{
						worldObj->Set<uint32>(object_fields::LockEntry, postUnlockLockType);
					}
				}
			}
		}

		void HandleApplyAreaAuraParty(SpellEffectContext& /*ctx*/) {}

		void HandleDispel(SpellEffectContext& /*ctx*/) {}

		void HandleSummon(SpellEffectContext& /*ctx*/) {}

		void HandleSummonPet(SpellEffectContext& /*ctx*/) {}

		void HandleWeaponDamage(SpellEffectContext& ctx)
		{
			HandleInternalWeaponDamage(ctx, static_cast<SpellSchool>(ctx.castContext.GetSpell().spellschool()));
		}

		void HandleProficiency(SpellEffectContext& ctx)
		{
			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			// Get proficiency ID from the effect's miscvaluea field
			const uint32 proficiencyId = ctx.effect.miscvaluea();
			if (proficiencyId == 0)
			{
				WLOG("Spell " << ctx.castContext.GetSpell().id() << " has Proficiency effect with no proficiency ID");
				return;
			}

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				ctx.markAffectedTarget(*targetObject);
				auto& unitTarget = targetObject->AsUnit();

				// Add the proficiency by ID
				unitTarget.AddProficiency(proficiencyId);
			}
		}

		void HandlePowerBurn(SpellEffectContext& /*ctx*/) {}

		void HandleTriggerSpell(SpellEffectContext& ctx)
		{
			const uint32 triggerSpellId = ctx.effect.triggerspell();
			if (!triggerSpellId)
			{
				ELOG("TriggerSpell effect has no trigger spell set for spell " << ctx.castContext.GetSpell().id());
				return;
			}

			GameUnitS& executer = ctx.castContext.GetExecutor();

			const proto::SpellEntry* triggerSpell = executer.GetProject().spells.getById(triggerSpellId);
			if (!triggerSpell)
			{
				ELOG("Unable to find trigger spell " << triggerSpellId << " referenced by spell " << ctx.castContext.GetSpell().id());
				return;
			}

			// Roll the proc chance taken from the casting spell. A value of 0 is treated
			// as "always proc" so designers only need to set a proc chance when they
			// actually want a chance-based trigger.
			const uint32 procChance = ctx.castContext.GetSpell().procchance();
			if (procChance > 0 && procChance < 100)
			{
				std::uniform_real_distribution<float> distribution(0.0f, 100.0f);
				if (distribution(randomGenerator) > static_cast<float>(procChance))
				{
					// Failed the proc chance roll - nothing happens.
					return;
				}
			}

			// Determine the target for the triggered spell. If the effect resolved to a
			// unit target other than the caster we forward it (so offensive triggers can
			// hit the original victim); otherwise we cast it on the caster itself, which
			// covers self-buffs like energy/health restoration.
			SpellTargetMap targetMap;
			GameUnitS* triggerTarget = nullptr;
			for (auto* targetObject : ctx.effectTargets)
			{
				if (targetObject->IsUnit())
				{
					triggerTarget = &targetObject->AsUnit();
					break;
				}
			}

			if (triggerTarget && triggerTarget != &executer)
			{
				targetMap.SetUnitTarget(triggerTarget->GetGuid());
				targetMap.SetTargetMap(spell_cast_target_flags::Unit);
			}
			else
			{
				targetMap.SetTargetMap(spell_cast_target_flags::Self);
			}

			ctx.markAffectedTarget(executer);

			// Defer the triggered cast to avoid re-entering the spell cast pipeline while
			// the current effect is still being applied. This mirrors how aura procs
			// trigger their spells.
			WorldInstance* worldInstance = executer.GetWorldInstance();
			if (!worldInstance)
			{
				return;
			}

			auto strongCaster = std::static_pointer_cast<GameUnitS>(executer.shared_from_this());
			worldInstance->GetUniverse().Post([strongCaster, targetMap, triggerSpell]()
				{
					strongCaster->CastSpell(targetMap, *triggerSpell, 0, true, 0);
				});
		}

		void HandleScript(SpellEffectContext& /*ctx*/) {}

		void HandleAddComboPoints(SpellEffectContext& /*ctx*/) {}

		void HandleDuel(SpellEffectContext& /*ctx*/) {}

		void HandleCharge(SpellEffectContext& ctx)
		{
			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			GameUnitS& executer = ctx.castContext.GetExecutor();

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				ctx.markAffectedTarget(*targetObject);
				auto& unitTarget = targetObject->AsUnit();

				auto& mover = executer.GetMover();

				const Radian orientation = unitTarget.GetAngle(executer);

				const Vector3 target = unitTarget.GetMover().GetCurrentLocation().GetRelativePosition(orientation.GetValueRadians(), executer.GetMeleeReach() * 0.5f);
				mover.MoveTo(target, 35.0f, executer.GetMeleeReach() * 0.8f);
			}
		}

		void HandleAttackMe(SpellEffectContext& /*ctx*/) {}

		void HandleNormalizedWeaponDamage(SpellEffectContext& ctx)
		{
			HandleInternalWeaponDamage(ctx, static_cast<SpellSchool>(ctx.castContext.GetSpell().spellschool()));
		}

		void HandleStealBeneficialBuff(SpellEffectContext& /*ctx*/) {}

		void HandleInterruptSpellCast(SpellEffectContext& ctx)
		{
			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			const proto::SpellEntry& spell = ctx.castContext.GetSpell();

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				// Interrupt spell cast (and apply cooldown)
				targetObject->AsUnit().CancelCast(spell_interrupt_flags::Interrupt, spell.duration());
			}
		}

		void HandleLearnSpell(SpellEffectContext& ctx)
		{
			const uint32 spellId = ctx.effect.triggerspell();
			if (!spellId)
			{
				ELOG("No spell index to learn set for spell id " << ctx.castContext.GetSpell().id());
				return;
			}

			// Look for spell
			const auto* spell = ctx.castContext.GetExecutor().GetProject().spells.getById(spellId);
			if (!spell)
			{
				ELOG("Unknown spell index to learn set for spell id " << ctx.castContext.GetSpell().id() << ": " << spellId);
				return;
			}

			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				ctx.markAffectedTarget(*targetObject);
				auto& unitTarget = targetObject->AsUnit();
				unitTarget.AddSpell(spellId);
			}
		}

		void HandleScriptEffect(SpellEffectContext& /*ctx*/) {}

		void HandleDispelMechanic(SpellEffectContext& /*ctx*/) {}

		namespace
		{
			/// Shared implementation for the Revive / RevivePct effects. Prompts every dead player
			/// target to accept a revival; on acceptance (handled by the owning connection) the
			/// target is teleported to the cast location and restored to @p computeHealth health.
			/// @param usePercent When true, base points are a percentage of the target's max health.
			void HandleReviveInternal(SpellEffectContext& ctx, bool usePercent)
			{
				if (ctx.effectTargets.empty())
				{
					return;
				}

				SpellCastContext& castContext = ctx.castContext;
				GameUnitS& executer = castContext.GetExecutor();

				// Capture the location the spell was cast at — this is where accepted targets are teleported to.
				const WorldInstance* world = castContext.GetWorldInstance();
				if (!world)
				{
					return;
				}

				const uint32 mapId = world->GetMapId();
				const Vector3 castPosition = executer.GetPosition();
				const Radian castFacing = executer.GetFacing();
				const uint32 spellId = castContext.GetSpell().id();
				const uint64 casterGuid = executer.GetGuid();

				for (auto* targetObject : ctx.effectTargets)
				{
					if (!targetObject->IsUnit())
					{
						continue;
					}

					GameUnitS& unitTarget = targetObject->AsUnit();

					// Revival is player-only and only valid while the target is dead. NPCs and living
					// units are silently skipped here (the cast-level validation already rejects the
					// common single-target cases; this guards multi-target / AoE revival).
					if (!unitTarget.IsPlayer() || unitTarget.IsAlive())
					{
						continue;
					}

					NetUnitWatcherS* watcher = unitTarget.GetNetUnitWatcher();
					if (!watcher)
					{
						continue;
					}

					// Resolve the absolute amount of health to restore on acceptance.
					uint32 reviveHealth;
					if (usePercent)
					{
						const uint32 maxHealth = unitTarget.Get<uint32>(object_fields::MaxHealth);
						const int32 pct = std::clamp<int32>(ctx.basePoints, 0, 100);
						reviveHealth = static_cast<uint32>(static_cast<uint64>(maxHealth) * pct / 100);
					}
					else
					{
						reviveHealth = static_cast<uint32>(std::max<int32>(0, ctx.basePoints));
					}

					// Always restore at least 1 health so the target does not arrive dead again.
					reviveHealth = std::max<uint32>(1, reviveHealth);

					ctx.markAffectedTarget(*targetObject);
					watcher->OnReviveOffer(casterGuid, spellId, reviveHealth, mapId, castPosition, castFacing);
				}
			}
		}

		void HandleRevive(SpellEffectContext& ctx)
		{
			HandleReviveInternal(ctx, false);
		}

		void HandleRevivePct(SpellEffectContext& ctx)
		{
			HandleReviveInternal(ctx, true);
		}

		void HandleKnockBack(SpellEffectContext& /*ctx*/) {}

		void HandleSkill(SpellEffectContext& /*ctx*/) {}

		void HandleTransDoor(SpellEffectContext& /*ctx*/) {}

		void HandleResetAttributePoints(SpellEffectContext& ctx)
		{
			if (ctx.effectTargets.empty())
			{
				WLOG("Unable to resolve effect unit target!");
				return;
			}

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				ctx.markAffectedTarget(*targetObject);

				if (const std::shared_ptr<GamePlayerS> playerTarget = std::dynamic_pointer_cast<GamePlayerS>(targetObject->AsUnit().shared_from_this()))
				{
					DLOG("Resetting attribute points for player!");
					playerTarget->ResetAttributePoints();
				}
				else
				{
					WLOG("Target is not a player character!");
				}
				break; // Only process first unit target, matching original GetEffectUnitTarget behavior
			}
		}

		void HandleParry(SpellEffectContext& ctx)
		{
			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				ctx.markAffectedTarget(*targetObject);
				auto& unitTarget = targetObject->AsUnit();
				unitTarget.NotifyCanParry(true);
			}
		}

		void HandleBlock(SpellEffectContext& ctx)
		{
			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				ctx.markAffectedTarget(*targetObject);
				auto& unitTarget = targetObject->AsUnit();
				unitTarget.NotifyCanBlock(true);
			}
		}

		void HandleCriticalBlock(SpellEffectContext& ctx)
		{
			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				ctx.markAffectedTarget(*targetObject);
				auto& unitTarget = targetObject->AsUnit();
				unitTarget.NotifyCanCriticalBlock(true);
			}
		}

		void HandleDodge(SpellEffectContext& ctx)
		{
			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				ctx.markAffectedTarget(*targetObject);
				auto& unitTarget = targetObject->AsUnit();
				unitTarget.NotifyCanDodge(true);
			}
		}

		void HandleDualWield(SpellEffectContext& ctx)
		{
			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				ctx.markAffectedTarget(*targetObject);
				auto& unitTarget = targetObject->AsUnit();
				unitTarget.NotifyCanDualWield(true);
			}
		}

		void HandleHealPct(SpellEffectContext& ctx)
		{
			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			const proto::SpellEntry& spell = ctx.castContext.GetSpell();
			GameUnitS& executer = ctx.castContext.GetExecutor();

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				ctx.markAffectedTarget(*targetObject);
				auto& unitTarget = targetObject->AsUnit();

				// TODO: Do real calculation including crit chance, miss chance, resists, etc.
				int32_t basePoints = ctx.basePoints;
				if (basePoints <= 0 || basePoints > 100)
				{
					WLOG("Spell " << spell.id() << " has invalid base points for spell Effect HealPct: " << basePoints << ". Will be clamped to 1-100.");
					return;
				}

				basePoints = Clamp(basePoints, 1, 100);

				const uint32 healAmount = static_cast<uint32>(floor(static_cast<float>(unitTarget.GetMaxHealth()) * (static_cast<float>(basePoints) / 100.0f)));
				const int32 effectiveHealing = unitTarget.Heal(healAmount, &executer);

				// Healing a unit under attack generates threat on its attackers and pulls the healer into combat.
				unitTarget.GenerateHealingThreat(executer, static_cast<uint32>(std::max<int32>(0, effectiveHealing)));

				// TODO: Heal log to show healing numbers at the clients
			}
		}

		void HandleAddExtraAttacks(SpellEffectContext& ctx)
		{
			const int32_t numAttacks = ctx.basePoints;
			if (numAttacks <= 0)
			{
				WLOG("Unable to perform extra attacks, because base points of spell " << ctx.castContext.GetSpell().id() << " rolled for " << numAttacks << " but have to be >= 1");
				return;
			}

			GameUnitS& executer = ctx.castContext.GetExecutor();
			for (int32_t i = 0; i < numAttacks; ++i)
			{
				ctx.markAffectedTarget(executer);
				executer.OnAttackSwing();
			}
		}

		void HandleResetTalents(SpellEffectContext& ctx)
		{
			if (ctx.effectTargets.empty())
			{
				ELOG("Failed to cast spell effect: Unable to resolve effect targets");
				return;
			}

			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				ctx.markAffectedTarget(*targetObject);

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

		// Applies a single weapon-damage roll against one unit target. Caster-side
		// values are recomputed per target so each victim gets an independent roll.
		static void ApplyWeaponDamageSingleTarget(SpellEffectContext& ctx, SpellSchool school, bool basePointsArePct, GameUnitS& unitTarget)
		{
			ctx.markAffectedTarget(unitTarget);

			auto& executer = ctx.castContext.GetExecutor();
			const proto::SpellEntry& spell = ctx.castContext.GetSpell();
			const uint32 casterLevel = executer.Get<uint32>(object_fields::Level);
			const int32_t bonus = ctx.basePoints;
			const auto& combatSettings = executer.GetCombatSettings();

			// Auto-attack spells (AutoRepeat) use the full melee combat table and send
			// AttackerStateUpdate (white damage text) instead of SpellDamageLog (yellow).
			const bool isAutoAttack = (spell.attributes(1) & spell_attributes_b::AutoRepeat) != 0;

			// Determine the swinging hand. Auto-attacks may be driven by the off-hand swing timer,
			// in which case the weapon damage range is sourced from the off-hand weapon and reduced
			// by the configurable off-hand damage multiplier.
			const WeaponAttack swingHand = isAutoAttack ? executer.GetCurrentAutoAttackType() : weapon_attack::BaseAttack;
			const bool isOffhandSwing = (swingHand == weapon_attack::OffhandAttack);

			// Source the weapon damage range. Auto-attacks use the swinging hand's weapon; regular
			// weapon-damage abilities keep using the main-hand MinDamage/MaxDamage object fields.
			float minDamage, maxDamage;
			if (isAutoAttack)
			{
				executer.GetAutoAttackDamageRange(swingHand, minDamage, maxDamage);
			}
			else
			{
				minDamage = executer.Get<float>(object_fields::MinDamage);
				maxDamage = executer.Get<float>(object_fields::MaxDamage);
			}

			const float offhandMultiplier = isOffhandSwing ? combatSettings.offhand_damage_multiplier() : 1.0f;

			// Rolls the base weapon damage for this swing. When the effect base points are a
			// percentage (WeaponPercentDamage) the rolled weapon damage is scaled by that
			// percentage; otherwise the base points are added as a flat bonus (WeaponDamage).
			// Off-hand swings additionally scale the rolled weapon damage by the off-hand multiplier.
			auto rollBaseWeaponDamage = [&]() -> uint32
			{
				if (basePointsArePct)
				{
					std::uniform_real_distribution<float> distribution(minDamage, maxDamage + 1.0f);
					return static_cast<uint32>(distribution(randomGenerator) * (static_cast<float>(bonus) / 100.0f) * offhandMultiplier);
				}

				std::uniform_real_distribution<float> distribution(minDamage + bonus, maxDamage + bonus + 1.0f);
				return static_cast<uint32>(distribution(randomGenerator) * offhandMultiplier);
			};
			if (isAutoAttack)
			{
				// Roll the full melee combat table for the swinging hand
				const MeleeAttackOutcome outcome = executer.RollMeleeOutcomeAgainst(unitTarget, swingHand);

				// Calculate base weapon damage
				uint32 totalDamage = rollBaseWeaponDamage();

				// Apply damage mod
				totalDamage = executer.CalculateModifiedValue(unit_mods::Damage, totalDamage);

				// LeftSwing flags the swing as an off-hand attack so the client can react accordingly.
				uint32 hitInfo = isOffhandSwing ? hit_info::LeftSwing : hit_info::NormalSwing;
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
						const int32_t skillDiff = unitTarget.GetMaxSkillValueForLevel(unitTarget.GetLevel()) - executer.GetMaxSkillValueForLevel(casterLevel);
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

				// If the target is immune to this weapon's damage school, negate the swing entirely
				// but still report it so the client can display an "Immune" floating combat text.
				if (hit && unitTarget.IsImmuneToSchool(school))
				{
					hitInfo |= hit_info::Immune;
					victimState = victim_state::IsImmune;
					hit = false;
					totalDamage = 0;
				}

				// Check for block after hit determination (physical, front attacks only).
				uint32 blockedDamage = 0;
				if (hit)
				{
					const BlockResult block = unitTarget.RollMeleeBlock(executer, static_cast<SpellSchool>(school), totalDamage);
					if (block.blocked)
					{
						blockedDamage = block.blockedAmount;
						totalDamage -= blockedDamage;

						hitInfo |= hit_info::Block;
						victimState = victim_state::Blocks;

						unitTarget.OnBlock();
					}
				}

				// Apply armor reduction for physical damage
				if (hit && totalDamage > 0 && school == spell_school::Normal)
				{
					totalDamage = unitTarget.CalculateArmorReducedDamage(casterLevel, totalDamage);
				}

				// Apply spell mods
				if (hit && totalDamage > 0)
				{
					executer.ApplySpellMod(spell_mod_op::Damage, spell.id(), totalDamage);
					totalDamage = static_cast<uint32>(static_cast<float>(totalDamage) * unitTarget.GetIncomingDamageTakenMultiplier(&executer, spell_dmg_class::Melee));
				}

				// Deal damage
				uint32 absorbedDamage = 0;
				if (hit && totalDamage > 0)
				{
					if (unitTarget.Damage(totalDamage, school, &executer, damage_type::AttackSwing) > 0)
					{
						unitTarget.threatened(executer, static_cast<float>(totalDamage));
					}
				}

				// Trigger defense events
				if (outcome == melee_attack_outcome::Parry)
				{
					unitTarget.OnParry();
				}
				else if (outcome == melee_attack_outcome::Dodge)
				{
					unitTarget.OnDodge();
				}

				// Send melee damage packet (white text) instead of SpellDamageLog (yellow text)
				executer.SendAttackerStateUpdate(unitTarget.GetGuid(), hitInfo, victimState, totalDamage, school, absorbedDamage, 0, blockedDamage);

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

					// Flag off-hand swings so procs that care about the swinging hand can react.
					const SpellProcFlags attackerProcFlags = isOffhandSwing
						? static_cast<SpellProcFlags>(spell_proc_flags::DoneMeleeAutoAttack | spell_proc_flags::DoneOffhandAttack)
						: spell_proc_flags::DoneMeleeAutoAttack;

					executer.TriggerProcEvent(attackerProcFlags, &unitTarget, totalDamage, procEx, school, false);
					unitTarget.TriggerProcEvent(spell_proc_flags::TakenMeleeAutoAttack, &executer, totalDamage, procEx, school, false);
				}
			}
			else
			{
				// Regular weapon damage spell (e.g., Heroic Strike, Mortal Strike)
				// Uses simplified crit roll and SpellDamageLog

				// If the target is immune to this weapon's damage school, report the immunity to
				// clients (so the floating combat text shows "Immune") and skip damage entirely.
				if (unitTarget.IsImmuneToSchool(school))
				{
					executer.SpellDamageLog(unitTarget.GetGuid(), 0, school, DamageFlags::Immune, spell);
					return;
				}

				// Calculate damage between minimum and maximum damage
				uint32 totalDamage = rollBaseWeaponDamage();

				// Apply damage mod
				totalDamage = executer.CalculateModifiedValue(unit_mods::Damage, totalDamage);

				// Physical damage is reduced by armor
				if (school == spell_school::Normal)
				{
					totalDamage = unitTarget.CalculateArmorReducedDamage(casterLevel, totalDamage);
				}

				executer.ApplySpellMod(spell_mod_op::Damage, spell.id(), totalDamage);

				// Get configurable crit chance and multiplier from combat settings
				float critChance = combatSettings.spell_weapon_default_crit_chance();
				std::uniform_real_distribution critDistribution(0.0f, 100.0f);

				executer.ApplySpellMod(spell_mod_op::CritChance, spell.id(), critChance);

				bool isCrit = false;
				if (critDistribution(randomGenerator) < critChance)
				{
					isCrit = true;
					totalDamage = static_cast<uint32>(totalDamage * combatSettings.spell_weapon_crit_multiplier());
				}

				// Roll for a block (physical front attacks only) — reduces the post-crit damage by a flat amount.
				uint32 blockedDamage = 0;
				const BlockResult block = unitTarget.RollMeleeBlock(executer, static_cast<SpellSchool>(school), totalDamage);
				if (block.blocked)
				{
					blockedDamage = block.blockedAmount;
					totalDamage -= blockedDamage;
					unitTarget.OnBlock();
				}

				totalDamage = static_cast<uint32>(static_cast<float>(totalDamage) * unitTarget.GetIncomingDamageTakenMultiplier(&executer, spell_dmg_class::Melee));

				uint8 damageFlags = DamageFlags::None;
				if (isCrit) damageFlags |= DamageFlags::Crit;
				if (block.blocked) damageFlags |= DamageFlags::Block;

				// Log spell damage to client
				if (unitTarget.Damage(totalDamage, school, &executer, damage_type::PhysicalAbility) > 0)
				{
					float threat = static_cast<float>(totalDamage) * spell.threat_multiplier();
					executer.ApplySpellMod(spell_mod_op::Threat, spell.id(), threat);
					unitTarget.threatened(executer, threat);
				}
				executer.SpellDamageLog(unitTarget.GetGuid(), totalDamage, school, static_cast<DamageFlags>(damageFlags), spell, blockedDamage);

				// Trigger proc events for spell damage
				executer.TriggerProcEvent(spell_proc_flags::DoneSpellMeleeDmgClass, &unitTarget, totalDamage, proc_ex_flags::NormalHit, spell.spellschool(), false, spell.familyflags());
				unitTarget.TriggerProcEvent(spell_proc_flags::TakenSpellMeleeDmgClass, &executer, totalDamage, proc_ex_flags::NormalHit, spell.spellschool(), false, spell.familyflags());
			}
		}

		void HandleInternalWeaponDamage(SpellEffectContext& ctx, SpellSchool school, bool basePointsArePct)
		{
			if (ctx.effectTargets.empty())
			{
				return;
			}

			// Apply weapon damage to every resolved unit target. Single-target weapon
			// spells resolve to exactly one unit; multi-target target types (such as
			// TargetSecondaryEnemy or cleave-style area targets) resolve to several,
			// and each victim takes a full, independent weapon-damage roll.
			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsUnit())
				{
					continue;
				}

				ApplyWeaponDamageSingleTarget(ctx, school, basePointsArePct, targetObject->AsUnit());
			}
		}

	} // namespace SpellEffects

} // namespace mmo
