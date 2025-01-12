// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include <thread>

#include "assets/asset_registry.h"
#include "base/typedefs.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"

#include "cxxopts/cxxopts.hpp"
#include "graphics/graphics_device.h"
#include "nav_build/mesh_builder.h"

namespace mmo
{
	/// Runs the actual navigation builder process multithreaded. This method expects AssetRegistry to be already initialized.
	///	@param worldName The name of the world to build. World will be located in the AssetRegistry using the schema: Worlds/<worldName>/<worldName>.hwld
	///	@param directoryPath The target directory where the built tiles will be stored.
	///	@param concurrentThreads The number of worker threads to use for building the tiles. Must be at least 1.
	///	@return 0 on success, 1 on failure. This should be thought of as the process exit code.
	int32 run(const std::string& worldName, const std::string& directoryPath, size_t concurrentThreads)
	{
		ASSERT(concurrentThreads > 0);

		// Create a new mesh builder instance. This class is thread safe!
		auto builder = std::make_unique<MeshBuilder>(directoryPath, worldName);
		ILOG("Building " << builder->GetTileCount() << " tiles for world " << worldName << " using " << concurrentThreads << " thrads...");

		volatile bool success = true;
		volatile std::atomic<uint32> finishedThreads = 0;

		// Spawn worker threads
		std::vector<std::thread> networkThreads{ concurrentThreads };
		if (concurrentThreads > 0)
		{
			std::generate_n(networkThreads.begin(), concurrentThreads, [&builder, &success, &finishedThreads]() {
				return std::thread{ [&builder, &success, &finishedThreads]
				{
					TileIndex nextTile;
					while (builder->GetNextTile(nextTile))
					{
						if (!builder->BuildAndSerializeTerrainTile(nextTile))
						{
							ELOG("Failed building tile " << nextTile.x << "x" << nextTile.y);
							success = false;
							return;
						}
					}

					++finishedThreads;
				} };
				});
		}

		// Get current timestamp
		auto lastReport = std::chrono::high_resolution_clock::now();
		while (finishedThreads < networkThreads.size())
		{
			// Wait a second
			std::this_thread::sleep_for(std::chrono::seconds(1));

			// If last report time is more than 5 seconds ago, print progress
			if (auto now = std::chrono::high_resolution_clock::now(); now > lastReport + std::chrono::seconds(5))
			{
				ILOG(builder->PercentComplete() << "% complete");
				lastReport = now;
			}
		}

		ILOG("Saving map...");
		builder->SaveMap();

		ILOG("Finished");
		
		// Wait for network threads to finish execution
		for (auto& thread : networkThreads)
		{
			thread.join();
		}

		return success ? 0 : 1;
	}
}

/// Entry point of the navigation builder console tool.
///	@param argc The number of command line arguments.
///	@param argv The command line arguments.
///	@return 0 on success, anything else on error.
int main(int argc, char* argv[])
{
	auto logOptions = mmo::g_DefaultConsoleLogOptions;

	// Add cout to the list of log output streams and make logging thread safe, so we don't write garbage to the console window
	std::mutex coutLogMutex;
	mmo::g_DefaultLog.signal().connect([&coutLogMutex, &logOptions](const mmo::LogEntry& entry) {
		std::scoped_lock lock{ coutLogMutex };
		printLogEntry(std::cout, entry, logOptions);
		});

	mmo::String dataDirectory;
	mmo::String worldName;
	mmo::String directoryPath;

	// Per default, use all available threads
	size_t concurrentThreads = std::thread::hardware_concurrency();

	// TODO: This is ugly! Let's remove this dependency. Right now this is needed because we deserialize Mesh Files which will also try to load
	// materials, which in turn will try to load referenced textures and shaders and stuff. So by initializing the NullGraphicsDevice, we prevent
	// this from crashing and not actually load textures and shaders and stuff.
	mmo::GraphicsDevice::CreateNull({});

	// Prepare available command line options
	cxxopts::Options options("Navigation Builder, available options");
	options.add_options()
		("help", "produce help message")
		("d,data", "set data directory path", cxxopts::value<std::string>(dataDirectory))
		("w,world", "sets world name to build", cxxopts::value<std::string>(worldName))
		("o,out", "set target directory", cxxopts::value<std::string>(directoryPath))
		("j,concurrency", "The number of threads used for building", cxxopts::value<size_t>(concurrentThreads))
		;

	// Add positional parameters to allow for something like this:
	//	nav_builder.exe world dir
	options.parse_positional({ "world", "dir" });

	// Catch exceptions from command line argument parsing. This is a huge try-block because
	// the cxxopts interface has no default constructor for parse results, thus the call to
	// parse needs to stay valid this whole block.
	try
	{
		// Parse command line arguments
		cxxopts::ParseResult result = options.parse(argc, argv);

		// Check for help output
		if (result.count("help"))
		{
			ILOG(options.help());
		}

		// We always want at least a single worker thread
		if (concurrentThreads == 0) concurrentThreads = 1;

		// Initialize AssetRegistry at the given data directory.
		std::vector<std::string> archives;
		mmo::AssetRegistry::Initialize(dataDirectory, archives);

		// Run the actual tool
		return mmo::run(worldName, directoryPath, concurrentThreads);
	}
	catch (const cxxopts::OptionException& e)
	{
		// Command line argument parse exception, print it and also the help string
		ELOG(e.what() << "\n");
		ILOG(options.help());
		return 1;
	}
}