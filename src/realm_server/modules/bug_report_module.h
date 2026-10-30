// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "realm_module.h"
#include "bug_report_rate_limiter.h"

#include "game/bug_report.h"

namespace mmo
{
	/// In-game bug reports, realm side: checks availability and the per-character rate limit, then
	/// forwards the report to the player's world node with the account and character identity
	/// attached. The world node files it and answers through World::OnBugReportResult.
	class BugReportModule final : public RealmModule
	{
	public:
		[[nodiscard]] const char* GetName() const override { return "BugReport"; }

		[[nodiscard]] std::optional<game::Subsystem> GetSubsystem() const override { return game::subsystem::BugReport; }

		void RegisterPacketHandlers(Player& player, Player::PacketHandlerHandleContainer& handlers) override;

	private:
		PacketParseResult OnBugReport(Player& player, game::IncomingPacket& packet);

	private:
		BugReportRateLimiter m_rateLimiter;
	};

	/// Sends the answer to a bug report to the player's client.
	void SendBugReportResult(Player& player, game::BugReportResult result);
}
