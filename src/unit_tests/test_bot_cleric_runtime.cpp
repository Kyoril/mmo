// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"

#include "mmo_bot/bot_actions/companion_follow_action.h"
#include "mmo_bot/bot_cleric_capabilities.h"
#include "mmo_bot/bot_context.h"
#include "mmo_bot/bot_nav_service.h"
#include "mmo_bot/bot_realm_connector.h"

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

#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>
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

		BotUnit MakePlayer(
			const uint64 guid,
			const std::string& name,
			const Vector3& position,
			const uint32 health,
			const uint32 maxHealth,
			const uint32 mana,
			const uint32 maxMana,
			const bool inCombat,
			const uint64 targetGuid)
		{
			BotUnit unit(guid, ObjectTypeId::Player);
			unit.SetName(name);
			unit.SetMovementInfo(MakeMovementInfo(position));
			unit.SetHealth(health);
			unit.SetMaxHealth(maxHealth);
			unit.SetPower(power_type::Mana, mana, maxMana);
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
			for (int i = 0; i < maps.entry_size(); ++i)
			{
				if (maps.entry(i).id() != 0)
				{
					return maps.entry(i).id();
				}
			}
			FAIL("runtime test fixture could not find a non-zero map id");
			return 0;
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

		struct ClericRuntimeFixture final
		{
			static constexpr uint64 kSelfGuid = 0x110u;
			static constexpr uint64 kLeaderGuid = 0x220u;
			static constexpr uint64 kEnemyGuid = 0x330u;

			asio::io_service io;
			std::shared_ptr<BotRealmConnector> connector;
			BotConfig config;
			std::shared_ptr<BotNavService> navService;
			BotContext context;
			BotClericCapabilities capabilities;
			uint32 mapId { 0 };

			ClericRuntimeFixture()
				: connector(std::make_shared<BotRealmConnector>(io))
				, navService(std::make_shared<BotNavService>())
				, context(connector, config, navService)
			{
				config.characterClass = 5;
				connector->PrimeWorldSessionForTesting(kSelfGuid);
				REQUIRE(navService->IsReady());
				capabilities = ResolveClericCapabilities(*navService->GetProject());
				REQUIRE(capabilities.resolved);
				REQUIRE(capabilities.emergencyHeal.has_value());
				REQUIRE(capabilities.efficientHeal.has_value());
				REQUIRE(capabilities.damageFiller.has_value());
				mapId = ResolveRuntimeMapId(*navService);
				context.SetCurrentMapId(mapId);
				context.UpdateMovementInfo(MakeMovementInfo(Vector3::Zero));
			}

			void SeedGroup(const std::vector<std::pair<std::string, uint64>>& members, const uint64 leaderGuid)
			{
				REQUIRE(HandleRealmPacket(*connector, BuildGroupListPacket(members, leaderGuid)) == PacketParseResult::Pass);
			}

			void SeedKnownSpells(const std::vector<uint32>& spellIds)
			{
				REQUIRE(HandleRealmPacket(*connector, BuildInitialSpellsPacket(spellIds)) == PacketParseResult::Pass);
			}

			void SeedCombatScene(
				const uint32 selfHealth,
				const uint32 selfMana,
				const uint32 leaderHealth,
				const bool includeLeaderUnit,
				const bool selfInCombat = true,
				const bool leaderInCombat = true,
				const uint64 leaderTargetGuid = kEnemyGuid)
			{
				BotObjectManager& objectManager = connector->GetObjectManager();
				objectManager.Clear();
				objectManager.SetSelfGuid(kSelfGuid);
				objectManager.AddOrUpdateUnit(MakePlayer(kSelfGuid, "cleric", Vector3::Zero, selfHealth, 500, selfMana, 100, selfInCombat, 0));
				if (includeLeaderUnit)
				{
					objectManager.AddOrUpdateUnit(MakePlayer(kLeaderGuid, "leader", Vector3(12.0f, 0.0f, 0.0f), leaderHealth, 500, 100, 100, leaderInCombat, leaderTargetGuid));
				}
				objectManager.AddOrUpdateUnit(MakeCreature(kEnemyGuid, Vector3(20.0f, 0.0f, 0.0f), true, kLeaderGuid));
				SeedGroup({ { "cleric", kSelfGuid }, { "leader", kLeaderGuid } }, kLeaderGuid);
			}

			void SendSpellFailure(const uint32 spellId, const SpellCastResult result)
			{
				REQUIRE(HandleRealmPacket(*connector, BuildSpellFailurePacket(kSelfGuid, spellId, result)) == PacketParseResult::Pass);
			}
		};
	}

	TEST_CASE("cleric runtime casts a real emergency heal inside the companion runtime", "[bot-cleric][runtime]")
	{
		ClericRuntimeFixture fixture;
		fixture.SeedCombatScene(475, 100, 90, true);
		fixture.SeedKnownSpells({ fixture.capabilities.emergencyHeal->spellId, fixture.capabilities.efficientHeal->spellId, fixture.capabilities.damageFiller->spellId });

		CompanionFollowAction action;
		LogCapture logs;

		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		INFO(logs.Dump());
		CHECK(fixture.context.GetLastCastState().status == BotUnit::CastState::Status::Pending);
		CHECK(fixture.context.GetLastCastState().spellId == fixture.capabilities.emergencyHeal->spellId);
		CHECK(fixture.context.GetLastCastState().unitTargetGuid == ClericRuntimeFixture::kLeaderGuid);
		CHECK(logs.Contains("cleric action=cast_spell reason=ally_emergency_heal"));
		CHECK(logs.Contains("spell_id=" + std::to_string(fixture.capabilities.emergencyHeal->spellId)));
		CHECK(logs.Contains("target_guid=" + std::to_string(ClericRuntimeFixture::kLeaderGuid)));
	}

	TEST_CASE("cleric runtime keeps healing when map id is unresolved", "[bot-cleric][runtime]")
	{
		ClericRuntimeFixture fixture;
		fixture.SeedCombatScene(475, 100, 90, true);
		fixture.SeedKnownSpells({ fixture.capabilities.emergencyHeal->spellId, fixture.capabilities.efficientHeal->spellId, fixture.capabilities.damageFiller->spellId });
		fixture.context.SetCurrentMapId(0);

		CompanionFollowAction action;
		LogCapture logs;

		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		INFO(logs.Dump());
		CHECK(fixture.context.GetLastCastState().status == BotUnit::CastState::Status::Pending);
		CHECK(fixture.context.GetLastCastState().spellId == fixture.capabilities.emergencyHeal->spellId);
		CHECK(logs.Contains("cleric action=cast_spell reason=ally_emergency_heal"));
		CHECK_FALSE(logs.Contains("follow_reason=map_unresolved"));
		CHECK(logs.Contains("movement decision=repath reason=map_inferred"));
	}

	TEST_CASE("cleric runtime blocks conservatively on invalid self prerequisites", "[bot-cleric][runtime]")
	{
		ClericRuntimeFixture fixture;
		fixture.SeedCombatScene(475, 100, 90, true);
		fixture.SeedKnownSpells({ fixture.capabilities.emergencyHeal->spellId, fixture.capabilities.efficientHeal->spellId, fixture.capabilities.damageFiller->spellId });

		MovementInfo invalidMovement = MakeMovementInfo(Vector3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f));
		fixture.context.UpdateMovementInfo(invalidMovement);

		CompanionFollowAction action;
		LogCapture logs;

		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		INFO(logs.Dump());
		CHECK(fixture.context.GetLastCastState().status != BotUnit::CastState::Status::Pending);
		CHECK(logs.Contains("cleric failure=follow_runtime_blocked"));
		CHECK(logs.Contains("follow_reason=self_anchor_invalid"));
	}

	TEST_CASE("cleric runtime remembers missing injured allies and holds conservatively", "[bot-cleric][runtime]")
	{
		ClericRuntimeFixture fixture;

		CompanionFollowAction action;
		LogCapture logs;

		fixture.SeedCombatScene(475, 100, 400, true);
		fixture.SeedKnownSpells({ fixture.capabilities.emergencyHeal->spellId, fixture.capabilities.efficientHeal->spellId, fixture.capabilities.damageFiller->spellId });
		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		const bool observedAnyClericSignal = logs.Contains("cleric failure=conservative_hold")
			|| logs.Contains("cleric action=")
			|| logs.Contains("cleric failure=");
		CHECK(observedAnyClericSignal);

		fixture.SeedCombatScene(475, 100, 400, false);
		fixture.SeedKnownSpells({ fixture.capabilities.emergencyHeal->spellId, fixture.capabilities.efficientHeal->spellId, fixture.capabilities.damageFiller->spellId });
		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		INFO(logs.Dump());
		CHECK(logs.Contains("cleric failure=injured_ally_out_of_awareness"));
		CHECK(logs.Contains("target_guid=" + std::to_string(ClericRuntimeFixture::kLeaderGuid)));
	}

	TEST_CASE("cleric runtime surfaces server cast failures and backs off retries", "[bot-cleric][runtime]")
	{
		ClericRuntimeFixture fixture;
		fixture.SeedCombatScene(475, 100, 90, true);
		fixture.SeedKnownSpells({ fixture.capabilities.emergencyHeal->spellId, fixture.capabilities.efficientHeal->spellId, fixture.capabilities.damageFiller->spellId });

		CompanionFollowAction action;
		LogCapture logs;

		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		CHECK(logs.Contains("cleric action=cast_spell reason=ally_emergency_heal"));

		BotUnit* self = fixture.connector->GetObjectManager().GetSelfMutable();
		REQUIRE(self != nullptr);
		self->SetLastCastFailed(
			fixture.capabilities.emergencyHeal->spellId,
			spell_cast_result::FailedMoving,
			fixture.context.GetServerTime());

		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		INFO(logs.Dump());
		CHECK(fixture.context.GetLastCastState().status == BotUnit::CastState::Status::Failed);
		CHECK(logs.Contains("cleric failure=cast_failed"));
		CHECK(logs.Contains("cleric failure=cast_failure_backoff"));
		CHECK(logs.Contains("failure_code=" + std::to_string(static_cast<uint32>(spell_cast_result::FailedMoving))));
		CHECK(logs.CountContaining("cleric action=cast_spell reason=ally_emergency_heal") == 1);
		CHECK(logs.CountContaining("cleric failure=cast_failure_backoff") == 1);
	}
}
