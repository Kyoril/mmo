// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bug_report_service.h"

#include "bug_report_transport.h"
#include "bug_snapshot_builder.h"
#include "player.h"

#include "base/clock.h"
#include "game/bug_report_compression.h"
#include "log/default_log_levels.h"
#include "version.h"

#include <chrono>

namespace mmo
{
	using json = nlohmann::json;

	namespace
	{
		constexpr uint32 MaxCommentCharacters = 1000;
		constexpr auto HealthCheckInterval = std::chrono::seconds(5);

		bool IsBlank(const std::string& text)
		{
			return text.find_first_not_of(" \t\r\n") == std::string::npos;
		}
	}

	BugReportService::BugReportService(asio::io_service& ioService, WorldSubsystemState& subsystems, const proto::Project& project, Config config)
		: m_ioService(ioService)
		, m_subsystems(subsystems)
		, m_project(project)
		, m_config(std::move(config))
		, m_healthTimer(ioService)
	{
	}

	BugReportService::~BugReportService()
	{
		Stop();
	}

	void BugReportService::Start()
	{
		if (!m_config.enabled)
		{
			ILOG("Bug reports are disabled in the world server config");
			m_subsystems.SetConfigEnabled(game::subsystem::BugReport, false);
			return;
		}

		auto transport = MakeBugReportTransport(m_config.apiUrl, m_config.apiKey);
		if (!transport)
		{
			ELOG("Bug reports disabled: invalid bug API url '" << m_config.apiUrl << "' (expected https://, http:// or file://)");
			m_subsystems.SetConfigEnabled(game::subsystem::BugReport, false);
			return;
		}

		BugApiUrl parsed;
		ParseBugApiUrl(m_config.apiUrl, parsed);
		if (parsed.scheme != "file" && m_config.apiKey.empty())
		{
			ELOG("Bug reports disabled: no bug API key configured");
			m_subsystems.SetConfigEnabled(game::subsystem::BugReport, false);
			return;
		}

		m_uploader = std::make_unique<BugReportUploader>(
			std::move(transport),
			[this](std::function<void()> work) { m_ioService.post(std::move(work)); },
			[this](const BugReportUploader::Outcome& outcome) { OnUploadOutcome(outcome); });
		m_uploader->Start();

		m_stopped = false;
		m_subsystems.SetConfigEnabled(game::subsystem::BugReport, true);
		ScheduleHealthCheck();

		ILOG("Bug reports enabled, uploading to " << m_config.apiUrl);
	}

	void BugReportService::Stop()
	{
		m_stopped = true;

		asio::error_code error;
		m_healthTimer.cancel(error);

		if (m_uploader)
		{
			m_uploader->Stop();
		}
	}

	game::BugReportResult BugReportService::HandleReport(Player& player, const BugReporterIdentity& reporter, const game::BugReportPayload& payload)
	{
		if (!m_uploader || !m_subsystems.IsAvailable(game::subsystem::BugReport))
		{
			return game::bug_report_result::Disabled;
		}

		if (payload.subjectType >= game::bug_report_subject::Count_ ||
			IsBlank(payload.comment) ||
			CountUtf8Characters(payload.comment) > MaxCommentCharacters)
		{
			WLOG("Rejected bug report from " << reporter.characterName << ": invalid subject or comment");
			return game::bug_report_result::Invalid;
		}

		std::string clientText;
		if (!game::InflateBugReportData(payload.clientData, payload.uncompressedSize, game::bug_report_limits::MaxUncompressedBytes, clientText))
		{
			WLOG("Rejected bug report from " << reporter.characterName << ": client data does not inflate to its announced size");
			return game::bug_report_result::Invalid;
		}

		json client;
		std::string logTail;
		if (!SanitizeBugReportClientJson(clientText, client, logTail))
		{
			WLOG("Rejected bug report from " << reporter.characterName << ": client data is not a JSON object");
			return game::bug_report_result::Invalid;
		}

		json server = BuildBugReportServerSnapshot(player, payload, m_project);
		const json document = BuildBugReportDocument(reporter, payload, std::move(client), std::move(logTail), std::move(server), m_config.worldNode);

		const std::string body = SerializeBugReportDocument(document);

		ILOG("Queued bug report from " << reporter.characterName << " about " << game::GetBugReportSubjectName(payload.subjectType) << " " << payload.subjectId);

		if (!m_uploader->Enqueue(body))
		{
			// The outcome handler logs the drop; the queue being full is a health problem.
			m_health.OnQueueFull(GetAsyncTimeMs());
			PublishHealth();
		}

		return game::bug_report_result::Accepted;
	}

	void BugReportService::OnUploadOutcome(const BugReportUploader::Outcome& outcome)
	{
		switch (outcome.kind)
		{
		case BugReportUploader::Outcome::Delivered:
			DLOG("Bug report delivered (HTTP " << outcome.status << ")");
			m_health.OnDelivered();
			break;
		case BugReportUploader::Outcome::AttemptFailed:
			WLOG("Bug report upload attempt failed: " << outcome.detail);
			m_health.OnAttemptFailed(GetAsyncTimeMs());
			break;
		case BugReportUploader::Outcome::Dropped:
			ELOG("Bug report dropped (HTTP " << outcome.status << "): " << outcome.detail);
			break;
		}

		PublishHealth();
	}

	void BugReportService::ScheduleHealthCheck()
	{
		if (m_stopped)
		{
			return;
		}

		m_healthTimer.expires_after(HealthCheckInterval);
		m_healthTimer.async_wait([this](const asio::error_code& error)
		{
			if (error || m_stopped)
			{
				return;
			}

			m_health.Update(GetAsyncTimeMs());
			PublishHealth();
			ScheduleHealthCheck();
		});
	}

	void BugReportService::PublishHealth()
	{
		const bool wasAvailable = m_subsystems.IsAvailable(game::subsystem::BugReport);
		m_subsystems.SetHealthy(game::subsystem::BugReport, m_health.IsHealthy());
		const bool isAvailable = m_subsystems.IsAvailable(game::subsystem::BugReport);

		if (wasAvailable && !isAvailable && !m_health.IsHealthy())
		{
			WLOG("Bug reporting is temporarily unavailable: the bug API is not reachable");
		}
		else if (!wasAvailable && isAvailable)
		{
			ILOG("Bug reporting is available again");
		}
	}
}
