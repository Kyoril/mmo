// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// asio has to be included before any windows header.
#include "asio/io_service.hpp"

#include "launcher_model.h"
#include "launcher_window.h"
#include "launcher_preview.h"
#include "resource.h"
#include "update_worker.h"
#include "version.h"

#include "base/filesystem.h"
#include "base/macros.h"
#include "base/typedefs.h"
#include "base/win_utility.h"

#include "log/default_log.h"
#include "log/default_log_levels.h"
#include "log/log_entry.h"
#include "log/log_std_stream.h"

#include "cxxopts/cxxopts.hpp"

#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include <ShlObj.h>
#include <Windows.h>

namespace
{
	const std::string DefaultUpdateSourceUrl = "https://patch.mmo-dev.net";

	std::string g_previousExecutableToBeRemoved;

	void ShowVersionInfoDialog()
	{
		const std::string body =
			"Version: " MMO_LAUNCHER_VERSION_STR "\n"
			"Build date: " __DATE__ " " __TIME__ "\n"
#ifndef NDEBUG
			"Debug configuration\n"
#endif
			;

		MessageBoxA(nullptr, body.c_str(), "MMORPG Launcher", MB_OK);
	}

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
			// Not important enough to fail the launch over; it is retried once the
			// update has finished and the file is no longer locked.
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

}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previousInstance, LPSTR commandLine, int showCommand)
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

	try
	{
		int argc = 0;
		PCHAR* argv = CommandLineToArgvA(GetCommandLineA(), &argc);

		const auto results = options.parse(argc, argv);

		if (results.count("version"))
		{
			// Reports the version and exits; it must not fall through into a full run.
			ShowVersionInfoDialog();
			return 0;
		}

		if (results.count("no-self-update"))
		{
			selfUpdateEnabled = false;
		}

		if (results.count("no-pipelining"))
		{
			pipeliningEnabled = false;
		}

		if (!renderPreviewDirectory.empty())
		{
			return mmo::RenderLauncherPreviews(renderPreviewDirectory) ? 0 : 1;
		}

		// Installation and updates are relative to the executable, not the shell's cwd.
		std::array<wchar_t, 32768> executable{};
		const DWORD executableLength = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
		if (executableLength == 0 || executableLength >= executable.size())
		{
			return 1;
		}
		const auto gameDirectory = std::filesystem::path(executable.data()).parent_path();
		if (!SetCurrentDirectoryW(gameDirectory.c_str()))
		{
			return 1;
		}

		std::array<char, MAX_PATH> documents = { "." };
		SHGetFolderPathA(0, CSIDL_MYDOCUMENTS, 0, SHGFP_TYPE_CURRENT, documents.data());

		std::filesystem::path documentsPath = documents.data();
		documentsPath /= "MMORPG";

		const auto logDirectory = documentsPath / "Logs";
		CreateDirectory(logDirectory);

		std::ofstream logFile;
		logFile.open((logDirectory / "Launcher.log").string());
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

		mmo::UpdateWorker worker(model, config);
		mmo::LauncherWindow window(model, worker);

		if (!window.Create(instance))
		{
			MessageBoxA(nullptr, "Failed to create the launcher window.", "Error", MB_OK | MB_ICONERROR);
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

		const int result = window.Run();

		// Stop() aborts the run and joins, so the worker cannot outlive the model it
		// holds a reference to. Downloads abort within a fraction of a second; the
		// timeout only guards against something unforeseen blocking the worker. The
		// window is already gone at this point, and a launcher that lingers invisibly
		// in the background while it keeps writing files is worse than cutting it off.
		if (!worker.Stop(std::chrono::seconds(5)))
		{
			WLOG("The update worker did not stop in time, terminating the launcher");
			ExitProcess(static_cast<UINT>(result));
		}

		return result;
	}
	catch (const cxxopts::OptionException& ex)
	{
		ELOG("Command line option exception: " << ex.what());
	}

	return 0;
}
