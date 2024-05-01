
#pragma once

#include "base/signal.h"
#include "math/vector3.h"

namespace mmo
{
	class WorldInstance;
	class GameCreatureS;
	class GameObjectS;
	class GameUnitS;
	class CreatureAIState;

	/// This class controls a creature unit. It decides, which target the controlled creature
	/// should attack, and also controls it's movement.
	class CreatureAI
	{
	public:

		typedef std::shared_ptr<CreatureAIState> CreatureAIStatePtr;

	public:

		/// Defines the home of a creature.
		struct Home final
		{
			Vector3 position;
			float orientation;
			float radius;

			/// Initializes a new instance of the Home class.
			/// @param pos The position of this home point in world units.
			/// @param o The orientation of this home point in radians.
			/// @param radius The tolerance radius of this home point in world units.
			explicit Home(const Vector3& pos, float o = 0.0f, float radius = 0.0f)
				: position(pos)
				, orientation(o)
				, radius(radius)
			{
			}
			/// Default destructor
			~Home();
		};

	public:

		/// Initializes a new instance of the CreatureAI class.
		/// @param controlled The controlled unit, which should also own this class instance.
		/// @param home The home point of the controlled unit. The unit will always return to it's
		///             home point if it returns to it's idle state.
		explicit CreatureAI(
			GameCreatureS& controlled,
			const Home& home
		);

		/// Default destructor. Overridden in case of inheritance of the CreatureAI class.
		virtual ~CreatureAI();

		/// Gets a reference of the controlled creature.
		GameCreatureS& GetControlled() const;

		/// Gets a reference of the controlled units home.
		const Home& GetHome() const;

		/// Sets a new home position
		void SetHome(Home home);

		/// Enters the idle state.
		void Idle();

		/// Enters the combat state. This is usually called from the creatures idle state.
		void EnterCombat(GameUnitS& victim);

		/// Makes the creature reset, leaving the combat state, reviving itself and run back
		/// to it's home position.
		void Reset();

		/// Executed by the underlying creature when combat movement gets enabled or disabled.
		void OnCombatMovementChanged();

		/// 
		void OnCreatureMovementChanged();

		/// Called when the controlled unit moved.
		void OnControlledMoved();

		/// Determines if this creature's AI is currently in evade mode.
		bool IsEvading() const { return m_evading; }

	public:
		// These methods are meant to be called by the AI states on specific events.
		void OnThreatened(GameUnitS& threat, float amount);

	protected:

		void SetState(CreatureAIStatePtr state);

		virtual void OnSpawned(WorldInstance& instance);

		virtual void OnDespawned(GameObjectS&);

	private:

		GameCreatureS& m_controlled;
		CreatureAIStatePtr m_state;
		Home m_home;
		scoped_connection m_onSpawned, m_onDamaged;
		scoped_connection m_onKilled;
		scoped_connection m_onDespawned;
		bool m_evading;
	};
}
