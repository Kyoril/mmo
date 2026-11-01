// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "updater/file_system_entry_handler.h"
#include "updater/prepare_parameters.h"
#include "updater/prepare_progress_handler.h"
// unique_ptr<IUpdateSource> needs the complete type wherever one is destroyed.
#include "updater/update_source.h"

#include "base/sha1.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace mmo;
using namespace mmo::updating;

#ifndef _WIN32
namespace
{
	struct NullPrepareProgressHandler final : IPrepareProgressHandler
	{
		void beginCheckLocalCopy(const std::string&) override {}
	};

	/// A scratch game directory holding one up-to-date file whose execute bits are cleared,
	/// the state launchers before 1.0.3 left macOS bundle executables in.
	struct LocalCopyFixture
	{
		std::filesystem::path root;
		const std::string content = "not really a Mach-O";

		LocalCopyFixture()
		{
			root = std::filesystem::temp_directory_path() / ("mmo_updater_exec_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(root);
		}

		~LocalCopyFixture()
		{
			std::error_code error;
			std::filesystem::remove_all(root, error);
		}

		std::filesystem::path WriteNonExecutable(const std::filesystem::path& relative) const
		{
			const auto path = root / relative;
			std::filesystem::create_directories(path.parent_path());
			{
				std::ofstream file(path, std::ios::binary);
				file << content;
			}
			std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
			return path;
		}

		PreparedUpdate PrepareUpToDate(const std::filesystem::path& path) const
		{
			NullPrepareProgressHandler progress;
			const PrepareParameters parameters(nullptr, {}, false, progress);
			const sff::read::tree::Table<std::string::const_iterator> description;

			FileSystemEntryHandler handler;
			return handler.handleFile(parameters, description, "source", path.string(), content.size(),
				sha1(content.data(), content.size()), "", content.size());
		}
	};

	bool IsOwnerExecutable(const std::filesystem::path& path)
	{
		return (std::filesystem::status(path).permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
	}
}

TEST_CASE("An up-to-date bundle executable without its execute bit is repaired", "[updater][file_system_entry_handler]")
{
	const LocalCopyFixture fixture;
	const auto path = fixture.WriteNonExecutable("mmo_client.app/Contents/MacOS/mmo_client");

	const PreparedUpdate update = fixture.PrepareUpToDate(path);

	CHECK(update.steps.empty());
	CHECK(IsOwnerExecutable(path));
}

TEST_CASE("An up-to-date file outside a bundle keeps its permissions", "[updater][file_system_entry_handler]")
{
	const LocalCopyFixture fixture;
	const auto path = fixture.WriteNonExecutable("Data/config.txt");

	const PreparedUpdate update = fixture.PrepareUpToDate(path);

	CHECK(update.steps.empty());
	CHECK_FALSE(IsOwnerExecutable(path));
}
#endif
