// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace mmo
{
	/// Configuration for the process-wide crash handler.
	struct CrashHandlerConfig
	{
		/// Name of the application, used in the report and its file name, e.g. "world_server".
		std::string applicationName;

		/// Directory crash reports are written to. Created on demand.
		std::filesystem::path outputDirectory;

		/// Invoked on the crashing thread just before the report is written.
		///
		/// Use it to flush buffered log output — server log files are written with
		/// alwaysFlush disabled, so without a flush here the tail of the log covering the
		/// crash is lost. Detail lines appended to @p details are written into the report,
		/// which is the place to record application state such as the active world instance.
		///
		/// The process is already dying when this runs: do the minimum and do not allocate
		/// more than necessary.
		std::function<void(std::vector<std::string>& details)> onCrash;
	};

	/// Installs a process-wide handler that writes a crash report when the process dies from an
	/// unhandled exception (Windows) or a fatal signal (POSIX).
	///
	/// The report is written in the format tools/symbolicate_crash.ps1 parses, so a server crash
	/// can be symbolicated offline from the archived PDBs exactly like a client crash.
	///
	/// In debug builds the handler is not installed while a debugger is attached, so the debugger
	/// keeps first chance at the exception.
	///
	/// @param config Handler configuration. Calling this more than once replaces the previous
	///        configuration.
	void InstallCrashHandler(CrashHandlerConfig config);

	/// Clears the crash callback installed by @ref InstallCrashHandler.
	///
	/// The callback captures the object that installed it, so it has to be dropped before that
	/// object goes away — otherwise a fault during shutdown runs a hook over destroyed state. The
	/// handler itself stays installed and keeps reporting; it simply stops calling back.
	void UninstallCrashHandler();
}
