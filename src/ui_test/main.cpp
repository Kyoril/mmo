// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "ui_test_app.h"
#include "ui_test_options.h"

#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#endif

namespace mmo
{
	namespace
	{
		/// Reports to the console the app was started from (it is a GUI app and has none of its own).
		void PrintToParentConsole(const std::string& text)
		{
#ifdef _WIN32
			if (AttachConsole(ATTACH_PARENT_PROCESS))
			{
				FILE* stream = nullptr;
				freopen_s(&stream, "CONOUT$", "w", stderr);
			}
#endif
			std::fputs(text.c_str(), stderr);
			std::fflush(stderr);
		}

		int32 RunUiTest(const std::vector<std::string>& args)
		{
			for (const auto& arg : args)
			{
				if (arg == "--help" || arg == "-h" || arg == "/?")
				{
					PrintToParentConsole(GetUiTestUsage());
					return 0;
				}
			}

			const UiTestOptions options = ParseUiTestOptions(args);
			if (!options.errors.empty())
			{
				std::string text;
				for (const auto& error : options.errors)
				{
					text += "ui_test: " + error + "\n";
				}
				PrintToParentConsole(text + "\n" + GetUiTestUsage());
				return 2;
			}

			UiTestApp app(options);
			if (!app.Initialize())
			{
				app.Shutdown();
				return 1;
			}

			const int32 exitCode = app.Run();
			app.Shutdown();
			return exitCode;
		}
	}
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	std::vector<std::string> args;
	for (int i = 1; i < __argc; ++i)
	{
		args.emplace_back(__argv[i]);
	}
	return mmo::RunUiTest(args);
}
#else
int main(int argc, char** argv)
{
	return mmo::RunUiTest(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
