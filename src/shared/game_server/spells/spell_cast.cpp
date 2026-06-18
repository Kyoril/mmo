// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/spells/spell_cast.h"

#include "game_server/world/each_tile_in_region.h"
#include "game_server/objects/game_unit_s.h"
#include "no_cast_state.h"
#include "single_cast_state.h"
#include "log/default_log_levels.h"

namespace mmo
{
	void CastSpell(SpellCast& cast, const proto::SpellEntry& spell, const SpellTargetMap& target, GameTime castTime, uint64 itemGuid, bool isProc)
	{
		auto newState = std::make_shared<SingleCastState>(cast, spell, target, castTime, isProc, itemGuid);

		cast.SetState(std::move(newState));
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
			CastSpell(*this, spell, target, castTime, itemGuid, true);
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
