// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/spells/spell_cast.h"

#include "game_server/world/each_tile_in_region.h"
#include "game_server/objects/game_unit_s.h"
#include "no_cast_state.h"
#include "single_cast_state.h"
#include "log/default_log_levels.h"

#include <algorithm>

namespace mmo
{
	SpellCastResult CastSpell(SpellCast& cast, const proto::SpellEntry& spell, const SpellTargetMap& target, GameTime castTime, uint64 itemGuid)
	{
		auto newState = std::make_shared<SingleCastState>(cast, spell, target, castTime, false, itemGuid);

		// SetState activates the state, which is where validation runs. The state stays owned by
		// the cast afterwards even when validation rejected it, so reading its result here is safe.
		SingleCastState* activated = newState.get();
		cast.SetState(std::move(newState));

		return activated->GetActivationResult();
	}

	SpellCast::SpellCast(TimerQueue& timer, GameUnitS& executor)
		: m_timerQueue(timer)
		, m_executor(executor)
		, m_castState(std::make_shared<NoCastState>())
	{
	}

	SpellCastResult SpellCast::StartCast(const proto::SpellEntry& spell, const SpellTargetMap& target, const GameTime castTime, bool isProc, uint64 itemGuid)
	{
		ASSERT(m_castState);

		// TODO: all kind of checks

		const auto* instance = m_executor.GetWorldInstance();
		if (!instance)
		{
			ELOG("Caster is not in a world instance");
			return spell_cast_result::FailedError;
		}

		const auto* map = instance->GetMapData();
		if (!map)
		{
			ELOG("World instance has no map data loaded");
			return spell_cast_result::FailedError;
		}

		// Check for focus object
		if (spell.focusobject() != 0)
		{
			// TODO: Find required focus object
		}

		if (spell.mechanic() != 0)
		{
			// TODO: Check mechanic requirement
		}

		// Check for cooldown
		if (!isProc && m_executor.SpellHasCooldown(spell.id(), spell.category(), spell.cooldownflags()))
		{
			return spell_cast_result::FailedNotReady;
		}

		// Check if we have enough resources for that spell
		if (isProc)
		{
			StartProcCast(spell, target, castTime, itemGuid);
			return spell_cast_result::CastOkay;
		}

		return m_castState->StartCast(
			*this,
			spell,
			target,
			castTime,
			false,
			itemGuid);
	}

	void SpellCast::StopCast(SpellInterruptFlags reason, const GameTime interruptCooldown) const
	{
		ASSERT(m_castState);
		m_castState->StopCast(reason, interruptCooldown);
	}

	void SpellCast::AbandonCast()
	{
		ASSERT(m_castState);
		m_castState->AbandonCast();

		// Back to idle so nothing can reach the abandoned state through this cast any more.
		m_castState = std::make_shared<NoCastState>();

		// A proc still resolving reaches the caster through this cast just the same, e.g. when
		// its projectile lands.
		auto procCasts = std::move(m_procCasts);
		m_procCasts.clear();
		for (const auto& weakProc : procCasts)
		{
			if (const auto procCast = weakProc.lock())
			{
				procCast->AbandonCast();
			}
		}
	}

	void SpellCast::StartProcCast(const proto::SpellEntry& spell, const SpellTargetMap& target, const GameTime castTime, const uint64 itemGuid)
	{
		// A proc never becomes the current state. Triggered spells go off while the caster is
		// casting or channeling something else - a channel's own periodic trigger does so on every
		// tick - and installing the proc there swapped that cast out: it kept running on its own
		// timer, but movement, interrupts and control effects reached the finished proc instead.
		m_procCasts.erase(
			std::remove_if(m_procCasts.begin(), m_procCasts.end(),
				[](const std::weak_ptr<CastState>& weakProc) { return weakProc.expired(); }),
			m_procCasts.end());

		auto procCast = std::make_shared<SingleCastState>(*this, spell, target, castTime, true, itemGuid);
		m_procCasts.push_back(procCast);

		// The state holds itself from here until it has resolved.
		procCast->Activate();
	}

	void SpellCast::OnUserStartsMoving()
	{
		ASSERT(m_castState);
		m_castState->OnUserStartsMoving();
	}

	void SpellCast::SetState(const std::shared_ptr<CastState>& castState)
	{
		ASSERT(castState);
		ASSERT(m_castState);

		m_castState = std::move(castState);
		m_castState->Activate();
	}

	void SpellCast::FinishChanneling()
	{
		ASSERT(m_castState);

		m_castState->FinishChanneling();
	}

	int32 SpellCast::CalculatePowerCost(const proto::SpellEntry& spell) const
	{
		int32 cost = spell.cost();

		m_executor.ApplySpellMod(spell_mod_op::Cost, spell.id(), cost);

		return std::max(0, cost);
	}
}
