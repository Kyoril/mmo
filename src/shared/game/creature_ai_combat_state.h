
#pragma once

#include "base/typedefs.h"
#include "creature_ai_state.h"
#include "base/countdown.h"
#include "game_unit_s.h"
#include "shared/proto_data/spells.pb.h"
#include "shared/proto_data/units.pb.h"

namespace mmo
{
	/// Handle the idle state of a creature AI. In this state, most units
	/// watch for hostile units which come close enough, and start attacking these
	/// units.
	class CreatureAICombatState : public CreatureAIState
	{
		/// Represents an entry in the threat list of this unit.
		struct ThreatEntry
		{
			/// Threatening unit.
			std::weak_ptr<GameUnitS> threatener;
			float amount;

			explicit ThreatEntry(GameUnitS& threatener, float amount = 0.0f)
				: threatener(std::static_pointer_cast<GameUnitS>(threatener.shared_from_this()))
				, amount(amount)
			{
			}
		};

		typedef std::map<::uint64, ThreatEntry> ThreatList;
		typedef std::map<uint64, scoped_connection> UnitSignals;
		typedef std::map<uint64, scoped_connection_container> UnitSignals2;

	public:

		/// Initializes a new instance of the CreatureAIIdleState class.
		/// @param ai The ai class instance this state belongs to.
		explicit CreatureAICombatState(CreatureAI& ai, GameUnitS& victim);
		/// Default destructor.
		~CreatureAICombatState() override;

		/// @copydoc CreatureAIState::OnEnter()
		virtual void OnEnter() override;
		/// @copydoc CreatureAIState::OnLeave()
		virtual void OnLeave() override;
		/// @copydoc CreatureAIState::OnDamage()
		virtual void OnDamage(GameUnitS& attacker) override;
		/// @copydoc CreatureAIState::OnCombatMovementChanged()
		virtual void OnCombatMovementChanged() override;
		/// @copydoc CreatureAIState::OnControlledMoved()
		virtual void OnControlledMoved() override;
	};
}
