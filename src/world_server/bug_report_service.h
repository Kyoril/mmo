// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bug_report_health.h"
#include "bug_report_uploader.h"
#include "subsystem_state.h"
#include "bug_report_document.h"

#include "base/non_copyable.h"
#include "game/bug_report.h"

#include "asio/io_service.hpp"
#include "asio/steady_timer.hpp"
#include "nlohmann/json.hpp"

#include <memory>
#include <string>

namespace mmo
{
	class Player;

	namespace proto
	{
		class Project;
	}

	/// Handles bug reports on the world node: validates them, attaches the server snapshot and
	/// queues them for upload. Owns the uploader thread and keeps the BugReport subsystem's health
	/// in sync with it.
	class BugReportService final : public NonCopyable
	{
	public:
		struct Config
		{
			bool enabled = false;
			std::string apiUrl;
			std::string apiKey;
			/// Name of this world node, put into every report.
			std::string worldNode;
		};

	public:
		BugReportService(asio::io_service& ioService, WorldSubsystemState& subsystems, const proto::Project& project, Config config);
		~BugReportService();

	public:
		/// Validates the configuration and starts the uploader. Leaves the subsystem unavailable
		/// if bug reports are disabled or misconfigured.
		void Start();

		/// Stops the uploader thread and the health timer. Queued reports are dropped.
		void Stop();

		/// Handles a report filed by a player of this node.
		/// @returns The result to send back to the client.
		game::BugReportResult HandleReport(Player& player, const BugReporterIdentity& reporter, const game::BugReportPayload& payload);

	private:
		void OnUploadOutcome(const BugReportUploader::Outcome& outcome);

		void ScheduleHealthCheck();

		void PublishHealth();

	private:
		asio::io_service& m_ioService;
		WorldSubsystemState& m_subsystems;
		const proto::Project& m_project;
		Config m_config;
		BugReportHealth m_health;
		std::unique_ptr<BugReportUploader> m_uploader;
		asio::steady_timer m_healthTimer;
		bool m_stopped = false;
	};
}
