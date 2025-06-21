// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <functional>
#include <memory>

#include "base/countdown.h"
#include "base/typedefs.h"
#include "game/aura.h"

namespace mmo
{
	class TimerQueue;

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
		explicit AuraEffect(AuraContainer& container, const proto::SpellEffect& effect, TimerQueue& timers, ::int32 basePoints);

	public:
		AuraType GetType() const;

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

		void HandleProcEffect(GameUnitS* instigator);

	public:
		void HandleEffect(bool apply);

	private:
		/// Starts periodic ticks.
		void HandlePeriodicBase();

		void HandleModStat(bool apply) const;

		void HandleModStatPct(bool apply) const;

		void HandleModDamageDone(bool apply) const;

		void HandleModDamageTaken(bool apply) const;

		void HandleModHealingDone(bool apply) const;

		void HandleModHealingTaken(bool apply) const;

		void HandleModAttackPower(bool apply) const;

		void HandleModAttackSpeed(bool apply) const;

		void HandleModResistance(bool apply) const;

		void HandleModResistancePct(bool apply) const;

		void HandleRunSpeedModifier(bool apply) const;

		void HandleSwimSpeedModifier(bool apply) const;

		void HandleFlySpeedModifier(bool apply) const;

		void HandleAddModifier(bool apply) const;

		void HandleModRoot(bool apply) const;

		void HandleModStun(bool apply) const;

		void HandleModFear(bool apply) const;

		void HandleModSleep(bool apply) const;

		void HandleModVisibility(bool apply) const;

	private:
		void HandlePeriodicDamage() const;

		void HandlePeriodicHeal() const;

		void HandlePeriodicEnergize();

		void HandlePeriodicTriggerSpell() const;

		void HandleProcForUnitTarget(GameUnitS& unit);

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

		float m_casterSpellPower = 0.0f;
		float m_casterSpellHeal = 0.0f;

	private:

		void StartPeriodicTimer() const;

		void OnTick();
	};

}
