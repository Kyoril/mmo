
#include "trigger_handler.h"

#include "vector_sink.h"
#include "base/utilities.h"
#include "game_server/game_creature_s.h"
#include "game_server/universe.h"
#include "log/default_log_levels.h"
#include "proto_data/project.h"
#include "shared/proto_data/triggers.pb.h"
#include "proto_data/trigger_helper.h"

namespace mmo
{
	TriggerHandler::TriggerHandler(proto::Project& project, PlayerManager& playerManager, TimerQueue& timers)
		: m_project(project)
		, m_playerManager(playerManager)
		, m_timers(timers)
	{
	}

	void TriggerHandler::ExecuteTrigger(const proto::TriggerEntry& entry, TriggerContext context, uint32 actionOffset/* = 0*/, bool ignoreProbability/* = false*/)
	{
		// Keep owner alive if provided
		std::shared_ptr<GameObjectS> strongOwner;
		if (context.owner) strongOwner = context.owner->shared_from_this();
		auto weakOwner = std::weak_ptr<GameObjectS>(strongOwner);

		// Remove all expired delays
		for (auto it = m_delays.begin(); it != m_delays.end();)
		{
			if (!(*it)->IsRunning())
			{
				it = m_delays.erase(it);
			}
			else
			{
				++it;
			}
		}

		if (static_cast<int>(actionOffset) >= entry.actions_size())
		{
			// Nothing to do here
			return;
		}

		// Run probability roll
		if (!ignoreProbability && entry.probability() < 100)
		{
			// Never execute?
			if (entry.probability() <= 0)
				return;

			// Roll!
			std::uniform_int_distribution<uint32> roll(0, 99);
			if (roll(randomGenerator) > entry.probability())
				return;	 // Didn't pass probability check
		}

		for (int i = actionOffset; i < entry.actions_size(); ++i)
		{
			// Abort trigger on owner death?
			if (!checkOwnerAliveFlag(entry, strongOwner.get()))
				return;

			// Abort trigger if not in combat
			if (!checkInCombatFlag(entry, strongOwner.get()))
				return;

			const auto& action = entry.actions(i);
			switch (action.action())
			{
#define MMO_HANDLE_TRIGGER_ACTION(name) \
			case trigger_actions::name: \
				handle##name(action, context); \
				break;

				MMO_HANDLE_TRIGGER_ACTION(Trigger)
				MMO_HANDLE_TRIGGER_ACTION(Say)
				MMO_HANDLE_TRIGGER_ACTION(Yell)
				MMO_HANDLE_TRIGGER_ACTION(SetWorldObjectState)
				MMO_HANDLE_TRIGGER_ACTION(SetSpawnState)
				MMO_HANDLE_TRIGGER_ACTION(SetRespawnState)
				MMO_HANDLE_TRIGGER_ACTION(CastSpell)
				MMO_HANDLE_TRIGGER_ACTION(MoveTo)
				MMO_HANDLE_TRIGGER_ACTION(SetCombatMovement)
				MMO_HANDLE_TRIGGER_ACTION(StopAutoAttack)
				MMO_HANDLE_TRIGGER_ACTION(CancelCast)
				MMO_HANDLE_TRIGGER_ACTION(SetStandState)
				MMO_HANDLE_TRIGGER_ACTION(SetVirtualEquipmentSlot)
				MMO_HANDLE_TRIGGER_ACTION(SetPhase)
				MMO_HANDLE_TRIGGER_ACTION(SetSpellCooldown)
				MMO_HANDLE_TRIGGER_ACTION(QuestKillCredit)
				MMO_HANDLE_TRIGGER_ACTION(QuestEventOrExploration)
				MMO_HANDLE_TRIGGER_ACTION(SetVariable)
				MMO_HANDLE_TRIGGER_ACTION(Dismount)
				MMO_HANDLE_TRIGGER_ACTION(SetMount)
				MMO_HANDLE_TRIGGER_ACTION(Despawn)

#undef WOWPP_HANDLE_TRIGGER_ACTION

			case trigger_actions::Delay:
				{
					uint32 timeMS = getActionData(action, 0);
					if (timeMS == 0)
					{
						WLOG("Delay with 0 ms ignored");
						break;
					}

					if (i == entry.actions_size() - 1)
					{
						WLOG("Delay as last trigger action has no effect and is ignored");
						break;
					}

					// Save delay
					auto delayCountdown = std::make_unique<Countdown>(m_timers);
					delayCountdown->ended.connect([&entry, i, this, context, weakOwner]()
						{
							GameObjectS* oldOwner = context.owner;

							auto strongOwner = weakOwner.lock();
							if (context.owner != nullptr && strongOwner == nullptr)
							{
								WLOG("Owner no longer exists, so the executing trigger might fail.");
								oldOwner = nullptr;
							}

							ExecuteTrigger(entry, context, i + 1, true);
						});
					delayCountdown->SetEnd(GetAsyncTimeMs() + timeMS);
					m_delays.emplace_back(std::move(delayCountdown));

					// Skip the other actions for now
					i = entry.actions_size();
					break;
				}
			default:
			{
				WLOG("Unsupported trigger action: " << action.action());
				break;
			}
			}
		}
	}

	void TriggerHandler::handleTrigger(const proto::TriggerAction& action, TriggerContext& context)
	{
		if (action.target() != trigger_action_target::None)
		{
			WLOG("Unsupported target provided for TRIGGER_ACTION_TRIGGER - has no effect");
		}

		uint32 data = getActionData(action, 0);

		// Find that trigger
		const auto* trigger = m_project.triggers.getById(data);
		if (!trigger)
		{
			ELOG("Unable to find trigger " << data << " - trigger is not executed");
			return;
		}

		// Execute another trigger
		ExecuteTrigger(*trigger, context, 0);
	}

	void TriggerHandler::handleSay(const proto::TriggerAction& action, TriggerContext& context)
	{
		// TODO: Implement
		DLOG("TODO: ACTION_SAY");
	}

	void TriggerHandler::handleYell(const proto::TriggerAction& action, TriggerContext& context)
	{
		// TODO
		DLOG("TODO: ACTION_YELL");
	}

	void TriggerHandler::handleSetWorldObjectState(const proto::TriggerAction& action, TriggerContext& context)
	{
		DLOG("TODO: ACTION_SET_WORLD_OBJECT_STATE");
	}

	void TriggerHandler::handleSetSpawnState(const proto::TriggerAction& action, TriggerContext& context)
	{
		auto world = getWorldInstance(context.owner);
		if (!world)
		{
			return;
		}

		if ((action.target() != trigger_action_target::NamedCreature && action.target() != trigger_action_target::NamedWorldObject) ||
			action.targetname().empty())
		{
			WLOG("TRIGGER_ACTION_SET_SPAWN_STATE: Invalid target");
			return;
		}

		if (action.target() == trigger_action_target::NamedCreature)
		{
			auto* spawner = world->FindCreatureSpawner(action.targetname());
			if (!spawner)
			{
				WLOG("TRIGGER_ACTION_SET_SPAWN_STATE: Could not find named creature spawner");
				return;
			}

			const bool isActive = (getActionData(action, 0) != 0);
			spawner->SetState(isActive);
		}
		else
		{
			// TODO
			DLOG("TODO: Implement SetSpawnState for ObjectSpawner");
		}
	}

	void TriggerHandler::handleSetRespawnState(const proto::TriggerAction& action, TriggerContext& context)
	{
		auto world = getWorldInstance(context.owner);
		if (!world)
		{
			return;
		}

		if ((action.target() != trigger_action_target::NamedCreature && action.target() != trigger_action_target::NamedWorldObject) ||
			action.targetname().empty())
		{
			WLOG("TRIGGER_ACTION_SET_RESPAWN_STATE: Invalid target");
			return;
		}

		if (action.target() == trigger_action_target::NamedCreature)
		{
			auto* spawner = world->FindCreatureSpawner(action.targetname());
			if (!spawner)
			{
				WLOG("TRIGGER_ACTION_SET_RESPAWN_STATE: Could not find named creature spawner");
				return;
			}

			const bool isEnabled = (getActionData(action, 0) == 0 ? false : true);
			spawner->SetRespawn(isEnabled);
		}
		else
		{
			// TODO
			DLOG("TODO: Implement SetSpawnState for ObjectSpawner");
		}
	}

	void TriggerHandler::handleCastSpell(const proto::TriggerAction& action, TriggerContext& context)
	{
		// Determine caster
		GameObjectS* caster = getActionTarget(action, context);
		if (!caster)
		{
			ELOG("TRIGGER_ACTION_CAST_SPELL: No valid target found");
			return;
		}
		if (!caster->IsUnit())
		{
			ELOG("TRIGGER_ACTION_CAST_SPELL: Caster has to be a unit");
			return;
		}

		// Resolve spell id
		const auto* spell = m_project.spells.getById(getActionData(action, 0));
		if (!spell)
		{
			ELOG("TRIGGER_ACTION_CAST_SPELL: Invalid spell index or spell not found");
			return;
		}

		// Determine spell target
		uint32 dataTarget = getActionData(action, 1);
		GameObjectS* target = nullptr;
		switch (dataTarget)
		{
		case trigger_spell_cast_target::Caster:
			target = caster;
			break;
		case trigger_spell_cast_target::CurrentTarget:
			// Type of caster already checked above
			target = caster->AsUnit().GetVictim();
			break;
		default:
			ELOG("TRIGGER_ACTION_CAST_SPELL: Invalid spell cast target value of " << dataTarget);
			return;
		}

		if (!target)
		{
			// Do a warning here because this could also happen when the caster simply has no target, which
			// is not always a bug, but could sometimes be intended behaviour (maybe we want to turn off this warning later).
			WLOG("TRIGGER_ACTION_CAST_SPELL: Could not find target");
			return;
		}

		// Create spell target map and cast that spell
		SpellTargetMap targetMap;
		if (target->IsUnit())
		{
			targetMap.SetTargetMap(spell_cast_target_flags::Unit);
			targetMap.SetUnitTarget(target->GetGuid());
		}
		
		reinterpret_cast<GameUnitS*>(caster)->CastSpell(std::move(targetMap), *spell, spell->casttime());
	}

	void TriggerHandler::handleMoveTo(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = getActionTarget(action, context);
		if (target == nullptr)
		{
			ELOG("TRIGGER_ACTION_MOVE_TO: No target found, action will be ignored");
			return;
		}

		// Verify that "target" extends GameUnit class
		if (!target->IsUnit())
		{
			WLOG("TRIGGER_ACTION_MOVE_TO: Needs a creature target, but target is no unit - action ignored");
			return;
		}

		auto& mover = reinterpret_cast<GameUnitS*>(target)->GetMover();
		mover.targetReached.connect([target]() {
			// Raise reached target trigger
			reinterpret_cast<GameCreatureS*>(target)->RaiseTrigger(trigger_event::OnReachedTriggeredTarget);

			// This slot shall be executed only once
			current_connection().disconnect();
			});

		mover.MoveTo(
			Vector3(
				static_cast<float>(getActionData(action, 0)),
				static_cast<float>(getActionData(action, 1)),
				static_cast<float>(getActionData(action, 2))
			)
		);
	}

	void TriggerHandler::handleSetCombatMovement(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = getActionTarget(action, context);
		if (target == nullptr)
		{
			ELOG("TRIGGER_ACTION_SET_COMBAT_MOVEMENT: No target found, action will be ignored");
			return;
		}

		// Verify that "target" extends GameUnit class
		if (!target->IsUnit())
		{
			WLOG("TRIGGER_ACTION_SET_COMBAT_MOVEMENT: Needs a unit target, but target is no creature - action ignored");
			return;
		}

		// Update combat movement setting
		/*reinterpret_cast<GameCreatureS*>(target)->SetCombatMovement(
			getActionData(action, 0) != 0);*/
	}

	void TriggerHandler::handleStopAutoAttack(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = getActionTarget(action, context);
		if (target == nullptr)
		{
			ELOG("TRIGGER_ACTION_STOP_AUTO_ATTACK: No target found, action will be ignored");
			return;
		}

		// Verify that "target" extends GameUnit class
		if (!target->IsUnit())
		{
			WLOG("TRIGGER_ACTION_STOP_AUTO_ATTACK: Needs a unit target - action ignored");
			return;
		}

		// Stop auto attack
		reinterpret_cast<GameUnitS*>(target)->StopAttack();
	}

	void TriggerHandler::handleCancelCast(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = getActionTarget(action, context);
		if (target == nullptr)
		{
			ELOG("TRIGGER_ACTION_CANCEL_CAST: No target found, action will be ignored");
			return;
		}

		// Verify that "target" extends GameUnit class
		if (!target->IsUnit())
		{
			WLOG("TRIGGER_ACTION_CANCEL_CAST: Needs a unit target - action ignored");
			return;
		}

		reinterpret_cast<GameUnitS*>(target)->CancelCast(spell_interrupt_flags::Any);
	}

	void TriggerHandler::handleSetStandState(const proto::TriggerAction& action, TriggerContext& context)
	{
		// TODO
		DLOG("TODO: ACTION_SET_STAND_STATE");
	}

	void TriggerHandler::handleSetVirtualEquipmentSlot(const proto::TriggerAction& action, TriggerContext& context)
	{
		// TODO
		DLOG("TODO: ACTION_SET_VIRTUAL_EQUIPMENT_SLOT");
	}

	void TriggerHandler::handleSetPhase(const proto::TriggerAction& action, TriggerContext& context)
	{
		WLOG("TODO: ACTION_SET_PHASE");
	}

	void TriggerHandler::handleSetSpellCooldown(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = getActionTarget(action, context);
		if (target == nullptr)
		{
			ELOG("TRIGGER_ACTION_SET_SPELL_COOLDOWN: No target found, action will be ignored");
			return;
		}

		// Verify that "target" extends GameUnit class
		if (!target->IsUnit())
		{
			WLOG("TRIGGER_ACTION_SET_SPELL_COOLDOWN: Needs a unit target - action ignored");
			return;
		}

		reinterpret_cast<GameUnitS*>(target)->SetCooldown(getActionData(action, 0), getActionData(action, 1));
	}

	void TriggerHandler::handleQuestKillCredit(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = getActionTarget(action, context);
		if (target == nullptr)
		{
			ELOG("TRIGGER_ACTION_QUEST_KILL_CREDIT: No target found, action will be ignored");
			return;
		}

		// Verify that "target" extends GameCharacter class
		if (!target->IsPlayer())
		{
			WLOG("TRIGGER_ACTION_QUEST_KILL_CREDIT: Needs a player target - action ignored");
			return;
		}

		uint32 entryId = getActionData(action, 0);
		if (!entryId)
		{
			WLOG("TRIGGER_ACTION_QUEST_KILL_CREDIT: Needs a valid unit entry - action ignored");
			return;
		}

		const auto* entry = target->GetProject().units.getById(entryId);
		if (!entry)
		{
			WLOG("TRIGGER_ACTION_QUEST_KILL_CREDIT: Unknown unit id " << entryId << " - action ignored");
			return;
		}

		if (!context.owner)
		{
			WLOG("TRIGGER_ACTION_QUEST_KILL_CREDIT: Unknown trigger owner (this is most likely due to a wrong assigned trigger! Assign it to a unit)");
			return;
		}

		target->AsPlayer().OnQuestKillCredit(context.owner->GetGuid(), *entry);
	}

	void TriggerHandler::handleQuestEventOrExploration(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = getActionTarget(action, context);
		if (target == nullptr)
		{
			ELOG("TRIGGER_ACTION_QUEST_EVENT_OR_EXPLORATION: No target found, action will be ignored");
			return;
		}

		// Verify that "target" extends GameCharacter class
		if (!target->IsPlayer())
		{
			WLOG("TRIGGER_ACTION_QUEST_EVENT_OR_EXPLORATION: Needs a player target - action ignored");
			return;
		}

		uint32 questId = getActionData(action, 0);
		target->AsPlayer().CompleteQuest(questId);
	}

	void TriggerHandler::handleSetVariable(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = getActionTarget(action, context);
		if (target == nullptr)
		{
			ELOG("TRIGGER_ACTION_SET_VARIABLE: No target found, action will be ignored");
			return;
		}

		// Get variable
		uint32 entryId = getActionData(action, 0);
		if (entryId == 0)
		{
			WLOG("TRIGGER_ACTION_SET_VARIABLE: Needs a valid variable entry - action ignored");
			return;
		}

		const auto* entry = target->GetProject().variables.getById(entryId);
		if (!entry)
		{
			WLOG("TRIGGER_ACTION_SET_VARIABLE: Unknown variable id " << entryId << " - action ignored");
			return;
		}

		// Determine variable type
		switch (entry->data_case())
		{
			// TODO
		case proto::VariableEntry::kIntvalue:
		case proto::VariableEntry::kLongvalue:
		case proto::VariableEntry::kFloatvalue:
		{
			target->SetVariable(entryId, static_cast<int64>(getActionData(action, 1)));
			break;
		}
		case proto::VariableEntry::kStringvalue:
		{
			target->SetVariable(entryId, getActionText(action, 0));
			break;
		}
		}
	}

	void TriggerHandler::handleDismount(const proto::TriggerAction& action, TriggerContext& context)
	{
		// TODO
		DLOG("TODO: ACTION_DISMOUNT");
	}

	void TriggerHandler::handleSetMount(const proto::TriggerAction& action, TriggerContext& context)
	{
		// TODO
		DLOG("TODO: ACTION_SET_MOUNT");
	}

	void TriggerHandler::handleDespawn(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = getActionTarget(action, context);
		if (target == nullptr)
		{
			ELOG("TRIGGER_ACTION_DESPAWN: No target found, action will be ignored");
			return;
		}

		if (!target->IsUnit())
		{
			ELOG("TRIGGER_ACTION_DESPAWN: Target has to be a creature or world object");
			return;
		}

		auto* world = target->GetWorldInstance();
		if (!world)
		{
			ELOG("TRIGGER_ACTION_DESPAWN: Target isn't spawned right now");
			return;
		}

		// Remove object in next world tick
		auto strong = target->shared_from_this();
		std::weak_ptr<GameObjectS> weak(strong);
		world->GetUniverse().Post([weak]() {
			auto strong = weak.lock();
			if (strong)
			{
				auto* world = strong->GetWorldInstance();
				if (world)
					world->RemoveGameObject(*strong);
			}
			});
	}

	bool TriggerHandler::checkInCombatFlag(const proto::TriggerEntry& entry, const GameObjectS* owner)
	{
		if (entry.flags() & trigger_flags::OnlyInCombat)
		{
			if (owner)
			{
				if (owner->IsUnit())
				{
					if (!owner->AsUnit().IsInCombat())
					{
						// Stop trigger execution here
						return false;
					}
				}
			}
			else
			{
				// Stop trigger execution here
				return false;
			}
		}

		return true;
	}

	bool TriggerHandler::checkOwnerAliveFlag(const proto::TriggerEntry& entry, const GameObjectS* owner)
	{
		if (entry.flags() & trigger_flags::AbortOnOwnerDeath)
		{
			if (owner)
			{
				if (owner->IsUnit())
				{
					if (!owner->AsUnit().IsAlive())
					{
						// Stop trigger execution here
						return false;
					}
				}
			}
			else
			{
				// Stop trigger execution here
				return false;
			}
		}

		return true;
	}

	int32 TriggerHandler::getActionData(const proto::TriggerAction& action, uint32 index) const
	{
		// Return default value (0) if not enough data provided
		if (static_cast<int>(index) >= action.data_size())
			return 0;

		return action.data(index);
	}

	const String& TriggerHandler::getActionText(const proto::TriggerAction& action, uint32 index) const
	{
		// Return default string (empty) if not enough data provided
		if (static_cast<int>(index) >= action.texts_size())
		{
			static String invalidString = "<INVALID_TEXT>";
			return invalidString;
		}

		return action.texts(index);
	}

	WorldInstance* TriggerHandler::getWorldInstance(GameObjectS* owner) const
	{
		WorldInstance* world = nullptr;
		if (owner)
		{
			world = owner->GetWorldInstance();
		}

		if (!world)
		{
			ELOG("Could not get world instance - action will be ignored.");
		}

		return world;
	}

	bool TriggerHandler::playSoundEntry(uint32 sound, GameObjectS* source)
	{
		if (sound)
		{
			// TODO
		}

		return true;
	}

	GameObjectS* TriggerHandler::getActionTarget(const proto::TriggerAction& action, TriggerContext& context)
	{
		switch (action.target())
		{
		case trigger_action_target::OwningObject:
			return context.owner;
		case trigger_action_target::TriggeringUnit:
			return context.triggeringUnit.lock().get();
		case trigger_action_target::OwningUnitVictim:
			return (context.owner && context.owner->IsUnit() ? context.owner->AsUnit().GetVictim() : nullptr);
		case trigger_action_target::NamedWorldObject:
		{
			auto* world = getWorldInstance(context.owner);
			if (!world) return nullptr;

			// Need to provide a name
			if (action.targetname().empty()) return nullptr;

			// Find it
			/*auto* spawner = world->FindObjectSpawner(action.targetname());
			if (!spawner) return nullptr;

			// Return the first spawned game object
			return (spawner->getSpawnedObjects().empty() ? nullptr : spawner->getSpawnedObjects()[0].get());*/
			return nullptr;
		}
		case trigger_action_target::NamedCreature:
		{
			auto* world = getWorldInstance(context.owner);
			if (!world) return nullptr;

			// Need to provide a name
			if (action.targetname().empty()) return nullptr;

			// Find it
			auto* spawner = world->FindCreatureSpawner(action.targetname());
			if (!spawner) return nullptr;

			// Return the first spawned game object
			return (spawner->GetCreatures().empty() ? nullptr : reinterpret_cast<GameObjectS*>(spawner->GetCreatures()[0].get()));
		}
		default:
			WLOG("Unhandled action target " << action.target());
			break;
		}

		return nullptr;
	}
}
