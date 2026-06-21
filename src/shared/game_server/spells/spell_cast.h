// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game/spell_target_map.h"
#include "base/signal.h"
#include "base/timer_queue.h"
#include "shared/proto_data/spells.pb.h"

#include <memory>

namespace mmo
{
	class GameUnitS;

	class SpellCast;

	class CastState
	{
	public:
		virtual ~CastState() { }

		virtual void Activate() = 0;

		virtual SpellCastResult StartCast(
			SpellCast& cast,
			const proto::SpellEntry& spell,
			const SpellTargetMap& target,
			GameTime castTime,
			bool doReplacePreviousCast,
			uint64 itemGuid
		) = 0;

		virtual void StopCast(SpellInterruptFlags reason, GameTime interruptCooldown = 0) = 0;

		virtual void OnUserStartsMoving() = 0;

		virtual void FinishChanneling() = 0;

		/// @returns The spell that is currently being cast or channeled by this state,
		///          or nullptr if no spell is active (NoCastState).
		virtual const proto::SpellEntry* GetSpell() const = 0;
	};

	void CastSpell(
		SpellCast& cast,
		const proto::SpellEntry& spell,
		const SpellTargetMap& target,
		GameTime castTime,
		uint64 itemGuid,
		bool isProc = false
	);

	class SpellCast
	{
	public:
		explicit SpellCast(TimerQueue& timer, GameUnitS& executor);

		GameUnitS& GetExecuter() const { return m_executor; }

		TimerQueue& GetTimerQueue() const { return m_timerQueue; }

		SpellCastResult StartCast(
			const proto::SpellEntry& spell,
			const SpellTargetMap& target,
			GameTime castTime,
			bool isProc, 
			uint64 itemGuid);

		void StopCast(SpellInterruptFlags reason, GameTime interruptCooldown = 0) const;

		void OnUserStartsMoving();

		void SetState(const std::shared_ptr<CastState>& castState);

		void FinishChanneling();

		/// @returns The spell currently being cast or channeled, or nullptr if idle.
		const proto::SpellEntry* GetSpell() const { return m_castState->GetSpell(); }

		int32 CalculatePowerCost(const proto::SpellEntry& spell) const;

	public:
		signal<void(bool)> ended;

	private:

		TimerQueue& m_timerQueue;
		GameUnitS& m_executor;
		std::shared_ptr<CastState> m_castState;
	};
}
