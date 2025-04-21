#pragma once

#include "spell_cast.h"

namespace mmo
{
	class NoCastState : public CastState
	{
	public:
		void Activate() override;

		std::pair<SpellCastResult, SpellCasting*> StartCast(
			SpellCast& cast,
			const proto::SpellEntry& spell,
			const SpellTargetMap& target,
			GameTime castTime,
			bool doReplacePreviousCast,
			uint64 itemGuid
		) override;

		void StopCast(SpellInterruptFlags reason, GameTime interruptCooldown = 0) override;

		void OnUserStartsMoving() override;

		void FinishChanneling() override;
	};
}