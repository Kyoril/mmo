// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"

#include "mmo_bot/bot_realm_connector.h"

#include "binary_io/memory_source.h"
#include "binary_io/string_sink.h"
#include "game/movement_info.h"
#include "game/object_type_id.h"
#include "game_protocol/game_incoming_packet.h"
#include "game_protocol/game_outgoing_packet.h"

#include "asio/io_service.hpp"

#include <functional>
#include <memory>
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

		Buffer BuildAuraUpdatePacket(
			const uint64 unitGuid,
			const std::vector<BotUnit::AuraState>& auras)
		{
			return BuildRealmPacket(game::realm_client_packet::AuraUpdate, [&](game::OutgoingPacket& packet)
			{
				packet << io::write_packed_guid(unitGuid);
				packet << io::write<uint32>(static_cast<uint32>(auras.size()));
				for (const BotUnit::AuraState& aura : auras)
				{
					packet << io::write<uint32>(aura.spellId);
					packet << io::write<uint32>(aura.remainingMs);
					packet << io::write_packed_guid(aura.casterGuid);
					packet << io::write<uint8>(static_cast<uint8>(aura.basePoints.size()));
					packet << io::write_range(aura.basePoints);
				}
			});
		}

		BotUnit MakePlayer(const uint64 guid)
		{
			BotUnit unit(guid, ObjectTypeId::Player);
			MovementInfo movement;
			movement.position = Vector3::Zero;
			movement.facing = Radian(0.0f);
			movement.timestamp = 1000;
			unit.SetMovementInfo(movement);
			unit.SetHealth(100);
			unit.SetMaxHealth(100);
			return unit;
		}
	}

	TEST_CASE("realm aura update consumes packed guid before aura count", "[bot-realm][aura]")
	{
		constexpr uint64 kSelfGuid = 0x100u;
		constexpr uint64 kCasterGuid = 0x200u;

		asio::io_service io;
		BotRealmConnector connector(io);
		connector.PrimeWorldSessionForTesting(kSelfGuid);
		connector.GetObjectManager().AddOrUpdateUnit(MakePlayer(kSelfGuid));

		BotUnit::AuraState aura;
		aura.spellId = 133u;
		aura.remainingMs = 12000u;
		aura.casterGuid = kCasterGuid;
		aura.basePoints = { 25, 10 };

		REQUIRE(HandleRealmPacket(connector, BuildAuraUpdatePacket(kSelfGuid, { aura })) == PacketParseResult::Pass);

		const BotUnit* self = connector.GetObjectManager().GetSelf();
		REQUIRE(self != nullptr);
		REQUIRE(self->GetVisibleAuras().size() == 1);
		CHECK(self->GetVisibleAuras()[0].spellId == 133u);
		CHECK(self->GetVisibleAuras()[0].remainingMs == 12000u);
		CHECK(self->GetVisibleAuras()[0].casterGuid == kCasterGuid);
		REQUIRE(self->GetVisibleAuras()[0].basePoints.size() == 2);
		CHECK(self->GetVisibleAuras()[0].basePoints[0] == 25);
		CHECK(self->GetVisibleAuras()[0].basePoints[1] == 10);
		CHECK(connector.GetLastSpellStateIssue().empty());
	}

	TEST_CASE("realm aura update ignores unknown non-self units without corrupting self spell state", "[bot-realm][aura]")
	{
		constexpr uint64 kSelfGuid = 0x100u;
		constexpr uint64 kUnknownGuid = 0x999u;

		asio::io_service io;
		BotRealmConnector connector(io);
		connector.PrimeWorldSessionForTesting(kSelfGuid);
		connector.GetObjectManager().AddOrUpdateUnit(MakePlayer(kSelfGuid));

		BotUnit::AuraState aura;
		aura.spellId = 777u;
		aura.remainingMs = 5000u;
		aura.casterGuid = kSelfGuid;
		aura.basePoints = { 1 };

		REQUIRE(HandleRealmPacket(connector, BuildAuraUpdatePacket(kUnknownGuid, { aura })) == PacketParseResult::Pass);

		const BotUnit* self = connector.GetObjectManager().GetSelf();
		REQUIRE(self != nullptr);
		CHECK(self->GetVisibleAuras().empty());
		CHECK(connector.GetLastSpellStateIssue().empty());
	}
}
