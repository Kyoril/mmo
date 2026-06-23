// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "spell_cast_context.h"
#include "game/damage_school.h"

namespace mmo
{
	namespace proto
	{
		class SpellEffect;
	}

	class GameObjectS;
	class GameUnitS;
	class AuraContainer;

	/// Data packet passed to every free-function spell-effect handler.
	/// The caller (SingleCastState::ApplyEffect) fills this struct before
	/// dispatching so that handlers never need to reach back into
	/// SingleCastState private state.
	struct SpellEffectContext
	{
		/// Access to caster, spell, target, packet sending, world.
		SpellCastContext& castContext;

		/// The individual effect proto being applied this invocation.
		const proto::SpellEffect& effect;

		/// Base-point value pre-calculated by the caller via CalculateBasePoints.
		int32_t basePoints;

		/// Effect target list pre-resolved by the caller via GetEffectTargets.
		std::vector<GameObjectS*>& effectTargets;

		/// Delegate that retrieves (or lazily creates) an AuraContainer for a
		/// unit, mirroring SingleCastState::GetOrCreateAuraContainer without
		/// exposing the private member map.
		std::function<AuraContainer&(GameUnitS&)> getOrCreateAuraContainer;

		/// Delegate that records a target as affected by this cast, mirroring
		/// SingleCastState::MarkAffectedTarget.
		std::function<void(GameObjectS&)> markAffectedTarget;
	};

	namespace SpellEffects
	{
		/// Pre-calculates the base-point value for an effect.
		/// Extracted from SingleCastState::CalculateEffectBasePoints so that
		/// handlers can call it without referencing SingleCastState internals.
		int32_t CalculateBasePoints(SpellCastContext& castCtx, const proto::SpellEffect& effect);

		// ------------------------------------------------------------------ //
		// One free function per handler, named Handle<MethodSuffix>.         //
		// The suffix is the part of the original method name after            //
		// "SpellEffect", except for InternalSpellEffectWeaponDamage which     //
		// maps to HandleInternalWeaponDamage.                                 //
		// ------------------------------------------------------------------ //

		void HandleInstantKill(SpellEffectContext& ctx);
		void HandleDummy(SpellEffectContext& ctx);
		void HandleSchoolDamage(SpellEffectContext& ctx);
		void HandleEnvironmentalDamage(SpellEffectContext& ctx);
		void HandleTeleportUnits(SpellEffectContext& ctx);
		void HandleApplyAura(SpellEffectContext& ctx);
		void HandlePersistentAreaAura(SpellEffectContext& ctx);
		void HandleDrainPower(SpellEffectContext& ctx);
		void HandleHeal(SpellEffectContext& ctx);
		void HandleBind(SpellEffectContext& ctx);
		void HandleQuestComplete(SpellEffectContext& ctx);
		void HandleWeaponDamageNoSchool(SpellEffectContext& ctx);
		void HandleCreateItem(SpellEffectContext& ctx);
		void HandleEnergize(SpellEffectContext& ctx);
		void HandleWeaponPercentDamage(SpellEffectContext& ctx);
		void HandleOpenLock(SpellEffectContext& ctx);
		void HandleApplyAreaAuraParty(SpellEffectContext& ctx);
		void HandleDispel(SpellEffectContext& ctx);
		void HandleSummon(SpellEffectContext& ctx);
		void HandleSummonPet(SpellEffectContext& ctx);
		void HandleWeaponDamage(SpellEffectContext& ctx);
		void HandleProficiency(SpellEffectContext& ctx);
		void HandlePowerBurn(SpellEffectContext& ctx);
		void HandleTriggerSpell(SpellEffectContext& ctx);
		void HandleScript(SpellEffectContext& ctx);
		void HandleAddComboPoints(SpellEffectContext& ctx);
		void HandleDuel(SpellEffectContext& ctx);
		void HandleCharge(SpellEffectContext& ctx);
		void HandleAttackMe(SpellEffectContext& ctx);
		void HandleNormalizedWeaponDamage(SpellEffectContext& ctx);
		void HandleStealBeneficialBuff(SpellEffectContext& ctx);
		void HandleInterruptSpellCast(SpellEffectContext& ctx);
		void HandleLearnSpell(SpellEffectContext& ctx);
		void HandleScriptEffect(SpellEffectContext& ctx);
		void HandleDispelMechanic(SpellEffectContext& ctx);
		/// Prompts dead player targets to be revived, restoring an absolute amount of health
		/// (base points) at the cast location once they accept.
		void HandleRevive(SpellEffectContext& ctx);
		/// Like HandleRevive, but base points are treated as a percentage of the target's max health.
		void HandleRevivePct(SpellEffectContext& ctx);
		void HandleKnockBack(SpellEffectContext& ctx);
		void HandleSkill(SpellEffectContext& ctx);
		void HandleTransDoor(SpellEffectContext& ctx);
		void HandleResetAttributePoints(SpellEffectContext& ctx);
		void HandleParry(SpellEffectContext& ctx);
		void HandleBlock(SpellEffectContext& ctx);
		void HandleCriticalBlock(SpellEffectContext& ctx);
		void HandleDodge(SpellEffectContext& ctx);
		void HandleDualWield(SpellEffectContext& ctx);
		void HandleHealPct(SpellEffectContext& ctx);
		void HandleAddExtraAttacks(SpellEffectContext& ctx);
		void HandleResetTalents(SpellEffectContext& ctx);
		/// Switches the target player's active class to the class id in miscvaluea (added at class
		/// level 1 if not yet known). Subject to combat and race-legality checks.
		void HandleChangeClass(SpellEffectContext& ctx);

		/// Internal helper: applies weapon damage in a given school.
		/// Corresponds to SingleCastState::InternalSpellEffectWeaponDamage.
		/// @param basePointsArePct When true, the effect base points are interpreted as a
		///        percentage of the weapon's damage (100 = 100%) instead of a flat bonus.
		void HandleInternalWeaponDamage(SpellEffectContext& ctx, SpellSchool school, bool basePointsArePct = false);

	} // namespace SpellEffects

} // namespace mmo
