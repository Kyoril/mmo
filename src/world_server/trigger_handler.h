#pragma once

#include "game_server/trigger_handler.h"

#include <list>
#include <memory>

namespace mmo
{
	namespace proto
	{
		class TriggerAction;
	}

	class PlayerManager;

	/// 
	class TriggerHandler final : public ITriggerHandler
	{
	public:
		/// 
		explicit TriggerHandler(proto::Project& project, PlayerManager& playerManager, TimerQueue& timers);

		/// Fires a trigger event.
		virtual void ExecuteTrigger(const proto::TriggerEntry& entry, TriggerContext context, uint32 actionOffset = 0, bool ignoreProbability = false) override;

	private:

		int32 getActionData(const proto::TriggerAction& action, uint32 index) const;
		const String& getActionText(const proto::TriggerAction& action, uint32 index) const;
		WorldInstance* getWorldInstance(GameObjectS* owner) const;
		GameObjectS* getActionTarget(const proto::TriggerAction& action, TriggerContext& context);
		bool playSoundEntry(uint32 sound, GameObjectS* source);

		void handleTrigger(const proto::TriggerAction& action, TriggerContext& context);
		void handleSay(const proto::TriggerAction& action, TriggerContext& context);
		void handleYell(const proto::TriggerAction& action, TriggerContext& context);
		void handleSetWorldObjectState(const proto::TriggerAction& action, TriggerContext& context);
		void handleSetSpawnState(const proto::TriggerAction& action, TriggerContext& context);
		void handleSetRespawnState(const proto::TriggerAction& action, TriggerContext& context);
		void handleCastSpell(const proto::TriggerAction& action, TriggerContext& context);
		void handleMoveTo(const proto::TriggerAction& action, TriggerContext& context);
		void handleSetCombatMovement(const proto::TriggerAction& action, TriggerContext& context);
		void handleStopAutoAttack(const proto::TriggerAction& action, TriggerContext& context);
		void handleCancelCast(const proto::TriggerAction& action, TriggerContext& context);
		void handleSetStandState(const proto::TriggerAction& action, TriggerContext& context);
		void handleSetVirtualEquipmentSlot(const proto::TriggerAction& action, TriggerContext& context);
		void handleSetPhase(const proto::TriggerAction& action, TriggerContext& context);
		void handleSetSpellCooldown(const proto::TriggerAction& action, TriggerContext& context);
		void handleQuestKillCredit(const proto::TriggerAction& action, TriggerContext& context);
		void handleQuestEventOrExploration(const proto::TriggerAction& action, TriggerContext& context);
		void handleSetVariable(const proto::TriggerAction& action, TriggerContext& context);
		void handleDismount(const proto::TriggerAction& action, TriggerContext& context);
		void handleSetMount(const proto::TriggerAction& action, TriggerContext& context);
		void handleDespawn(const proto::TriggerAction& action, TriggerContext& context);

	private:

		bool checkInCombatFlag(const proto::TriggerEntry& entry, const GameObjectS* owner);
		bool checkOwnerAliveFlag(const proto::TriggerEntry& entry, const GameObjectS* owner);

	private:

		proto::Project& m_project;
		PlayerManager& m_playerManager;
		TimerQueue& m_timers;
		std::list<std::unique_ptr<Countdown>> m_delays;
	};
	
}