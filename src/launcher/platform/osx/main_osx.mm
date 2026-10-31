// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "asio/io_service.hpp"

#include "launcher_model.h"
#include "launcher_osx.h"
#include "launcher_preview.h"
#include "launcher_window.h"
#include "update_worker.h"

#include "base/filesystem.h"
#include "base/macros.h"
#include "base/typedefs.h"

#include "log/default_log.h"
#include "log/default_log_levels.h"
#include "log/log_entry.h"
#include "log/log_std_stream.h"

#include "cxxopts/cxxopts.hpp"

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include <unistd.h>

#import <Cocoa/Cocoa.h>

namespace
{
	const std::string DefaultUpdateSourceUrl = "https://patch.mmo-dev.net";

	std::string g_previousExecutableToBeRemoved;

	/// Removes the executable left behind by a previous self-update.
	bool RemovePreviousExecutable()
	{
		try
		{
			std::this_thread::sleep_for(std::chrono::seconds(1));
			std::filesystem::remove(g_previousExecutableToBeRemoved);
			return true;
		}
		catch (const std::exception& ex)
		{
			WLOG("Could not remove the previous launcher yet: " << ex.what());
			return false;
		}
	}

	void CreateDirectory(const std::filesystem::path& directory)
	{
		try
		{
			std::filesystem::create_directories(directory);
		}
		catch (const std::filesystem::filesystem_error& ex)
		{
			ELOG(ex.what());
		}
	}

	/// The directory that contains Launcher.app, where mmo_client.app and its Data,
	/// Config and Logs live: the macOS equivalent of the folder next to Launcher.exe.
	std::filesystem::path GetGameDirectory()
	{
		@autoreleasepool
		{
			NSString* bundle = [[NSBundle mainBundle] bundlePath];
			return std::filesystem::path([[bundle stringByDeletingLastPathComponent] fileSystemRepresentation]);
		}
	}

	/// Shows an alert before the application has a window.
	void ShowFatalError(NSString* message)
	{
		NSAlert* alert = [[NSAlert alloc] init];
		alert.messageText = @"Error";
		alert.informativeText = message;
		alert.alertStyle = NSAlertStyleCritical;
		[alert runModal];
	}
}

int main(int argc, char* argv[])
{
	size_t concurrency = 8;
	bool selfUpdateEnabled = true;
	bool pipeliningEnabled = true;
	bool preview = false;
	std::string contentUrl = "https://patch.mmo-dev.net/launcher/launcher.json";
	bool noContent = false;
	std::string renderPreviewDirectory;

	cxxopts::Options options("Available options");
	options.add_options()("v,version", "Displays the version of the launcher on screen.")(
		"remove-previous", "Tries to remove a specified file", cxxopts::value<std::string>(g_previousExecutableToBeRemoved))(
		"preview", "Shows the launcher without checking for or downloading updates", cxxopts::value<bool>(preview))(
		"content-url", "HTTPS launcher content manifest URL", cxxopts::value<std::string>(contentUrl))(
		"no-content", "Uses embedded content without contacting the content server", cxxopts::value<bool>(noContent))(
		"render-preview", "Renders all launcher pages to BMP files without downloading updates",
		cxxopts::value<std::string>(renderPreviewDirectory))("no-self-update", "Disables self-update of the launcher executable")(
		"no-pipelining", "Requests files one at a time instead of pipelining them (for troubleshooting)")(
		"j,concurrency", "The number of threads used for downloading and updating", cxxopts::value<size_t>(concurrency));

	// Finder and older macOS versions pass a -psn_X_Y process serial number, which is
	// not an option of ours and would make the parse fail.
	std::vector<char*> arguments;
	for (int i = 0; i < argc; ++i)
	{
		if (std::strncmp(argv[i], "-psn_", 5) != 0)
		{
			arguments.push_back(argv[i]);
		}
	}
	int argumentCount = static_cast<int>(arguments.size());
	char** argumentValues = arguments.data();

	try
	{
		const auto results = options.parse(argumentCount, argumentValues);

		if (results.count("no-self-update"))
		{
			selfUpdateEnabled = false;
		}

		if (results.count("no-pipelining"))
		{
			pipeliningEnabled = false;
		}

		@autoreleasepool
		{
			[NSApplication sharedApplication];
			[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

			if (results.count("version"))
			{
				// Reports the version and exits; it must not fall through into a full run.
				[NSApp activateIgnoringOtherApps:YES];
				mmo::ShowLauncherVersionInfo();
				return 0;
			}
		}

		// Started from a terminal, the log is useful there too.
		if (isatty(STDOUT_FILENO))
		{
			mmo::g_DefaultLog.signal().connect(std::bind(mmo::printLogEntry,
				std::ref(std::cout), std::placeholders::_1, mmo::g_DefaultConsoleLogOptions));
		}

		// Before the chdir below, so a relative directory means the shell's.
		if (!renderPreviewDirectory.empty())
		{
			return mmo::RenderLauncherPreviews(renderPreviewDirectory) ? 0 : 1;
		}

		// Installation and updates are relative to the bundle, not the shell's cwd.
		const std::filesystem::path gameDirectory = GetGameDirectory();
		if (chdir(gameDirectory.c_str()) != 0)
		{
			return 1;
		}

		std::ofstream logFile;
		const std::filesystem::path logDirectory = mmo::GetLauncherLogDirectory();
		if (!logDirectory.empty())
		{
			CreateDirectory(logDirectory);
			logFile.open((logDirectory / "Launcher.log").string());
		}
		if (logFile)
		{
			auto logOptions = mmo::g_DefaultFileLogOptions;
			logOptions.alwaysFlush = true;

			mmo::g_DefaultLog.signal().connect(std::bind(mmo::printLogEntry,
				std::ref(logFile), std::placeholders::_1, logOptions));
		}

		bool retryRemovePreviousExecutable = false;
		if (!g_previousExecutableToBeRemoved.empty())
		{
			retryRemovePreviousExecutable = !RemovePreviousExecutable();
		}

#ifndef NDEBUG
		mmo::VerifyEmbeddedAssets();
#endif

		mmo::LauncherModel model;

		mmo::UpdateWorkerConfig config;
		config.sourceUrl = DefaultUpdateSourceUrl;
		config.concurrency = concurrency;
		if (!pipeliningEnabled)
		{
			config.pipelineConnections = 0;
		}
		config.selfUpdateEnabled = selfUpdateEnabled;

		int result = 0;
		@autoreleasepool
		{
			mmo::UpdateWorker worker(model, config);
			mmo::LauncherWindow window(model, worker);

			if (!window.Create())
			{
				ShowFatalError(@"Failed to create the launcher window.");
				return 1;
			}

			worker.onSelfUpdateFinished = [&window] { window.NotifySelfUpdateFinished(); };
			worker.onFinished = [&retryRemovePreviousExecutable]
			{
				if (retryRemovePreviousExecutable)
				{
					RemovePreviousExecutable();
				}
			};

			if (preview)
			{
				model.SetPhase(mmo::UpdatePhase::Updating);
				model.SetStatus("Preview mode - no files are being downloaded");
				model.SetProgress(0.57f);
			}
			else
			{
				worker.Start();
				if (!noContent)
				{
					window.StartContent(contentUrl);
				}
			}

			result = window.Run();

			// Stop() aborts the run and joins, so the worker cannot outlive the model it
			// holds a reference to. See main_win32.cpp for why a timeout ends the process.
			if (!worker.Stop(std::chrono::seconds(5)))
			{
				WLOG("The update worker did not stop in time, terminating the launcher");
				_exit(result);
			}
		}

		return result;
	}
	catch (const cxxopts::OptionException& ex)
	{
		ELOG("Command line option exception: " << ex.what());
		std::cerr << ex.what() << '\n';
	}

	return 0;
}
