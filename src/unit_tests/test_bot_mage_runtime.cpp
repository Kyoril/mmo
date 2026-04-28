// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"

#include "mmo_bot/bot_actions/companion_follow_action.h"
#include "mmo_bot/bot_context.h"
#include "mmo_bot/bot_mage_capabilities.h"
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
#include <memory>
#include <string>
#include <unordered_set>
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

		struct MageRuntimeFixture final
		{
			static constexpr uint64 kSelfGuid = 0x410u;
			static constexpr uint64 kLeaderGuid = 0x420u;
			static constexpr uint64 kEnemyGuid = 0x430u;
			static constexpr uint64 kCloseEnemyGuid = 0x431u;

			asio::io_service io;
			std::shared_ptr<BotRealmConnector> connector;
			BotConfig config;
			std::shared_ptr<BotNavService> navService;
			BotContext context;
			BotMageCapabilities capabilities;
			uint32 mapId { 0 };

			MageRuntimeFixture()
				: connector(std::make_shared<BotRealmConnector>(io))
				, navService(std::make_shared<BotNavService>())
				, context(connector, config, navService)
			{
				config.characterClass = 8;
				connector->PrimeWorldSessionForTesting(kSelfGuid);
				REQUIRE(navService->IsReady());
				capabilities = ResolveMageCapabilities(*navService->GetProject());
				REQUIRE(capabilities.primaryNuke.has_value());
				mapId = ResolveRuntimeMapId(*navService);
				context.SetCurrentMapId(mapId);
				context.UpdateMovementInfo(MakeMovementInfo(Vector3::Zero));
			}

			[[nodiscard]] std::vector<uint32> KnownSpellIdsForRuntime() const
			{
				std::vector<uint32> spellIds;
				std::unordered_set<uint32> seen;
				auto append = [&](const std::optional<BotMageResolvedSpell>& spell)
				{
					if (spell.has_value() && seen.insert(spell->spellId).second)
					{
						spellIds.push_back(spell->spellId);
					}
				};
				append(capabilities.primaryNuke);
				append(capabilities.instantFallback);
				append(capabilities.control);
				append(capabilities.selfBuffEscape);
				append(capabilities.emergencySpacing);
				return spellIds;
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
				const float anchorTargetDistance,
				const bool addCloseThreat = false,
				const bool closeThreatTargetsSelf = true,
				const uint64 selfTargetGuid = 0)
			{
				BotObjectManager& objectManager = connector->GetObjectManager();
				objectManager.Clear();
				objectManager.SetSelfGuid(kSelfGuid);
				objectManager.AddOrUpdateUnit(MakePlayer(kSelfGuid, "mage", Vector3::Zero, selfHealth, 500, selfMana, 100, true, selfTargetGuid));
				objectManager.AddOrUpdateUnit(MakePlayer(kLeaderGuid, "leader", Vector3(8.0f, 0.0f, 0.0f), 500, 500, 100, 100, true, kEnemyGuid));
				objectManager.AddOrUpdateUnit(MakeCreature(kEnemyGuid, Vector3(anchorTargetDistance, 0.0f, 0.0f), true, kLeaderGuid));
				if (addCloseThreat)
				{
					objectManager.AddOrUpdateUnit(MakeCreature(kCloseEnemyGuid, Vector3(4.0f, 0.0f, 0.0f), true, closeThreatTargetsSelf ? kSelfGuid : kLeaderGuid));
				}
				SeedGroup({ { "mage", kSelfGuid }, { "leader", kLeaderGuid } }, kLeaderGuid);
			}
		};
	}

	TEST_CASE("mage runtime casts a safe primary nuke inside the companion runtime", "[bot-mage][runtime]")
	{
		MageRuntimeFixture fixture;
		fixture.SeedCombatScene(500, 100, 24.0f);
		fixture.SeedKnownSpells(fixture.KnownSpellIdsForRuntime());

		CompanionFollowAction action;
		LogCapture logs;

		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		INFO(logs.Dump());
		CHECK(fixture.context.GetLastCastState().status == BotUnit::CastState::Status::Pending);
		CHECK(fixture.context.GetLastCastState().spellId == fixture.capabilities.primaryNuke->spellId);
		CHECK(fixture.context.GetLastCastState().unitTargetGuid == MageRuntimeFixture::kEnemyGuid);
		CHECK(logs.Contains("mage action=cast_spell reason=primary_nuke"));
		CHECK(logs.Contains("spell_id=" + std::to_string(fixture.capabilities.primaryNuke->spellId)));
		CHECK(logs.Contains("target_guid=" + std::to_string(MageRuntimeFixture::kEnemyGuid)));
		CHECK(logs.Contains("target_source=anchor_target"));
	}

	TEST_CASE("mage runtime surfaces authored close-range recovery or an explicit hold reason", "[bot-mage][runtime]")
	{
		MageRuntimeFixture fixture;
		fixture.SeedCombatScene(500, 100, 24.0f, true, true);
		fixture.SeedKnownSpells(fixture.KnownSpellIdsForRuntime());

		CompanionFollowAction action;
		LogCapture logs;

		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		INFO(logs.Dump());
		if (fixture.capabilities.emergencySpacing.has_value())
		{
			CHECK(fixture.context.GetLastCastState().status == BotUnit::CastState::Status::Pending);
			CHECK(fixture.context.GetLastCastState().spellId == fixture.capabilities.emergencySpacing->spellId);
			CHECK(logs.Contains("mage action=emergency_spacing reason=emergency_spacing"));
		}
		else if (fixture.capabilities.control.has_value())
		{
			CHECK(fixture.context.GetLastCastState().status == BotUnit::CastState::Status::Pending);
			CHECK(fixture.context.GetLastCastState().spellId == fixture.capabilities.control->spellId);
			CHECK(logs.Contains("mage action=emergency_spacing reason=emergency_control"));
		}
		else
		{
			CHECK(fixture.context.GetLastCastState().status != BotUnit::CastState::Status::Pending);
			CHECK((logs.Contains("mage failure=emergency_spacing_missing") || logs.Contains("mage failure=too_close_no_recovery")));
		}
		CHECK(logs.Contains("nearby_hostiles="));
	}

	TEST_CASE("mage runtime logs conservative close-range gating instead of speculative casts", "[bot-mage][runtime]")
	{
		MageRuntimeFixture fixture;
		fixture.SeedCombatScene(500, 0, 24.0f, true, true);
		fixture.SeedKnownSpells(fixture.KnownSpellIdsForRuntime());

		CompanionFollowAction action;
		LogCapture logs;

		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		INFO(logs.Dump());
		CHECK(fixture.context.GetLastCastState().status != BotUnit::CastState::Status::Pending);
		if (fixture.capabilities.emergencySpacing.has_value())
		{
			CHECK(logs.Contains("mage failure=emergency_spacing_oom"));
		}
		else if (fixture.capabilities.control.has_value())
		{
			CHECK(logs.Contains("mage failure=emergency_control_oom"));
		}
		else
		{
			CHECK((logs.Contains("mage failure=emergency_spacing_missing") || logs.Contains("mage failure=too_close_no_recovery")));
		}
		CHECK(logs.Contains("mana=0"));
	}

	TEST_CASE("mage runtime surfaces cast failures and explicit retry backoff", "[bot-mage][runtime]")
	{
		MageRuntimeFixture fixture;
		fixture.SeedCombatScene(500, 100, 24.0f);
		fixture.SeedKnownSpells(fixture.KnownSpellIdsForRuntime());

		CompanionFollowAction action;
		LogCapture logs;

		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		BotUnit* self = fixture.connector->GetObjectManager().GetSelfMutable();
		REQUIRE(self != nullptr);
		self->SetLastCastFailed(
			fixture.capabilities.primaryNuke->spellId,
			spell_cast_result::FailedMoving,
			fixture.context.GetServerTime());

		REQUIRE(action.Execute(fixture.context) == ActionResult::InProgress);
		INFO(logs.Dump());
		CHECK(fixture.context.GetLastCastState().status == BotUnit::CastState::Status::Failed);
		CHECK(logs.Contains("mage failure=cast_failed"));
		CHECK(logs.Contains("failure_code=" + std::to_string(static_cast<uint32>(spell_cast_result::FailedMoving))));
		CHECK(logs.Contains("mage failure=cast_failure_backoff"));
		CHECK(logs.CountContaining("mage action=cast_spell reason=primary_nuke") == 1);
	}
}
