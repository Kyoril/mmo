// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "scenario_engine.h"
#include "scenario_transcript.h"

#include "base/clock.h"
#include "game/spell.h"
#include "game/spell_target_map.h"
#include "log/default_log_levels.h"
#include "mmo_client/luabind_lambda.h"

#include "deps/lua/lua.hpp"
#include "luabind/luabind.hpp"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>

namespace mmo
{
	namespace
	{
		/// Shared state of the currently running scenario. The engine is strictly
		/// single-threaded and runs one scenario per process, so a single global is fine.
		struct ScenarioRuntime final
		{
			E2eSession* session { nullptr };
			ScenarioTranscript* transcript { nullptr };
			std::chrono::steady_clock::time_point deadline;
			e2e_exit_code::Type abortCode { e2e_exit_code::ScenarioFailed };
			bool aborted { false };
			uint64 selectedTarget { 0 };
		};

		ScenarioRuntime* g_runtime = nullptr;

		/// Aborts the running scenario: records the exit code, then throws so luabind
		/// converts this into a Lua error that unwinds to the engine's pcall.
		[[noreturn]] void abortScenario(const e2e_exit_code::Type code, const std::string& message)
		{
			g_runtime->abortCode = code;
			g_runtime->aborted = true;
			if (g_runtime->transcript)
			{
				g_runtime->transcript->AssertFailed(message);
			}
			throw std::runtime_error(message);
		}

		/// One pump step that also enforces the global watchdog and connection health.
		void pumpChecked()
		{
			E2eSession& session = *g_runtime->session;
			session.Pump();

			if (session.IsStopRequested())
			{
				abortScenario(e2e_exit_code::Disconnected, "connection to the server was lost");
			}
			if (std::chrono::steady_clock::now() >= g_runtime->deadline)
			{
				abortScenario(e2e_exit_code::Timeout, "scenario watchdog timeout exceeded");
			}
		}

		std::string guidToString(const uint64 guid)
		{
			char buffer[24];
			std::snprintf(buffer, sizeof(buffer), "0x%" PRIx64, guid);
			return buffer;
		}

		uint64 guidFromString(const std::string& guid)
		{
			if (guid.empty())
			{
				return 0;
			}
			return std::strtoull(guid.c_str(), nullptr, 0);
		}

		const BotUnit* findUnit(const std::string& guidStr)
		{
			return g_runtime->session->GetContext().GetUnit(guidFromString(guidStr));
		}

		const BotUnit* self()
		{
			return g_runtime->session->GetContext().GetSelf();
		}

		bool isObjectTruthy(const luabind::object& value)
		{
			const int type = luabind::type(value);
			if (type == LUA_TNIL || type == LUA_TNONE)
			{
				return false;
			}
			if (type == LUA_TBOOLEAN)
			{
				return luabind::object_cast<bool>(value);
			}
			return true;
		}

		// ============================================================
		// Lua API implementation
		// ============================================================

		void luaLog(const std::string& message)
		{
			ILOG("[Scenario] " << message);
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Log(message);
			}
		}

		void luaSleep(const uint32 milliseconds)
		{
			const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
			while (std::chrono::steady_clock::now() < until)
			{
				pumpChecked();
			}
		}

		void luaAssert(const bool condition, const std::string& message)
		{
			if (!condition)
			{
				abortScenario(e2e_exit_code::ScenarioFailed, "Assert failed: " + message);
			}

			if (g_runtime->transcript)
			{
				g_runtime->transcript->AssertPassed(message);
			}
		}

		void luaFail(const std::string& message)
		{
			abortScenario(e2e_exit_code::ScenarioFailed, "Fail: " + message);
		}

		bool luaWaitUntil(luabind::object predicate, const uint32 timeoutMs, const std::string& description)
		{
			if (luabind::type(predicate) != LUA_TFUNCTION)
			{
				abortScenario(e2e_exit_code::ScenarioFailed, "WaitUntil: first argument must be a function");
			}

			const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
			while (std::chrono::steady_clock::now() < until)
			{
				const luabind::object result = luabind::object(predicate());
				if (isObjectTruthy(result))
				{
					if (g_runtime->transcript)
					{
						g_runtime->transcript->Event("wait_until", { { "description", description }, { "result", "fulfilled" } });
					}
					return true;
				}

				pumpChecked();
			}

			if (g_runtime->transcript)
			{
				g_runtime->transcript->Event("wait_until", { { "description", description }, { "result", "timeout" } });
			}
			return false;
		}

		std::string luaMe()
		{
			const BotUnit* selfUnit = self();
			return selfUnit ? guidToString(selfUnit->GetGuid()) : std::string();
		}

		bool luaUnitExists(const std::string& guid)
		{
			return findUnit(guid) != nullptr;
		}

		int32 luaGetHealth(const std::string& guid)
		{
			const BotUnit* unit = findUnit(guid);
			return unit ? static_cast<int32>(unit->GetHealth()) : -1;
		}

		int32 luaGetMaxHealth(const std::string& guid)
		{
			const BotUnit* unit = findUnit(guid);
			return unit ? static_cast<int32>(unit->GetMaxHealth()) : -1;
		}

		int32 luaGetLevel(const std::string& guid)
		{
			const BotUnit* unit = findUnit(guid);
			return unit ? static_cast<int32>(unit->GetLevel()) : -1;
		}

		int32 luaGetPower(const std::string& guid)
		{
			const BotUnit* unit = findUnit(guid);
			return unit ? static_cast<int32>(unit->GetPower()) : -1;
		}

		int32 luaGetMaxPower(const std::string& guid)
		{
			const BotUnit* unit = findUnit(guid);
			return unit ? static_cast<int32>(unit->GetMaxPower()) : -1;
		}

		bool luaIsAlive(const std::string& guid)
		{
			const BotUnit* unit = findUnit(guid);
			return unit && !unit->IsDead();
		}

		std::string luaGetName(const std::string& guid)
		{
			const BotUnit* unit = findUnit(guid);
			return unit ? unit->GetName() : std::string();
		}

		float luaGetPosX(const std::string& guid)
		{
			const BotUnit* unit = findUnit(guid);
			return unit ? unit->GetPosition().x : 0.0f;
		}

		float luaGetPosY(const std::string& guid)
		{
			const BotUnit* unit = findUnit(guid);
			return unit ? unit->GetPosition().y : 0.0f;
		}

		float luaGetPosZ(const std::string& guid)
		{
			const BotUnit* unit = findUnit(guid);
			return unit ? unit->GetPosition().z : 0.0f;
		}

		float luaGetDistance(const std::string& guidA, const std::string& guidB)
		{
			const BotUnit* unitA = findUnit(guidA);
			const BotUnit* unitB = findUnit(guidB);
			if (!unitA || !unitB)
			{
				return -1.0f;
			}
			return unitA->GetDistanceTo(*unitB);
		}

		bool luaHasAura(const std::string& guid, const uint32 spellId)
		{
			const BotUnit* unit = findUnit(guid);
			if (!unit)
			{
				return false;
			}

			for (const auto& aura : unit->GetVisibleAuras())
			{
				if (aura.spellId == spellId)
				{
					return true;
				}
			}
			return false;
		}

		bool luaHasSpell(const uint32 spellId)
		{
			return g_runtime->session->GetContext().HasKnownSpell(spellId);
		}

		int32 luaGetItemCount(const uint32 itemId)
		{
			return static_cast<int32>(g_runtime->session->GetRealm().GetObjectManager().GetItemCountByEntry(itemId));
		}

		int32 luaGetMoney()
		{
			const BotUnit* selfUnit = self();
			return selfUnit ? static_cast<int32>(selfUnit->GetMoney()) : -1;
		}

		std::string luaLastCastResult()
		{
			const BotUnit::CastState state = g_runtime->session->GetContext().GetLastCastState();
			switch (state.status)
			{
			case BotUnit::CastState::Status::None:
				return "none";
			case BotUnit::CastState::Status::Pending:
				return "pending";
			case BotUnit::CastState::Status::Started:
				return "started";
			case BotUnit::CastState::Status::Succeeded:
				return "ok";
			case BotUnit::CastState::Status::Failed:
				return "failed:" + std::to_string(static_cast<int32>(state.failureReason));
			}
			return "unknown";
		}

		std::string luaFindUnitByEntry(const uint32 entry)
		{
			std::string result;
			g_runtime->session->GetContext().GetObjectManager().ForEachCreature([&result, entry](const BotUnit& unit)
				{
					if (result.empty() && unit.GetEntry() == entry)
					{
						result = guidToString(unit.GetGuid());
					}
				});
			return result;
		}

		std::string luaFindUnitByName(const std::string& name)
		{
			std::string result;
			g_runtime->session->GetContext().GetObjectManager().ForEachUnit([&result, &name](const BotUnit& unit)
				{
					if (result.empty() && unit.GetName() == name)
					{
						result = guidToString(unit.GetGuid());
					}
				});
			return result;
		}

		void luaTargetUnit(const std::string& guid)
		{
			const uint64 targetGuid = guidFromString(guid);
			g_runtime->selectedTarget = targetGuid;
			g_runtime->session->GetRealm().SetSelection(targetGuid);
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("TargetUnit", { { "guid", guid } });
			}
		}

		void luaFaceUnit(const std::string& guid)
		{
			g_runtime->session->GetContext().FaceUnit(guidFromString(guid));
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("FaceUnit", { { "guid", guid } });
			}
		}

		bool luaCastSpell(const uint32 spellId, const std::string& targetGuidStr)
		{
			uint64 targetGuid = guidFromString(targetGuidStr);
			if (targetGuid == 0)
			{
				targetGuid = g_runtime->selectedTarget;
			}

			SpellTargetMap targetMap{};
			const BotUnit* selfUnit = self();
			if (targetGuid != 0 && (!selfUnit || targetGuid != selfUnit->GetGuid()))
			{
				targetMap.SetTargetMap(spell_cast_target_flags::Unit);
				targetMap.SetUnitTarget(targetGuid);

				// Most damage spells require the caster to face the target.
				g_runtime->session->GetContext().FaceUnit(targetGuid);
			}

			const bool queued = g_runtime->session->GetContext().CastSpell(spellId, targetMap);
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("CastSpell", {
					{ "spell_id", spellId },
					{ "target", guidToString(targetGuid) },
					{ "queued", queued } });
			}
			return queued;
		}

		void luaCancelCast()
		{
			g_runtime->session->GetRealm().sendSinglePacket([](game::OutgoingPacket& packet)
				{
					packet.Start(game::client_realm_packet::CancelCast);
					packet.Finish();
				});
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("CancelCast");
			}
		}

		void luaStartAttack(const std::string& guid)
		{
			g_runtime->session->GetContext().StartAutoAttack(guidFromString(guid));
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("StartAttack", { { "guid", guid } });
			}
		}

		void luaStopAttack()
		{
			g_runtime->session->GetContext().StopAutoAttack();
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("StopAttack");
			}
		}

		bool luaMoveTo(const float x, const float y, const float z, const uint32 timeoutMs)
		{
			E2eSession& session = *g_runtime->session;
			BotMovementController& controller = session.GetMovementController();
			BotContext& context = session.GetContext();

			const Vector3 target(x, y, z);
			if (!controller.MoveTo(context, target))
			{
				if (g_runtime->transcript)
				{
					g_runtime->transcript->Action("MoveTo", { { "x", x }, { "y", y }, { "z", z }, { "result", "no_path" } });
				}
				return false;
			}

			const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
			while (controller.IsActive())
			{
				if (std::chrono::steady_clock::now() >= until)
				{
					controller.Stop(context, "scenario MoveTo timeout");
					if (g_runtime->transcript)
					{
						g_runtime->transcript->Action("MoveTo", { { "x", x }, { "y", y }, { "z", z }, { "result", "timeout" } });
					}
					return false;
				}

				controller.Update(context);
				pumpChecked();
			}

			const bool reached = controller.GetStatus() == BotMovementStatus::Reached;
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("MoveTo", { { "x", x }, { "y", y }, { "z", z },
					{ "result", reached ? "reached" : controller.GetLastReason() } });
			}
			return reached;
		}

		void luaSendChat(const std::string& message)
		{
			g_runtime->session->GetContext().SendChatMessage(message, ChatType::Say);
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("SendChat", { { "message", message } });
			}
		}

		// ============================================================
		// GM commands
		// ============================================================

		void luaGmAddItem(const uint32 itemId, const uint32 count)
		{
			g_runtime->session->GetRealm().CheatAddItem(itemId, static_cast<uint8>(count));
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("GM.AddItem", { { "item_id", itemId }, { "count", count } });
			}
		}

		void luaGmLearnSpell(const uint32 spellId)
		{
			g_runtime->session->GetRealm().CheatLearnSpell(spellId);
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("GM.LearnSpell", { { "spell_id", spellId } });
			}
		}

		void luaGmLevelUp(const uint32 levels)
		{
			g_runtime->session->GetRealm().CheatLevelUp(static_cast<uint8>(levels));
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("GM.LevelUp", { { "levels", levels } });
			}
		}

		void luaGmGiveMoney(const uint32 amount)
		{
			g_runtime->session->GetRealm().CheatGiveMoney(amount);
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("GM.GiveMoney", { { "amount", amount } });
			}
		}

		std::string luaGmCreateMonster(const uint32 entry)
		{
			BotObjectManager& objects = g_runtime->session->GetRealm().GetObjectManager();

			// Snapshot known creature guids so the newly spawned one can be identified.
			std::set<uint64> knownGuids;
			objects.ForEachCreature([&knownGuids](const BotUnit& unit)
				{
					knownGuids.insert(unit.GetGuid());
				});

			g_runtime->session->GetRealm().CheatCreateMonster(entry);

			const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(15);
			while (std::chrono::steady_clock::now() < until)
			{
				uint64 spawnedGuid = 0;
				objects.ForEachCreature([&](const BotUnit& unit)
					{
						if (spawnedGuid == 0 && unit.GetEntry() == entry && knownGuids.find(unit.GetGuid()) == knownGuids.end())
						{
							spawnedGuid = unit.GetGuid();
						}
					});

				if (spawnedGuid != 0)
				{
					const std::string guidStr = guidToString(spawnedGuid);
					if (g_runtime->transcript)
					{
						g_runtime->transcript->Action("GM.CreateMonster", { { "entry", entry }, { "guid", guidStr } });
					}
					return guidStr;
				}

				pumpChecked();
			}

			abortScenario(e2e_exit_code::ScenarioFailed,
				"GM.CreateMonster: monster with entry " + std::to_string(entry) + " did not spawn within 15s");
		}

		void luaGmDestroyMonster(const std::string& guid)
		{
			g_runtime->session->GetRealm().CheatDestroyMonster(guidFromString(guid));
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("GM.DestroyMonster", { { "guid", guid } });
			}
		}

		void luaGmKillTarget()
		{
			g_runtime->session->GetRealm().CheatKill();
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("GM.KillTarget");
			}
		}

		void luaGmWorldPort(const uint32 mapId, const float x, const float y, const float z, const float facing)
		{
			E2eSession& session = *g_runtime->session;
			session.GetRealm().CheatWorldPort(mapId, Vector3(x, y, z), facing);

			// Same-map teleports arrive as a MoveTeleport packet the connector acks. Wait for
			// it and resync the context's movement cache so facing/position math stays correct.
			const Vector3 target(x, y, z);
			const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(15);
			while (std::chrono::steady_clock::now() < until)
			{
				const Vector3& position = session.GetRealm().GetMovementInfo().position;
				if ((position - target).GetSquaredLength() < 1.0f)
				{
					session.GetContext().UpdateMovementInfo(session.GetRealm().GetMovementInfo());
					break;
				}
				pumpChecked();
			}

			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("GM.Worldport", { { "map", mapId }, { "x", x }, { "y", y }, { "z", z } });
			}
		}

		void luaGmSetSpeed(const float speed)
		{
			g_runtime->session->GetRealm().CheatSpeed(speed);
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("GM.SetSpeed", { { "speed", speed } });
			}
		}

		void registerScenarioApi(lua_State* state)
		{
			luabind::scope apiScope = (
				// Control
				luabind::def_lambda("Log", &luaLog),
				luabind::def_lambda("SleepImpl", &luaSleep),
				luabind::def_lambda("AssertImpl", &luaAssert),
				luabind::def_lambda("Fail", &luaFail),
				luabind::def_lambda("WaitUntilImpl", &luaWaitUntil),

				// Queries
				luabind::def_lambda("Me", &luaMe),
				luabind::def_lambda("UnitExists", &luaUnitExists),
				luabind::def_lambda("GetHealth", &luaGetHealth),
				luabind::def_lambda("GetMaxHealth", &luaGetMaxHealth),
				luabind::def_lambda("GetLevel", &luaGetLevel),
				luabind::def_lambda("GetPower", &luaGetPower),
				luabind::def_lambda("GetMaxPower", &luaGetMaxPower),
				luabind::def_lambda("IsAlive", &luaIsAlive),
				luabind::def_lambda("GetName", &luaGetName),
				luabind::def_lambda("GetPosX", &luaGetPosX),
				luabind::def_lambda("GetPosY", &luaGetPosY),
				luabind::def_lambda("GetPosZ", &luaGetPosZ),
				luabind::def_lambda("GetDistance", &luaGetDistance),
				luabind::def_lambda("HasAura", &luaHasAura),
				luabind::def_lambda("HasSpell", &luaHasSpell),
				luabind::def_lambda("GetItemCount", &luaGetItemCount),
				luabind::def_lambda("GetMoney", &luaGetMoney),
				luabind::def_lambda("LastCastResult", &luaLastCastResult),
				luabind::def_lambda("FindUnitByEntryImpl", &luaFindUnitByEntry),
				luabind::def_lambda("FindUnitByNameImpl", &luaFindUnitByName),

				// Actions
				luabind::def_lambda("TargetUnit", &luaTargetUnit),
				luabind::def_lambda("FaceUnit", &luaFaceUnit),
				luabind::def_lambda("CastSpellImpl", &luaCastSpell),
				luabind::def_lambda("CancelCast", &luaCancelCast),
				luabind::def_lambda("StartAttack", &luaStartAttack),
				luabind::def_lambda("StopAttack", &luaStopAttack),
				luabind::def_lambda("MoveToImpl", &luaMoveTo),
				luabind::def_lambda("SendChat", &luaSendChat),

				// GM commands (grouped into the GM table by the prelude)
				luabind::def_lambda("GM_AddItem", &luaGmAddItem),
				luabind::def_lambda("GM_LearnSpell", &luaGmLearnSpell),
				luabind::def_lambda("GM_LevelUp", &luaGmLevelUp),
				luabind::def_lambda("GM_GiveMoney", &luaGmGiveMoney),
				luabind::def_lambda("GM_CreateMonster", &luaGmCreateMonster),
				luabind::def_lambda("GM_DestroyMonster", &luaGmDestroyMonster),
				luabind::def_lambda("GM_KillTarget", &luaGmKillTarget),
				luabind::def_lambda("GM_WorldPort", &luaGmWorldPort),
				luabind::def_lambda("GM_SetSpeed", &luaGmSetSpeed)
			);

			luabind::module(state)[std::move(apiScope)];
		}

		/// Lua-side sugar: optional arguments, the GM table, and nil instead of "" for
		/// find functions. Runs before the scenario script.
		const char* const s_scenarioPrelude = R"LUA(
			function Sleep(ms) SleepImpl(ms or 0) end
			function Assert(cond, msg) AssertImpl(not not cond, msg or "assertion failed") end
			function WaitUntil(fn, timeoutMs, desc) return WaitUntilImpl(fn, timeoutMs or 10000, desc or "condition") end
			function CastSpell(spellId, target) return CastSpellImpl(spellId, target or "") end
			function MoveTo(x, y, z, timeoutMs) return MoveToImpl(x, y, z, timeoutMs or 30000) end

			function FindUnitByEntry(entry)
				local guid = FindUnitByEntryImpl(entry)
				if guid == "" then return nil end
				return guid
			end

			function FindUnitByName(name)
				local guid = FindUnitByNameImpl(name)
				if guid == "" then return nil end
				return guid
			end

			GM = {
				AddItem = GM_AddItem,
				LearnSpell = GM_LearnSpell,
				LevelUp = GM_LevelUp,
				GiveMoney = GM_GiveMoney,
				CreateMonster = GM_CreateMonster,
				DestroyMonster = GM_DestroyMonster,
				KillTarget = GM_KillTarget,
				Worldport = GM_WorldPort,
				SetSpeed = GM_SetSpeed,
			}
		)LUA";

		int luaTraceback(lua_State* state)
		{
			const char* message = lua_tostring(state, 1);
			luaL_traceback(state, state, message, 1);
			return 1;
		}
	}

	e2e_exit_code::Type ScenarioEngine::Run(
		E2eSession& session,
		const std::string& scriptPath,
		const uint32 timeoutSeconds,
		const std::string& transcriptPath)
	{
		const std::string scenarioName = std::filesystem::path(scriptPath).stem().string();
		ScenarioTranscript transcript(transcriptPath, scenarioName);

		ScenarioRuntime runtime;
		runtime.session = &session;
		runtime.transcript = &transcript;
		runtime.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
		g_runtime = &runtime;

		lua_State* state = luaL_newstate();
		luaL_openlibs(state);
		luabind::open(state);
		registerScenarioApi(state);

		e2e_exit_code::Type result = e2e_exit_code::Success;

		if (luaL_dostring(state, s_scenarioPrelude) != LUA_OK)
		{
			ELOG("Scenario prelude failed: " << lua_tostring(state, -1));
			lua_close(state);
			g_runtime = nullptr;
			return e2e_exit_code::SetupFailed;
		}

		ILOG("Running scenario " << scenarioName << " (" << scriptPath << ")");

		lua_pushcfunction(state, luaTraceback);
		const int tracebackIndex = lua_gettop(state);

		if (luaL_loadfile(state, scriptPath.c_str()) != LUA_OK)
		{
			ELOG("Failed to load scenario script: " << lua_tostring(state, -1));
			transcript.ScenarioEnd(e2e_exit_code::SetupFailed, "load_failed");
			lua_close(state);
			g_runtime = nullptr;
			return e2e_exit_code::SetupFailed;
		}

		if (lua_pcall(state, 0, 0, tracebackIndex) != LUA_OK)
		{
			const char* errorMessage = lua_tostring(state, -1);
			result = runtime.aborted ? runtime.abortCode : e2e_exit_code::ScenarioFailed;
			ELOG("Scenario " << scenarioName << " FAILED (exit " << static_cast<int>(result) << "): "
				<< (errorMessage ? errorMessage : "unknown error"));
			transcript.Event("error", { { "message", errorMessage ? errorMessage : "unknown error" } });
		}
		else
		{
			ILOG("Scenario " << scenarioName << " PASSED");
		}

		const char* outcome = (result == e2e_exit_code::Success) ? "passed"
			: (result == e2e_exit_code::Timeout) ? "timeout"
			: (result == e2e_exit_code::Disconnected) ? "disconnected"
			: "failed";
		transcript.ScenarioEnd(result, outcome);

		lua_close(state);
		g_runtime = nullptr;
		return result;
	}
}
