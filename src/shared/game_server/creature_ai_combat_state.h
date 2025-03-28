
#pragma once

#include "base/typedefs.h"
#include "creature_ai_state.h"
#include "base/countdown.h"
#include "game_unit_s.h"

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

	protected:

		/// Adds threat of an attacker to the threat list.
		/// @param threatener The unit which generated threat.
		/// @param amount Amount of threat which will be added. May be 0.0 to simply
		///               add the unit to the threat list.
		void AddThreat(GameUnitS& threatener, float amount);

		/// Removes a unit from the threat list. This may change the AI state.
		/// @param threatener The unit that will be removed from the threat list.
		void RemoveThreat(GameUnitS& threatener);

		/// Gets the amount of threat of an attacking unit. Returns 0.0f, if that unit
		/// does not exist, so don't use this method to check if a unit is on the threat
		/// list at all.
		/// @param threatener The unit whose threat-value is requested.
		/// @returns Threat value of the attacking unit, which may be 0
		float GetThreat(const GameUnitS& threatener);

		/// Sets the amount of threat of an attacking unit. This also adds the unit to
		/// the threat list if it isn't already added. Setting the amount to 0 will not
		/// remove the unit from the threat list!
		/// @param threatener The attacking unit whose threat value should be set.
		/// @param amount The new total threat amount, which may be 0.
		void SetThreat(const GameUnitS& threatener, float amount);

		/// Determines the unit with the most amount of threat. May return nullptr if no
		/// unit available.
		/// @returns Pointer of the unit with the highest threat-value or nullptr.
		GameUnitS* GetTopThreatener();

		/// Updates the current victim of the controlled unit based on the current threat table.
		/// A call of this method may change the AI state, in which case this state can be deleted!
		/// So be careful here.
		void UpdateVictim();

		/// Starts chasing a unit so that the controlled unit is in melee hit range.
		/// @param target The unit to chase.
		void ChaseTarget(GameUnitS& target);

		/// Determines the next action to execute. This method calls updateVictim, and thus may
		/// change the AI state. So be careful as well.
		void ChooseNextAction();

	protected:
		std::weak_ptr<GameUnitS> m_combatInitiator;
		ThreatList m_threat;
		UnitSignals m_killedSignals;
		UnitSignals2 m_miscSignals;
		scoped_connection m_onThreatened, m_onMoveTargetChanged;
		scoped_connection m_getThreat, m_setThreat, m_getTopThreatener;
		scoped_connection m_onUnitStateChanged;
		scoped_connection m_onAutoAttackDone;
		GameTime m_lastThreatTime;
		Countdown m_nextActionCountdown;
		uint32 m_stuckCounter;
		
		bool m_isCasting;
		bool m_entered;
		bool m_isRanged;
		bool m_canReset;
	};
}
