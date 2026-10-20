// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

namespace mmo
{
	/// Hands the crash reports a server tier left behind to the crash API — the one the client's
	/// mmo_error uploader posts to and /fix-crash fetches from.
	///
	/// Uploading from the dying process is not safe, so it happens on the next start instead: a
	/// worker thread picks up every report in the report directory that has no sent marker yet,
	/// attaches the tail of the log the report names, posts it and marks it sent. Reports the API
	/// cannot be reached for stay unsent and are tried again on the next start.
	///
	/// The worker never logs (server logging is main-thread only); outcomes are handed back
	/// through the poster, which should run them on the io thread.
	class CrashReportSender final : public NonCopyable
	{
	public:
		struct Config
		{
			/// Whether reports are uploaded at all.
			bool enabled = true;

			/// Base url of the crash API (https://host[:port][/prefix]); reports are posted to
			/// <prefix>/api/reports. file://<directory> writes each upload body there instead (tests).
			std::string apiUrl;

			/// Application whose reports to send, e.g. "world_server".
			std::string applicationName;

			/// Directory the crash handler writes reports to.
			std::filesystem::path reportDirectory;

			/// Version of the running build. Reports are sent by the next start, which may be a
			/// newer build, so the report's own "Version" detail is preferred when it has one.
			std::string appVersion;

			/// Identifies this server, e.g. the world node name; sent as the device model.
			std::string nodeName;
		};

		struct Outcome
		{
			enum Kind
			{
				/// Accepted by the API and marked sent.
				Sent,
				/// Rejected for good (or unreadable); marked so it is not tried again.
				Rejected,
				/// Could not be delivered now; stays unsent for the next start.
				Deferred
			};

			Kind kind = Deferred;
			std::filesystem::path report;
			uint32 status = 0;
			std::string detail;
		};

		/// Runs work on the owning thread, e.g. by posting it to the io_service.
		typedef std::function<void(std::function<void()>)> Poster;

		/// Receives every outcome on the owning thread.
		typedef std::function<void(const Outcome&)> OutcomeHandler;

		/// Sends one upload body; returns the HTTP status, 0 if no response arrived. Fills
		/// @p error on failure.
		typedef std::function<uint32(const std::string& body, const std::string& contentType, std::string& error)> Transport;

		/// At most this many reports are sent per start, so a crash loop's backlog cannot make
		/// one start hammer the API. The rest follow on later starts.
		static constexpr size_t MaxReportsPerStart = 10;

		/// At most this much of the log is attached to a report.
		static constexpr size_t MaxLogTailBytes = 256 * 1024;

	public:
		/// @param config What to send where.
		/// @param poster Runs outcome handlers on the owning thread.
		/// @param onOutcome Receives outcomes; when empty they are logged.
		/// @param transport Overrides the transport made from @p config.apiUrl (tests).
		CrashReportSender(Config config, Poster poster, OutcomeHandler onOutcome = {}, Transport transport = {});
		~CrashReportSender();

	public:
		/// Starts sending pending reports in the background. Does nothing when disabled.
		void Start();

		/// Cancels an upload in flight and waits for the worker to finish. Safe to call twice.
		void Stop();

		/// Sends pending reports on the calling thread, delivering outcomes directly instead of
		/// through the poster. This is the worker's body; tests call it directly.
		/// @returns The number of reports handled (sent or rejected).
		size_t SendPending();

		/// Creates the transport for a crash API url, or an empty function for an invalid url.
		/// @param cancel Polled during https uploads; when it turns true the upload is aborted.
		static Transport MakeTransport(const std::string& apiUrl, const std::atomic<bool>* cancel);

	private:
		/// @param direct Deliver outcomes on the calling thread rather than through the poster.
		size_t SendPendingReports(bool direct);

		void Deliver(const Outcome& outcome, bool direct);

		static void LogOutcome(const Outcome& outcome);

	private:
		Config m_config;
		Poster m_poster;
		OutcomeHandler m_onOutcome;
		Transport m_transport;
		std::atomic<bool> m_stopping{ false };
		std::thread m_worker;
	};
}
