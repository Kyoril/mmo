
#include "no_cast_state.h"

namespace mmo
{
	void NoCastState::Activate()
	{
	}

	std::pair<SpellCastResult, SpellCasting*> NoCastState::StartCast(SpellCast& cast, const proto::SpellEntry& spell,
		const SpellTargetMap& target, GameTime castTime, bool doReplacePreviousCast)
	{
		SpellCasting& casting = CastSpell(
			cast,
			spell,
			std::move(target),
			castTime
		);

		return std::make_pair(spell_cast_result::CastOkay, &casting);
	}

	void NoCastState::StopCast(GameTime interruptCooldown)
	{
	}

	void NoCastState::OnUserStartsMoving()
	{
	}

	void NoCastState::FinishChanneling()
	{
	}
}
