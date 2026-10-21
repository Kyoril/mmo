// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/crash_context.h"
#include "base/crash_handler.h"
#include "base/crash_report_outbox.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace mmo
{
	/// Installs the crash handler for a server tier and uninstalls it again on destruction.
	///
	/// All three server tiers need the same wiring: report next to the log files, and flush the log
	/// before the report is written because file logging runs with alwaysFlush disabled and the tail
	/// covering the crash would otherwise be lost. The scope guard matters as much as the install —
	/// the flush callback captures the log stream, so it must not outlive it.
	///
	/// Every report also names the build and the log file it belongs to, which is what lets the
	/// next start upload it together with the log tail (see CrashReportSender), and carries the
	/// crashing thread's CrashContextScope lines — the player, packet or world instance it was on.
	class ServerCrashHandlerScope final
	{
	public:
		/// Installs the handler.
		/// @param applicationName Name of the server, e.g. "world_server".
		/// @param logFileName Configured log file name; its directory receives the crash reports.
		/// @param logFile The open log stream to flush when a crash is reported.
		/// @param logFilePath Path of the file @p logFile writes to, recorded in the report.
		/// @param version Version of this build, recorded in the report.
		ServerCrashHandlerScope(std::string applicationName, const std::string& logFileName, std::ofstream& logFile,
			const std::string& logFilePath, std::string version)
		{
			std::string versionLine = "Version: " + std::move(version);
			std::string logLine;
			if (logFile.is_open() && !logFilePath.empty())
			{
				std::error_code error;
				const auto absolutePath = std::filesystem::absolute(logFilePath, error);
				logLine = std::string(CrashReportLogFileKey) + ": " + (error ? logFilePath : absolutePath.string());
			}

			CrashHandlerConfig config;
			config.applicationName = std::move(applicationName);
			config.outputDirectory = GetReportDirectory(logFileName);
			config.onCrash = [&logFile, versionLine = std::move(versionLine), logLine = std::move(logLine)](std::vector<std::string>& details)
			{
				details.push_back(versionLine);
				if (!logLine.empty())
				{
					details.push_back(logLine);
				}

				AppendCrashContext(details);

				if (logFile.is_open())
				{
					logFile.flush();
				}
			};

			InstallCrashHandler(std::move(config));
		}

		~ServerCrashHandlerScope()
		{
			UninstallCrashHandler();
		}

		ServerCrashHandlerScope(const ServerCrashHandlerScope&) = delete;
		ServerCrashHandlerScope& operator=(const ServerCrashHandlerScope&) = delete;

		/// @returns The directory the crash reports of a tier with this log file name go to.
		static std::filesystem::path GetReportDirectory(const std::string& logFileName)
		{
			// Same fallback InstallCrashHandler applies, so the sender looks where reports land.
			std::filesystem::path directory = std::filesystem::path(logFileName).parent_path();
			if (directory.empty())
			{
				directory = "logs";
			}

			return directory;
		}
	};
}
