// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <filesystem>
#include <string>
#include <vector>

namespace mmo
{
	/// @brief Command line options of the UI test app.
	struct UiTestOptions final
	{
		/// @brief Root of the client data (the folder holding Interface/, Locales/, Fonts/ ...).
		std::filesystem::path dataPath;

		/// @brief Folder with the ui_test tooling (mocks/ and scenarios/).
		std::filesystem::path toolPath;

		/// @brief Folder that receives screenshots, status.json and the commands/ inbox.
		std::filesystem::path outPath;

		/// @brief The TOC file to load, relative to the data path.
		std::string toc = "Interface/GameUI/GameUI.toc";

		/// @brief Locale code, e.g. enUS.
		std::string locale = "enUS";

		/// @brief Scenario script run after the UI has loaded. Empty for none.
		std::filesystem::path scenario;

		/// @brief Window client size, which is also the screenshot size.
		uint16 width = 1920;
		uint16 height = 1080;

		/// @brief Frames rendered after a reload before the automatic screenshot is taken.
		int32 settleFrames = 10;

		/// @brief If set, the app takes one screenshot to this file after the first load and exits.
		std::filesystem::path oneShotScreenshot;

		/// @brief Whether files are watched for changes (live mode).
		bool watch = true;

		/// @brief Backdrop colour behind the UI, 0xRRGGBB.
		uint32 backdropColor = 0x2B3038;

		/// @brief Optional texture drawn behind the UI instead of the backdrop colour (path in the data).
		std::string backgroundTexture;

		/// @brief Errors found while parsing the command line.
		std::vector<std::string> errors;
	};

	/// @brief Parses the command line and resolves default paths.
	/// @param args Arguments without the program name.
	/// @return The parsed options. Check UiTestOptions::errors before using them.
	UiTestOptions ParseUiTestOptions(const std::vector<std::string>& args);

	/// @brief Usage text printed for --help and on errors.
	const char* GetUiTestUsage();
}
