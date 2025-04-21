
#pragma once

#include <memory>

namespace mmo
{
	class CreatureAI;
	class GameCreatureS;
	class GameUnitS;

	/// Represents a specific AI state of a creature (for example the idle state or the combat state).
	class CreatureAIState : public std::enable_shared_from_this<CreatureAIState>
	{
	public:

		/// Initializes a new instance of the CreatureAIState class.
		/// @param ai The ai class instance this state belongs to.
		explicit CreatureAIState(CreatureAI& ai);
		/// Default destructor.
		virtual ~CreatureAIState();

		/// Gets a reference of the AI this state belongs to.
		CreatureAI& GetAI() const;
		/// Gets a reference of the controlled creature.
		GameCreatureS& GetControlled() const;
		/// Executed when the AI state is activated.
		virtual void OnEnter();
		/// Executed when the AI state becomes inactive.
		virtual void OnLeave();
		/// Executed when the controlled unit was damaged by a known attacker (not executed when
		/// the attacker is unknown, for example in case of Auras where the caster is despawned)
		virtual void OnDamage(GameUnitS& attacker);
		/// Executed when the controlled unit was healed by a known healer (same as onDamage).
		virtual void OnHeal(GameUnitS& healer);
		/// Executed when the controlled unit dies.
		virtual void OnControlledDeath();
		/// Executed when combat movement for the controlled unit is enabled or disabled.
		virtual void OnCombatMovementChanged();
		/// 
		virtual void OnCreatureMovementChanged();
		/// Executed when the controlled unit moved.
		virtual void OnControlledMoved();

		/// Determines if this ai state is currently active.
		bool IsActive() const { return m_isActive; }

	private:

		CreatureAI& m_ai;
		bool m_isActive;
	};
}
