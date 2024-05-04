#pragma once

#include "base/typedefs.h"

#include <map>

#include "game_unit_s.h"
#include "spell_cast.h"
#include "tile_subscriber.h"
#include "binary_io/vector_sink.h"
#include "game_protocol/game_protocol.h"
#include "each_tile_in_sight.h"
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
			bool isProc = false);

		void Activate() override;

		std::pair<SpellCastResult, SpellCasting*> StartCast(
			SpellCast& cast,
			const proto::SpellEntry& spell,
			const SpellTargetMap& target,
			GameTime castTime,
			bool doReplacePreviousCast) override;

		void StopCast(GameTime interruptCooldown = 0) override;

		void OnUserStartsMoving() override;

		void FinishChanneling() override;

		SpellCasting& GetCasting()
		{
			return m_casting;
		}

	private:
		bool Validate();

		template<class T>
		bool HasAttributes(uint32 index, T attributes)
		{
			return (m_spell.attributes(index) & static_cast<uint32>(attributes)) != 0;
		}

	public:
		template <class T>
		void SendPacketFromCaster(GameUnitS& caster, T generator)
		{
			const auto* worldInstance = caster.GetWorldInstance();
			if (!worldInstance)
			{
				return;
			}

			TileIndex2D tileIndex;
			worldInstance->GetGrid().GetTilePosition(caster.GetPosition(), tileIndex[0], tileIndex[1]);

			std::vector<char> buffer;
			io::VectorSink sink(buffer);
			game::Protocol::OutgoingPacket packet(sink);
			generator(packet);

			ForEachSubscriberInSight(
				worldInstance->GetGrid(),
				tileIndex,
				[&buffer, &packet](TileSubscriber& subscriber)
				{
					subscriber.SendPacket(
						packet,
						buffer
					);
				});
		}

		template <class T>
		void SendPacketToCaster(GameUnitS& caster, T generator)
		{
			const auto* worldInstance = caster.GetWorldInstance();
			if (!worldInstance)
			{
				return;
			}

			TileIndex2D tileIndex;
			worldInstance->GetGrid().GetTilePosition(caster.GetPosition(), tileIndex[0], tileIndex[1]);

			std::vector<char> buffer;
			io::VectorSink sink(buffer);
			game::Protocol::OutgoingPacket packet(sink);
			generator(packet);

			ForEachSubscriberInSight(
				worldInstance->GetGrid(),
				tileIndex,
				[&buffer, &packet, &caster](TileSubscriber& subscriber)
				{
					if (&subscriber.GetGameUnit() == &caster)
					{
						subscriber.SendPacket(
							packet,
							buffer
						);
					}
				});
		}

	public:

		/// Determines if this spell is a channeled spell.
		bool IsChanneled() const { return m_spell.attributes(0) & spell_attributes::Channeled; }

	private:

		bool ConsumeItem(bool delayed = true);

		bool ConsumeReagents(bool delayed = true);

		bool ConsumePower();

		void ApplyCooldown(GameTime cooldownTimeMs, GameTime categoryCooldownTimeMs);

		void ApplyAllEffects();

		int32 CalculateEffectBasePoints(const proto::SpellEffect& effect);

		uint32 GetSpellPointsTotal(const proto::SpellEffect& effect, uint32 spellPower, uint32 bonusPct);

		void MeleeSpecialAttack(const proto::SpellEffect& effect, bool basepointsArePct);

		// Spell effect handlers implemented in spell_effects.cpp and spell_effects_scripted.cpp

		void SpellEffectInstantKill(const proto::SpellEffect& effect);
		void SpellEffectDummy(const proto::SpellEffect& effect);
		void SpellEffectSchoolDamage(const proto::SpellEffect& effect);
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
		void SpellEffectInterruptCast(const proto::SpellEffect& effect);
		void SpellEffectLearnSpell(const proto::SpellEffect& effect);
		void SpellEffectScriptEffect(const proto::SpellEffect& effect);
		void SpellEffectDispelMechanic(const proto::SpellEffect& effect);
		void SpellEffectResurrect(const proto::SpellEffect& effect);
		void SpellEffectResurrectNew(const proto::SpellEffect& effect);
		void SpellEffectKnockBack(const proto::SpellEffect& effect);
		void SpellEffectSkill(const proto::SpellEffect& effect);
		void SpellEffectTransDoor(const proto::SpellEffect& effect);

	private:

		SpellCast& m_cast;
		const proto::SpellEntry& m_spell;
		SpellTargetMap m_target;
		SpellCasting m_casting;
		std::vector<uint32> m_meleeDamage;	// store damage results of melee effects
		bool m_hasFinished;
		Countdown m_countdown;
		Countdown m_impactCountdown;
		signal<void()> completedEffects;
		std::unordered_map<uint64, scoped_connection> m_completedEffectsExecution;
		scoped_connection m_onTargetDied;
		scoped_connection m_onTargetRemoved, m_damaged;
		scoped_connection m_onThreatened;
		scoped_connection m_onAttackError, m_removeAurasOnImmunity;
		float m_x, m_y, m_z;
		GameTime m_castTime;
		GameTime m_castEnd;
		bool m_isProc;
		GameTime m_projectileStart, m_projectileEnd;
		Vector3 m_projectileOrigin, m_projectileDest;
		bool m_connectedMeleeSignal;
		uint32 m_delayCounter;
		std::set<std::weak_ptr<GameObjectS>, std::owner_less<std::weak_ptr<GameObjectS>>> m_affectedTargets;
		bool m_tookCastItem;
		bool m_tookReagents;
		uint32 m_attackerProc;
		uint32 m_victimProc;
		bool m_canTrigger;
		HitResultMap m_hitResults;
		//game::WeaponAttack m_attackType;
		std::vector<uint64> m_dynObjectsToDespawn;
		bool m_instantsCast, m_delayedCast;
		scoped_connection m_onChannelAuraRemoved;
		//std::map<UInt64, std::shared_ptr<AuraSpellSlot>> m_auraSlots;

		void SendEndCast(SpellCastResult result);
		void OnCastFinished();
		void OnTargetKilled(GameUnitS*);
		void OnTargetDespawned(GameObjectS&);
		void OnUserDamaged();
		void ExecuteMeleeAttack();	// deal damage stored in m_meleeDamage

		typedef std::function<void(const proto::SpellEffect&)> EffectHandler;
	};
}
