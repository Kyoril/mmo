// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bug_snapshot_builder.h"

#include "player.h"

#include "base/clock.h"
#include "game_server/ai/creature_ai.h"
#include "game_server/ai/creature_ai_alert_state.h"
#include "game_server/ai/creature_ai_combat_state.h"
#include "game_server/ai/creature_ai_death_state.h"
#include "game_server/ai/creature_ai_idle_state.h"
#include "game_server/ai/creature_ai_prepare_state.h"
#include "game_server/ai/creature_ai_reset_state.h"
#include "game_server/loot_instance.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/objects/game_item_s.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/objects/game_world_object_s.h"
#include "game_server/quest_status_data.h"
#include "game_server/spells/aura_container.h"
#include "game_server/world/world_instance.h"
#include "proto_data/project.h"

#include <sstream>

namespace mmo
{
	using json = nlohmann::json;

	namespace
	{
		/// 64 bit ids travel as decimal strings: JSON numbers lose precision above 2^53.
		std::string GuidString(const uint64 guid)
		{
			return std::to_string(guid);
		}

		json PositionOf(const GameObjectS& object)
		{
			const auto& pos = object.GetPosition();
			return json{
				{ "x", pos.x },
				{ "y", pos.y },
				{ "z", pos.z },
				{ "facing", object.GetFacing().GetValueRadians() }
			};
		}

		json AuraToJson(const AuraContainer& aura)
		{
			return json{
				{ "spellId", aura.GetSpellId() },
				{ "spellName", aura.GetSpell().name() },
				{ "casterGuid", GuidString(aura.GetCasterId()) },
				{ "applied", aura.IsApplied() },
				{ "visible", aura.IsVisible() },
				{ "expires", aura.DoesExpire() },
				{ "durationMs", aura.GetDuration() },
				{ "remainingMs", aura.DoesExpire() ? aura.GetRemainingTime() : 0 },
				{ "stacks", aura.GetStackCount() },
				{ "itemGuid", GuidString(aura.GetItemGuid()) }
			};
		}

		json AurasOf(const GameUnitS& unit)
		{
			json auras = json::array();
			unit.ForEachAura([&auras](const AuraContainer& aura)
			{
				auras.push_back(AuraToJson(aura));
			});
			return auras;
		}

		json UnitBasics(const GameUnitS& unit)
		{
			return json{
				{ "guid", GuidString(unit.GetGuid()) },
				{ "name", unit.GetName() },
				{ "entry", unit.Get<uint32>(object_fields::Entry) },
				{ "level", unit.GetLevel() },
				{ "alive", unit.IsAlive() },
				{ "health", unit.GetHealth() },
				{ "maxHealth", unit.GetMaxHealth() },
				{ "powerType", unit.GetPowerType() },
				{ "power", unit.GetPower() },
				{ "maxPower", unit.GetMaxPower() },
				{ "inCombat", unit.IsInCombat() },
				{ "targetGuid", GuidString(unit.Get<uint64>(object_fields::TargetUnit)) },
				{ "mapId", unit.GetMapId() },
				{ "position", PositionOf(unit) }
			};
		}

		const char* AiStateName(const CreatureAI* ai)
		{
			if (!ai || !ai->GetCurrentState())
			{
				return "none";
			}

			const CreatureAIState* state = ai->GetCurrentState();
			if (dynamic_cast<const CreatureAIIdleState*>(state)) return "idle";
			if (dynamic_cast<const CreatureAIAlertState*>(state)) return "alert";
			if (dynamic_cast<const CreatureAICombatState*>(state)) return "combat";
			if (dynamic_cast<const CreatureAIDeathState*>(state)) return "death";
			if (dynamic_cast<const CreatureAIPrepareState*>(state)) return "prepare";
			if (dynamic_cast<const CreatureAIResetState*>(state)) return "reset";
			return "unknown";
		}

		json LootOf(const GameObjectS& object)
		{
			const auto& loot = object.GetLoot();
			if (!loot)
			{
				return nullptr;
			}

			return json{
				{ "empty", loot->IsEmpty() },
				{ "gold", loot->GetGold() },
				{ "itemCount", loot->GetItemCount() }
			};
		}

		json CharacterSnapshot(Player& player)
		{
			GamePlayerS& character = player.GetCharacter();

			json classes = json::array();
			for (const auto& known : character.GetKnownClasses())
			{
				classes.push_back(json{ { "classId", known.classId }, { "level", known.classLevel }, { "xp", known.classXp } });
			}

			json character_json = UnitBasics(character);
			character_json["race"] = character.Get<uint32>(object_fields::Race);
			character_json["class"] = character.Get<uint32>(object_fields::Class);
			character_json["gender"] = character.GetGender();
			character_json["knownClasses"] = std::move(classes);
			character_json["groupId"] = GuidString(character.GetGroupId());
			character_json["gameMaster"] = character.IsGameMaster();
			return character_json;
		}

		json CombatEvents(const Player& player)
		{
			json events = json::array();
			const GameTime now = GetAsyncTimeMs();
			for (const auto& event : player.GetCombatEvents().GetEvents())
			{
				events.push_back(json{
					{ "msAgo", now >= event.time ? now - event.time : 0 },
					{ "type", combat_event_type::GetName(event.type) },
					{ "otherGuid", GuidString(event.otherGuid) },
					{ "spellId", event.spellId },
					{ "amount", event.amount },
					{ "school", event.school }
				});
			}
			return events;
		}

		json ItemSubject(Player& player, const game::BugReportPayload& payload, const proto::Project& project)
		{
			json subject;
			if (const auto* entry = project.items.getById(payload.subjectId))
			{
				subject["entryName"] = entry->name();
				subject["entryDump"] = entry->DebugString();
			}
			else
			{
				subject["entryMissing"] = true;
			}

			auto& inventory = player.GetCharacter().GetInventory();
			subject["ownedCount"] = inventory.GetItemCount(payload.subjectId);

			uint16 slot = 0;
			if (payload.subjectGuid != 0 && inventory.FindItemByGUID(payload.subjectGuid, slot))
			{
				if (const auto item = inventory.GetItemAtSlot(slot))
				{
					subject["instance"] = json{
						{ "guid", GuidString(item->GetGuid()) },
						{ "slot", slot },
						{ "entry", item->GetEntry().id() },
						{ "stackCount", item->GetStackCount() },
						{ "durability", item->Get<uint32>(object_fields::Durability) },
						{ "maxDurability", item->Get<uint32>(object_fields::MaxDurability) },
						{ "enchantment", item->Get<uint32>(object_fields::Enchantment) },
						{ "creatorGuid", GuidString(item->Get<uint64>(object_fields::Creator)) },
						{ "broken", item->IsBroken() }
					};
				}
			}

			return subject;
		}

		json SpellSubject(Player& player, const game::BugReportPayload& payload, const proto::Project& project)
		{
			const GamePlayerS& character = player.GetCharacter();

			json subject;
			if (const auto* entry = project.spells.getById(payload.subjectId))
			{
				subject["entryName"] = entry->name();
				subject["entryDump"] = entry->DebugString();
			}
			else
			{
				subject["entryMissing"] = true;
			}

			subject["known"] = character.HasSpell(payload.subjectId);

			subject["cooldownRemainingMs"] = 0;
			for (const auto& cooldown : character.GetPersistentCooldowns())
			{
				if (cooldown.spellId == payload.subjectId)
				{
					subject["cooldownRemainingMs"] = cooldown.remainingMs;
				}
			}

			if (const auto* lastCast = player.GetCombatEvents().FindLastCastResult(payload.subjectId))
			{
				subject["lastCastSucceeded"] = lastCast->amount != 0;
				subject["lastCastMsAgo"] = GetAsyncTimeMs() - lastCast->time;
			}

			return subject;
		}

		json CreatureSubject(Player& player, const game::BugReportPayload& payload, const proto::Project& project)
		{
			json subject;
			if (const auto* entry = project.units.getById(payload.subjectId))
			{
				subject["entryName"] = entry->name();
				subject["entryDump"] = entry->DebugString();
			}
			else
			{
				subject["entryMissing"] = true;
			}

			WorldInstance* world = player.GetCharacter().GetWorldInstance();
			GameObjectS* object = world && payload.subjectGuid != 0 ? world->FindObjectByGuid(payload.subjectGuid) : nullptr;
			if (!object || !object->IsUnit())
			{
				subject["inWorld"] = false;
				return subject;
			}

			subject["inWorld"] = true;
			const auto& unit = static_cast<const GameUnitS&>(*object);
			json instance = UnitBasics(unit);
			instance["auras"] = AurasOf(unit);
			instance["distance"] = (unit.GetPosition() - player.GetCharacter().GetPosition()).GetLength();
			instance["loot"] = LootOf(unit);

			if (const auto* creature = dynamic_cast<const GameCreatureS*>(&unit))
			{
				const CreatureAI* ai = creature->GetAI();
				instance["aiState"] = AiStateName(ai);
				instance["evading"] = ai ? ai->IsEvading() : false;
				instance["tagged"] = creature->IsTagged();
				instance["combatPhase"] = creature->GetCombatPhase();

				if (ai)
				{
					const auto& home = ai->GetHome();
					instance["home"] = json{ { "x", home.position.x }, { "y", home.position.y }, { "z", home.position.z }, { "radius", home.radius } };

					if (const auto* combat = dynamic_cast<const CreatureAICombatState*>(ai->GetCurrentState()))
					{
						json threat = json::array();
						combat->ForEachThreat([&threat](const uint64 guid, const float amount)
						{
							threat.push_back(json{ { "guid", GuidString(guid) }, { "threat", amount } });
						});
						instance["threat"] = std::move(threat);
						instance["casting"] = combat->IsCasting();
					}
				}
			}

			subject["instance"] = std::move(instance);
			return subject;
		}

		json QuestSubject(Player& player, const game::BugReportPayload& payload, const proto::Project& project)
		{
			const GamePlayerS& character = player.GetCharacter();

			json subject;
			if (const auto* entry = project.quests.getById(payload.subjectId))
			{
				subject["entryName"] = entry->name();
				subject["entryDump"] = entry->DebugString();
			}
			else
			{
				subject["entryMissing"] = true;
			}

			subject["status"] = static_cast<uint32>(character.GetQuestStatus(payload.subjectId));
			subject["rewarded"] = character.HasRewardedQuest(payload.subjectId);

			if (const QuestStatusData* data = character.GetQuestData(payload.subjectId))
			{
				json counters = json::array();
				for (const auto count : data->creatures)
				{
					counters.push_back(count);
				}

				subject["questLog"] = json{
					{ "status", static_cast<uint32>(data->status) },
					{ "explored", data->explored },
					{ "expiration", data->expiration },
					{ "creatureCounters", std::move(counters) }
				};
			}

			return subject;
		}

		json AuraSubject(Player& player, const game::BugReportPayload& payload, const proto::Project& project)
		{
			json subject;
			if (const auto* entry = project.spells.getById(payload.subjectId))
			{
				subject["entryName"] = entry->name();
				subject["entryDump"] = entry->DebugString();
			}
			else
			{
				subject["entryMissing"] = true;
			}

			json matches = json::array();
			player.GetCharacter().ForEachAura([&](const AuraContainer& aura)
			{
				if (aura.GetSpellId() == payload.subjectId)
				{
					matches.push_back(AuraToJson(aura));
				}
			});
			subject["activeOnCharacter"] = std::move(matches);
			return subject;
		}

		json ObjectSubject(Player& player, const game::BugReportPayload& payload, const proto::Project& project)
		{
			json subject;
			if (const auto* entry = project.objects.getById(payload.subjectId))
			{
				subject["entryName"] = entry->name();
				subject["entryDump"] = entry->DebugString();
			}
			else
			{
				subject["entryMissing"] = true;
			}

			WorldInstance* world = player.GetCharacter().GetWorldInstance();
			GameObjectS* object = world && payload.subjectGuid != 0 ? world->FindObjectByGuid(payload.subjectGuid) : nullptr;
			const auto* worldObject = dynamic_cast<const GameWorldObjectS*>(object);
			if (!worldObject)
			{
				subject["inWorld"] = false;
				return subject;
			}

			subject["inWorld"] = true;
			subject["instance"] = json{
				{ "guid", GuidString(worldObject->GetGuid()) },
				{ "entry", worldObject->Get<uint32>(object_fields::Entry) },
				{ "type", static_cast<uint32>(worldObject->GetType()) },
				{ "state", worldObject->Get<uint32>(object_fields::State) },
				{ "open", worldObject->IsOpen() },
				{ "usable", worldObject->IsUsable(player.GetCharacter()) },
				{ "position", PositionOf(*worldObject) },
				{ "distance", (worldObject->GetPosition() - player.GetCharacter().GetPosition()).GetLength() },
				{ "loot", LootOf(*worldObject) }
			};
			return subject;
		}
	}

	json BuildBugReportServerSnapshot(Player& player, const game::BugReportPayload& payload, const proto::Project& project)
	{
		GamePlayerS& character = player.GetCharacter();

		json snapshot;
		snapshot["character"] = CharacterSnapshot(player);
		snapshot["auras"] = AurasOf(character);
		snapshot["combatEvents"] = CombatEvents(player);

		json world;
		world["mapId"] = character.GetMapId();
		if (const auto* map = project.maps.getById(character.GetMapId()))
		{
			world["mapName"] = map->name();
		}
		if (WorldInstance* instance = character.GetWorldInstance())
		{
			std::ostringstream id;
			id << instance->GetId();
			world["instanceId"] = id.str();
			world["dungeon"] = instance->IsDungeon();
			world["raid"] = instance->IsRaid();
			world["playerCount"] = instance->GetPlayerCount();
		}
		snapshot["world"] = std::move(world);

		const uint64 targetGuid = character.Get<uint64>(object_fields::TargetUnit);
		if (WorldInstance* instance = character.GetWorldInstance(); instance && targetGuid != 0)
		{
			if (GameObjectS* target = instance->FindObjectByGuid(targetGuid); target && target->IsUnit())
			{
				snapshot["target"] = UnitBasics(static_cast<const GameUnitS&>(*target));
			}
		}

		json subjectState;
		switch (payload.subjectType)
		{
		case game::bug_report_subject::Item:
			subjectState = ItemSubject(player, payload, project);
			break;
		case game::bug_report_subject::Spell:
			subjectState = SpellSubject(player, payload, project);
			break;
		case game::bug_report_subject::Creature:
			subjectState = CreatureSubject(player, payload, project);
			break;
		case game::bug_report_subject::Quest:
			subjectState = QuestSubject(player, payload, project);
			break;
		case game::bug_report_subject::Aura:
			subjectState = AuraSubject(player, payload, project);
			break;
		case game::bug_report_subject::Object:
			subjectState = ObjectSubject(player, payload, project);
			break;
		default:
			subjectState = nullptr;
			break;
		}
		snapshot["subjectState"] = std::move(subjectState);

		return snapshot;
	}
}
