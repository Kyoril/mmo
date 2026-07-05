// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "export_command.h"
#include "import_command.h"
#include "preview_command.h"

#include "assets/asset_registry.h"
#include "base/typedefs.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"

#include "cxxopts/cxxopts.hpp"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <mutex>

namespace mmo
{
	namespace
	{
		bool ParsePageRect(const String &value, int32 &x0, int32 &z0, int32 &x1, int32 &z1)
		{
			if (std::sscanf(value.c_str(), "%d,%d,%d,%d", &x0, &z0, &x1, &z1) != 4)
			{
				ELOG("Invalid page rect '" << value << "', expected format: x0,z0,x1,z1 (e.g. 30,30,33,33)");
				return false;
			}

			return true;
		}

		void PrintUsage()
		{
			ILOG("Usage: terrain_tool <import|export|preview> [options]");
			ILOG("");
			ILOG("  import  --data <dir> --heightmap <png> --meta <json> [--world <name>] [--material <asset>]");
			ILOG("          Converts a 16-bit grayscale heightmap into terrain page (.tile) files.");
			ILOG("  export  --data <dir> --world <name> --pages x0,z0,x1,z1 --out <png> [--meta-out <json>]");
			ILOG("          Exports terrain pages back into a heightmap image plus metadata sidecar.");
			ILOG("  preview --data <dir> --world <name> --pages x0,z0,x1,z1 --out <png> [--contours <units>] [--scale <n>]");
			ILOG("          Renders a shaded relief preview from the stored terrain pages.");
		}
	}
}

/// Entry point of the terrain tool.
///	@param argc The number of command line arguments.
///	@param argv The command line arguments.
///	@return 0 on success, anything else on error.
int main(int argc, char *argv[])
{
	auto logOptions = mmo::g_DefaultConsoleLogOptions;

	std::mutex coutLogMutex;
	mmo::g_DefaultLog.signal().connect([&coutLogMutex, &logOptions](const mmo::LogEntry &entry) {
		std::scoped_lock lock{ coutLogMutex };
		printLogEntry(std::cout, entry, logOptions);
		});

	if (argc < 2)
	{
		mmo::PrintUsage();
		return 1;
	}

	const mmo::String command = argv[1];
	if (command != "import" && command != "export" && command != "preview")
	{
		mmo::PrintUsage();
		return command == "help" || command == "--help" ? 0 : 1;
	}

	mmo::String dataDirectory;
	mmo::String world;
	mmo::String heightmapPath;
	mmo::String metaPath;
	mmo::String material;
	mmo::String pageRect;
	mmo::String outPath;
	mmo::String metaOutPath;
	mmo::String waterMaterial;
	float waterLevel = std::numeric_limits<float>::quiet_NaN();
	float contourInterval = 0.0f;
	mmo::uint32 scale = 1;

	cxxopts::Options options("terrain_tool " + command, "Terrain page import/export tool");
	options.add_options()
		("help", "produce help message")
		("d,data", "path of the client data directory (e.g. H:/mmo/data/client)", cxxopts::value<std::string>(dataDirectory))
		("w,world", "world name", cxxopts::value<std::string>(world))
		("heightmap", "path of the 16-bit grayscale heightmap PNG to import", cxxopts::value<std::string>(heightmapPath))
		("m,meta", "path of the zone metadata JSON", cxxopts::value<std::string>(metaPath))
		("material", "per-tile material asset path override", cxxopts::value<std::string>(material))
		("water-level", "flag water quads below this world height (overrides metadata)", cxxopts::value<float>(waterLevel))
		("water-material", "water surface material asset path", cxxopts::value<std::string>(waterMaterial))
		("p,pages", "page rect as x0,z0,x1,z1 (inclusive)", cxxopts::value<std::string>(pageRect))
		("o,out", "output file path", cxxopts::value<std::string>(outPath))
		("meta-out", "output metadata JSON path (default: output path with .json extension)", cxxopts::value<std::string>(metaOutPath))
		("contours", "contour line interval in world units (0 = off)", cxxopts::value<float>(contourInterval))
		("s,scale", "integer upscale factor for preview output", cxxopts::value<mmo::uint32>(scale))
		;

	try
	{
		// Skip the subcommand for option parsing
		int commandArgc = argc - 1;
		char **commandArgv = argv + 1;
		cxxopts::ParseResult result = options.parse(commandArgc, commandArgv);

		if (result.count("help"))
		{
			ILOG(options.help());
			return 0;
		}

		if (dataDirectory.empty())
		{
			ELOG("Missing --data option (path of the client data directory)");
			return 1;
		}

		std::vector<std::string> archives;
		mmo::AssetRegistry::Initialize(dataDirectory, archives);

		if (command == "import")
		{
			if (heightmapPath.empty() || metaPath.empty())
			{
				ELOG("import requires --heightmap and --meta");
				return 1;
			}

			mmo::ImportArgs args;
			args.world = world;
			args.heightmapPath = heightmapPath;
			args.metaPath = metaPath;
			args.materialOverride = material;
			args.waterLevelOverride = waterLevel;
			args.waterMaterialOverride = waterMaterial;
			return mmo::RunImport(args);
		}

		if (world.empty() || pageRect.empty() || outPath.empty())
		{
			ELOG(command << " requires --world, --pages and --out");
			return 1;
		}

		mmo::int32 x0, z0, x1, z1;
		if (!mmo::ParsePageRect(pageRect, x0, z0, x1, z1))
		{
			return 1;
		}

		if (command == "export")
		{
			mmo::ExportArgs args;
			args.world = world;
			args.pageX0 = x0; args.pageZ0 = z0; args.pageX1 = x1; args.pageZ1 = z1;
			args.outPath = outPath;
			args.metaOutPath = metaOutPath.empty() ? std::filesystem::path(outPath).replace_extension(".json") : std::filesystem::path(metaOutPath);
			return mmo::RunExport(args);
		}

		mmo::PreviewArgs args;
		args.world = world;
		args.pageX0 = x0; args.pageZ0 = z0; args.pageX1 = x1; args.pageZ1 = z1;
		args.outPath = outPath;
		args.contourInterval = contourInterval;
		args.scale = std::max<mmo::uint32>(1, scale);
		return mmo::RunPreview(args);
	}
	catch (const cxxopts::OptionException &e)
	{
		ELOG(e.what());
		ILOG(options.help());
		return 1;
	}
}
