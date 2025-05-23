
#include "trigger_handler.h"

#include "vector_sink.h"
#include "base/utilities.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/world/universe.h"
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

		if (actionOffset == 0)
		{
			if (entry.flags() & trigger_flags::OnlyOneInstance)
			{
				// Nothing to do here
				if (strongOwner && strongOwner->IsTriggerRunning(entry.id()))
				{
					WLOG("Trigger " << entry.id() << " is already running on " << log_hex_digit(strongOwner->GetGuid()));
					return;
				}
			}
		}
		
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

		// Notify trigger started
		if (strongOwner)
		{
			strongOwner->NotifyTriggerRunning(entry.id());
		}

		for (int i = actionOffset; i < entry.actions_size(); ++i)
		{
			// Abort trigger on owner death?
			if (!CheckOwnerAliveFlag(entry, strongOwner.get()))
				return;

			// Abort trigger if not in combat
			if (!CheckInCombatFlag(entry, strongOwner.get()))
				return;

			const auto& action = entry.actions(i);
			switch (action.action())
			{
#define MMO_HANDLE_TRIGGER_ACTION(name) \
			case trigger_actions::name: \
				Handle##name(action, context); \
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

#undef MMO_HANDLE_TRIGGER_ACTION

			case trigger_actions::Delay:
				{
					uint32 timeMS = GetActionData(action, 0);
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
					return;
				}
			default:
			{
				WLOG("Unsupported trigger action: " << action.action());
				break;
			}
			}

			if (i >= entry.actions_size() - 1)
			{
				// Trigger is done
				if (strongOwner)
				{
					strongOwner->NotifyTriggerEnded(entry.id());
				}
			}
		}
	}

	void TriggerHandler::HandleTrigger(const proto::TriggerAction& action, TriggerContext& context)
	{
		if (action.target() != trigger_action_target::None)
		{
			WLOG("Unsupported target provided for TRIGGER_ACTION_TRIGGER - has no effect");
		}

		uint32 data = GetActionData(action, 0);

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

	void TriggerHandler::HandleSay(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = GetActionTarget(action, context);
		if (target == nullptr)
		{
			WLOG("TRIGGER_ACTION_SAY: No target found, action will be ignored");
			return;
		}

		auto* world = GetWorldInstance(target);
		if (!world)
		{
			return;
		}

		// Verify that "target" extends GameUnit class
		if (!target->IsUnit())
		{
			WLOG("TRIGGER_ACTION_SAY: Needs a unit target, but target is no unit - action ignored");
			return;
		}

		auto triggeringUnit = context.triggeringUnit.lock();
		target->AsUnit().ChatSay(GetActionText(action, 0));

		// Eventually play sound file
		if (action.data_size() > 0)
		{
			PlaySoundEntry(action.data(0), target);
		}
	}

	void TriggerHandler::HandleYell(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = GetActionTarget(action, context);
		if (target == nullptr)
		{
			WLOG("TRIGGER_ACTION_YELL: No target found, action will be ignored");
			return;
		}

		auto* world = GetWorldInstance(target);
		if (!world)
		{
			return;
		}

		// Verify that "target" extends GameUnit class
		if (!target->IsUnit())
		{
			WLOG("TRIGGER_ACTION_YELL: Needs a unit target, but target is no unit - action ignored");
			return;
		}

		auto triggeringUnit = context.triggeringUnit.lock();
		target->AsUnit().ChatYell(GetActionText(action, 0));

		// Eventually play sound file
		if (action.data_size() > 0)
		{
			PlaySoundEntry(action.data(0), target);
		}
	}

	void TriggerHandler::HandleSetWorldObjectState(const proto::TriggerAction& action, TriggerContext& context)
	{
		DLOG("TODO: ACTION_SET_WORLD_OBJECT_STATE");
	}

	void TriggerHandler::HandleSetSpawnState(const proto::TriggerAction& action, TriggerContext& context)
	{
		auto world = GetWorldInstance(context.owner);
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

			const bool isActive = (GetActionData(action, 0) != 0);
			spawner->SetState(isActive);
		}
		else
		{
			// TODO
			DLOG("TODO: Implement SetSpawnState for ObjectSpawner");
		}
	}

	void TriggerHandler::HandleSetRespawnState(const proto::TriggerAction& action, TriggerContext& context)
	{
		auto world = GetWorldInstance(context.owner);
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

			const bool isEnabled = (GetActionData(action, 0) == 0 ? false : true);
			spawner->SetRespawn(isEnabled);
		}
		else
		{
			// TODO
			DLOG("TODO: Implement SetSpawnState for ObjectSpawner");
		}
	}

	void TriggerHandler::HandleCastSpell(const proto::TriggerAction& action, TriggerContext& context)
	{
		// Determine caster
		GameObjectS* caster = GetActionTarget(action, context);
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
		const auto* spell = m_project.spells.getById(GetActionData(action, 0));
		if (!spell)
		{
			ELOG("TRIGGER_ACTION_CAST_SPELL: Invalid spell index or spell not found");
			return;
		}

		// Determine spell target
		uint32 dataTarget = GetActionData(action, 1);
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
		case trigger_spell_cast_target::TriggeringUnit:
			target = context.triggeringUnit.lock().get();
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

		DLOG("Make unit " << log_hex_digit(caster->GetGuid()) << " cast spell " << log_hex_digit(spell->id()) << " on target " << log_hex_digit(target->GetGuid()));

		// Create spell target map and cast that spell
		SpellTargetMap targetMap;
		if (target->IsUnit())
		{
			targetMap.SetTargetMap(spell_cast_target_flags::Unit);
			targetMap.SetUnitTarget(target->GetGuid());
		}
		
		reinterpret_cast<GameUnitS*>(caster)->CastSpell(std::move(targetMap), *spell, spell->casttime());
	}

	void TriggerHandler::HandleMoveTo(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = GetActionTarget(action, context);
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
				static_cast<float>(GetActionData(action, 0)),
				static_cast<float>(GetActionData(action, 1)),
				static_cast<float>(GetActionData(action, 2))
			),
			0.0f
		);
	}

	void TriggerHandler::HandleSetCombatMovement(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = GetActionTarget(action, context);
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

	void TriggerHandler::HandleStopAutoAttack(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = GetActionTarget(action, context);
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

	void TriggerHandler::HandleCancelCast(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = GetActionTarget(action, context);
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

	void TriggerHandler::HandleSetStandState(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = GetActionTarget(action, context);
		if (target == nullptr)
		{
			ELOG("TRIGGER_ACTION_SET_STAND_STATE: No target found, action will be ignored");
			return;
		}

		// Verify that "target" extends GameUnit class
		if (!target->IsUnit())
		{
			WLOG("TRIGGER_ACTION_SET_STAND_STATE: Needs a unit target - action ignored");
			return;
		}

		const uint32 standState = GetActionData(action, 0);
		if (standState >= unit_stand_state::Count_)
		{
			WLOG("TRIGGER_ACTION_SET_STAND_STATE: Invalid stand state " << standState << " - action ignored");
			return;
		}

		target->AsUnit().SetStandState(static_cast<unit_stand_state::Type>(standState));
	}

	void TriggerHandler::HandleSetVirtualEquipmentSlot(const proto::TriggerAction& action, TriggerContext& context)
	{
		// TODO
		DLOG("TODO: ACTION_SET_VIRTUAL_EQUIPMENT_SLOT");
	}

	void TriggerHandler::HandleSetPhase(const proto::TriggerAction& action, TriggerContext& context)
	{
		WLOG("TODO: ACTION_SET_PHASE");
	}

	void TriggerHandler::HandleSetSpellCooldown(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = GetActionTarget(action, context);
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

		reinterpret_cast<GameUnitS*>(target)->SetCooldown(GetActionData(action, 0), GetActionData(action, 1));
	}

	void TriggerHandler::HandleQuestKillCredit(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = GetActionTarget(action, context);
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

		uint32 entryId = GetActionData(action, 0);
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

	void TriggerHandler::HandleQuestEventOrExploration(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = GetActionTarget(action, context);
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

		uint32 questId = GetActionData(action, 0);
		target->AsPlayer().CompleteQuest(questId);
	}

	void TriggerHandler::HandleSetVariable(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = GetActionTarget(action, context);
		if (target == nullptr)
		{
			ELOG("TRIGGER_ACTION_SET_VARIABLE: No target found, action will be ignored");
			return;
		}

		// Get variable
		uint32 entryId = GetActionData(action, 0);
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
			target->SetVariable(entryId, static_cast<int64>(GetActionData(action, 1)));
			break;
		}
		case proto::VariableEntry::kStringvalue:
		{
			target->SetVariable(entryId, GetActionText(action, 0));
			break;
		}
		}
	}

	void TriggerHandler::HandleDismount(const proto::TriggerAction& action, TriggerContext& context)
	{
		// TODO
		DLOG("TODO: ACTION_DISMOUNT");
	}

	void TriggerHandler::HandleSetMount(const proto::TriggerAction& action, TriggerContext& context)
	{
		// TODO
		DLOG("TODO: ACTION_SET_MOUNT");
	}

	void TriggerHandler::HandleDespawn(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = GetActionTarget(action, context);
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

	bool TriggerHandler::CheckInCombatFlag(const proto::TriggerEntry& entry, const GameObjectS* owner)
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

	bool TriggerHandler::CheckOwnerAliveFlag(const proto::TriggerEntry& entry, const GameObjectS* owner)
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

	int32 TriggerHandler::GetActionData(const proto::TriggerAction& action, uint32 index) const
	{
		// Return default value (0) if not enough data provided
		if (static_cast<int>(index) >= action.data_size())
			return 0;

		return action.data(index);
	}

	const String& TriggerHandler::GetActionText(const proto::TriggerAction& action, uint32 index) const
	{
		// Return default string (empty) if not enough data provided
		if (static_cast<int>(index) >= action.texts_size())
		{
			static String invalidString = "<INVALID_TEXT>";
			return invalidString;
		}

		return action.texts(index);
	}

	WorldInstance* TriggerHandler::GetWorldInstance(GameObjectS* owner) const
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

	bool TriggerHandler::PlaySoundEntry(uint32 sound, GameObjectS* source)
	{
		if (sound)
		{
			// TODO
		}

		return true;
	}

	GameObjectS* TriggerHandler::GetActionTarget(const proto::TriggerAction& action, TriggerContext& context)
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
			auto* world = GetWorldInstance(context.owner);
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
			auto* world = GetWorldInstance(context.owner);
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
