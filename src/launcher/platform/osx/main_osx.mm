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

#include <vector>

#include <dlfcn.h>
#include <sys/xattr.h>
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

	/// Finishes the application's launch so an alert can be shown before [NSApp run].
	///
	/// Without it the alert's modal loop runs but its window never reaches the screen.
	/// Only the early exit paths call this; they never reach [NSApp run], which finishes
	/// the launch on its own.
	void FinishLaunchingForAlert()
	{
		static bool s_launched = false;
		if (!s_launched)
		{
			s_launched = true;
			[NSApp finishLaunching];
		}
		[NSApp activateIgnoringOtherApps:YES];
	}

	/// Shows an alert before the application has a window.
	void ShowFatalError(NSString* message, NSString* title = @"Error")
	{
		FinishLaunchingForAlert();

		NSAlert* alert = [[NSAlert alloc] init];
		alert.messageText = title;
		alert.informativeText = message;
		alert.alertStyle = NSAlertStyleCritical;
		[alert runModal];
	}

	/// Returns the bundle's real location if Gatekeeper translocated it, else nil.
	///
	/// A quarantined app that was not moved on its own by Finder (the DMG holds a whole
	/// game folder, so it never is) runs from a random read-only copy, and the game
	/// would be installed into that copy. The SecTranslocate functions are exported by
	/// Security.framework but not declared in the SDK, hence the lookup by name.
	NSURL* GetTranslocationOrigin(NSURL* bundle)
	{
		using IsTranslocatedFn = Boolean (*)(CFURLRef, bool*, CFErrorRef*);
		using CreateOriginalPathFn = CFURLRef (*)(CFURLRef, CFErrorRef*);

		void* security = dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY);
		const auto isTranslocated = security ? reinterpret_cast<IsTranslocatedFn>(dlsym(security, "SecTranslocateIsTranslocatedURL")) : nullptr;
		const auto createOriginalPath = security ? reinterpret_cast<CreateOriginalPathFn>(dlsym(security, "SecTranslocateCreateOriginalPathForURL")) : nullptr;

		bool translocated = false;
		if (isTranslocated)
		{
			isTranslocated((__bridge CFURLRef)bundle, &translocated, nullptr);
		}
		else
		{
			translocated = [[bundle path] containsString:@"/AppTranslocation/"];
		}

		if (!translocated || !createOriginalPath)
		{
			return translocated ? [NSURL fileURLWithPath:@"/"] : nil;
		}

		return (__bridge_transfer NSURL*)createOriginalPath((__bridge CFURLRef)bundle, nullptr);
	}

	/// Removes the quarantine flag from everything in `bundle`.
	///
	/// The player already confirmed the first start in Gatekeeper's dialog; without the
	/// flag the bundle is no longer translocated. Files the launcher downloads later are
	/// never quarantined, because the launcher does not opt into quarantining.
	void RemoveQuarantine(NSURL* bundle)
	{
		const std::filesystem::path root([bundle fileSystemRepresentation]);
		removexattr(root.c_str(), "com.apple.quarantine", XATTR_NOFOLLOW);

		std::error_code error;
		for (auto it = std::filesystem::recursive_directory_iterator(root, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
		{
			removexattr(it->path().c_str(), "com.apple.quarantine", XATTR_NOFOLLOW);
		}
	}

	/// Starts the bundle at `url` with the same arguments and waits until it runs.
	bool Relaunch(NSURL* url, const std::vector<char*>& arguments)
	{
		NSMutableArray<NSString*>* forwarded = [NSMutableArray array];
		for (size_t i = 1; i < arguments.size(); ++i)
		{
			[forwarded addObject:[NSString stringWithUTF8String:arguments[i]]];
		}

		NSWorkspaceOpenConfiguration* configuration = [NSWorkspaceOpenConfiguration configuration];
		configuration.arguments = forwarded;
		configuration.createsNewApplicationInstance = YES;

		dispatch_semaphore_t done = dispatch_semaphore_create(0);
		__block bool started = false;
		[[NSWorkspace sharedWorkspace] openApplicationAtURL:url
											  configuration:configuration
										  completionHandler:^(NSRunningApplication* application, NSError* error) {
											  started = application != nil;
											  if (error)
											  {
												  NSLog(@"Relaunch failed: %@", error);
											  }
											  dispatch_semaphore_signal(done);
										  }];
		dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC));
		return started;
	}

	enum class InstallLocation
	{
		Usable,
		Relaunched,
		Unusable
	};

	/// Makes sure the game can be installed next to the bundle, as the setup's folder
	/// checks do on Windows.
	InstallLocation CheckInstallLocation(const std::vector<char*>& arguments)
	{
		NSURL* bundle = [[NSBundle mainBundle] bundleURL];

		if (NSURL* origin = GetTranslocationOrigin(bundle))
		{
			if ([[origin path] isEqualToString:@"/"])
			{
				ShowFatalError(@"macOS started Alestia Online from a temporary location. Move the Alestia Online folder to Applications and start the launcher from there.",
					@"Alestia Online cannot start from here");
				return InstallLocation::Unusable;
			}

			RemoveQuarantine(origin);
			if (Relaunch(origin, arguments))
			{
				return InstallLocation::Relaunched;
			}

			ShowFatalError(@"macOS started Alestia Online from a temporary location and it could not restart from its real one. Start the launcher again from the Alestia Online folder in Applications.",
				@"Alestia Online cannot start from here");
			return InstallLocation::Unusable;
		}

		// The game is downloaded next to the bundle. On the mounted disk image, or any
		// folder the player cannot write to, that cannot work.
		if (access([[[bundle URLByDeletingLastPathComponent] path] fileSystemRepresentation], W_OK) != 0)
		{
			ShowFatalError(@"Drag the Alestia Online folder to Applications, then start the launcher from there. The game is downloaded into that folder.",
				@"Install Alestia Online first");
			return InstallLocation::Unusable;
		}

		return InstallLocation::Usable;
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
				FinishLaunchingForAlert();
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

		@autoreleasepool
		{
			switch (CheckInstallLocation(arguments))
			{
			case InstallLocation::Usable:
				break;
			case InstallLocation::Relaunched:
				return 0;
			case InstallLocation::Unusable:
				return 1;
			}
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
