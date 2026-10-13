// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include <thread>

#include "assets/asset_registry.h"
#include "base/crash_handler.h"
#include "base/typedefs.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"

#include "cxxopts/cxxopts.hpp"
#include "graphics/graphics_device.h"
#include "game/object_type_id.h"
#include "nav_build/map.h"
#include "nav_build/mesh_builder.h"
#include "nav_mesh/door_volume.h"
#include "proto_data/project.h"

namespace mmo
{
	/// Collects the footprint of every door spawned on the map whose directory is the world name.
	/// @param project The loaded editor project.
	/// @param worldName The world being built (MapEntry::directory()).
	std::vector<nav::DoorVolume> CollectDoorVolumes(const proto::Project& project, const std::string& worldName)
	{
		std::vector<nav::DoorVolume> doors;

		const proto::MapEntry* map = nullptr;
		for (const auto& entry : project.maps.getTemplates().entry())
		{
			if (entry.directory() == worldName)
			{
				map = &entry;
				break;
			}
		}

		if (!map)
		{
			WLOG("No map in the project uses directory '" << worldName << "' - no doors are baked into the nav mesh");
			return doors;
		}

		std::map<std::string, std::unique_ptr<MapEntity>> meshes;
		for (const auto& spawn : map->objectspawns())
		{
			const auto* object = project.objects.getById(spawn.objectentry());
			if (!object || object->type() != game_world_object_type::Door)
			{
				continue;
			}

			const auto* display = project.objectDisplays.getById(object->displayid());
			if (!display || display->filename().empty())
			{
				WLOG("Door spawn '" << spawn.name() << "' (object " << spawn.objectentry() << ") has no display mesh - skipped");
				continue;
			}

			auto& mesh = meshes[display->filename()];
			if (!mesh)
			{
				mesh = std::make_unique<MapEntity>(display->filename());
			}

			if (mesh->Vertices.empty())
			{
				WLOG("Door mesh '" << display->filename() << "' has no collision - door spawn '" << spawn.name() << "' skipped");
				continue;
			}

			const auto& location = spawn.location();
			const Matrix4 transform = nav::MakeDoorTransform(
				Vector3(location.positionx(), location.positiony(), location.positionz()),
				Quaternion(location.rotationw(), location.rotationx(), location.rotationy(), location.rotationz()),
				object->scale());
			doors.push_back(nav::ComputeDoorVolume(mesh->Bounds, transform));
		}

		ILOG("Baking " << doors.size() << " door(s) into the nav mesh of " << worldName);
		return doors;
	}

	/// Runs the actual navigation builder process multithreaded. This method expects AssetRegistry to be already initialized.
	///	@param worldName The name of the world to build. World will be located in the AssetRegistry using the schema: Worlds/<worldName>/<worldName>.hwld
	///	@param directoryPath The target directory where the built tiles will be stored.
	///	@param concurrentThreads The number of worker threads to use for building the tiles. Must be at least 1.
	///	@param doors Door footprints to cut out as door polys.
	///	@return 0 on success, 1 on failure. This should be thought of as the process exit code.
	int32 run(const std::string& worldName, const std::string& directoryPath, size_t concurrentThreads, std::vector<nav::DoorVolume> doors)
	{
		ASSERT(concurrentThreads > 0);

		// Create a new mesh builder instance. This class is thread safe!
		auto builder = std::make_unique<MeshBuilder>(directoryPath, worldName);
		builder->SetDoorVolumes(std::move(doors));
		ILOG("Building " << builder->GetTileCount() << " tiles for world " << worldName << " using " << concurrentThreads << " threads...");

		std::atomic<bool> success { true };
		std::atomic<uint32> finishedThreads { 0 };

		// Spawn worker threads
		std::vector<std::thread> networkThreads{ concurrentThreads };
		if (concurrentThreads > 0)
		{
			std::generate_n(networkThreads.begin(), concurrentThreads, [&builder, &success, &finishedThreads]() {
				return std::thread{ [&builder, &success, &finishedThreads]
				{
					// Once any tile failed the build is lost, so the other workers stop as well. Every worker
					// counts itself finished on the way out: the progress loop below waits for all of them.
					TileIndex nextTile;
					while (success && builder->GetNextTile(nextTile))
					{
						if (!builder->BuildAndSerializeTerrainTile(nextTile))
						{
							ELOG("Failed building tile " << nextTile.x << "x" << nextTile.y);
							success = false;
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

		// Wait for network threads to finish execution
		for (auto& thread : networkThreads)
		{
			thread.join();
		}

		// A failed build leaves pages unwritten. The .map would list them anyway, so it is not saved at all.
		if (!success)
		{
			ELOG("Building world " << worldName << " failed. The .map was not saved and the nav data in "
				<< directoryPath << " is incomplete: fix the error above and build again");
			return 1;
		}

		ILOG("Saving map...");
		builder->SaveMap();

		ILOG("Finished");

		return 0;
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
	// Scoped: the slot captures locals of main, so it must not outlive main into the static log's destruction.
	const mmo::scoped_connection coutLogConnection{ mmo::g_DefaultLog.signal().connect([&coutLogMutex, &logOptions](const mmo::LogEntry& entry) {
		std::scoped_lock lock{ coutLogMutex };
		printLogEntry(std::cout, entry, logOptions);
		}) };

	// A crash leaves a report with a stack trace in the working directory, which tools/symbolicate_crash.ps1 reads.
	mmo::CrashHandlerConfig crashConfig;
	crashConfig.applicationName = "nav_builder";
	crashConfig.outputDirectory = std::filesystem::current_path();
	mmo::InstallCrashHandler(std::move(crashConfig));

	mmo::String dataDirectory;
	mmo::String worldName;
	mmo::String directoryPath;
	mmo::String projectPath;

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
		("p,project", "editor data folder (e.g. data/editor/data) to read door spawns from", cxxopts::value<std::string>(projectPath))
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

		std::vector<mmo::nav::DoorVolume> doors;
		if (projectPath.empty())
		{
			WLOG("No --project given: doors are not baked into the nav mesh, so closed doors will not block paths");
		}
		else
		{
			mmo::proto::Project project;
			if (!project.load(projectPath))
			{
				ELOG("Failed to load the project from " << projectPath);
				return 1;
			}

			doors = mmo::CollectDoorVolumes(project, worldName);
		}

		// Run the actual tool
		return mmo::run(worldName, directoryPath, concurrentThreads, std::move(doors));
	}
	catch (const cxxopts::OptionException& e)
	{
		// Command line argument parse exception, print it and also the help string
		ELOG(e.what() << "\n");
		ILOG(options.help());
		return 1;
	}
}