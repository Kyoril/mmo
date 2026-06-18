// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "binary_io/memory_source.h"
#include "binary_io/string_sink.h"
#include "binary_io/writer.h"
#include "game/auto_attack.h"
#include "game/group.h"
#include "game_protocol/game_incoming_packet.h"
#include "game_protocol/game_outgoing_packet.h"
#include "mmo_bot/bot_action.h"
#include "mmo_bot/bot_context.h"
#include "mmo_bot/bot_actions/accept_party_invitation_action.h"
#include "mmo_bot/bot_realm_connector.h"

#include "asio/io_service.hpp"

#include <zlib.h>

#define private public
#include "mmo_bot/bot_profiles/party_follow_profile.h"
#undef private

namespace mmo
{
	namespace
	{
		class InterruptibleProbeAction final : public BotAction
		{
		public:
			explicit InterruptibleProbeAction(bool& aborted)
				: m_aborted(aborted)
			{
			}

			std::string GetDescription() const override
			{
				return "interruptible probe";
			}

			ActionResult Execute(BotContext& context) override
			{
				return ActionResult::InProgress;
			}

			void OnAbort(BotContext& context) override
			{
				m_aborted = true;
			}

			bool IsInterruptible() const override
			{
				return true;
			}

		private:
			bool& m_aborted;
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

		PacketParseResult HandleRealmPacket(BotRealmConnector& connector, Buffer packetBuffer)
		{
			io::MemorySource source(packetBuffer);
			game::IncomingPacket packet;
			REQUIRE(game::IncomingPacket::Start(packet, source) == ReceiveState::Complete);
			return connector.HandleIncomingPacket(packet);
		}

		Buffer BuildGroupListPacket(const uint64 leaderGuid)
		{
			return BuildRealmPacket(game::realm_client_packet::GroupList, [leaderGuid](game::OutgoingPacket& packet)
			{
				packet
					<< io::write<uint8>(1)
					<< io::write<uint8>(0)
					<< io::write<uint8>(1)
					<< io::write_dynamic_range<uint8>(std::string("Leader"))
					<< io::write<uint64>(leaderGuid)
					<< io::write<uint8>(group_member_status::Online)
					<< io::write<uint8>(0)
					<< io::write<uint8>(0)
					<< io::write<uint64>(leaderGuid)
					<< io::write<uint8>(0)
					<< io::write<uint64>(0)
					<< io::write<uint8>(2);
			});
		}

		Buffer BuildPartyMemberStatsPacket(const uint64 guid, const Vector3& position)
		{
			return BuildRealmPacket(game::realm_client_packet::PartyMemberStats, [guid, position](game::OutgoingPacket& packet)
			{
				packet
					<< io::write_packed_guid(guid)
					<< io::write<uint32>(group_update_flags::Full)
					<< io::write<uint16>(group_member_status::Online)
					<< io::write<uint16>(100)
					<< io::write<uint16>(100)
					<< io::write<uint8>(power_type::Mana)
					<< io::write<uint16>(50)
					<< io::write<uint16>(50)
					<< io::write<uint16>(10)
					<< io::write<uint16>(0)
					<< io::write<float>(position.x)
					<< io::write<float>(position.y)
					<< io::write<float>(position.z);
			});
		}

		Buffer BuildCompressedEmptyObjectUpdatePacket()
		{
			Buffer updateBody;
			io::StringSink updateSink{ updateBody };
			io::Writer updateWriter{ updateSink };
			updateWriter << io::write<uint16>(0);
			updateSink.Flush();

			uLongf compressedSize = compressBound(static_cast<uLong>(updateBody.size()));
			std::vector<Bytef> compressed(compressedSize);
			REQUIRE(compress2(
				compressed.data(),
				&compressedSize,
				reinterpret_cast<const Bytef*>(updateBody.data()),
				static_cast<uLong>(updateBody.size()),
				Z_BEST_SPEED) == Z_OK);

			return BuildRealmPacket(game::realm_client_packet::CompressedUpdateObject, [&](game::OutgoingPacket& packet)
			{
				packet << io::write<uint32>(static_cast<uint32>(updateBody.size()));
				packet.Sink().Write(reinterpret_cast<const char*>(compressed.data()), compressedSize);
			});
		}
	}

	TEST_CASE("party follow queues an urgent accept action for party invites", "[bot-party-follow][invite]")
	{
		BotConfig config;
		config.characterClass = 1;

		auto realm = std::shared_ptr<BotRealmConnector>{};
		auto nav = std::shared_ptr<BotNavService>{};
		BotContext context(realm, config, nav);
		context.SetWorldReady(true);

		PartyFollowProfile profile;
		profile.OnActivate(context);

		bool aborted = false;
		profile.m_currentAction = std::make_shared<InterruptibleProbeAction>(aborted);
		profile.m_actionQueue.clear();

		const bool shouldAccept = profile.OnPartyInvitation(context, "Leader");
		REQUIRE(shouldAccept);
		CHECK(aborted);
		REQUIRE(profile.m_currentAction == nullptr);
		REQUIRE(profile.m_actionQueue.size() == 1);
		CHECK(profile.m_actionQueue.front()->GetDescription() == "Accept party invitation");
	}

	TEST_CASE("party follow stores leader position from party member stats", "[bot-party-follow][follow]")
	{
		constexpr uint64 kSelfGuid = 10;
		constexpr uint64 kLeaderGuid = 20;
		const Vector3 leaderPosition(8.0f, 2.0f, 3.0f);

		asio::io_service io;
		auto connector = std::make_shared<BotRealmConnector>(io);
		connector->PrimeWorldSessionForTesting(kSelfGuid);
		REQUIRE(HandleRealmPacket(*connector, BuildGroupListPacket(kLeaderGuid)) == PacketParseResult::Pass);
		REQUIRE(HandleRealmPacket(*connector, BuildPartyMemberStatsPacket(kLeaderGuid, leaderPosition)) == PacketParseResult::Pass);

		const BotPartyMember* leader = connector->GetPartyMemberByGuid(kLeaderGuid);
		REQUIRE(leader != nullptr);
		CHECK(leader->hasPosition);
		CHECK(leader->position.x == Approx(leaderPosition.x));
		CHECK(leader->position.y == Approx(leaderPosition.y));
		CHECK(leader->position.z == Approx(leaderPosition.z));
		CHECK(leader->hasHealth);
		CHECK(leader->health == 100);
		CHECK((leader->status & group_member_status::Online) != 0);
	}

	TEST_CASE("party follow bot accepts compressed object update packets", "[bot-party-follow][follow]")
	{
		asio::io_service io;
		BotRealmConnector connector(io);
		connector.PrimeWorldSessionForTesting(10);

		CHECK(HandleRealmPacket(connector, BuildCompressedEmptyObjectUpdatePacket()) == PacketParseResult::Pass);
	}
}
