// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Cooks client content for a release.
//
//   mmo_cook shaders --data <dir> --platform D3D11|METAL --out <dir> [--github]
//       Builds one platform's material shaders into shader packs (D3D11 on Windows, METAL on macOS).
//   mmo_cook assemble --data <dir> --packs <dir>... --out <dir> [--github]
//       Merges the packs of all platforms into the materials and writes them as an overlay, with
//       overlay.json, for the deployer to lay over the data checkout.
//   mmo_cook all --data <dir> --out <dir> [--github]
//       Cooks the current platform and writes a full cooked copy of the data directory.
//
// Exit code 0 even with warnings, 1 on errors.

#include "assets/asset_registry.h"
#include "content_cook/cook_report.h"
#include "content_cook/material_assembly.h"
#include "content_cook/shader_cook.h"
#include "graphics/shader_compiler.h"
#include "graphics/shader_platform.h"
#include "log/default_log.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"

#ifdef _WIN32
#	include "graphics_d3d11/shader_compiler_d3d11.h"
#endif
#if defined(__APPLE__) && defined(MMO_COOK_WITH_METAL)
#	include "msl_cross/shader_compiler_metal.h"
#endif

#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace
{
	using namespace mmo;

	struct Options
	{
		String command;
		std::filesystem::path data;
		std::filesystem::path out;
		String platform;
		std::vector<std::filesystem::path> packs;
		bool github { false };
	};

	void PrintUsage()
	{
		std::cerr
			<< "Usage:\n"
			<< "  mmo_cook shaders --data <dir> --platform D3D11|METAL --out <dir> [--github]\n"
			<< "  mmo_cook assemble --data <dir> --packs <dir>... --out <dir> [--github]\n"
			<< "  mmo_cook all --data <dir> --out <dir> [--github]\n";
	}

	bool ParseOptions(const int argc, char** argv, Options& options)
	{
		if (argc < 2)
		{
			return false;
		}

		options.command = argv[1];
		for (int i = 2; i < argc; ++i)
		{
			const String argument = argv[i];
			const bool hasValue = i + 1 < argc && argv[i + 1][0] != '-';
			if (argument == "--github")
			{
				options.github = true;
			}
			else if (argument == "--data" && hasValue)
			{
				options.data = argv[++i];
			}
			else if (argument == "--out" && hasValue)
			{
				options.out = argv[++i];
			}
			else if (argument == "--platform" && hasValue)
			{
				options.platform = argv[++i];
			}
			else if (argument == "--packs" && hasValue)
			{
				while (i + 1 < argc && argv[i + 1][0] != '-')
				{
					options.packs.emplace_back(argv[++i]);
				}
			}
			else
			{
				std::cerr << "Unknown or incomplete argument: " << argument << "\n";
				return false;
			}
		}

		if (options.data.empty() || options.out.empty())
		{
			return false;
		}

		if (options.command == "shaders")
		{
			return !options.platform.empty();
		}
		if (options.command == "assemble")
		{
			return !options.packs.empty();
		}
		return options.command == "all";
	}

	/// Creates the compiler of a shader platform, if this build can compile it.
	std::unique_ptr<ShaderCompiler> CreateCompiler(const std::string_view platform)
	{
#ifdef _WIN32
		if (platform == shader_platform::D3D11)
		{
			return std::make_unique<ShaderCompilerD3D11>();
		}
#endif
#if defined(__APPLE__) && defined(MMO_COOK_WITH_METAL)
		if (platform == shader_platform::Metal)
		{
			return std::make_unique<ShaderCompilerMetal>(ShaderCompilerMetal::Output::Library);
		}
#endif
		(void)platform;
		return nullptr;
	}

	/// Copies a data directory without its Cache folder.
	bool CopyData(const std::filesystem::path& from, const std::filesystem::path& to)
	{
		std::error_code error;
		std::filesystem::create_directories(to, error);
		for (const auto& entry : std::filesystem::directory_iterator(from, error))
		{
			const auto name = entry.path().filename();
			if (name == "Cache" || name.string().front() == '.')
			{
				continue;
			}

			std::filesystem::copy(entry.path(), to / name, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, error);
			if (error)
			{
				ELOG("Unable to copy " << entry.path() << ": " << error.message());
				return false;
			}
		}
		return !error;
	}

	int Run(const Options& options)
	{
		if (!std::filesystem::is_directory(options.data))
		{
			ELOG("Data directory " << options.data << " does not exist");
			return 1;
		}

		// Material functions are read through the asset registry.
		AssetRegistry::Initialize(options.data, {});

		CookReport report;
		bool succeeded = true;

		if (options.command == "shaders")
		{
			const auto compiler = CreateCompiler(options.platform);
			if (!compiler)
			{
				ELOG("This build of mmo_cook can't compile shaders for platform " << options.platform);
				AssetRegistry::Destroy();
				return 1;
			}

			succeeded = CookShaders(options.data, *compiler, options.out, report);
		}
		else if (options.command == "assemble")
		{
			succeeded = AssembleMaterials(options.data, options.packs, options.out, report);
		}
		else
		{
			const auto compiler = CreateCompiler(shader_platform::GetCurrentPlatform());
			if (!compiler)
			{
				ELOG("This build of mmo_cook can't compile shaders for this platform");
				AssetRegistry::Destroy();
				return 1;
			}

			std::filesystem::path packs = options.out;
			packs += ".packs";
			succeeded = CookShaders(options.data, *compiler, packs, report)
				&& CopyData(options.data, options.out);

			CookReport assemblyReport;
			succeeded = succeeded && AssembleMaterials(options.data, { packs }, options.out, assemblyReport);

			std::error_code error;
			std::filesystem::remove_all(packs, error);
			std::filesystem::remove(options.out / "overlay.json", error);
		}

		AssetRegistry::Destroy();

		ILOG("Cook finished with " << report.GetSortedWarnings().size() << " warning(s)");
		if (options.github)
		{
			report.WriteGithubAnnotations(std::cout);
		}

		return succeeded ? 0 : 1;
	}
}

int main(const int argc, char** argv)
{
	std::mutex logMutex;
	mmo::g_DefaultLog.signal().connect([&logMutex](const mmo::LogEntry& entry)
	{
		std::scoped_lock lock{ logMutex };
		printLogEntry(std::cout, entry, mmo::g_DefaultConsoleLogOptions);
	});

	Options options;
	if (!ParseOptions(argc, argv, options))
	{
		PrintUsage();
		return 1;
	}

	return Run(options);
}
