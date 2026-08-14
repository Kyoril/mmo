// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/crash_handler.h"

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
	class ServerCrashHandlerScope final
	{
	public:
		/// Installs the handler.
		/// @param applicationName Name of the server, e.g. "world_server".
		/// @param logFileName Configured log file name; its directory receives the crash reports.
		/// @param logFile The open log stream to flush when a crash is reported.
		ServerCrashHandlerScope(std::string applicationName, const std::string& logFileName, std::ofstream& logFile)
		{
			CrashHandlerConfig config;
			config.applicationName = std::move(applicationName);
			config.outputDirectory = std::filesystem::path(logFileName).parent_path();
			config.onCrash = [&logFile](std::vector<std::string>& /*details*/)
			{
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
	};
}
