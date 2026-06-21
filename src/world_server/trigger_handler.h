#pragma once

#include "game_server/trigger_handler.h"
#include "shared/proto_data/triggers.pb.h"

#include <list>
#include <memory>
#include <vector>
#include <functional>

namespace mmo
{
	namespace proto
	{
		class TriggerAction;
	}

	class PlayerManager;

	/// Handles trigger events and actions in the game world.
	class TriggerHandler final : public ITriggerHandler
	{
	public:
		/// Constructs a TriggerHandler instance.
		/// @param project Reference to the project containing static game data.
		/// @param playerManager Reference to the player manager.
		/// @param timers Reference to the timer queue.
		explicit TriggerHandler(proto::Project& project, PlayerManager& playerManager, TimerQueue& timers);

		/// Default destructor.
		~TriggerHandler() override = default;

		/// Fires a trigger event.
		/// @param entry The trigger entry to execute.
		/// @param context The context in which the trigger is executed.
		/// @param actionOffset The offset for the action to execute (default is 0).
		/// @param ignoreProbability Whether to ignore probability checks (default is false).
		void ExecuteTrigger(const proto::TriggerEntry& entry, TriggerContext context, uint32 actionOffset = 0, bool ignoreProbability = false) override;

	private:
		/// Retrieves action data from a trigger action.
		/// @param action The trigger action to retrieve data from.
		/// @param index The index of the data to retrieve.
		/// @return The action data as an int32.
		int32 GetActionData(const proto::TriggerAction& action, uint32 index) const;

		/// Retrieves action text from a trigger action.
		/// @param action The trigger action to retrieve text from.
		/// @param index The index of the text to retrieve.
		/// @return The action text as a string.
		const String& GetActionText(const proto::TriggerAction& action, uint32 index) const;

		/// Retrieves the world instance associated with a game object.
		/// @param owner The game object to retrieve the world instance for.
		/// @return A pointer to the world instance, or nullptr if none exists.
		WorldInstance* GetWorldInstance(GameObjectS* owner) const;

		/// Retrieves the world instance for a trigger context. Prefers the explicit world set on the
		/// context (which is available even for ownerless instance-global triggers) and falls back to
		/// the owner's world instance.
		/// @param context The trigger context to resolve the world instance for.
		/// @return A pointer to the world instance, or nullptr if none exists.
		WorldInstance* GetWorldInstance(TriggerContext& context) const;

		/// Retrieves the target of a trigger action.
		/// @param action The trigger action to retrieve the target from.
		/// @param context The context in which the action is executed.
		/// @return A pointer to the target game object.
		GameObjectS* GetActionTarget(const proto::TriggerAction& action, TriggerContext& context);

		/// Plays a sound entry.
		/// @param sound The ID of the sound to play.
		/// @param source The source game object for the sound.
		/// @return True if the sound was played successfully, false otherwise.
		bool PlaySoundEntry(uint32 sound, GameObjectS* source);

	private:
		// Action handlers

		/// Handles a generic trigger action.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleTrigger(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles a "say" action.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleSay(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles a "yell" action.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleYell(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles an "emote" action.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleEmote(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles setting the state of a world object.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleSetWorldObjectState(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles setting the spawn state of an object.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleSetSpawnState(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles setting the respawn state of an object.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleSetRespawnState(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles casting a spell.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleCastSpell(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles moving an object to a specified location.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleMoveTo(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles setting combat movement for an object.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleSetCombatMovement(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles stopping auto-attack for an object.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleStopAutoAttack(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles canceling a spell cast.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleCancelCast(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles setting the stand state of an object.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleSetStandState(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles setting a virtual equipment slot for an object.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleSetVirtualEquipmentSlot(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles setting the phase of an object.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleSetPhase(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles setting a spell cooldown for an object.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleSetSpellCooldown(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles granting quest kill credit.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleQuestKillCredit(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles triggering a quest event or exploration.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleQuestEventOrExploration(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles setting a variable for an object.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleSetVariable(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles dismounting an object.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleDismount(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles setting a mount for an object.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleSetMount(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles despawning an object.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleDespawn(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles teleporting an object.
		/// @param action The trigger action to handle.
		/// @param context The context in which the action is executed.
		void HandleTeleport(const proto::TriggerAction& action, TriggerContext& context);

		void HandleSetEncounterState(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles summoning a new creature into the world instance.
		void HandleSummonCreature(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles forcing a creature's threat towards a target to the top of its threat table.
		void HandleTaunt(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles modifying the threat a unit has generated on the owning creature.
		void HandleModifyThreat(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles wiping the entire threat table of a creature.
		void HandleResetThreat(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles applying the auras of a spell directly to a target.
		void HandleApplyAura(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles removing all auras of a spell from a target.
		void HandleRemoveAura(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles setting an instance-scoped variable.
		void HandleSetInstanceVariable(const proto::TriggerAction& action, TriggerContext& context);

		/// Handles broadcasting a message to all players in the instance.
		void HandleBroadcastMessage(const proto::TriggerAction& action, TriggerContext& context);

		/// Collects all alive players currently in the given world instance.
		/// @param world The world instance to enumerate. May be nullptr.
		/// @return Vector of alive player units (possibly empty).
		std::vector<GameUnitS*> GetPlayersInWorld(WorldInstance* world) const;

		/// Invokes a callback for each target an action resolves to. For most targets this is a single
		/// object; for the AllPlayers target it is every player in the instance.
		/// @param action The action whose target should be resolved.
		/// @param context The trigger context.
		/// @param callback Callable receiving a `GameObjectS&` for each resolved target.
		void ForEachActionTarget(const proto::TriggerAction& action, TriggerContext& context, const std::function<void(GameObjectS&)>& callback);

		bool EvaluateCondition(const proto::TriggerCondition& condition, TriggerContext& context);

		double EvaluateTriggerFunction(proto::TriggerFunction func, const google::protobuf::RepeatedField<google::protobuf::int64>& funcData, TriggerContext& context);

	private:
		/// Checks if the in-combat flag is set for a trigger entry.
		/// @param entry The trigger entry to check.
		/// @param owner The owner game object.
		/// @return True if the in-combat flag is set, false otherwise.
		bool CheckInCombatFlag(const proto::TriggerEntry& entry, const GameObjectS* owner);

		/// Checks if the owner alive flag is set for a trigger entry.
		/// @param entry The trigger entry to check.
		/// @param owner The owner game object.
		/// @return True if the owner alive flag is set, false otherwise.
		bool CheckOwnerAliveFlag(const proto::TriggerEntry& entry, const GameObjectS* owner);

	private:
		proto::Project& m_project; ///< Reference to the project containing static game data.
		PlayerManager& m_playerManager; ///< Reference to the player manager.
		TimerQueue& m_timers; ///< Reference to the timer queue.
		std::list<std::unique_ptr<Countdown>> m_delays; ///< List of countdown timers for delayed actions.
	};
	
}