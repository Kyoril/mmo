
#include "trigger_handler.h"

#include "vector_sink.h"
#include "base/utilities.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/objects/game_world_object_s.h"
#include "game_server/world/universe.h"
#include "log/default_log_levels.h"
#include "proto_data/project.h"
#include "shared/proto_data/triggers.pb.h"
#include "proto_data/trigger_helper.h"
#include "player_manager.h"
#include "player.h"

#include <limits>

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

		if (entry.has_condition())
		{
			if (!EvaluateCondition(entry.condition(), context))
			{
				return;
			}
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
				MMO_HANDLE_TRIGGER_ACTION(Teleport)
				MMO_HANDLE_TRIGGER_ACTION(Emote)
				MMO_HANDLE_TRIGGER_ACTION(SetEncounterState)
				MMO_HANDLE_TRIGGER_ACTION(SummonCreature)
				MMO_HANDLE_TRIGGER_ACTION(Taunt)
				MMO_HANDLE_TRIGGER_ACTION(ModifyThreat)
				MMO_HANDLE_TRIGGER_ACTION(ResetThreat)
				MMO_HANDLE_TRIGGER_ACTION(ApplyAura)
				MMO_HANDLE_TRIGGER_ACTION(RemoveAura)
				MMO_HANDLE_TRIGGER_ACTION(SetInstanceVariable)
				MMO_HANDLE_TRIGGER_ACTION(BroadcastMessage)

#undef MMO_HANDLE_TRIGGER_ACTION

			case trigger_actions::MoveTo:
				{
					const bool waitForArrival = (GetActionData(action, 3) != 0);

					if (waitForArrival && i < entry.actions_size() - 1)
					{
						GameObjectS* moveTarget = GetActionTarget(action, context);
						if (!moveTarget || !moveTarget->IsUnit())
						{
							ELOG("TRIGGER_ACTION_MOVE_TO (wait): No unit target");
							break;
						}

						auto& mover = reinterpret_cast<GameUnitS*>(moveTarget)->GetMover();

						// Shared flag: whichever signal fires first wins; prevents double-execution.
						auto fired = std::make_shared<bool>(false);

						mover.targetReached.connect([&entry, i, this, context, fired]() mutable
							{
								current_connection().disconnect();
								if (*fired)
									return;
								*fired = true;
								ExecuteTrigger(entry, context, i + 1, true);
							});

						mover.movementStopped.connect([fired]()
							{
								current_connection().disconnect();
								if (*fired)
									return;
								*fired = true;
								// Movement interrupted — trigger chain is abandoned here.
							});

						mover.MoveTo(
							Vector3(
								static_cast<float>(GetActionData(action, 0)),
								static_cast<float>(GetActionData(action, 1)),
								static_cast<float>(GetActionData(action, 2))
							),
							0.0f
						);

						if (strongOwner)
						{
							strongOwner->NotifyTriggerEnded(entry.id());
						}
						return;
					}

					HandleMoveTo(action, context);
					break;
				}

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

	void TriggerHandler::HandleEmote(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = GetActionTarget(action, context);
		if (target == nullptr)
		{
			WLOG("TRIGGER_ACTION_EMOTE: No target found, action will be ignored");
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
			WLOG("TRIGGER_ACTION_EMOTE: Needs a unit target, but target is no unit - action ignored");
			return;
		}

		target->AsUnit().ChatEmote(GetActionText(action, 0));

		// Eventually play sound file
		if (action.data_size() > 0)
		{
			PlaySoundEntry(action.data(0), target);
		}
	}

	void TriggerHandler::HandleSetWorldObjectState(const proto::TriggerAction& action, TriggerContext& context)
	{
		auto* world = GetWorldInstance(context);
		if (!world)
		{
			ELOG("TRIGGER_ACTION_SET_WORLD_OBJECT_STATE: No world instance");
			return;
		}

		if (action.target() != trigger_action_target::NamedWorldObject || action.targetname().empty())
		{
			WLOG("TRIGGER_ACTION_SET_WORLD_OBJECT_STATE: Invalid target (requires NamedWorldObject with name)");
			return;
		}

		auto* spawner = world->FindObjectSpawner(action.targetname());
		if (!spawner)
		{
			WLOG("TRIGGER_ACTION_SET_WORLD_OBJECT_STATE: Could not find object spawner '" << action.targetname() << "'");
			return;
		}

		const uint32 newState = GetActionData(action, 0);
		for (auto& obj : spawner->getSpawnedObjects())
		{
			obj->Set<uint32>(object_fields::State, newState);
		}

		DLOG("TRIGGER_ACTION_SET_WORLD_OBJECT_STATE: Set state to " << newState << " for spawner '" << action.targetname() << "'");
	}

	void TriggerHandler::HandleSetSpawnState(const proto::TriggerAction& action, TriggerContext& context)
	{
		auto world = GetWorldInstance(context);
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
			auto* spawner = world->FindObjectSpawner(action.targetname());
			if (!spawner)
			{
				WLOG("TRIGGER_ACTION_SET_SPAWN_STATE: Could not find named object spawner");
				return;
			}

			const bool isActive = (GetActionData(action, 0) != 0);
			spawner->SetState(isActive);
		}
	}

	void TriggerHandler::HandleSetRespawnState(const proto::TriggerAction& action, TriggerContext& context)
	{
		auto world = GetWorldInstance(context);
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
			auto* spawner = world->FindObjectSpawner(action.targetname());
			if (!spawner)
			{
				WLOG("TRIGGER_ACTION_SET_RESPAWN_STATE: Could not find named object spawner");
				return;
			}

			const bool isEnabled = (GetActionData(action, 0) != 0);
			spawner->SetRespawn(isEnabled);
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
		
		// Triggers are server-authoritative, so bypass the "unit knows spell" check.
		reinterpret_cast<GameUnitS*>(caster)->CastSpell(std::move(targetMap), *spell, spell->casttime(), false, 0, true);
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
		reinterpret_cast<GameCreatureS*>(target)->SetCombatMovement(
			GetActionData(action, 0) != 0);
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
		GameObjectS* target = GetActionTarget(action, context);
		if (target == nullptr)
		{
			ELOG("TRIGGER_ACTION_SET_VIRTUAL_EQUIPMENT_SLOT: No target found, action will be ignored");
			return;
		}

		if (!target->IsUnit())
		{
			WLOG("TRIGGER_ACTION_SET_VIRTUAL_EQUIPMENT_SLOT: Needs a unit target - action ignored");
			return;
		}

		const uint32 slot = GetActionData(action, 0);
		const uint32 displayId = GetActionData(action, 1);

		if (slot >= 3)
		{
			WLOG("TRIGGER_ACTION_SET_VIRTUAL_EQUIPMENT_SLOT: Invalid slot " << slot << " (must be 0-2)");
			return;
		}

		static const uint16 virtualItemFields[] =
		{
			object_fields::VirtualItem0,
			object_fields::VirtualItem1,
			object_fields::VirtualItem2
		};

		target->AsUnit().Set<uint32>(virtualItemFields[slot], displayId);
		DLOG("TRIGGER_ACTION_SET_VIRTUAL_EQUIPMENT_SLOT: Set slot " << slot << " displayId=" << displayId);
	}

	void TriggerHandler::HandleSetPhase(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = GetActionTarget(action, context);
		if (target == nullptr)
		{
			ELOG("TRIGGER_ACTION_SET_PHASE: No target found, action will be ignored");
			return;
		}

		if (!target->IsUnit())
		{
			WLOG("TRIGGER_ACTION_SET_PHASE: Needs a unit target - action ignored");
			return;
		}

		const uint32 phase = GetActionData(action, 0);
		auto* creature = reinterpret_cast<GameCreatureS*>(target);
		if (!creature->SetCombatPhase(phase))
		{
			WLOG("TRIGGER_ACTION_SET_PHASE: Target has no active combat script");
			return;
		}

		DLOG("TRIGGER_ACTION_SET_PHASE: Set combat phase to " << phase);
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
		GameObjectS* target = GetActionTarget(action, context);
		if (target == nullptr)
		{
			ELOG("TRIGGER_ACTION_DISMOUNT: No target found, action will be ignored");
			return;
		}

		if (!target->IsUnit())
		{
			WLOG("TRIGGER_ACTION_DISMOUNT: Needs a unit target - action ignored");
			return;
		}

		target->AsUnit().Set<uint32>(object_fields::MountDisplayId, 0);
		DLOG("TRIGGER_ACTION_DISMOUNT: Cleared mount display");
	}

	void TriggerHandler::HandleSetMount(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = GetActionTarget(action, context);
		if (target == nullptr)
		{
			ELOG("TRIGGER_ACTION_SET_MOUNT: No target found, action will be ignored");
			return;
		}

		if (!target->IsUnit())
		{
			WLOG("TRIGGER_ACTION_SET_MOUNT: Needs a unit target - action ignored");
			return;
		}

		const uint32 mountDisplayId = GetActionData(action, 0);
		target->AsUnit().Set<uint32>(object_fields::MountDisplayId, mountDisplayId);
		DLOG("TRIGGER_ACTION_SET_MOUNT: Set mount display " << mountDisplayId);
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

	void TriggerHandler::HandleTeleport(const proto::TriggerAction& action, TriggerContext& context)
	{
		GameObjectS* target = GetActionTarget(action, context);
		if (target == nullptr)
		{
			ELOG("TRIGGER_ACTION_TELEPORT: No target found, action will be ignored");
			return;
		}

		if (!target->IsPlayer())
		{
			ELOG("TRIGGER_ACTION_TELEPORT: Target has to be a player");
			return;
		}

		auto* world = target->GetWorldInstance();
		if (!world)
		{
			ELOG("TRIGGER_ACTION_TELEPORT: Target isn't spawned right now");
			return;
		}

		const uint32 map = GetActionData(action, 0);
		const uint32 x = GetActionData(action, 1);
		const uint32 y = GetActionData(action, 2);
		const uint32 z = GetActionData(action, 3);
		const Radian facing = Degree(static_cast<float>(GetActionData(action, 4)));

		// Remove object in next world tick
		auto strong = std::static_pointer_cast<GamePlayerS>(target->shared_from_this());
		std::weak_ptr weak(strong);
		world->GetUniverse().Post([weak, map, x, y, z, facing]() {
			const auto strong = weak.lock();
			if (strong)
			{
				auto* world = strong->GetWorldInstance();
				if (world)
				{
					strong->Teleport(map, Vector3(x, y, z), facing);
				}
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

	WorldInstance* TriggerHandler::GetWorldInstance(TriggerContext& context) const
	{
		// Prefer the explicit world instance from the context. This is set even when there is no
		// owner (e.g. for map-instance-global triggers), allowing named target/spawner resolution.
		WorldInstance* world = context.world;
		if (!world && context.owner)
		{
			world = context.owner->GetWorldInstance();
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
			auto* world = GetWorldInstance(context);
			if (!world) return nullptr;

			// Need to provide a name
			if (action.targetname().empty()) return nullptr;

			// Find spawner by name and return first spawned object
			auto* spawner = world->FindObjectSpawner(action.targetname());
			if (!spawner) return nullptr;

			return (spawner->getSpawnedObjects().empty() ? nullptr : spawner->getSpawnedObjects()[0].get());
		}
		case trigger_action_target::NamedCreature:
		{
			auto* world = GetWorldInstance(context);
			if (!world) return nullptr;

			// Need to provide a name
			if (action.targetname().empty()) return nullptr;

			// Find it
			auto* spawner = world->FindCreatureSpawner(action.targetname());
			if (!spawner) return nullptr;

			// Return the first spawned game object
			return (spawner->GetCreatures().empty() ? nullptr : reinterpret_cast<GameObjectS*>(spawner->GetCreatures()[0].get()));
		}
		case trigger_action_target::RandomUnit:
		{
			auto* world = GetWorldInstance(context);
			if (!world) return nullptr;

			// Collect all alive units in the instance and pick one at random.
			std::vector<GameUnitS*> units;
			world->ForEachObject([&units](GameObjectS& obj)
				{
					if (obj.IsUnit() && obj.AsUnit().IsAlive())
					{
						units.push_back(&obj.AsUnit());
					}
					return true;
				});

			if (units.empty()) return nullptr;
			std::uniform_int_distribution<size_t> dist(0, units.size() - 1);
			return units[dist(randomGenerator)];
		}
		case trigger_action_target::RandomPlayer:
		{
			auto players = GetPlayersInWorld(GetWorldInstance(context));
			if (players.empty()) return nullptr;
			std::uniform_int_distribution<size_t> dist(0, players.size() - 1);
			return players[dist(randomGenerator)];
		}
		case trigger_action_target::NearestPlayer:
		{
			auto players = GetPlayersInWorld(GetWorldInstance(context));
			if (players.empty() || !context.owner) return nullptr;

			const Vector3 origin = context.owner->GetPosition();
			GameUnitS* nearest = nullptr;
			float nearestSqr = std::numeric_limits<float>::max();
			for (auto* player : players)
			{
				const float distSqr = (player->GetPosition() - origin).GetSquaredLength();
				if (distSqr < nearestSqr)
				{
					nearestSqr = distSqr;
					nearest = player;
				}
			}
			return nearest;
		}
		case trigger_action_target::HighestThreat:
			// The current victim of the owning creature is its highest threat target (tank).
			return (context.owner && context.owner->IsUnit() ? context.owner->AsUnit().GetVictim() : nullptr);
		case trigger_action_target::AllPlayers:
		{
			// Single-target callers get the first player; multi-target callers use ForEachActionTarget.
			auto players = GetPlayersInWorld(GetWorldInstance(context));
			return players.empty() ? nullptr : players[0];
		}
		default:
			WLOG("Unhandled action target " << action.target());
			break;
		}

		return nullptr;
	}

	std::vector<GameUnitS*> TriggerHandler::GetPlayersInWorld(WorldInstance* world) const
	{
		std::vector<GameUnitS*> players;
		if (!world)
		{
			return players;
		}

		world->ForEachObject([&players](GameObjectS& obj)
			{
				if (obj.IsPlayer() && obj.AsUnit().IsAlive())
				{
					players.push_back(&obj.AsUnit());
				}
				return true;
			});

		return players;
	}

	void TriggerHandler::ForEachActionTarget(const proto::TriggerAction& action, TriggerContext& context, const std::function<void(GameObjectS&)>& callback)
	{
		if (action.target() == trigger_action_target::AllPlayers)
		{
			for (auto* player : GetPlayersInWorld(GetWorldInstance(context)))
			{
				callback(*player);
			}
			return;
		}

		if (GameObjectS* target = GetActionTarget(action, context))
		{
			callback(*target);
		}
	}

	void TriggerHandler::HandleSetEncounterState(const proto::TriggerAction& action, TriggerContext& context)
	{
		auto* world = GetWorldInstance(context);
		if (!world)
		{
			ELOG("TRIGGER_ACTION_SET_ENCOUNTER_STATE: No world instance");
			return;
		}

		const uint32 slotId   = static_cast<uint32>(GetActionData(action, 0));
		const uint32 newState = static_cast<uint32>(GetActionData(action, 1));

		if (newState > encounter_state::Fail)
		{
			WLOG("TRIGGER_ACTION_SET_ENCOUNTER_STATE: Invalid state " << newState);
			return;
		}

		world->SetEncounterState(slotId, newState);
		DLOG("TRIGGER_ACTION_SET_ENCOUNTER_STATE: Slot " << slotId << " set to " << newState);
	}

	void TriggerHandler::HandleSummonCreature(const proto::TriggerAction& action, TriggerContext& context)
	{
		WorldInstance* world = GetWorldInstance(context);
		if (!world)
		{
			ELOG("TRIGGER_ACTION_SUMMON_CREATURE: No world instance");
			return;
		}

		const uint32 entryId = static_cast<uint32>(GetActionData(action, 0));
		const auto* unitEntry = m_project.units.getById(entryId);
		if (!unitEntry)
		{
			ELOG("TRIGGER_ACTION_SUMMON_CREATURE: Unknown creature entry " << entryId);
			return;
		}

		// Determine spawn position: explicit coordinates (data 1..3) or the position of the resolved
		// target / owner.
		Vector3 position;
		if (action.data_size() >= 4)
		{
			position = Vector3(
				static_cast<float>(GetActionData(action, 1)),
				static_cast<float>(GetActionData(action, 2)),
				static_cast<float>(GetActionData(action, 3)));
		}
		else
		{
			GameObjectS* origin = (action.target() != trigger_action_target::None) ? GetActionTarget(action, context) : nullptr;
			if (!origin)
			{
				origin = context.owner;
			}

			if (!origin)
			{
				ELOG("TRIGGER_ACTION_SUMMON_CREATURE: No spawn position (no explicit coords, no owner/target)");
				return;
			}
			position = origin->GetPosition();
		}

		const uint32 despawnMs = static_cast<uint32>(GetActionData(action, 4));
		const bool attackNearest = (GetActionData(action, 5) != 0);

		// Owner (for OnSummonedUnitDied) and triggering unit (potential aggro target) captured weakly.
		std::weak_ptr<GameObjectS> weakOwner;
		if (context.owner)
		{
			weakOwner = context.owner->shared_from_this();
		}
		std::weak_ptr<GameUnitS> weakTriggering = context.triggeringUnit;

		// World mutations must not happen during the world update tick — defer to the next tick.
		world->GetUniverse().Post([this, world, unitEntry, position, despawnMs, attackNearest, weakOwner, weakTriggering]()
			{
				auto summon = world->CreateTemporaryCreature(*unitEntry, position, 0.0f, 0.0f);
				if (!summon)
				{
					return;
				}
				summon->ClearFieldChanges();

				GameCreatureS* summonPtr = summon.get();

				// Raise OnSummonedUnitDied on the owner (or as an instance event) when the summon dies.
				summon->killed.connect([world, weakOwner, summonPtr](GameUnitS* /*killer*/)
					{
						if (const auto owner = weakOwner.lock())
						{
							if (owner->IsUnit())
							{
								owner->AsUnit().RaiseTrigger(trigger_event::OnSummonedUnitDied, summonPtr);
								return;
							}
						}

						// Ownerless (instance) summon — raise the event on the instance instead.
						world->RaiseInstanceTrigger(trigger_event::OnSummonedUnitDied, summonPtr);
					});

				world->AddGameObject(*summon);

				if (despawnMs > 0)
				{
					summon->TriggerDespawnTimer(despawnMs);
				}

				// Make the summon hostile to a target so it joins the fight immediately.
				GameUnitS* aggroTarget = nullptr;
				if (const auto triggering = weakTriggering.lock())
				{
					aggroTarget = triggering.get();
				}
				else if (attackNearest)
				{
					auto players = GetPlayersInWorld(world);
					float nearestSqr = std::numeric_limits<float>::max();
					for (auto* player : players)
					{
						const float distSqr = (player->GetPosition() - position).GetSquaredLength();
						if (distSqr < nearestSqr)
						{
							nearestSqr = distSqr;
							aggroTarget = player;
						}
					}
				}

				if (aggroTarget && aggroTarget->IsAlive())
				{
					// Generating threat causes the AI to enter combat with the target.
					summon->threatened(*aggroTarget, 1.0f);
				}
			});
	}

	void TriggerHandler::HandleTaunt(const proto::TriggerAction& action, TriggerContext& context)
	{
		// The creature to taunt is the action target (defaults to owner).
		GameObjectS* creatureObj = (action.target() != trigger_action_target::None) ? GetActionTarget(action, context) : context.owner;
		if (!creatureObj)
		{
			creatureObj = context.owner;
		}

		auto* creature = dynamic_cast<GameCreatureS*>(creatureObj);
		if (!creature)
		{
			WLOG("TRIGGER_ACTION_TAUNT: Target is not a creature - action ignored");
			return;
		}

		// The taunter is the triggering unit by default.
		GameUnitS* taunter = context.triggeringUnit.lock().get();
		if (!taunter)
		{
			WLOG("TRIGGER_ACTION_TAUNT: No triggering unit to taunt with - action ignored");
			return;
		}

		if (!creature->Taunt(*taunter))
		{
			WLOG("TRIGGER_ACTION_TAUNT: Creature is not in combat - action ignored");
		}
	}

	void TriggerHandler::HandleModifyThreat(const proto::TriggerAction& action, TriggerContext& context)
	{
		// The creature whose threat table is modified is the owner.
		auto* creature = dynamic_cast<GameCreatureS*>(context.owner);
		if (!creature)
		{
			WLOG("TRIGGER_ACTION_MODIFY_THREAT: Owner is not a creature - action ignored");
			return;
		}

		GameObjectS* targetObj = GetActionTarget(action, context);
		if (!targetObj || !targetObj->IsUnit())
		{
			WLOG("TRIGGER_ACTION_MODIFY_THREAT: Needs a unit target - action ignored");
			return;
		}

		const float amount = static_cast<float>(GetActionData(action, 0));
		if (!creature->ModifyThreat(targetObj->AsUnit(), amount))
		{
			WLOG("TRIGGER_ACTION_MODIFY_THREAT: Creature is not in combat - action ignored");
		}
	}

	void TriggerHandler::HandleResetThreat(const proto::TriggerAction& action, TriggerContext& context)
	{
		// The creature whose threat is reset is the action target (defaults to owner).
		GameObjectS* creatureObj = (action.target() != trigger_action_target::None) ? GetActionTarget(action, context) : context.owner;
		if (!creatureObj)
		{
			creatureObj = context.owner;
		}

		auto* creature = dynamic_cast<GameCreatureS*>(creatureObj);
		if (!creature)
		{
			WLOG("TRIGGER_ACTION_RESET_THREAT: Target is not a creature - action ignored");
			return;
		}

		if (!creature->ResetThreat())
		{
			WLOG("TRIGGER_ACTION_RESET_THREAT: Creature is not in combat - action ignored");
		}
	}

	void TriggerHandler::HandleApplyAura(const proto::TriggerAction& action, TriggerContext& context)
	{
		const auto* spell = m_project.spells.getById(GetActionData(action, 0));
		if (!spell)
		{
			ELOG("TRIGGER_ACTION_APPLY_AURA: Invalid spell id");
			return;
		}

		// The caster is the owning unit if any; otherwise the aura is applied as a self-cast.
		GameUnitS* caster = (context.owner && context.owner->IsUnit()) ? &context.owner->AsUnit() : nullptr;

		ForEachActionTarget(action, context, [spell, caster](GameObjectS& obj)
			{
				if (!obj.IsUnit())
				{
					return;
				}

				GameUnitS& unit = obj.AsUnit();
				GameUnitS* effectiveCaster = caster ? caster : &unit;

				SpellTargetMap targetMap;
				targetMap.SetTargetMap(spell_cast_target_flags::Unit);
				targetMap.SetUnitTarget(unit.GetGuid());

				// Instant, cost-free application (no cast time, treated like a proc).
				effectiveCaster->CastSpell(targetMap, *spell, 0, true);
			});
	}

	void TriggerHandler::HandleRemoveAura(const proto::TriggerAction& action, TriggerContext& context)
	{
		const uint32 spellId = static_cast<uint32>(GetActionData(action, 0));
		if (spellId == 0)
		{
			WLOG("TRIGGER_ACTION_REMOVE_AURA: Needs a valid spell id - action ignored");
			return;
		}

		ForEachActionTarget(action, context, [spellId](GameObjectS& obj)
			{
				if (obj.IsUnit())
				{
					obj.AsUnit().RemoveAuraBySpellId(spellId);
				}
			});
	}

	void TriggerHandler::HandleSetInstanceVariable(const proto::TriggerAction& action, TriggerContext& context)
	{
		WorldInstance* world = GetWorldInstance(context);
		if (!world)
		{
			ELOG("TRIGGER_ACTION_SET_INSTANCE_VARIABLE: No world instance");
			return;
		}

		const uint32 key = static_cast<uint32>(GetActionData(action, 0));
		const int64 value = static_cast<int64>(GetActionData(action, 1));
		world->SetInstanceVariable(key, value);
		DLOG("TRIGGER_ACTION_SET_INSTANCE_VARIABLE: Variable " << key << " = " << value);
	}

	void TriggerHandler::HandleBroadcastMessage(const proto::TriggerAction& action, TriggerContext& context)
	{
		WorldInstance* world = GetWorldInstance(context);
		if (!world)
		{
			ELOG("TRIGGER_ACTION_BROADCAST_MESSAGE: No world instance");
			return;
		}

		const String& message = GetActionText(action, 0);
		if (message.empty() || message == "<INVALID_TEXT>")
		{
			WLOG("TRIGGER_ACTION_BROADCAST_MESSAGE: No message text - action ignored");
			return;
		}

		// Deliver the message to every player currently in the instance via their network session.
		for (auto* player : GetPlayersInWorld(world))
		{
			const auto connection = m_playerManager.GetPlayerByCharacterGuid(player->GetGuid());
			if (connection)
			{
				connection->LocalChatMessage(ChatType::System, message);
			}
		}
	}

	double TriggerHandler::EvaluateTriggerFunction(
		proto::TriggerFunction func,
		const google::protobuf::RepeatedField<google::protobuf::int64>& funcData,
		TriggerContext& context)
	{
		GameObjectS* owner = context.owner;

		switch (func)
		{
		case proto::Phase:
			if (owner)
			{
				if (const auto* creature = dynamic_cast<GameCreatureS*>(owner))
				{
					return static_cast<double>(creature->GetCombatPhase());
				}
			}
			return 0.0;

		case proto::Health:
			if (owner && owner->IsUnit())
			{
				return static_cast<double>(owner->AsUnit().GetHealth());
			}
			return 0.0;

		case proto::HealthPct:
			if (owner && owner->IsUnit())
			{
				const uint32 maxHp = owner->AsUnit().GetMaxHealth();
				if (maxHp > 0)
				{
					return static_cast<double>(owner->AsUnit().GetHealth()) / static_cast<double>(maxHp) * 100.0;
				}
			}
			return 0.0;

		case proto::Mana:
			if (owner && owner->IsUnit())
			{
				return static_cast<double>(owner->AsUnit().GetPower());
			}
			return 0.0;

		case proto::ManaPct:
			if (owner && owner->IsUnit())
			{
				const uint32 maxPower = owner->AsUnit().GetMaxPower();
				if (maxPower > 0)
				{
					return static_cast<double>(owner->AsUnit().GetPower()) / static_cast<double>(maxPower) * 100.0;
				}
			}
			return 0.0;

		case proto::IsInCombat:
			if (owner && owner->IsUnit())
			{
				return owner->AsUnit().IsInCombat() ? 1.0 : 0.0;
			}
			return 0.0;

		case proto::EncounterState:
		{
			const uint32 slotId = funcData.size() > 0 ? static_cast<uint32>(funcData[0]) : 0;
			auto* world = GetWorldInstance(context);
			if (!world)
			{
				return 0.0;
			}
			return static_cast<double>(world->GetEncounterState(slotId));
		}

		case proto::PlayerCount:
		{
			auto* world = GetWorldInstance(context);
			return world ? static_cast<double>(world->GetPlayerCount()) : 0.0;
		}

		case proto::TargetHealthPct:
			if (owner && owner->IsUnit())
			{
				if (GameUnitS* victim = owner->AsUnit().GetVictim())
				{
					const uint32 maxHp = victim->GetMaxHealth();
					if (maxHp > 0)
					{
						return static_cast<double>(victim->GetHealth()) / static_cast<double>(maxHp) * 100.0;
					}
				}
			}
			return 0.0;

		case proto::HasAura:
		{
			const uint32 spellId = funcData.size() > 0 ? static_cast<uint32>(funcData[0]) : 0;
			if (spellId != 0 && owner && owner->IsUnit())
			{
				return owner->AsUnit().HasAuraSpell(spellId) ? 1.0 : 0.0;
			}
			return 0.0;
		}

		case proto::RandomValue:
		{
			// FunctionData: [max] (default 100) or [min, max]. Inclusive.
			int64 minValue = 0;
			int64 maxValue = 100;
			if (funcData.size() >= 2)
			{
				minValue = funcData[0];
				maxValue = funcData[1];
			}
			else if (funcData.size() == 1)
			{
				maxValue = funcData[0];
			}

			if (maxValue < minValue)
			{
				std::swap(minValue, maxValue);
			}

			std::uniform_int_distribution<int64> dist(minValue, maxValue);
			return static_cast<double>(dist(randomGenerator));
		}

		case proto::InstanceVariable:
		{
			const uint32 key = funcData.size() > 0 ? static_cast<uint32>(funcData[0]) : 0;
			auto* world = GetWorldInstance(context);
			return world ? static_cast<double>(world->GetInstanceVariable(key)) : 0.0;
		}

		default:
			WLOG("EvaluateTriggerFunction: Unknown function " << func);
			return 0.0;
		}
	}

	bool TriggerHandler::EvaluateCondition(const proto::TriggerCondition& condition, TriggerContext& context)
	{
		double leftVal  = 0.0;
		double rightVal = 0.0;

		if (condition.has_leftlong())
		{
			leftVal = static_cast<double>(condition.leftlong());
		}
		else if (condition.has_leftfloat())
		{
			leftVal = static_cast<double>(condition.leftfloat());
		}
		else if (condition.has_leftfunction())
		{
			leftVal = EvaluateTriggerFunction(condition.leftfunction(), condition.leftfunctiondata(), context);
		}
		else if (condition.has_leftcondition())
		{
			leftVal = EvaluateCondition(condition.leftcondition(), context) ? 1.0 : 0.0;
		}

		if (condition.has_rightlong())
		{
			rightVal = static_cast<double>(condition.rightlong());
		}
		else if (condition.has_rightfloat())
		{
			rightVal = static_cast<double>(condition.rightfloat());
		}
		else if (condition.has_rightfunction())
		{
			rightVal = EvaluateTriggerFunction(condition.rightfunction(), condition.rightfunctiondata(), context);
		}
		else if (condition.has_rightcondition())
		{
			rightVal = EvaluateCondition(condition.rightcondition(), context) ? 1.0 : 0.0;
		}

		switch (condition.operator_())
		{
		case proto::Equal:        return leftVal == rightVal;
		case proto::NotEqual:     return leftVal != rightVal;
		case proto::Greater:      return leftVal >  rightVal;
		case proto::GreaterEqual: return leftVal >= rightVal;
		case proto::Less:         return leftVal <  rightVal;
		case proto::LessEqual:    return leftVal <= rightVal;
		default:
			WLOG("EvaluateCondition: Unknown operator");
			return false;
		}
	}
}
