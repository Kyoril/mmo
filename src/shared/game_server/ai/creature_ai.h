// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/signal.h"
#include "base/typedefs.h"
#include "game/pet_defines.h"
#include "math/vector3.h"

#include <memory>
#include <vector>

namespace mmo
{
	namespace proto
	{
		class UnitSpellEntry;
	}

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
		[[nodiscard]] const Home& GetHome() const;

		/// Whether the controlled creature may die: false while a combat script forbids it
		/// (e.g. a training dummy in combat).
		[[nodiscard]] bool CanControlledDie() const;

		/// Sets a new home position
		/// @param home The new home position of the controlled unit.
		void SetHome(Home home);

		/// Saves the world position the creature occupied when it left the patrol route
		/// (e.g. when entering combat). The reset state uses this to return the creature
		/// to the route rather than back to its spawn position.
		/// @param position The position to save.
		void SavePatrolReturnPosition(const Vector3& position);

		/// Gets the saved patrol return position.
		/// @return The saved position.
		[[nodiscard]] const Vector3& GetSavedPatrolReturnPosition() const;

		/// Returns whether a patrol return position has been saved.
		/// @return True if a saved position is available.
		[[nodiscard]] bool HasSavedPatrolReturnPosition() const;

		/// Clears any previously saved patrol return position.
		void ClearSavedPatrolReturnPosition();

		/// Saves the patrol waypoint index to resume from after leaving idle
		/// (e.g. entering combat). Stores the first waypoint of the chain the
		/// creature was traversing so direction is preserved on resume.
		/// @param index Zero-based waypoint index.
		void SavePatrolWaypointIndex(size_t index);

		/// Gets the saved patrol waypoint index.
		/// @return The saved index.
		[[nodiscard]] size_t GetSavedPatrolWaypointIndex() const;

		/// Returns whether a patrol waypoint index has been saved.
		/// @return True if a saved index is available.
		[[nodiscard]] bool HasSavedPatrolWaypointIndex() const;

		/// Clears any previously saved patrol waypoint index.
		void ClearSavedPatrolWaypointIndex();

		/// Enters the idle state.
		void Idle();

		/// Enters the combat state. This is usually called from the creatures idle state.
		void EnterCombat(GameUnitS& victim);

		/// Enters the alert state after detecting a stealthed hostile unit. The creature
		/// stops, faces the unit and decides after a short duration whether to engage.
		/// Does nothing if the creature is already alerted.
		void EnterAlert(GameUnitS& target);

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

		/// @brief Returns the current AI state.
		/// @return Pointer to the current AI state, or nullptr if none.
		CreatureAIState* GetCurrentState() const { return m_state.get(); }

	public:
		// === Pet mode ===
		// A creature owned by a unit runs in pet mode: out of combat it follows its owner (or
		// stays where it was told to) instead of idling at a home point, it engages what its
		// strategy picks from its owner's fight, and it never resets home or evade-heals.

		/// Switches the AI into pet mode for an owner and enters the pet state. Called by
		/// every pet spawn (SummonPet effect, and persistent pets) after the pet entered the world.
		/// @param owner The pet's owner.
		void EnterPetMode(GameUnitS& owner);

		/// @return Whether the AI runs in pet mode.
		[[nodiscard]] bool IsPetMode() const { return m_petMode; }

		/// @return The owner of the pet, or nullptr if not in pet mode or the owner is gone.
		[[nodiscard]] std::shared_ptr<GameUnitS> GetPetOwner() const { return m_petOwner.lock(); }

		/// @return The pet's strategy.
		[[nodiscard]] pet_strategy::Type GetPetStrategy() const { return m_petStrategy; }

		/// @return Whether the pet was told to stay where it is.
		[[nodiscard]] bool IsPetStaying() const { return m_petStaying; }

		/// @return Where the pet stays while IsPetStaying().
		[[nodiscard]] const Vector3& GetPetStayPosition() const { return m_petStayPosition; }

		/// @return Guid of the unit the owner commanded the pet to attack, or 0.
		[[nodiscard]] uint64 GetPetCommandedTarget() const { return m_petCommandedTarget; }

		/// Forgets the commanded attack target (it died or is gone).
		void ClearPetCommandedTarget() { m_petCommandedTarget = 0; }

		/// @return Whether the pet was called back with Follow and does not engage on its own until
		///         something new happens (the owner attacks or is attacked, the pet is hit, or a command).
		[[nodiscard]] bool IsPetRecalled() const { return m_petRecalled; }

		/// @return Whether the pet only ever fights what its owner attacks (or what it is commanded
		///         to): it neither defends its owner nor itself. Temporary pets work this way.
		[[nodiscard]] bool IsPetOwnerTargetOnly() const { return m_petOwnerTargetOnly; }

		/// @return What the owner fights, for a pet that only fights its owner's target: the unit the
		///         owner attacked last, else the owner's auto attack victim, if the pet can attack and
		///         see it. nullptr if there is none or the AI is not in pet mode.
		[[nodiscard]] GameUnitS* FindPetOwnerTarget() const;

		/// Executes a command of the pet's owner. Dismiss is handled by the PetManager.
		/// @param command The command.
		/// @param targetGuid The target of an Attack command, otherwise ignored.
		/// @param param SetStrategy: the new pet_strategy, otherwise ignored.
		void OnPetCommand(pet_command::Type command, uint64 targetGuid, uint32 param);

		/// Called when the owner starts attacking a unit: auto attack, or a harmful spell cast at it.
		/// An assisting pet joins in, unless it has a commanded target.
		/// @param victim The unit the owner attacks.
		void OnOwnerAttack(GameUnitS& victim);

		/// Called when a unit starts attacking the owner. An assisting pet that is not fighting yet
		/// defends its owner, unless it only fights its owner's target.
		/// @param attacker The unit attacking the owner.
		void OnOwnerAttacked(GameUnitS& attacker);

		/// Called when a unit attacks or threatens the pet itself. An assisting pet defends itself,
		/// unless it only fights its owner's target.
		/// @param attacker The unit attacking the pet.
		void OnPetAttacked(GameUnitS& attacker);

		/// Engages a unit: enters combat against it, or makes the combat state reconsider its
		/// victim when already fighting. Does nothing for friendly, dead or invalid targets.
		/// @param target The unit to attack.
		void EngagePetTarget(GameUnitS& target);

		/// @return Whether a unit is within reach of a staying pet's stay position (melee reach), or
		///         true if the pet is not staying. A staying pet only takes on what comes within reach.
		[[nodiscard]] bool IsWithinPetStayReach(const GameUnitS& unit) const;

		/// Sets combat spells the pet uses in addition to its creature entry's `creaturespells`
		/// (hook for persistent pets: their known spells plus the pet entry's `ai_spells` tuning).
		/// Takes effect when the pet enters combat next.
		/// @param spells The extra spells, chosen like creature spells (priority, cooldowns, ranges).
		void SetPetSpells(std::vector<proto::UnitSpellEntry> spells);

		/// @return The extra combat spells set with SetPetSpells.
		[[nodiscard]] const std::vector<proto::UnitSpellEntry>& GetPetSpells() const { return m_petSpells; }

		/// The base run speed the pet had before it started matching its owner's speed.
		[[nodiscard]] float GetPetNaturalRunBaseSpeed() const { return m_petNaturalRunBaseSpeed; }

	public:
		// These methods are meant to be called by the AI states on specific events.
		void OnThreatened(GameUnitS& threat, float amount);

		/// Calls nearby friendly creatures that are not yet in combat into the fight against an
		/// enemy that threatened this creature. Does nothing if the threat is friendly.
		/// @param threat The unit that attacked or threatened this creature.
		void CallForHelp(GameUnitS& threat);

		/// Answers a nearby ally's call for help: enters combat against the threat if this creature is
		/// idle or preparing. Does not call for help itself, so one call reaches one wave of allies.
		/// @param threat The unit that attacked the ally.
		void OnCalledForHelp(GameUnitS& threat);

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
		Vector3 m_savedPatrolReturnPosition;
		bool m_hasPatrolReturnPosition;
		size_t m_savedPatrolWaypointIndex;
		bool m_hasPatrolWaypointSaved;

		// === Pet mode ===
		bool m_petMode = false;
		std::weak_ptr<GameUnitS> m_petOwner;
		pet_strategy::Type m_petStrategy = pet_strategy::Assist;
		bool m_petStaying = false;
		Vector3 m_petStayPosition;
		uint64 m_petCommandedTarget = 0;
		bool m_petRecalled = false;
		bool m_petOwnerTargetOnly = false;
		float m_petNaturalRunBaseSpeed = 0.0f;
		std::vector<proto::UnitSpellEntry> m_petSpells;

	};
}
