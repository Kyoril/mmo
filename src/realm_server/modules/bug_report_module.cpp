// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bug_report_module.h"

#include "world.h"

#include "base/clock.h"
#include "log/default_log_levels.h"

namespace mmo
{
	void BugReportModule::RegisterPacketHandlers(Player& player, Player::PacketHandlerHandleContainer& handlers)
	{
		handlers += player.RegisterAutoPacketHandler(game::client_realm_packet::BugReport, [this, &player](game::IncomingPacket& packet)
		{
			return OnBugReport(player, packet);
		});
	}

	PacketParseResult BugReportModule::OnBugReport(Player& player, game::IncomingPacket& packet)
	{
		// An unreadable or oversized report costs the report, never the session.
		game::BugReportPayload payload;
		if (!(packet >> payload))
		{
			const bool tooLarge = packet.GetSize() > game::bug_report_limits::MaxCompressedBytes;
			WLOG("Player " << player.GetCharacterName() << " sent an unreadable bug report (" << packet.GetSize() << " bytes)");
			SendBugReportResult(player, tooLarge ? game::bug_report_result::TooLarge : game::bug_report_result::Invalid);
			return PacketParseResult::Pass;
		}

		const auto world = player.GetWorld();
		if (!world || !IsAvailableTo(player))
		{
			SendBugReportResult(player, game::bug_report_result::Disabled);
			return PacketParseResult::Pass;
		}

		if (!m_rateLimiter.TryConsume(player.GetCharacterGuid(), GetAsyncTimeMs()))
		{
			SendBugReportResult(player, game::bug_report_result::RateLimited);
			return PacketParseResult::Pass;
		}

		world->SendBugReport(player.GetCharacterGuid(), player.GetAccountId(), player.GetCharacterName(), player.GetManager().GetRealmName(), payload);
		return PacketParseResult::Pass;
	}

	void SendBugReportResult(Player& player, const game::BugReportResult result)
	{
		player.SendPacket([result](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::BugReportResult);
			packet << io::write<uint8>(result);
			packet.Finish();
		});
	}
}
