// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// asio has to be included before any windows header.
#include "asio/io_service.hpp"

#include "launcher_model.h"
#include "launcher_window.h"
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

#ifndef NDEBUG
#	include "bitmap.h"
#	include "text_renderer.h"
#endif

namespace
{
	const std::string DefaultUpdateSourceUrl = "https://patch.mmo-dev.net";

	std::string g_previousExecutableToBeRemoved;

	void ShowVersionInfoDialog()
	{
		const std::string body =
			"Version: " MMO_VERSION_STR "\n"
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

#ifndef NDEBUG
	/// Verifies that every embedded asset can be found and decoded.
	///
	/// Runs on startup in debug builds so that a mismatch between resource.h,
	/// launcher.rc and the CMake asset list surfaces immediately rather than as a
	/// blank rectangle somewhere in the UI much later.
	void VerifyEmbeddedAssets()
	{
		constexpr std::array<uint32, 13> images = {
			IDR_PNG_SPLASH, IDR_PNG_PANEL_BOTTOM, IDR_PNG_BORDER_FRAME,
			IDR_PNG_BUTTON_UP, IDR_PNG_BUTTON_OVER, IDR_PNG_BUTTON_DOWN, IDR_PNG_BUTTON_DISABLED,
			IDR_PNG_PROGRESS_TRACK, IDR_PNG_PROGRESS_FILL,
			IDR_PNG_ICON_CLOSE, IDR_PNG_ICON_MINIMIZE,
			IDR_PNG_CONTENT_TEXTURE, IDR_PNG_HERO_FRAME
		};

		for (const uint32 id : images)
		{
			const mmo::ResourceBlob blob = mmo::LoadResourceBlob(id);
			ASSERT(blob.IsValid() && "An embedded image resource is missing");

			mmo::Bitmap bitmap;
			VERIFY(mmo::Bitmap::DecodePng(blob, bitmap) && "An embedded image failed to decode");
			ASSERT(bitmap.IsValid());
		}

		for (const uint32 id : { IDR_TTF_DISPLAY, IDR_TTF_BODY })
		{
			const mmo::ResourceBlob blob = mmo::LoadResourceBlob(id);
			ASSERT(blob.IsValid() && "An embedded font resource is missing");

			mmo::FontFace face;
			VERIFY(face.Initialize(blob, 16) && "An embedded font failed to load");
			ASSERT(face.GetGlyph('A') != nullptr && "An embedded font produced no glyph");
		}

		DLOG("Embedded assets verified (" << images.size() << " images, 2 fonts)");
	}
#endif
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previousInstance, LPSTR commandLine, int showCommand)
{
	size_t concurrency = 4;
	bool selfUpdateEnabled = true;

	cxxopts::Options options("Available options");
	options.add_options()
		("v,version", "Displays the version of the launcher on screen.")
		("remove-previous", "Tries to remove a specified file", cxxopts::value<std::string>(g_previousExecutableToBeRemoved))
		("no-self-update", "Disables self-update of the launcher executable")
		("j,concurrency", "The number of threads used for downloading and updating", cxxopts::value<size_t>(concurrency))
		;

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
		VerifyEmbeddedAssets();
#endif

		mmo::LauncherModel model;

		mmo::UpdateWorkerConfig config;
		config.sourceUrl = DefaultUpdateSourceUrl;
		config.concurrency = concurrency;
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

		worker.Start();

		const int result = window.Run();

		// Stop() signals the run to abort and joins, so the worker cannot outlive the
		// model it holds a reference to.
		worker.Stop();
		return result;
	}
	catch (const cxxopts::OptionException& ex)
	{
		ELOG("Command line option exception: " << ex.what());
	}

	return 0;
}
