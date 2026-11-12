// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "ui_test_options.h"

#include <cstdlib>
#include <cstdio>
#include <optional>

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#endif

namespace mmo
{
	namespace
	{
		/// Walks up from start and returns the first folder for which test(folder) holds.
		template <typename Predicate>
		std::optional<std::filesystem::path> FindUpwards(std::filesystem::path start, Predicate test)
		{
			std::error_code ec;
			start = std::filesystem::absolute(start, ec);
			while (!start.empty())
			{
				if (test(start))
				{
					return start;
				}

				const auto parent = start.parent_path();
				if (parent == start)
				{
					break;
				}
				start = parent;
			}

			return std::nullopt;
		}

		/// Searches upwards from the working directory and from the executable's folder.
		template <typename Predicate>
		std::optional<std::filesystem::path> FindFromKnownRoots(Predicate test)
		{
			std::error_code ec;
			if (auto found = FindUpwards(std::filesystem::current_path(ec), test))
			{
				return found;
			}

#ifdef _WIN32
			char modulePath[1024] = {};
			if (GetModuleFileNameA(nullptr, modulePath, sizeof(modulePath)) > 0)
			{
				return FindUpwards(std::filesystem::path(modulePath).parent_path(), test);
			}
#endif
			return std::nullopt;
		}

		bool ParseSize(const std::string& value, uint16& width, uint16& height)
		{
			int w = 0, h = 0;
			if (std::sscanf(value.c_str(), "%dx%d", &w, &h) != 2 || w < 64 || h < 64 || w > 8192 || h > 8192)
			{
				return false;
			}

			width = static_cast<uint16>(w);
			height = static_cast<uint16>(h);
			return true;
		}
	}

	const char* GetUiTestUsage()
	{
		return
			"ui_test - renders the frame UI outside the game client and reloads it when files change.\n"
			"\n"
			"  --game                 load Interface/GameUI/GameUI.toc (default)\n"
			"  --glue                 load Interface/GlueUI/GlueUI.toc\n"
			"  --toc <path>           load any TOC file (path relative to the data folder)\n"
			"  --scenario <name|file> run tools/ui_test/scenarios/<name>.lua (or a file) after every load\n"
			"  --size <W>x<H>         window and screenshot size (default 1920x1080)\n"
			"  --locale <code>        enUS (default), deDE, frFR, ruRU\n"
			"  --screenshot <png>     one-shot: load, settle, write the PNG and status.json, exit (1 on errors)\n"
			"  --settle <frames>      frames rendered before a screenshot (default 10)\n"
			"  --no-watch             do not reload on file changes\n"
			"  --backdrop <RRGGBB>    colour behind the UI (default 2B3038)\n"
			"  --background <file>    texture drawn behind the UI, e.g. Interface/Loading.htex\n"
			"  --data <dir>           client data folder (default: nearest data/client upwards)\n"
			"  --tools <dir>          ui_test tooling folder (default: nearest tools/ui_test upwards)\n"
			"  --out <dir>            output folder (default: <repo>/artifacts/ui_test)\n";
	}

	UiTestOptions ParseUiTestOptions(const std::vector<std::string>& args)
	{
		UiTestOptions options;
		std::string scenario;

		for (size_t i = 0; i < args.size(); ++i)
		{
			const std::string& arg = args[i];
			const auto next = [&]() -> std::string
			{
				if (i + 1 >= args.size())
				{
					options.errors.push_back("Missing value for " + arg);
					return {};
				}
				return args[++i];
			};

			if (arg == "--game")
			{
				options.toc = "Interface/GameUI/GameUI.toc";
			}
			else if (arg == "--glue")
			{
				options.toc = "Interface/GlueUI/GlueUI.toc";
			}
			else if (arg == "--toc")
			{
				options.toc = next();
			}
			else if (arg == "--scenario")
			{
				scenario = next();
			}
			else if (arg == "--size")
			{
				const std::string value = next();
				if (!ParseSize(value, options.width, options.height))
				{
					options.errors.push_back("Invalid --size '" + value + "', expected e.g. 1920x1080");
				}
			}
			else if (arg == "--locale")
			{
				options.locale = next();
			}
			else if (arg == "--screenshot")
			{
				options.oneShotScreenshot = next();
				options.watch = false;
			}
			else if (arg == "--settle")
			{
				options.settleFrames = std::max(1, std::atoi(next().c_str()));
			}
			else if (arg == "--no-watch")
			{
				options.watch = false;
			}
			else if (arg == "--backdrop")
			{
				options.backdropColor = static_cast<uint32>(std::strtoul(next().c_str(), nullptr, 16)) & 0xFFFFFF;
			}
			else if (arg == "--background")
			{
				options.backgroundTexture = next();
			}
			else if (arg == "--data")
			{
				options.dataPath = next();
			}
			else if (arg == "--tools")
			{
				options.toolPath = next();
			}
			else if (arg == "--out")
			{
				options.outPath = next();
			}
			else
			{
				options.errors.push_back("Unknown argument '" + arg + "'");
			}
		}

		if (options.dataPath.empty())
		{
			// Worktrees usually have empty data submodules, so the first folder that really holds the UI wins.
			if (const auto root = FindFromKnownRoots([](const std::filesystem::path& p) { return std::filesystem::exists(p / "data/client/Interface/GameUI/GameUI.toc"); }))
			{
				options.dataPath = *root / "data/client";
			}
			else
			{
				options.errors.push_back("Could not find data/client with the UI files, pass --data");
			}
		}

		if (options.toolPath.empty())
		{
			if (const auto root = FindFromKnownRoots([](const std::filesystem::path& p) { return std::filesystem::exists(p / "tools/ui_test/mocks"); }))
			{
				options.toolPath = *root / "tools/ui_test";
			}
			else
			{
				options.errors.push_back("Could not find tools/ui_test, pass --tools");
			}
		}

		if (options.outPath.empty() && !options.toolPath.empty())
		{
			options.outPath = options.toolPath.parent_path().parent_path() / "artifacts/ui_test";
		}

		if (!scenario.empty())
		{
			std::filesystem::path path = scenario;
			if (!path.has_extension() && !options.toolPath.empty())
			{
				path = options.toolPath / "scenarios" / (scenario + ".lua");
			}

			if (!std::filesystem::exists(path))
			{
				options.errors.push_back("Scenario not found: " + path.string());
			}
			options.scenario = std::filesystem::absolute(path);
		}

		return options;
	}
}
