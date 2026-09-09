// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "scenario_engine.h"
#include "scenario_transcript.h"
#include "bot_core/secondary_login_session.h"

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
#include <map>
#include <memory>
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
			BotSession* session { nullptr };
			ScenarioTranscript* transcript { nullptr };
			std::chrono::steady_clock::time_point deadline;
			bot_exit_code::Type abortCode { bot_exit_code::ScenarioFailed };
			bool aborted { false };
			uint64 selectedTarget { 0 };

			/// Resolved auto-attack swings by our character, keyed by victim guid. A swing counts
			/// whether or not it dealt damage: the server resolves a dodge or a miss into the same
			/// AttackerStateUpdate as a hit, and swing cadence is what scenarios need to measure.
			/// Health is not a substitute -- a creature can heal between swings.
			std::map<uint64, uint32> meleeSwings;

			/// The last auto-attack swing error the server reported, and how many times it has
			/// told us swings started landing again. The server reports swing outcomes as
			/// transitions, so a scenario that provokes an error and then removes its cause reads
			/// the recovery count to tell "the error was cleared" from "the error is simply still
			/// being repeated by the client".
			AttackSwingEvent lastSwingError { attack_swing_event::Unknown };
			uint32 swingErrors { 0 };
			uint32 swingRecoveries { 0 };

			/// A second session on the same account, created by LoginElsewhere. Kept alive for the
			/// rest of the scenario so its connection is not torn down while the first session is
			/// still being observed.
			std::unique_ptr<SecondaryLoginSession> secondSession;
		};

		ScenarioRuntime* g_runtime = nullptr;

		/// Aborts the running scenario: records the exit code, then throws so luabind
		/// converts this into a Lua error that unwinds to the engine's pcall.
		[[noreturn]] void abortScenario(const bot_exit_code::Type code, const std::string& message)
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
			BotSession& session = *g_runtime->session;
			session.Pump();

			// Keep the second session's io service moving too, so its connection stays healthy for
			// as long as the scenario runs rather than going silent the moment it authenticated.
			if (g_runtime->secondSession)
			{
				g_runtime->secondSession->Pump();
			}

			if (session.IsStopRequested())
			{
				abortScenario(bot_exit_code::Disconnected, "connection to the server was lost");
			}
			if (std::chrono::steady_clock::now() >= g_runtime->deadline)
			{
				abortScenario(bot_exit_code::Timeout, "scenario watchdog timeout exceeded");
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

		/// Names an attack swing event for the transcript. The server volunteers the reason a
		/// swing did not connect, but a scenario only ever sees the consequence -- health that
		/// stops dropping -- so an unrecorded swing error reads as a broken swing timer.
		const char* attackSwingEventName(const AttackSwingEvent event)
		{
			switch (event)
			{
			case attack_swing_event::NotStanding: return "not_standing";
			case attack_swing_event::OutOfRange: return "out_of_range";
			case attack_swing_event::CantAttack: return "cant_attack";
			case attack_swing_event::WrongFacing: return "wrong_facing";
			case attack_swing_event::TargetDead: return "target_dead";
			case attack_swing_event::Success: return "success";
			default: return "unknown";
			}
		}

		const BotUnit* findUnit(const std::string& guidStr)
		{
			return g_runtime->session->GetContext().GetUnit(guidFromString(guidStr));
		}

		const BotUnit* self()
		{
			return g_runtime->session->GetContext().GetSelf();
		}

		bool isSelfGuid(const uint64 guid)
		{
			const BotUnit* selfUnit = self();
			return selfUnit && selfUnit->GetGuid() == guid;
		}

		/// Resolves a unit's position. The server never echoes our own movement back to
		/// us (the client is authoritative for its own position), so the self unit's
		/// object-manager position goes stale as soon as we move - use the context's
		/// simulated movement state for self instead.
		bool resolvePosition(const std::string& guidStr, Vector3& outPosition)
		{
			const uint64 guid = guidFromString(guidStr);
			if (isSelfGuid(guid))
			{
				outPosition = g_runtime->session->GetContext().GetPosition();
				return true;
			}

			const BotUnit* unit = g_runtime->session->GetContext().GetUnit(guid);
			if (!unit)
			{
				return false;
			}

			outPosition = unit->GetPosition();
			return true;
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

		void luaExpectDisconnect()
		{
			g_runtime->session->ExpectDisconnect();
		}

		bool luaIsDisconnected()
		{
			return g_runtime->session->IsDisconnected();
		}

		std::string luaLastKickReason()
		{
			const auto reason = g_runtime->session->GetKickReason();
			if (!reason)
			{
				return "none";
			}

			switch (*reason)
			{
			case auth::session_kick_reason::LoggedInElsewhere:
				return "logged_in_elsewhere";
			case auth::session_kick_reason::AccountBanned:
				return "banned";
			}

			return "unknown";
		}

		/// A spell visual has no gameplay side effect, so scenarios assert on the packet: this
		/// returns the visualization id of the last PlaySpellVisual the server sent, or 0.
		uint32 luaLastSpellVisualId()
		{
			return g_runtime->session->GetLastSpellVisualId();
		}

		/// Guid the last observed spell visual played on, formatted like every other guid query.
		std::string luaLastSpellVisualTarget()
		{
			return guidToString(g_runtime->session->GetLastSpellVisualTarget());
		}

		/// Which visualization event the last observed spell visual fired (4 = IMPACT), or 255.
		uint32 luaLastSpellVisualEvent()
		{
			return g_runtime->session->GetLastSpellVisualEvent();
		}

		void luaClearLastSpellVisual()
		{
			g_runtime->session->ClearLastSpellVisual();
		}

		/// Logs the session back in and returns to the world, over the same connector objects the
		/// previous session used. That reuse is the point: it is what the game client does, and it
		/// is where session state that was not cleared shows up.
		bool luaReconnect(const uint32 timeoutMs)
		{
			const uint32 timeoutSeconds = (timeoutMs + 999) / 1000;
			const bot_exit_code::Type result = g_runtime->session->Reconnect(timeoutSeconds);

			if (g_runtime->transcript)
			{
				g_runtime->transcript->Event("reconnect", { { "result",
					result == bot_exit_code::Success ? "in_world" : "failed" } });
			}

			// Reconnecting resets the session's stop flag expectations but a genuine setup failure
			// still has to end the run rather than leave the scenario querying a dead session.
			if (g_runtime->session->IsStopRequested())
			{
				abortScenario(bot_exit_code::Disconnected, "connection was lost while reconnecting");
			}

			return result == bot_exit_code::Success;
		}

		/// Opens a second login-server session on the same account and waits for it to authenticate.
		/// That is the point at which the server displaces older sessions, so nothing further (realm
		/// selection, character entry) is needed to provoke the kick.
		bool luaLoginElsewhere(const uint32 timeoutMs)
		{
			if (g_runtime->secondSession)
			{
				abortScenario(bot_exit_code::ScenarioFailed, "LoginElsewhere: a second session is already open");
			}

			const BotConfig& config = g_runtime->session->GetConfig();
			g_runtime->secondSession = std::make_unique<SecondaryLoginSession>(
				config.loginHost, config.loginPort, config.username, config.password);
			g_runtime->secondSession->Start();

			const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
			while (std::chrono::steady_clock::now() < until)
			{
				if (g_runtime->secondSession->IsAuthenticated())
				{
					if (g_runtime->transcript)
					{
						g_runtime->transcript->Event("login_elsewhere", { { "result", "authenticated" } });
					}
					return true;
				}

				if (g_runtime->secondSession->HasFailed())
				{
					if (g_runtime->transcript)
					{
						g_runtime->transcript->Event("login_elsewhere", { { "result", "rejected" } });
					}
					return false;
				}

				pumpChecked();
			}

			if (g_runtime->transcript)
			{
				g_runtime->transcript->Event("login_elsewhere", { { "result", "timeout" } });
			}
			return false;
		}

		void luaAssert(const bool condition, const std::string& message)
		{
			if (!condition)
			{
				abortScenario(bot_exit_code::ScenarioFailed, "Assert failed: " + message);
			}

			if (g_runtime->transcript)
			{
				g_runtime->transcript->AssertPassed(message);
			}
		}

		void luaFail(const std::string& message)
		{
			abortScenario(bot_exit_code::ScenarioFailed, "Fail: " + message);
		}

		bool luaWaitUntil(luabind::object predicate, const uint32 timeoutMs, const std::string& description)
		{
			if (luabind::type(predicate) != LUA_TFUNCTION)
			{
				abortScenario(bot_exit_code::ScenarioFailed, "WaitUntil: first argument must be a function");
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

		/// Counts auto-attack swings our character has resolved against `guid`, damaging or not.
		/// Cadence is the only honest measure of the swing timer: health can be dodged away, and a
		/// creature that goes untouched for long enough resets its encounter and heals back up.
		int32 luaMeleeSwingCount(const std::string& guid)
		{
			const auto it = g_runtime->meleeSwings.find(guidFromString(guid));
			return it == g_runtime->meleeSwings.end() ? 0 : static_cast<int32>(it->second);
		}

		/// Name of the last swing error the server reported ("out_of_range", ...), or "none".
		std::string luaLastSwingError()
		{
			return g_runtime->swingErrors == 0 ? std::string("none")
				: std::string(attackSwingEventName(g_runtime->lastSwingError));
		}

		int32 luaSwingErrorCount()
		{
			return static_cast<int32>(g_runtime->swingErrors);
		}

		int32 luaSwingRecoveryCount()
		{
			return static_cast<int32>(g_runtime->swingRecoveries);
		}

		/// Whether the server currently has us auto-attacking. This mirrors the real client: the
		/// state is driven purely by the broadcast AttackStart/AttackStop packets, never by the
		/// requests we send, so it answers "did the server acknowledge?" rather than "did we ask?".
		bool luaIsAutoAttacking()
		{
			return g_runtime->session->GetContext().IsAutoAttacking();
		}

		std::string luaGetName(const std::string& guid)
		{
			const BotUnit* unit = findUnit(guid);
			return unit ? unit->GetName() : std::string();
		}

		float luaGetPosX(const std::string& guid)
		{
			Vector3 position;
			return resolvePosition(guid, position) ? position.x : 0.0f;
		}

		float luaGetPosY(const std::string& guid)
		{
			Vector3 position;
			return resolvePosition(guid, position) ? position.y : 0.0f;
		}

		float luaGetPosZ(const std::string& guid)
		{
			Vector3 position;
			return resolvePosition(guid, position) ? position.z : 0.0f;
		}

		float luaGetDistance(const std::string& guidA, const std::string& guidB)
		{
			Vector3 positionA;
			Vector3 positionB;
			if (!resolvePosition(guidA, positionA) || !resolvePosition(guidB, positionB))
			{
				return -1.0f;
			}
			return (positionB - positionA).GetLength();
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

		int32 luaGetXp()
		{
			const BotUnit* selfUnit = self();
			return selfUnit ? static_cast<int32>(selfUnit->GetXp()) : -1;
		}

		int32 luaGetNextLevelXp()
		{
			const BotUnit* selfUnit = self();
			return selfUnit ? static_cast<int32>(selfUnit->GetNextLevelXp()) : -1;
		}

		int32 luaGetStandState(const std::string& guid)
		{
			const BotUnit* unit = findUnit(guid);
			return unit ? static_cast<int32>(unit->GetStandState()) : -1;
		}

		int32 luaGetMoodEmote(const std::string& guid)
		{
			const BotUnit* unit = findUnit(guid);
			return unit ? static_cast<int32>(unit->GetMoodEmote()) : -1;
		}

		int32 luaGetIdlePoseEmote(const std::string& guid)
		{
			const BotUnit* unit = findUnit(guid);
			return unit ? static_cast<int32>(unit->GetIdlePoseEmote()) : -1;
		}

		int32 luaGetSitPoseEmote(const std::string& guid)
		{
			const BotUnit* unit = findUnit(guid);
			return unit ? static_cast<int32>(unit->GetSitPoseEmote()) : -1;
		}

		int32 luaGetSleepPoseEmote(const std::string& guid)
		{
			const BotUnit* unit = findUnit(guid);
			return unit ? static_cast<int32>(unit->GetSleepPoseEmote()) : -1;
		}

		void luaDoEmote(const uint32 emoteId)
		{
			g_runtime->session->GetRealm().SendEmote(emoteId, g_runtime->selectedTarget);
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("DoEmote", { { "emote_id", emoteId } });
			}
		}

		void luaCyclePose()
		{
			g_runtime->session->GetRealm().SendCyclePose();
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("CyclePose");
			}
		}

		void luaGmLearnEmote(const uint32 emoteId)
		{
			g_runtime->session->GetRealm().CheatLearnEmote(emoteId);
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("GM.LearnEmote", { { "emote_id", emoteId } });
			}
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

		/// Counts creatures of a given entry the client currently knows about.
		///
		/// FindUnitByEntry only ever yields one guid, which makes "how many of these are up?"
		/// unaskable from a scenario - and that is exactly the question an add cap needs answered.
		/// Only units the client has been told about are counted, so a scenario has to be standing
		/// somewhere they are visible from.
		uint32 luaCountUnitsByEntry(const uint32 entry, const bool aliveOnly)
		{
			uint32 count = 0;
			g_runtime->session->GetContext().GetObjectManager().ForEachCreature([&count, entry, aliveOnly](const BotUnit& unit)
				{
					if (unit.GetEntry() != entry)
					{
						return;
					}

					if (aliveOnly && !unit.IsAlive())
					{
						return;
					}

					++count;
				});
			return count;
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

		void luaSendAreaTrigger(const uint32 areaTriggerId)
		{
			// The real client detects area-trigger overlap locally and notifies the server, which
			// validates the reported position before executing the linked trigger. The headless
			// e2e client has no overlap detection, so scenarios walk into the area and send the
			// notification explicitly — the server-side position validation still applies.
			g_runtime->session->GetRealm().sendSinglePacket([areaTriggerId](game::OutgoingPacket& packet)
				{
					packet.Start(game::client_realm_packet::AreaTriggerTriggered);
					packet << io::write<uint32>(areaTriggerId);
					packet.Finish();
				});
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("SendAreaTrigger", { { "area_trigger_id", areaTriggerId } });
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
			BotSession& session = *g_runtime->session;
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

				// Path following is advanced by BotSession::Update, which pumpChecked drives via
				// Pump. Doing it here as well would step the controller twice per frame.
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

		void luaGmClassLevelUp(const uint32 levels)
		{
			g_runtime->session->GetRealm().CheatClassLevelUp(static_cast<uint8>(levels));
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("GM.ClassLevelUp", { { "levels", levels } });
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

			abortScenario(bot_exit_code::ScenarioFailed,
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

		std::string luaGmCreateObject(const uint32 entry, const uint32 state)
		{
			BotObjectManager& objects = g_runtime->session->GetRealm().GetObjectManager();

			// Remember the previously known first match (if any) so the newly spawned object can
			// be told apart. Scenarios are expected to spawn at most one object per entry.
			const BotWorldObjectState* existing = objects.FindWorldObjectByEntry(entry);
			const uint64 existingGuid = existing ? existing->guid : 0;

			g_runtime->session->GetRealm().CheatCreateObject(entry, state);

			const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(15);
			while (std::chrono::steady_clock::now() < until)
			{
				const BotWorldObjectState* spawned = objects.FindWorldObjectByEntry(entry);
				if (spawned && spawned->guid != existingGuid)
				{
					const std::string guidStr = guidToString(spawned->guid);
					if (g_runtime->transcript)
					{
						g_runtime->transcript->Action("GM.CreateObject", { { "entry", entry }, { "state", state }, { "guid", guidStr } });
					}
					return guidStr;
				}

				pumpChecked();
			}

			abortScenario(bot_exit_code::ScenarioFailed,
				"GM.CreateObject: object with entry " + std::to_string(entry) + " did not spawn within 15s");
		}

		std::string luaFindObjectByEntry(const uint32 entry)
		{
			const BotWorldObjectState* object = g_runtime->session->GetRealm().GetObjectManager().FindWorldObjectByEntry(entry);
			if (!object)
			{
				return "";
			}

			return guidToString(object->guid);
		}

		uint32 luaGetObjectState(const std::string& guidStr)
		{
			const BotWorldObjectState* object = g_runtime->session->GetRealm().GetObjectManager().GetWorldObject(guidFromString(guidStr));
			if (!object)
			{
				abortScenario(bot_exit_code::ScenarioFailed, "GetObjectState: unknown object guid " + guidStr);
			}

			return object->state;
		}

		bool luaGmCheckLoS(const std::string& guidStr)
		{
			BotRealmConnector& realm = g_runtime->session->GetRealm();

			const uint32 previousCounter = realm.GetLosResultCounter();
			realm.CheatCheckLineOfSight(guidFromString(guidStr));

			const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (std::chrono::steady_clock::now() < until)
			{
				if (realm.GetLosResultCounter() != previousCounter)
				{
					const bool result = realm.GetLastLosResult();
					if (g_runtime->transcript)
					{
						g_runtime->transcript->Action("GM.CheckLoS", { { "guid", guidStr }, { "clear", result } });
					}
					return result;
				}

				pumpChecked();
			}

			abortScenario(bot_exit_code::ScenarioFailed, "GM.CheckLoS: no line of sight result received within 10s");
		}

		bool luaCastSpellOnObject(const uint32 spellId, const std::string& targetGuidStr)
		{
			const uint64 targetGuid = guidFromString(targetGuidStr);

			SpellTargetMap targetMap{};
			targetMap.SetTargetMap(spell_cast_target_flags::Object);
			targetMap.SetObjectTarget(targetGuid);

			const bool queued = g_runtime->session->GetContext().CastSpell(spellId, targetMap);
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("CastSpellOnObject", {
					{ "spell_id", spellId },
					{ "target", targetGuidStr },
					{ "queued", queued } });
			}
			return queued;
		}

		void luaGmKillTarget()
		{
			g_runtime->session->GetRealm().CheatKill();
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("GM.KillTarget");
			}
		}

		void luaGmSetInstanceVariable(const uint32 key, const int64 value)
		{
			g_runtime->session->GetRealm().CheatSetInstanceVariable(key, value);
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("GM.SetInstanceVariable(" + std::to_string(key) + ", " + std::to_string(value) + ")");
			}
		}

		void luaGmDamageTarget(const uint32 amount)
		{
			g_runtime->session->GetRealm().CheatDamage(amount);
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("GM.DamageTarget(" + std::to_string(amount) + ")");
			}
		}

		void luaGmGodmode(const bool enable)
		{
			g_runtime->session->GetRealm().CheatGodmode(enable);
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action(enable ? "GM.Godmode(true)" : "GM.Godmode(false)");
			}
		}

		void luaGmWorldPort(const uint32 mapId, const float x, const float y, const float z, const float facing)
		{
			BotSession& session = *g_runtime->session;
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

		void luaGmAcceptQuest(const uint32 questId)
		{
			g_runtime->session->GetRealm().CheatAcceptQuest(questId);
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("GM.AcceptQuest", { { "quest_id", questId } });
			}
		}

		void luaGmTurnInQuest(const uint32 questId, const uint32 rewardChoice)
		{
			g_runtime->session->GetRealm().CheatTurnInQuest(questId, static_cast<uint8>(rewardChoice));
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("GM.TurnInQuest", { { "quest_id", questId }, { "reward_choice", rewardChoice } });
			}
		}

		void luaGmClearInventory()
		{
			g_runtime->session->GetRealm().CheatClearInventory();
			if (g_runtime->transcript)
			{
				g_runtime->transcript->Action("GM.ClearInventory", {});
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
				luabind::def_lambda("ExpectDisconnect", &luaExpectDisconnect),
				luabind::def_lambda("IsDisconnected", &luaIsDisconnected),
				luabind::def_lambda("LastKickReason", &luaLastKickReason),
				luabind::def_lambda("LastSpellVisualId", &luaLastSpellVisualId),
				luabind::def_lambda("LastSpellVisualTarget", &luaLastSpellVisualTarget),
				luabind::def_lambda("LastSpellVisualEvent", &luaLastSpellVisualEvent),
				luabind::def_lambda("ClearLastSpellVisual", &luaClearLastSpellVisual),
				luabind::def_lambda("LoginElsewhereImpl", &luaLoginElsewhere),
				luabind::def_lambda("ReconnectImpl", &luaReconnect),

				// Queries
				luabind::def_lambda("Me", &luaMe),
				luabind::def_lambda("UnitExists", &luaUnitExists),
				luabind::def_lambda("GetHealth", &luaGetHealth),
				luabind::def_lambda("GetMaxHealth", &luaGetMaxHealth),
				luabind::def_lambda("GetLevel", &luaGetLevel),
				luabind::def_lambda("GetPower", &luaGetPower),
				luabind::def_lambda("GetMaxPower", &luaGetMaxPower),
				luabind::def_lambda("IsAlive", &luaIsAlive),
				luabind::def_lambda("MeleeSwingCount", &luaMeleeSwingCount),
				luabind::def_lambda("IsAutoAttacking", &luaIsAutoAttacking),
				luabind::def_lambda("GetName", &luaGetName),
				luabind::def_lambda("GetPosX", &luaGetPosX),
				luabind::def_lambda("GetPosY", &luaGetPosY),
				luabind::def_lambda("GetPosZ", &luaGetPosZ),
				luabind::def_lambda("GetDistance", &luaGetDistance),
				luabind::def_lambda("HasAura", &luaHasAura),
				luabind::def_lambda("HasSpell", &luaHasSpell),
				luabind::def_lambda("GetItemCount", &luaGetItemCount),
				luabind::def_lambda("GetMoney", &luaGetMoney),
				luabind::def_lambda("GetXp", &luaGetXp),
				luabind::def_lambda("GetNextLevelXp", &luaGetNextLevelXp),
				luabind::def_lambda("GetStandState", &luaGetStandState),
				luabind::def_lambda("GetMoodEmote", &luaGetMoodEmote),
				luabind::def_lambda("GetIdlePoseEmote", &luaGetIdlePoseEmote),
				luabind::def_lambda("GetSitPoseEmote", &luaGetSitPoseEmote),
				luabind::def_lambda("GetSleepPoseEmote", &luaGetSleepPoseEmote),
				luabind::def_lambda("LastCastResult", &luaLastCastResult),
				luabind::def_lambda("LastSwingError", &luaLastSwingError),
				luabind::def_lambda("SwingErrorCount", &luaSwingErrorCount),
				luabind::def_lambda("SwingRecoveryCount", &luaSwingRecoveryCount),
				luabind::def_lambda("FindUnitByEntryImpl", &luaFindUnitByEntry),
				luabind::def_lambda("FindUnitByNameImpl", &luaFindUnitByName),
				luabind::def_lambda("CountUnitsByEntryImpl", &luaCountUnitsByEntry),
				luabind::def_lambda("FindObjectByEntryImpl", &luaFindObjectByEntry),
				luabind::def_lambda("GetObjectState", &luaGetObjectState),

				// Actions
				luabind::def_lambda("TargetUnit", &luaTargetUnit),
				luabind::def_lambda("FaceUnit", &luaFaceUnit),
				luabind::def_lambda("CastSpellImpl", &luaCastSpell),
				luabind::def_lambda("CastSpellOnObject", &luaCastSpellOnObject),
				luabind::def_lambda("CancelCast", &luaCancelCast),
				luabind::def_lambda("StartAttack", &luaStartAttack),
				luabind::def_lambda("StopAttack", &luaStopAttack),
				luabind::def_lambda("MoveToImpl", &luaMoveTo),
				luabind::def_lambda("SendAreaTrigger", &luaSendAreaTrigger),
				luabind::def_lambda("SendChat", &luaSendChat),
				luabind::def_lambda("DoEmote", &luaDoEmote),
				luabind::def_lambda("CyclePose", &luaCyclePose),

				// GM commands (grouped into the GM table by the prelude)
				luabind::def_lambda("GM_AddItem", &luaGmAddItem),
				luabind::def_lambda("GM_LearnSpell", &luaGmLearnSpell),
				luabind::def_lambda("GM_LearnEmote", &luaGmLearnEmote),
				luabind::def_lambda("GM_LevelUp", &luaGmLevelUp),
				luabind::def_lambda("GM_ClassLevelUp", &luaGmClassLevelUp),
				luabind::def_lambda("GM_GiveMoney", &luaGmGiveMoney),
				luabind::def_lambda("GM_CreateMonster", &luaGmCreateMonster),
				luabind::def_lambda("GM_DestroyMonster", &luaGmDestroyMonster),
				luabind::def_lambda("GM_CreateObject", &luaGmCreateObject),
				luabind::def_lambda("GM_CheckLoS", &luaGmCheckLoS),
				luabind::def_lambda("GM_KillTarget", &luaGmKillTarget),
				luabind::def_lambda("GM_Godmode", &luaGmGodmode),
				luabind::def_lambda("GM_SetInstanceVariable", &luaGmSetInstanceVariable),
				luabind::def_lambda("GM_DamageTarget", &luaGmDamageTarget),
				luabind::def_lambda("GM_WorldPort", &luaGmWorldPort),
				luabind::def_lambda("GM_SetSpeed", &luaGmSetSpeed),
				luabind::def_lambda("GM_AcceptQuest", &luaGmAcceptQuest),
				luabind::def_lambda("GM_TurnInQuest", &luaGmTurnInQuest),
				luabind::def_lambda("GM_ClearInventory", &luaGmClearInventory)
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
			function LoginElsewhere(timeoutMs) return LoginElsewhereImpl(timeoutMs or 15000) end
			function Reconnect(timeoutMs) return ReconnectImpl(timeoutMs or 30000) end

			function FindUnitByEntry(entry)
				local guid = FindUnitByEntryImpl(entry)
				if guid == "" then return nil end
				return guid
			end

			function CountUnitsByEntry(entry, includeDead)
				return CountUnitsByEntryImpl(entry, not includeDead)
			end

			function FindUnitByName(name)
				local guid = FindUnitByNameImpl(name)
				if guid == "" then return nil end
				return guid
			end

			function FindObjectByEntry(entry)
				local guid = FindObjectByEntryImpl(entry)
				if guid == "" then return nil end
				return guid
			end

			GM = {
				AddItem = GM_AddItem,
				LearnSpell = GM_LearnSpell,
				LearnEmote = GM_LearnEmote,
				LevelUp = GM_LevelUp,
				ClassLevelUp = GM_ClassLevelUp,
				GiveMoney = GM_GiveMoney,
				CreateMonster = GM_CreateMonster,
				DestroyMonster = GM_DestroyMonster,
				CreateObject = function(entry, state) return GM_CreateObject(entry, state or 0) end,
				DestroyObject = GM_DestroyMonster,
				CheckLoS = GM_CheckLoS,
				KillTarget = GM_KillTarget,
				Godmode = GM_Godmode,
				SetInstanceVariable = GM_SetInstanceVariable,
				DamageTarget = GM_DamageTarget,
				Worldport = GM_WorldPort,
				SetSpeed = GM_SetSpeed,
				AcceptQuest = GM_AcceptQuest,
				TurnInQuest = function(questId, rewardChoice) GM_TurnInQuest(questId, rewardChoice or 0) end,
				ClearInventory = GM_ClearInventory,
			}
		)LUA";

		int luaTraceback(lua_State* state)
		{
			const char* message = lua_tostring(state, 1);
			luaL_traceback(state, state, message, 1);
			return 1;
		}
	}

	bot_exit_code::Type ScenarioEngine::Run(
		BotSession& session,
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

		// Combat feedback the server sends unprompted. Without this the only trace a failing swing
		// leaves in a transcript is health that stops dropping, which is indistinguishable from a
		// swing timer that never re-arms -- the two have very different causes.
		BotRealmConnector& realm = session.GetRealm();
		const scoped_connection swingErrorRecorder { realm.AttackSwingError.connect(
			[&transcript, &runtime](const AttackSwingEvent event)
			{
				runtime.lastSwingError = event;
				++runtime.swingErrors;
				transcript.Event("attack_swing_error", { { "reason", attackSwingEventName(event) } });
			}) };
		const scoped_connection swingRecoveredRecorder { realm.AttackSwingRecovered.connect(
			[&transcript, &runtime]()
			{
				++runtime.swingRecoveries;
				// The counterpart of the above: swings are landing again. A scenario that walks
				// out of range and back needs this to tell "the error stopped" from "the error was
				// never cleared and is simply still being repeated".
				transcript.Event("attack_swing_recovered", {});
			}) };
		const scoped_connection attackStartedRecorder { realm.AttackStarted.connect(
			[&transcript](const uint64 attacker, const uint64 victim)
			{
				transcript.Event("attack_started",
					{ { "attacker", guidToString(attacker) }, { "victim", guidToString(victim) } });
			}) };
		const scoped_connection attackStoppedRecorder { realm.AttackStopped.connect(
			[&transcript](const uint64 attacker)
			{
				transcript.Event("attack_stopped", { { "attacker", guidToString(attacker) } });
			}) };
		const scoped_connection attackHitRecorder { realm.AttackHit.connect(
			[&transcript, &runtime, &realm](const uint64 attacker, const uint64 victim,
				const uint32 damage, const uint32 hitInfo, const uint32 victimState)
			{
				// Read live rather than captured: Reconnect() re-selects the character mid-scenario.
				if (attacker == realm.GetSelectedGuid())
				{
					++runtime.meleeSwings[victim];
				}

				transcript.Event("attack_hit",
					{ { "attacker", guidToString(attacker) }, { "victim", guidToString(victim) },
					  { "damage", damage }, { "hit_info", hitInfo }, { "victim_state", victimState } });
			}) };

		lua_State* state = luaL_newstate();
		luaL_openlibs(state);
		luabind::open(state);
		registerScenarioApi(state);

		bot_exit_code::Type result = bot_exit_code::Success;

		if (luaL_dostring(state, s_scenarioPrelude) != LUA_OK)
		{
			ELOG("Scenario prelude failed: " << lua_tostring(state, -1));
			lua_close(state);
			g_runtime = nullptr;
			return bot_exit_code::SetupFailed;
		}

		ILOG("Running scenario " << scenarioName << " (" << scriptPath << ")");

		lua_pushcfunction(state, luaTraceback);
		const int tracebackIndex = lua_gettop(state);

		if (luaL_loadfile(state, scriptPath.c_str()) != LUA_OK)
		{
			ELOG("Failed to load scenario script: " << lua_tostring(state, -1));
			transcript.ScenarioEnd(bot_exit_code::SetupFailed, "load_failed");
			lua_close(state);
			g_runtime = nullptr;
			return bot_exit_code::SetupFailed;
		}

		if (lua_pcall(state, 0, 0, tracebackIndex) != LUA_OK)
		{
			const char* errorMessage = lua_tostring(state, -1);
			result = runtime.aborted ? runtime.abortCode : bot_exit_code::ScenarioFailed;
			ELOG("Scenario " << scenarioName << " FAILED (exit " << static_cast<int>(result) << "): "
				<< (errorMessage ? errorMessage : "unknown error"));
			transcript.Event("error", { { "message", errorMessage ? errorMessage : "unknown error" } });
		}
		else
		{
			ILOG("Scenario " << scenarioName << " PASSED");
		}

		const char* outcome = (result == bot_exit_code::Success) ? "passed"
			: (result == bot_exit_code::Timeout) ? "timeout"
			: (result == bot_exit_code::Disconnected) ? "disconnected"
			: "failed";
		transcript.ScenarioEnd(result, outcome);

		lua_close(state);
		g_runtime = nullptr;
		return result;
	}
}
