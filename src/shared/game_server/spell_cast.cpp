
#include "spell_cast.h"

#include "each_tile_in_region.h"
#include "game_unit_s.h"
#include "no_cast_state.h"
#include "single_cast_state.h"
#include "log/default_log_levels.h"

namespace mmo
{
	SpellCasting& CastSpell(SpellCast& cast, const proto::SpellEntry& spell, const SpellTargetMap& target, GameTime castTime, uint64 itemGuid)
	{
		auto newState = std::make_shared<SingleCastState>(cast, spell, target, castTime, false, itemGuid);

		auto& casting = newState->GetCasting();
		cast.SetState(std::move(newState));
		return casting;
	}

	SpellCast::SpellCast(TimerQueue& timer, GameUnitS& executor)
		: m_timerQueue(timer)
		, m_executor(executor)
		, m_castState(std::make_shared<NoCastState>())
	{
	}

	std::pair<SpellCastResult, SpellCasting*> SpellCast::StartCast(const proto::SpellEntry& spell, const SpellTargetMap& target, const GameTime castTime, bool isProc, uint64 itemGuid)
	{
		ASSERT(m_castState);

		// TODO: all kind of checks

		const auto* instance = m_executor.GetWorldInstance();
		if (!instance)
		{
			ELOG("Caster is not in a world instance");
			return std::make_pair(spell_cast_result::FailedError, nullptr);
		}

		const auto* map = instance->GetMapData();
		if (!map)
		{
			ELOG("World instance has no map data loaded");
			return std::make_pair(spell_cast_result::FailedError, nullptr);
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
		if (m_executor.SpellHasCooldown(spell.id(), spell.category()))
		{
			return std::make_pair(spell_cast_result::FailedNotReady, nullptr);
		}

		// Check if we have enough resources for that spell
		if (isProc)
		{
			const auto newCastState = std::make_shared<SingleCastState>(
				*this, spell, std::move(target), castTime, true, itemGuid
			);
			newCastState->Activate();

			return std::make_pair(spell_cast_result::CastOkay, nullptr);
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
