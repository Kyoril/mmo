// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"

#include "mmo_bot/bot_context.h"
#include "mmo_bot/bot_realm_connector.h"

#include "binary_io/memory_source.h"
#include "binary_io/string_sink.h"
#include "game/field_map.h"
#include "game/object_type_id.h"
#include "game/spell.h"
#include "game_protocol/game_incoming_packet.h"
#include "game_protocol/game_outgoing_packet.h"

#include "asio/io_service.hpp"

namespace mmo
{
	namespace
	{
		struct ParsedPacket final
		{
			Buffer buffer;
			io::MemorySource source;
			game::IncomingPacket packet;

			explicit ParsedPacket(Buffer packetBuffer)
				: buffer(std::move(packetBuffer))
				, source(buffer)
			{
				REQUIRE(game::IncomingPacket::Start(packet, source) == ReceiveState::Complete);
			}
		};

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

		Buffer BuildPlayerUpdatePacket(
			const uint64 guid,
			const bool creation,
			const std::function<void(FieldMap<uint32>&)>& mutateFields)
		{
			return BuildRealmPacket(game::realm_client_packet::UpdateObject, [&](game::OutgoingPacket& packet)
			{
				packet << io::write<uint16>(1);
				packet << io::write<uint8>(static_cast<uint8>(ObjectTypeId::Player));
				packet << io::write<uint8>(creation ? 1 : 0);
				if (!creation)
				{
					packet << io::write_packed_guid(guid);
				}

				packet << io::write<uint32>(0u);

				FieldMap<uint32> fields;
				fields.Initialize(object_fields::PlayerFieldCount);
				fields.SetFieldValue<uint64>(object_fields::Guid, guid);
				fields.SetFieldValue<uint32>(object_fields::Level, 10);
				fields.SetFieldValue<uint32>(object_fields::Health, 123);
				fields.SetFieldValue<uint32>(object_fields::MaxHealth, 456);
				fields.SetFieldValue<uint32>(object_fields::PowerType, power_type::Rage);
				fields.SetFieldValue<uint32>(object_fields::Rage, 35);
				fields.SetFieldValue<uint32>(object_fields::MaxRage, 100);
				mutateFields(fields);
				if (creation)
				{
					fields.SerializeComplete(packet);
				}
				else
				{
					fields.SerializeChanges(packet);
				}

				packet
					<< io::write<float>(2.5f)
					<< io::write<float>(7.0f)
					<< io::write<float>(4.5f)
					<< io::write<float>(4.7f)
					<< io::write<float>(2.5f)
					<< io::write<float>(7.0f)
					<< io::write<float>(4.5f)
					<< io::write<float>(3.14f);
			});
		}
	}

	TEST_CASE("warrior runtime mirrors self power snapshots through bot context", "[bot-warrior][state]")
	{
		asio::io_service io;
		auto connector = std::make_shared<BotRealmConnector>(io);
		connector->PrimeWorldSessionForTesting(0x100);

		BotConfig config;
		BotContext context(connector, config);

		CHECK(context.GetSelf() == nullptr);
		CHECK(context.GetSelfPowerType() == power_type::Invalid_);
		CHECK(context.GetSelfPower() == 0);
		CHECK(context.GetSelfMaxPower() == 0);

		ParsedPacket createPacket{ BuildPlayerUpdatePacket(0x100, true, [](FieldMap<uint32>&) {}) };
		CHECK(connector->HandleIncomingPacket(createPacket.packet) == PacketParseResult::Pass);

		REQUIRE(context.GetSelf() != nullptr);
		CHECK(context.GetSelfPowerType() == power_type::Rage);
		CHECK(context.GetSelfPower() == 35);
		CHECK(context.GetSelfMaxPower() == 100);
		CHECK(context.GetSelfPowerPercent() == Approx(0.35f));

		ParsedPacket manaTransitionPacket{ BuildPlayerUpdatePacket(0x100, false, [](FieldMap<uint32>& fields)
		{
			fields.SetFieldValue<uint32>(object_fields::PowerType, power_type::Mana);
			fields.MarkAsChanged(object_fields::Mana);
			fields.SetFieldValue<uint32>(object_fields::Mana, 0);
			fields.SetFieldValue<uint32>(object_fields::MaxMana, 120);
		}) };
		CHECK(connector->HandleIncomingPacket(manaTransitionPacket.packet) == PacketParseResult::Pass);
		CHECK(context.GetSelfPowerType() == power_type::Mana);
		CHECK(context.GetSelfPower() == 0);
		CHECK(context.GetSelfMaxPower() == 120);
		CHECK(context.GetSelfPowerPercent() == Approx(0.0f));
	}

	TEST_CASE("warrior runtime mirrors spellbook and preserves aura state on malformed updates", "[bot-warrior][state]")
	{
		asio::io_service io;
		auto connector = std::make_shared<BotRealmConnector>(io);
		connector->PrimeWorldSessionForTesting(0x200);
		BotConfig config;
		BotContext context(connector, config);

		ParsedPacket createPacket{ BuildPlayerUpdatePacket(0x200, true, [](FieldMap<uint32>&) {}) };
		REQUIRE(connector->HandleIncomingPacket(createPacket.packet) == PacketParseResult::Pass);

		ParsedPacket initialSpellsPacket{ BuildRealmPacket(game::realm_client_packet::InitialSpells, [](game::OutgoingPacket& packet)
		{
			packet << io::write<uint16>(4);
			packet << io::write<uint32>(6673);
			packet << io::write<uint32>(355);
			packet << io::write<uint32>(6673);
			packet << io::write<uint32>(0);
		}) };
		REQUIRE(connector->HandleIncomingPacket(initialSpellsPacket.packet) == PacketParseResult::Pass);
		const std::vector<uint32> initialKnown = context.GetKnownSpellIds();
		REQUIRE(initialKnown.size() == 2);
		CHECK(initialKnown[0] == 355);
		CHECK(initialKnown[1] == 6673);

		ParsedPacket learnedPacket{ BuildRealmPacket(game::realm_client_packet::LearnedSpell, [](game::OutgoingPacket& packet)
		{
			packet << io::write<uint32>(2458);
		}) };
		REQUIRE(connector->HandleIncomingPacket(learnedPacket.packet) == PacketParseResult::Pass);
		CHECK(context.HasKnownSpell(2458));

		ParsedPacket duplicateLearnedPacket{ BuildRealmPacket(game::realm_client_packet::LearnedSpell, [](game::OutgoingPacket& packet)
		{
			packet << io::write<uint32>(2458);
		}) };
		REQUIRE(connector->HandleIncomingPacket(duplicateLearnedPacket.packet) == PacketParseResult::Pass);
		CHECK(context.GetKnownSpellIds().size() == 3);

		ParsedPacket unknownCooldownPacket{ BuildRealmPacket(game::realm_client_packet::SpellCooldown, [](game::OutgoingPacket& packet)
		{
			packet << io::write<uint32>(999999u) << io::write<uint32>(1500u);
		}) };
		REQUIRE(connector->HandleIncomingPacket(unknownCooldownPacket.packet) == PacketParseResult::Pass);
		CHECK_FALSE(context.IsSpellOnCooldown(999999u));
		CHECK(context.GetLastSpellStateIssue() == "spell_cooldown_unknown_spell");

		ParsedPacket auraPacket{ BuildRealmPacket(game::realm_client_packet::AuraUpdate, [](game::OutgoingPacket& packet)
		{
			packet << io::write<uint32>(1u);
			packet << io::write<uint32>(6673u);
			packet << io::write<uint32>(5000u);
			packet << io::write_packed_guid(0x200u);
			packet << io::write<uint8>(2u);
			packet << io::write<int32>(5);
			packet << io::write<int32>(10);
		}) };
		REQUIRE(connector->HandleIncomingPacket(auraPacket.packet) == PacketParseResult::Pass);
		REQUIRE(context.HasVisibleAura(6673));
		const std::vector<BotUnit::AuraState> visibleAuras = context.GetVisibleAuras();
		REQUIRE(visibleAuras.size() == 1);
		CHECK(visibleAuras[0].remainingMs == 5000u);
		REQUIRE(visibleAuras[0].basePoints.size() == 2);
		CHECK(visibleAuras[0].basePoints[0] == 5);
		CHECK(visibleAuras[0].basePoints[1] == 10);

		ParsedPacket malformedAuraPacket{ BuildRealmPacket(game::realm_client_packet::AuraUpdate, [](game::OutgoingPacket& packet)
		{
			packet << io::write<uint32>(1u);
			packet << io::write<uint32>(0u);
			packet << io::write<uint32>(1000u);
			packet << io::write_packed_guid(0x200u);
			packet << io::write<uint8>(1u);
			packet << io::write<int32>(7);
		}) };
		REQUIRE(connector->HandleIncomingPacket(malformedAuraPacket.packet) == PacketParseResult::Pass);
		CHECK(context.GetLastSpellStateIssue() == "aura_update_invalid_spell_id");
		CHECK(context.HasVisibleAura(6673));
		CHECK(context.GetVisibleAuras().size() == 1);

		ParsedPacket clearAuraPacket{ BuildRealmPacket(game::realm_client_packet::AuraUpdate, [](game::OutgoingPacket& packet)
		{
			packet << io::write<uint32>(0u);
		}) };
		REQUIRE(connector->HandleIncomingPacket(clearAuraPacket.packet) == PacketParseResult::Pass);
		CHECK_FALSE(context.HasVisibleAura(6673));
		CHECK(context.GetVisibleAuras().empty());

		ParsedPacket unlearnedPacket{ BuildRealmPacket(game::realm_client_packet::UnlearnedSpell, [](game::OutgoingPacket& packet)
		{
			packet << io::write<uint32>(2458u);
		}) };
		REQUIRE(connector->HandleIncomingPacket(unlearnedPacket.packet) == PacketParseResult::Pass);
		CHECK_FALSE(context.HasKnownSpell(2458u));
	}

	TEST_CASE("warrior runtime tracks cooldown and cast lifecycle while reusing the real cast packet format", "[bot-warrior][state]")
	{
		asio::io_service io;
		auto connector = std::make_shared<BotRealmConnector>(io);
		connector->PrimeWorldSessionForTesting(0x300);
		BotConfig config;
		BotContext context(connector, config);

		ParsedPacket createPacket{ BuildPlayerUpdatePacket(0x300, true, [](FieldMap<uint32>&) {}) };
		REQUIRE(connector->HandleIncomingPacket(createPacket.packet) == PacketParseResult::Pass);

		ParsedPacket initialSpellsPacket{ BuildRealmPacket(game::realm_client_packet::InitialSpells, [](game::OutgoingPacket& packet)
		{
			packet << io::write<uint16>(1u);
			packet << io::write<uint32>(6673u);
		}) };
		REQUIRE(connector->HandleIncomingPacket(initialSpellsPacket.packet) == PacketParseResult::Pass);

		SpellTargetMap targetMap;
		targetMap.SetTargetMap(spell_cast_target_flags::Unit);
		targetMap.SetUnitTarget(0x999u);

		REQUIRE(connector->SendCastSpell(6673u, targetMap, false));
		ParsedPacket castPacket{ connector->getSendBuffer() };
		CHECK(castPacket.packet.GetId() == game::client_realm_packet::CastSpell);
		uint32 serializedSpellId = 0;
		SpellTargetMap serializedTargetMap;
		REQUIRE(castPacket.packet >> io::read<uint32>(serializedSpellId) >> serializedTargetMap);
		CHECK(serializedSpellId == 6673u);
		CHECK(serializedTargetMap.GetTargetMap() == spell_cast_target_flags::Unit);
		CHECK(serializedTargetMap.GetUnitTarget() == 0x999u);
		connector->getSendBuffer().clear();

		CHECK_FALSE(context.CastSpell(999999u, targetMap));
		CHECK(context.GetLastCastState().status == BotUnit::CastState::Status::Failed);
		CHECK(context.GetLastCastState().failureReason == spell_cast_result::FailedNotKnown);
		CHECK(context.GetLastSpellStateIssue() == "cast_spell_unknown_spell");

		ParsedPacket spellStartPacket{ BuildRealmPacket(game::realm_client_packet::SpellStart, [&](game::OutgoingPacket& packet)
		{
			packet << io::write_packed_guid(0x300u);
			packet << io::write<uint32>(6673u);
			packet << io::write<GameTime>(1500u);
			packet << targetMap;
			packet << io::write<uint32>(1500u);
		}) };
		REQUIRE(connector->HandleIncomingPacket(spellStartPacket.packet) == PacketParseResult::Pass);
		CHECK(context.GetLastCastState().status == BotUnit::CastState::Status::Started);
		CHECK(context.GetLastCastState().spellId == 6673u);
		CHECK(context.GetLastCastState().unitTargetGuid == 0x999u);
		CHECK(context.IsSpellOnCooldown(6673u));
		CHECK(context.GetSpellCooldownRemaining(6673u) > 0u);

		ParsedPacket spellFailurePacket{ BuildRealmPacket(game::realm_client_packet::SpellFailure, [](game::OutgoingPacket& packet)
		{
			packet << io::write_packed_guid(0x300u);
			packet << io::write<uint32>(6673u);
			packet << io::write<GameTime>(1510u);
			packet << io::write<uint8>(spell_cast_result::FailedMoving);
		}) };
		REQUIRE(connector->HandleIncomingPacket(spellFailurePacket.packet) == PacketParseResult::Pass);
		CHECK(context.GetLastCastState().status == BotUnit::CastState::Status::Failed);
		CHECK(context.GetLastCastState().failureReason == spell_cast_result::FailedMoving);

		ParsedPacket cooldownClearPacket{ BuildRealmPacket(game::realm_client_packet::SpellCooldown, [](game::OutgoingPacket& packet)
		{
			packet << io::write<uint32>(6673u) << io::write<uint32>(0u);
		}) };
		REQUIRE(connector->HandleIncomingPacket(cooldownClearPacket.packet) == PacketParseResult::Pass);
		CHECK_FALSE(context.IsSpellOnCooldown(6673u));
	}
}
