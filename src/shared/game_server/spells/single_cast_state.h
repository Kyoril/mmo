// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "game_server/spells/aura_container.h"
#include "game_server/objects/game_unit_s.h"
#include "game_server/spells/spell_cast.h"
#include "game_server/spells/spell_cast_context.h"
#include "game_server/spells/spell_target_resolver.h"
#include "game/spell.h"

namespace mmo
{

	///
	class SingleCastState final
		: public CastState
		, public std::enable_shared_from_this<SingleCastState>
	{
	private:
		SingleCastState(const SingleCastState& other) = delete;
		SingleCastState& operator=(const SingleCastState& other) = delete;

	public:

		explicit SingleCastState(
			SpellCast& cast,
			const proto::SpellEntry& spell,
			const SpellTargetMap& target,
			GameTime castTime,
			bool isProc = false,
			uint64 itemGuid = 0);

		void Activate() override;

		std::pair<SpellCastResult, SpellCasting*> StartCast(
			SpellCast& cast,
			const proto::SpellEntry& spell,
			const SpellTargetMap& target,
			GameTime castTime,
			bool doReplacePreviousCast,
			uint64 itemGuid) override;

		void StopCast(SpellInterruptFlags reason, GameTime interruptCooldown = 0) override;

		void OnUserStartsMoving() override;

		void FinishChanneling() override;

		SpellCasting& GetCasting()
		{
			return m_casting;
		}

	private:
		bool Validate();
		bool ValidatePlayerRequirements();
		bool ValidateCasterRequirements();
		bool ValidateTargetRequirements(GameUnitS* unitTarget);
		[[nodiscard]] bool ShouldStartCooldownOnCastStart() const;
		[[nodiscard]] bool UsesGlobalCooldown() const;
		void NotifyCastEnded(bool succeeded);
		void EndChanneling(bool succeeded);
		GameUnitS* ResolveUnitTarget() const;
		void ConnectTargetSignals(GameUnitS* unitTarget);

		template<class T>
		bool HasAttributes(uint32 index, T attributes)
		{
			return (m_spell.attributes(index) & static_cast<uint32>(attributes)) != 0;
		}

		std::shared_ptr<GameUnitS> GetEffectUnitTarget(const proto::SpellEffect& effect);

		/// Determines if this spell is a channeled spell.
		bool IsChanneled() const { return m_spell.attributes(0) & spell_attributes::Channeled; }

	private:

		bool ConsumeItem(bool delayed = true);

		bool ConsumeReagents(bool delayed = true);

		bool ConsumePower();

		/// @brief Calculates the final cooldown for this spell after applying spell mods.
		/// @return The final cooldown in milliseconds, or 0 if no cooldown.
		GameTime CalculateFinalCooldown() const;

		void ApplyCooldown(GameTime cooldownTimeMs, GameTime categoryCooldownTimeMs);

		void ApplyAllEffects();
		void ApplyEffect(const proto::SpellEffect& effect);

		int32 CalculateEffectBasePoints(const proto::SpellEffect& effect);

		uint32 GetSpellPointsTotal(const proto::SpellEffect& effect, uint32 spellPower, uint32 bonusPct);

		void MeleeSpecialAttack(const proto::SpellEffect& effect, bool basepointsArePct);

		// Spell effect handlers implemented in spell_effects.cpp and spell_effects_scripted.cpp

		void SpellEffectInstantKill(const proto::SpellEffect& effect);
		void SpellEffectDummy(const proto::SpellEffect& effect);
		void SpellEffectSchoolDamage(const proto::SpellEffect& effect);
		void SpellEffectEnvironmentalDamage(const proto::SpellEffect& effect);
		void SpellEffectTeleportUnits(const proto::SpellEffect& effect);
		void SpellEffectApplyAura(const proto::SpellEffect& effect);
		void SpellEffectPersistentAreaAura(const proto::SpellEffect& effect);
		void SpellEffectDrainPower(const proto::SpellEffect& effect);
		void SpellEffectHeal(const proto::SpellEffect& effect);
		void SpellEffectBind(const proto::SpellEffect& effect);
		void SpellEffectQuestComplete(const proto::SpellEffect& effect);
		void SpellEffectWeaponDamageNoSchool(const proto::SpellEffect& effect);
		void SpellEffectCreateItem(const proto::SpellEffect& effect);
		void SpellEffectEnergize(const proto::SpellEffect& effect);
		void SpellEffectWeaponPercentDamage(const proto::SpellEffect& effect);
		void SpellEffectOpenLock(const proto::SpellEffect& effect);
		void SpellEffectApplyAreaAuraParty(const proto::SpellEffect& effect);
		void SpellEffectDispel(const proto::SpellEffect& effect);
		void SpellEffectSummon(const proto::SpellEffect& effect);
		void SpellEffectSummonPet(const proto::SpellEffect& effect);
		void SpellEffectWeaponDamage(const proto::SpellEffect& effect);
		void SpellEffectProficiency(const proto::SpellEffect& effect);
		void SpellEffectPowerBurn(const proto::SpellEffect& effect);
		void SpellEffectTriggerSpell(const proto::SpellEffect& effect);
		void SpellEffectScript(const proto::SpellEffect& effect);
		void SpellEffectAddComboPoints(const proto::SpellEffect& effect);
		void SpellEffectDuel(const proto::SpellEffect& effect);
		void SpellEffectCharge(const proto::SpellEffect& effect);
		void SpellEffectAttackMe(const proto::SpellEffect& effect);
		void SpellEffectNormalizedWeaponDamage(const proto::SpellEffect& effect);
		void SpellEffectStealBeneficialBuff(const proto::SpellEffect& effect);
		void SpellEffectInterruptSpellCast(const proto::SpellEffect& effect);
		void SpellEffectLearnSpell(const proto::SpellEffect& effect);
		void SpellEffectScriptEffect(const proto::SpellEffect& effect);
		void SpellEffectDispelMechanic(const proto::SpellEffect& effect);
		void SpellEffectResurrect(const proto::SpellEffect& effect);
		void SpellEffectResurrectNew(const proto::SpellEffect& effect);
		void SpellEffectKnockBack(const proto::SpellEffect& effect);
		void SpellEffectSkill(const proto::SpellEffect& effect);
		void SpellEffectTransDoor(const proto::SpellEffect& effect);
		void SpellEffectResetAttributePoints(const proto::SpellEffect& effect);
		void SpellEffectParry(const proto::SpellEffect& effect);
		void SpellEffectBlock(const proto::SpellEffect& effect);
		void SpellEffectDodge(const proto::SpellEffect& effect);
		void SpellEffectHealPct(const proto::SpellEffect& effect);
		void SpellEffectAddExtraAttacks(const proto::SpellEffect& effect);
		void SpellEffectResetTalents(const proto::SpellEffect& effect);

	private:
		bool GetEffectTargets(const proto::SpellEffect& effect, std::vector<GameObjectS*>& targets);
		void MarkAffectedTarget(GameObjectS& target);

	private:
		void InternalSpellEffectWeaponDamage(const proto::SpellEffect& effect, SpellSchool school);

	private:
		AuraContainer& GetOrCreateAuraContainer(GameUnitS& target);

	private:
		SpellCast& m_cast;
		const proto::SpellEntry& m_spell;
		SpellTargetMap m_target;
		SpellCasting m_casting;
		bool m_hasFinished;
		Countdown m_countdown;
		Countdown m_impactCountdown;
		// m_postEffectCallbacks: callbacks pushed by ConsumeItem(delayed=true) and drained at
		// the end of ApplyAllEffects(). Accumulation-safe: the m_tookCastItem guard inside
		// removeItem prevents double-removal even if ConsumeItem is called twice.
		std::vector<std::function<void()>> m_postEffectCallbacks;
		scoped_connection m_onTargetDied;
		scoped_connection m_onTargetRemoved;
		float m_x, m_y, m_z;
		GameTime m_castTime;
		GameTime m_castEnd;
		bool m_isProc;
		GameTime m_projectileStart, m_projectileEnd;
		Vector3 m_projectileOrigin, m_projectileDest;
		std::unordered_set<uint64> m_affectedTargetGuids;
		std::vector<GameObjectS*> m_effectTargetsScratch;
		bool m_tookCastItem { false };
		bool m_tookReagents { false };
		bool m_delayedCast;
		bool m_cooldownStartedOnCastStart { false };
		GameTime m_cooldownStartedAtCastStartMs { 0 };
		bool m_endNotified { false };
		std::shared_ptr<SingleCastState> m_selfHold;

		void SendEndCast(SpellCastResult result);
		void OnCastFinished();
		void OnTargetKilled(GameUnitS*);
		void OnTargetDespawned(GameObjectS&);
		void OnUserDamaged();
		void ExecuteMeleeAttack();

		std::unordered_map<uint64, std::unique_ptr<AuraContainer>> m_targetAuraContainers;
		uint64 m_itemGuid;
		SpellCastContext m_context;
		SpellTargetResolver m_targetResolver;

		using EffectHandler = void (SingleCastState::*)(const proto::SpellEffect&);
	};
}
