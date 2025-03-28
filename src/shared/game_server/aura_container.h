#pragma once

#include <vector>

#include "spell_cast.h"
#include "base/countdown.h"
#include "base/typedefs.h"
#include "binary_io/writer.h"
#include "game/aura.h"
#include "game/damage_school.h"

namespace mmo
{
	class Vector3;

	namespace proto
	{
		class SpellEntry;
		class SpellEffect;
	}

	class GameUnitS;
	class AuraContainer;

	class AuraEffect final : public std::enable_shared_from_this<AuraEffect>
	{
	public:
		explicit AuraEffect(AuraContainer& container, const proto::SpellEffect& effect, TimerQueue& timers, int32 basePoints);

	public:
		AuraType GetType() const
		{
			return static_cast<AuraType>(m_effect.aura());
		}

		int32 GetBasePoints() const
		{
			return m_basePoints;
		}

		GameTime GetTickInterval() const
		{
			return m_tickInterval;
		}

		const proto::SpellEffect& GetEffect() const
		{
			return m_effect;
		}

		uint32 GetTickCount()
		{
			return m_tickCount;
		}

		uint32 GetMaxTickCount()
		{
			return m_totalTicks;
		}

		bool IsPeriodic() const
		{
			return m_isPeriodic;
		}

	public:
		void HandleEffect(bool apply);

	private:
		/// Starts periodic ticks.
		void HandlePeriodicBase();

		void HandleModStat(bool apply) const;

		void HandleProcTriggerSpell(bool apply);

		void HandleModDamageDone(bool apply) const;

		void HandleModDamageTaken(bool apply) const;

		void HandleModHealingDone(bool apply) const;

		void HandleModHealingTaken(bool apply) const;

		void HandleModAttackPower(bool apply) const;

		void HandleModAttackSpeed(bool apply) const;

		void HandleModResistance(bool apply) const;

		void HandleRunSpeedModifier(bool apply) const;

		void HandleSwimSpeedModifier(bool apply) const;

		void HandleFlySpeedModifier(bool apply) const;

		void HandleAddModifier(bool apply) const;

	private:
		void HandlePeriodicDamage() const;

		void HandlePeriodicHeal() const;

		void HandlePeriodicEnergize();

		void HandlePeriodicTriggerSpell() const;

		void HandleProcForUnitTarget(GameUnitS& unit);

		bool RollProcChance() const;

		void ForEachProcTarget(const proto::SpellEffect& effect, GameUnitS* instigator, const std::function<bool(GameUnitS&)>& proc);

		bool ExecuteSpellProc(const proto::SpellEntry* procSpell, const GameUnitS& unit) const;

	private:
		AuraContainer& m_container;
		int32 m_basePoints = 0;
		GameTime m_tickInterval = 0;
		const proto::SpellEffect& m_effect;
		Countdown m_tickCountdown;
		uint32 m_totalTicks = 0;
		uint32 m_tickCount = 0;
		scoped_connection m_onTick;
		bool m_isPeriodic = false;
		float m_procChance = 0;

		float m_casterSpellPower = 0.0f;
		float m_casterSpellHeal = 0.0f;

		scoped_connection_container m_procEffects;

	private:

		void StartPeriodicTimer() const;

		void OnTick();
	};

	/// Holds and manages instances of auras for one unit.
	class AuraContainer final : public NonCopyable, public std::enable_shared_from_this<AuraContainer>
	{
	public:

		/// Initializes a new AuraContainer for a specific owner unit.
		explicit AuraContainer(GameUnitS &owner, uint64 casterId, const proto::SpellEntry& spell, GameTime duration, uint64 itemGuid);

		~AuraContainer() override;

	public:
		/// Adds a new aura effect to the container (an Aura can have multiple different effects like a movement speed slow and a damage over time effect - these would be two aura effects).
		///	Effects are grouped by the spell which applied the aura (a spell can have multiple ApplyAura effects). ApplyAura effects with the same target are grouped together in one such aura container.
		void AddAuraEffect(const proto::SpellEffect& effect, int32 basePoints);

		/// Marks the aura as applied or misapplied. If set to true, this will also make the aura effective!
		void SetApplied(bool apply, bool notify = true);

		/// Returns true if the aura can ever expire and is currently expired.
		bool IsExpired() const;

		/// Writes aura update data to a given writer (usually an outgoing network packet for clients).
		void WriteAuraUpdate(io::Writer& writer) const;

		bool HasEffect(AuraType type) const;

		void NotifyOwnerMoved();

	private:
		void HandleAreaAuraTick();

		void RemoveSelf();

		bool ShouldRemoveAreaAuraDueToCasterConditions(const std::shared_ptr<GameUnitS>& caster, uint64 ownerGroupId, const Vector3& position, float range);

		void OnOwnerDamaged(GameUnitS* instigator, uint32 school, DamageType type);

	public:
		/// Gets the owning unit of this aura (the target of the aura).
		GameUnitS& GetOwner() const
		{
			return m_owner;
		}

		/// Determines whether the aura container is currently applied (which means it is active). Only active auras can have an effect and be visible on the client.
		bool IsApplied() const
		{
			return m_applied;
		}

		/// Gets the guid of the caster who caused this aura.
		uint64 GetCasterId() const
		{
			return m_casterId;
		}

		/// Gets the spell id of the spell which caused this aura.
		uint32 GetSpellId() const;

		/// Gets the total aura duration (not the remaining time!) in milliseconds.
		GameTime GetDuration() const
		{
			return m_duration;
		}

		/// Determines whether the aura can ever expire.
		bool DoesExpire() const
		{
			return m_duration > 0;
		}

		/// Determines whether the aura is visible on client side.
		bool IsVisible() const
		{
			return
				IsApplied() && 
				!IsExpired() &&
				(m_spell.attributes(0) & spell_attributes::HiddenClientSide) == 0;
		}

		bool IsAreaAura() const { return m_areaAura; }

		/// Gets the maximum number of base points for a specific aura type.
		int32 GetMaximumBasePoints(AuraType type) const;

		/// Gets the minimum number of base points for a specified aura type.
		int32 GetMinimumBasePoints(AuraType type) const;

		/// Gets the total multiplier value for a specific aura type.
		float GetTotalMultiplier(AuraType type) const;

		/// Returns true if an aura container should be overwritten by this aura container.
		bool ShouldOverwriteAura(AuraContainer& other) const;

		const proto::SpellEntry& GetSpell() const { return m_spell; }

		uint32 GetBaseSpellId() const { return GetSpell().baseid(); }

		uint32 GetSpellRank() const { return GetSpell().rank(); }

		bool HasSameBaseSpellId(const proto::SpellEntry& spell) const;

		GameUnitS* GetCaster() const;

		uint64 GetItemGuid() const { return m_itemGuid; }

	private:

		GameUnitS &m_owner;

		uint64 m_casterId;

		const proto::SpellEntry& m_spell;

		std::vector<std::shared_ptr<AuraEffect>> m_auras;

		bool m_applied = false;

		GameTime m_duration = 0;

		GameTime m_expiration = 0;

		Countdown m_expirationCountdown;

		mutable std::weak_ptr<GameUnitS> m_caster;

		uint64 m_itemGuid = 0;

		scoped_connection m_expiredConnection;

		bool m_areaAura = false;

		Countdown m_areaAuraTick;

		scoped_connection m_areaAuraTickConnection;

		scoped_connection_container m_ownerEventConnections;
	};
}
