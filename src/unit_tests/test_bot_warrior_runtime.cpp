// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"

#include "mmo_bot/bot_actions/companion_follow_action.h"
#include "mmo_bot/bot_context.h"
#include "mmo_bot/bot_nav_service.h"
#include "mmo_bot/bot_realm_connector.h"
#include "mmo_bot/bot_warrior_capabilities.h"

#include "binary_io/memory_source.h"
#include "binary_io/string_sink.h"
#include "game/movement_info.h"
#include "game/object_type_id.h"
#include "game/spell.h"
#include "game_protocol/game_incoming_packet.h"
#include "game_protocol/game_outgoing_packet.h"
#include "log/default_log.h"
#include "log/log_entry.h"

#include "asio/io_service.hpp"

#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace mmo
{
	namespace
	{
		Buffer BuildRealmPacket(const uint16 opCode, const std::function<void(game::OutgoingPacket&)>& writer)
		{
			Buffer buffer;
			io::StringSink sink{ buffer };
			game::OutgoingPacket packet{ sink };
			packet.Start(opCode);
			writer(packet);
			packet.Finish();
			sink.Flush();
			return buffer;
		}

		PacketParseResult HandleRealmPacket(BotRealmConnector& connector, Buffer packetBuffer)
		{
			io::MemorySource source(packetBuffer);
			game::IncomingPacket packet;
			REQUIRE(game::IncomingPacket::Start(packet, source) == ReceiveState::Complete);
			return connector.HandleIncomingPacket(packet);
		}

		Buffer BuildGroupListPacket(const std::vector<std::pair<std::string, uint64>>& members, const uint64 leaderGuid)
		{
			return BuildRealmPacket(game::realm_client_packet::GroupList, [&](game::OutgoingPacket& packet)
			{
				packet << io::write<uint8>(1u);
				packet << io::write<uint8>(0u);
				packet << io::write<uint8>(static_cast<uint8>(members.size()));
				for (const auto& [name, guid] : members)
				{
					packet << io::write_dynamic_range<uint8>(name);
					packet << io::write<uint64>(guid);
					packet << io::write<uint8>(0u);
					packet << io::write<uint8>(0u);
					packet << io::write<uint8>(0u);
				}
				packet << io::write<uint64>(leaderGuid);
			});
		}

		Buffer BuildInitialSpellsPacket(const std::vector<uint32>& spellIds)
		{
			return BuildRealmPacket(game::realm_client_packet::InitialSpells, [&](game::OutgoingPacket& packet)
			{
				packet << io::write<uint16>(static_cast<uint16>(spellIds.size()));
				for (const uint32 spellId : spellIds)
				{
					packet << io::write<uint32>(spellId);
				}
			});
		}

		Buffer BuildSpellFailurePacket(const uint64 casterGuid, const uint32 spellId, const SpellCastResult result)
		{
			return BuildRealmPacket(game::realm_client_packet::SpellFailure, [&](game::OutgoingPacket& packet)
			{
				packet << io::write_packed_guid(casterGuid);
				packet << io::write<uint32>(spellId);
				packet << io::write<GameTime>(2500u);
				packet << io::write<uint8>(result);
			});
		}

		MovementInfo MakeMovementInfo(const Vector3& position, const float facing = 0.0f)
		{
			MovementInfo info;
			info.position = position;
			info.facing = Radian(facing);
			info.timestamp = 1000;
			return info;
		}

		BotUnit MakePlayer(const uint64 guid, const std::string& name, const Vector3& position, const bool inCombat, const uint64 targetGuid)
		{
			BotUnit unit(guid, ObjectTypeId::Player);
			unit.SetName(name);
			unit.SetMovementInfo(MakeMovementInfo(position));
			unit.SetHealth(500);
			unit.SetMaxHealth(500);
			unit.SetPower(power_type::Rage, 35, 100);
			unit.SetUnitFlags(inCombat ? 0x00000001u : 0u);
			unit.SetTargetGuid(targetGuid);
			return unit;
		}

		BotUnit MakeCreature(const uint64 guid, const Vector3& position, const bool inCombat, const uint64 targetGuid)
		{
			BotUnit unit(guid, ObjectTypeId::Unit);
			unit.SetEntry(345);
			unit.SetMovementInfo(MakeMovementInfo(position));
			unit.SetHealth(350);
			unit.SetMaxHealth(350);
			unit.SetUnitFlags(inCombat ? 0x00000001u : 0u);
			unit.SetTargetGuid(targetGuid);
			return unit;
		}

		uint32 ResolveRuntimeMapId(const BotNavService& navService)
		{
			const proto::Project* project = navService.GetProject();
			REQUIRE(project != nullptr);
			const auto& maps = project->maps.getTemplates();
			REQUIRE(maps.entry_size() > 0);
			return maps.entry(0).id();
		}

		class LogCapture final
		{
		public:
			LogCapture()
			{
				m_connection = g_DefaultLog.signal().connect([this](const LogEntry& entry)
				{
					m_messages.push_back(entry.message);
				});
			}

			[[nodiscard]] bool Contains(const std::string& fragment) const
			{
				for (const std::string& message : m_messages)
				{
					if (message.find(fragment) != std::string::npos)
					{
						return true;
					}
				}
				return false;
			}

			[[nodiscard]] size_t CountContaining(const std::string& fragment) const
			{
				size_t count = 0;
				for (const std::string& message : m_messages)
				{
					if (message.find(fragment) != std::string::npos)
					{
						++count;
					}
				}
				return count;
			}

			[[nodiscard]] std::string Dump() const
			{
				std::string result;
				for (const std::string& message : m_messages)
				{
					if (!result.empty())
					{
						result += "\n";
					}
					result += message;
				}
				return result;
			}

		private:
			scoped_connection m_connection;
			std::vector<std::string> m_messages;
		};

		struct WarriorRuntimeFixture final
		{
			static constexpr uint64 kSelfGuid = 0x100u;
			static constexpr uint64 kLeaderGuid = 0x200u;
			static constexpr uint64 kEnemyGuid = 0x300u;

			asio::io_service io;
			std::shared_ptr<BotRealmConnector> connector;
			BotConfig config;
			std::shared_ptr<BotNavService> navService;
			BotContext context;
			BotWarriorCapabilities capabilities;
			uint32 mapId { 0 };

			WarriorRuntimeFixture()
				: connector(std::make_shared<BotRealmConnector>(io))
				, navService(std::make_shared<BotNavService>())
				, context(connector, config, navService)
			{
				config.characterClass = 1;
				connector->PrimeWorldSessionForTesting(kSelfGuid);
				REQUIRE(navService->IsReady());
				capabilities = ResolveWarriorCapabilities(*navService->GetProject());
				REQUIRE(capabilities.resolved);
				REQUIRE(capabilities.gapCloser.has_value());
				mapId = ResolveRuntimeMapId(*navService);
				context.SetCurrentMapId(mapId);
				context.UpdateMovementInfo(MakeMovementInfo(Vector3::Zero));
			}

			void SeedCombatScene(const float targetDistance, const bool selfInCombat = false, const bool leaderInCombat = true, const uint64 enemyTargetGuid = kLeaderGuid)
			{
				BotObjectManager& objectManager = connector->GetObjectManager();
				objectManager.Clear();
				objectManager.SetSelfGuid(kSelfGuid);
				objectManager.AddOrUpdateUnit(MakePlayer(kSelfGuid, "bot", Vector3::Zero, selfInCombat, 0));
				objectManager.AddOrUpdateUnit(MakePlayer(kLeaderGuid, "leader", Vector3(2.0f, 0.0f, 0.0f), leaderInCombat, kEnemyGuid));
				objectManager.AddOrUpdateUnit(MakeCreature(kEnemyGuid, Vector3(targetDistance, 0.0f, 0.0f), true, enemyTargetGuid));

				REQUIRE(HandleRealmPacket(*connector, BuildGroupListPacket({ { "bot", kSelfGuid }, { "leader", kLeaderGuid } }, kLeaderGuid)) == PacketParseResult::Pass);
			}

			void SeedKnownSpells(const std::initializer_list<uint32> spellIds)
			{
				REQUIRE(HandleRealmPacket(*connector, BuildInitialSpellsPacket(std::vector<uint32>(spellIds))) == PacketParseResult::Pass);
			}

			void SendSpellFailure(const uint32 spellId, const SpellCastResult result)
			{
				REQUIRE(HandleRealmPacket(*connector, BuildSpellFailurePacket(kSelfGuid, spellId, result)) == PacketParseResult::Pass);
			}

			void MoveEnemy(const Vector3& position)
			{
				BotUnit* enemy = connector->GetObjectManager().GetUnitMutable(kEnemyGuid);
				REQUIRE(enemy != nullptr);
				enemy->SetMovementInfo(MakeMovementInfo(position));
			}
		};
	}

	TEST_CASE("warrior runtime arms conservative auto attack once when spellbook state is incomplete", "[bot-warrior][runtime]")
	{
		WarriorRuntimeFixture fixture;
		fixture.SeedCombatScene(3.0f);

		CompanionFollowAction action;
		LogCapture logs;

		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		if (!logs.Contains("warrior "))
		{
			FAIL(logs.Dump());
		}
		INFO(logs.Dump());
		INFO("last_spell_issue=" << fixture.context.GetLastSpellStateIssue());
		CHECK(fixture.context.GetAutoAttackTarget() == WarriorRuntimeFixture::kEnemyGuid);
		CHECK_FALSE(fixture.context.IsAutoAttacking());
		CHECK(logs.Contains("warrior action=ensure_auto_attack reason=conservative_auto_attack"));
		CHECK(logs.Contains("companion mode=combat_anchor"));

		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		CHECK(logs.CountContaining("warrior action=ensure_auto_attack") == 1);
		CHECK(logs.Contains("warrior failure=auto_attack_pending"));
	}

	TEST_CASE("warrior runtime faces the hostile before starting conservative auto attack", "[bot-warrior][runtime]")
	{
		WarriorRuntimeFixture fixture;
		fixture.SeedCombatScene(3.0f, true, true, WarriorRuntimeFixture::kSelfGuid);
		fixture.MoveEnemy(Vector3(0.0f, 0.0f, -3.0f));
		fixture.context.UpdateMovementInfo(MakeMovementInfo(Vector3::Zero, 0.0f));

		CompanionFollowAction action;
		LogCapture logs;

		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		INFO(logs.Dump());
		CHECK(fixture.context.GetAutoAttackTarget() == WarriorRuntimeFixture::kEnemyGuid);
		CHECK(fixture.context.GetMovementInfo().facing.GetValueRadians() == Approx(fixture.context.GetAngleTo(Vector3(0.0f, 0.0f, -3.0f)).GetValueRadians()).margin(0.001f));
		CHECK(logs.Contains("warrior action=ensure_auto_attack"));
	}

	TEST_CASE("warrior runtime keeps contributing when map id is unresolved", "[bot-warrior][runtime]")
	{
		WarriorRuntimeFixture fixture;
		fixture.SeedCombatScene(12.0f, false, true, WarriorRuntimeFixture::kLeaderGuid);
		fixture.MoveEnemy(Vector3(0.0f, 0.0f, -12.0f));
		fixture.context.UpdateMovementInfo(MakeMovementInfo(Vector3::Zero, 0.0f));
		fixture.SeedKnownSpells({ fixture.capabilities.gapCloser->spellId });
		fixture.context.SetCurrentMapId(0);

		CompanionFollowAction action;
		LogCapture logs;

		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		INFO(logs.Dump());
		CHECK(fixture.context.GetLastCastState().status == BotUnit::CastState::Status::Pending);
		CHECK(fixture.context.GetLastCastState().spellId == fixture.capabilities.gapCloser->spellId);
		CHECK(fixture.context.GetMovementInfo().facing.GetValueRadians() == Approx(fixture.context.GetAngleTo(Vector3(0.0f, 0.0f, -12.0f)).GetValueRadians()).margin(0.001f));
		CHECK(logs.Contains("warrior action=cast_spell reason=charge_opener"));
		CHECK_FALSE(logs.Contains("follow_reason=map_unresolved"));
	}

	TEST_CASE("warrior runtime blocks conservatively on invalid self prerequisites", "[bot-warrior][runtime]")
	{
		WarriorRuntimeFixture fixture;
		fixture.SeedCombatScene(12.0f, false, true, WarriorRuntimeFixture::kLeaderGuid);
		fixture.SeedKnownSpells({ fixture.capabilities.gapCloser->spellId });

		MovementInfo invalidMovement = MakeMovementInfo(Vector3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f));
		fixture.context.UpdateMovementInfo(invalidMovement);

		CompanionFollowAction action;
		LogCapture logs;

		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		INFO(logs.Dump());
		CHECK(fixture.context.GetLastCastState().status != BotUnit::CastState::Status::Pending);
		CHECK(logs.Contains("warrior failure=follow_runtime_blocked"));
		CHECK(logs.Contains("follow_reason=self_anchor_invalid"));
	}

	TEST_CASE("warrior runtime surfaces cast failures and explicit retry backoff", "[bot-warrior][runtime]")
	{
		WarriorRuntimeFixture fixture;
		fixture.SeedCombatScene(12.0f, false, true, WarriorRuntimeFixture::kLeaderGuid);
		fixture.SeedKnownSpells({ fixture.capabilities.gapCloser->spellId });

		CompanionFollowAction action;
		LogCapture logs;

		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		CHECK(fixture.context.GetLastCastState().status == BotUnit::CastState::Status::Pending);
		CHECK(logs.Contains("warrior action=cast_spell reason=charge_opener"));

		BotUnit* self = fixture.connector->GetObjectManager().GetSelfMutable();
		REQUIRE(self != nullptr);
		self->SetLastCastFailed(
			fixture.capabilities.gapCloser->spellId,
			spell_cast_result::FailedMoving,
			fixture.context.GetServerTime());

		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		INFO(logs.Dump());
		CHECK(fixture.context.GetLastCastState().status == BotUnit::CastState::Status::Failed);
		CHECK(logs.Contains("warrior failure=cast_failed"));
		CHECK(logs.Contains("warrior failure=cast_failure_backoff"));
		CHECK(logs.Contains("failure_code=" + std::to_string(static_cast<uint32>(spell_cast_result::FailedMoving))));
		CHECK(logs.CountContaining("warrior action=cast_spell reason=charge_opener") == 1);
		CHECK(logs.CountContaining("warrior failure=cast_failure_backoff") == 1);
	}
}
