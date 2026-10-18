// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_api.h"
#include "bot_spell_info.h"

#include "game_states/world_state.h"
#include "luabind_lambda.h"
#include "net/realm_connector.h"
#include "player_controller.h"

#include "game_client/game_player_c.h"
#include "game_client/game_unit_c.h"
#include "game_client/object_mgr.h"
#include "math/constants.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace mmo
{
	extern uint32 g_mapId;

	namespace
	{
		/// Upper bound (yards) for GetNearbyUnits and FindRandomNavPoint, which keeps a script
		/// from scanning the whole map or loading half its nav pages in one call.
		constexpr float MaxQueryRadius = 200.0f;

		constexpr float DefaultMoveToTargetRange = 3.0f;

		/// Every global this file registers. They are cleared before registering again, because
		/// luabind chains a second registration of a name into an overload set, and two identical
		/// signatures make every call ambiguous after a UI reload.
		constexpr const char* BotFunctionNames[] =
		{
			"IsBotApiEnabled", "UnitPosition", "UnitFacing", "UnitDistance", "UnitAffectingCombat",
			"GetNearbyUnits", "TargetUnitByGuid", "FaceTarget", "MoveTo", "MoveToTarget", "StopMoving",
			"GetMoveStatus", "FindRandomNavPoint", "InteractWithTarget", "GetCurrentMapId", "GetBotSpellInfo"
		};

		/// Resolves the unit tokens the bot API understands: "player" and "target".
		std::shared_ptr<GameUnitC> ResolveUnit(const std::string& token)
		{
			if (token == "player")
			{
				return ObjectMgr::GetActivePlayer();
			}

			if (token == "target")
			{
				return ObjectMgr::GetSelectedObject();
			}

			return nullptr;
		}

		luabind::object MakePosition(lua_State* luaState, const Vector3& position)
		{
			luabind::object result = luabind::newtable(luaState);
			result["x"] = position.x;
			result["y"] = position.y;
			result["z"] = position.z;
			return result;
		}

		float ClampRadius(const float radius)
		{
			return std::isfinite(radius) ? std::clamp(radius, 0.0f, MaxQueryRadius) : 0.0f;
		}

		luabind::object MakeUnitEntry(lua_State* luaState, const GamePlayerC& player, const GameUnitC& unit, const float distance)
		{
			const Vector3& position = unit.GetPosition();

			luabind::object entry = luabind::newtable(luaState);
			entry["guid"] = std::to_string(unit.GetGuid());
			entry["name"] = unit.GetName();
			entry["entry"] = unit.Get<uint32>(object_fields::Entry);
			entry["level"] = unit.GetLevel();
			entry["x"] = position.x;
			entry["y"] = position.y;
			entry["z"] = position.z;
			entry["distance"] = distance;
			entry["alive"] = unit.IsAlive();
			entry["hostile"] = !player.IsFriendlyTo(unit);
			entry["isPlayer"] = unit.IsPlayer();
			entry["lootable"] = unit.CanBeLooted();
			entry["inCombat"] = unit.IsInCombat();
			entry["targetingPlayer"] = unit.Get<uint64>(object_fields::TargetUnit) == player.GetGuid();
			return entry;
		}
	}

	void BotApi::RegisterLuaFunctions(lua_State* luaState)
	{
		luabind::object globals = luabind::globals(luaState);
		for (const char* name : BotFunctionNames)
		{
			globals[name] = luabind::object();
		}

		luabind::module(luaState)
		[
			luabind::def<std::function<bool()>>("IsBotApiEnabled", []()
			{
				return bot_api::IsEnabled();
			}),

			luabind::def<std::function<luabind::object(lua_State*, const std::string&)>>("UnitPosition", [](lua_State* L, const std::string& token)
			{
				const std::shared_ptr<GameUnitC> unit = bot_api::IsEnabled() ? ResolveUnit(token) : nullptr;
				return unit ? MakePosition(L, unit->GetPosition()) : luabind::object();
			}),

			luabind::def<std::function<luabind::object(lua_State*, const std::string&)>>("UnitFacing", [](lua_State* L, const std::string& token)
			{
				const std::shared_ptr<GameUnitC> unit = bot_api::IsEnabled() ? ResolveUnit(token) : nullptr;
				if (!unit)
				{
					return luabind::object();
				}

				float facing = std::fmod(unit->GetFacing().GetValueRadians(), TwoPi);
				if (facing < 0.0f)
				{
					facing += TwoPi;
				}

				return luabind::object(L, facing);
			}),

			luabind::def<std::function<luabind::object(lua_State*, const std::string&)>>("UnitDistance", [](lua_State* L, const std::string& token)
			{
				const std::shared_ptr<GamePlayerC> player = bot_api::IsEnabled() ? ObjectMgr::GetActivePlayer() : nullptr;
				const std::shared_ptr<GameUnitC> unit = player ? ResolveUnit(token) : nullptr;
				return unit ? luabind::object(L, player->GetPosition().GetDistanceTo(unit->GetPosition())) : luabind::object();
			}),

			luabind::def<std::function<luabind::object(lua_State*, const std::string&)>>("UnitAffectingCombat", [](lua_State* L, const std::string& token)
			{
				const std::shared_ptr<GameUnitC> unit = bot_api::IsEnabled() ? ResolveUnit(token) : nullptr;
				return unit ? luabind::object(L, unit->IsInCombat()) : luabind::object();
			}),

			luabind::def<std::function<luabind::object(lua_State*, float)>>("GetNearbyUnits", [](lua_State* L, const float radius)
			{
				const std::shared_ptr<GamePlayerC> player = bot_api::IsEnabled() ? ObjectMgr::GetActivePlayer() : nullptr;
				if (!player)
				{
					return luabind::object();
				}

				const float maxDistance = ClampRadius(radius);
				std::vector<std::pair<float, const GameUnitC*>> units;
				ObjectMgr::ForEachUnit([&](const GameUnitC& unit)
				{
					if (unit.GetGuid() == player->GetGuid())
					{
						return;
					}

					const float distance = player->GetPosition().GetDistanceTo(unit.GetPosition());
					if (distance <= maxDistance)
					{
						units.emplace_back(distance, &unit);
					}
				});

				std::sort(units.begin(), units.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

				luabind::object result = luabind::newtable(L);
				int index = 1;
				for (const auto& [distance, unit] : units)
				{
					result[index++] = MakeUnitEntry(L, *player, *unit, distance);
				}

				return result;
			}),

			luabind::def<std::function<luabind::object(lua_State*, const std::string&)>>("TargetUnitByGuid", [this](lua_State* L, const std::string& guidText)
			{
				const std::shared_ptr<GamePlayerC> player = bot_api::IsEnabled() ? ObjectMgr::GetActivePlayer() : nullptr;
				if (!player)
				{
					return luabind::object();
				}

				uint64 guid = 0;
				const char* end = guidText.data() + guidText.size();
				const auto [parsedEnd, error] = std::from_chars(guidText.data(), end, guid);
				const std::shared_ptr<GameUnitC> unit = error == std::errc() && parsedEnd == end ? ObjectMgr::Get<GameUnitC>(guid) : nullptr;
				if (!unit)
				{
					return luabind::object(L, false);
				}

				ObjectMgr::SetSelectedObjectGuid(guid);
				player->SetTargetUnit(unit);
				m_realmConnector.SetSelection(guid);
				return luabind::object(L, true);
			}),

			luabind::def<std::function<luabind::object(lua_State*)>>("FaceTarget", [](lua_State* L)
			{
				const std::shared_ptr<GamePlayerC> player = bot_api::IsEnabled() ? ObjectMgr::GetActivePlayer() : nullptr;
				if (!player)
				{
					return luabind::object();
				}

				const std::shared_ptr<GameUnitC> target = ObjectMgr::GetSelectedObject();
				if (!target || target == player)
				{
					return luabind::object(L, false);
				}

				player->SetFacing(player->GetAngle(*target));
				return luabind::object(L, true);
			}),

			luabind::def<std::function<luabind::object(lua_State*, float, float, float)>>("MoveTo", [this](lua_State* L, const float x, const float y, const float z)
			{
				if (!bot_api::IsEnabled())
				{
					return luabind::object();
				}

				return luabind::object(L, MoveTo(Vector3(x, y, z)));
			}),

			luabind::def<std::function<luabind::object(lua_State*)>>("MoveToTarget", [this](lua_State* L)
			{
				return bot_api::IsEnabled() ? luabind::object(L, MoveToTarget(DefaultMoveToTargetRange)) : luabind::object();
			}),

			luabind::def<std::function<luabind::object(lua_State*, float)>>("MoveToTarget", [this](lua_State* L, const float range)
			{
				return bot_api::IsEnabled() ? luabind::object(L, MoveToTarget(range)) : luabind::object();
			}),

			luabind::def<std::function<void()>>("StopMoving", [this]()
			{
				if (bot_api::IsEnabled())
				{
					Stop(MoveStatus::Idle);
				}
			}),

			luabind::def<std::function<luabind::object(lua_State*)>>("GetMoveStatus", [this](lua_State* L)
			{
				return bot_api::IsEnabled() ? luabind::object(L, GetMoveStatusName()) : luabind::object();
			}),

			luabind::def<std::function<luabind::object(lua_State*, float)>>("FindRandomNavPoint", [this](lua_State* L, const float radius)
			{
				const std::shared_ptr<GamePlayerC> player = bot_api::IsEnabled() ? ObjectMgr::GetActivePlayer() : nullptr;
				if (!player)
				{
					return luabind::object();
				}

				Vector3 point;
				if (m_nav.FindRandomPoint(g_mapId, player->GetPosition(), ClampRadius(radius), point) != BotNavResult::Ok)
				{
					return luabind::object();
				}

				return MakePosition(L, point);
			}),

			luabind::def<std::function<luabind::object(lua_State*)>>("InteractWithTarget", [](lua_State* L)
			{
				PlayerController* controller = bot_api::IsEnabled() ? WorldState::GetPlayerController() : nullptr;
				if (!controller)
				{
					return luabind::object();
				}

				const std::shared_ptr<GameUnitC> target = ObjectMgr::GetSelectedObject();
				if (!target)
				{
					return luabind::object(L, false);
				}

				controller->InteractWithObject(*target);
				return luabind::object(L, true);
			}),

			luabind::def<std::function<luabind::object(lua_State*)>>("GetCurrentMapId", [](lua_State* L)
			{
				return bot_api::IsEnabled() ? luabind::object(L, g_mapId) : luabind::object();
			}),

			luabind::def<std::function<luabind::object(lua_State*, uint32)>>("GetBotSpellInfo", [this](lua_State* L, const uint32 spellId)
			{
				const proto_client::SpellEntry* spell = bot_api::IsEnabled() ? m_project.spells.getById(spellId) : nullptr;
				if (!spell)
				{
					return luabind::object();
				}

				const BotSpellInfo info = DescribeBotSpell(*spell, m_project.ranges);
				luabind::object result = luabind::newtable(L);
				result["offensive"] = info.offensive;
				result["maxRange"] = info.maxRange;
				result["castTime"] = info.castTime;
				result["cost"] = info.cost;
				result["powerType"] = info.powerType;
				result["score"] = info.score;
				return result;
			})
		];
	}
}
