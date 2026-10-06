// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "update_compilation/compile_directory.h"
#include "virtual_dir/file_system_reader.h"
#include "virtual_dir/file_system_writer.h"

#include "zstr/zstr.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace mmo;

namespace
{
	const std::string DirectoryPrefix = "update-compilation-test-";

	struct TempDirectory
	{
		std::filesystem::path path =
			std::filesystem::temp_directory_path() /
			(DirectoryPrefix + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

		~TempDirectory()
		{
			std::error_code error;
			const auto target = std::filesystem::absolute(path, error).lexically_normal();
			const auto root = std::filesystem::temp_directory_path().lexically_normal();
			if (!error && target.parent_path() == root && target.filename().string().find(DirectoryPrefix) == 0)
			{
				std::filesystem::remove_all(target, error);
			}
		}
	};

	void writeFile(const std::filesystem::path &path, const std::string &content)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file(path, std::ios::binary);
		file << content;
	}

	std::string readFile(const std::filesystem::path &path)
	{
		std::ifstream file(path, std::ios::binary);
		std::ostringstream buffer;
		buffer << file.rdbuf();
		return buffer.str();
	}

	/// Builds a source tree shaped like the live patch: loose executables behind a platform
	/// condition and data folders packed into hpak archives.
	void createSourceTree(const std::filesystem::path &root)
	{
		writeFile(root / "source.txt",
			"version = 0\n"
			"root = (type = \"fs\", from = \".\", entries =\n"
			"{\n"
			"\t(type = \"if\", condition = \"WINDOWS\", value = (type = \"fs\", from = \"bin/Game.exe\", to = \"Game.exe\"))\n"
			"\t(type = \"fs\", from = \"Data\", entries =\n"
			"\t{\n"
			"\t\t(type = \"hpak2\", from = \".\", to = \"Textures.hpak\", entries = { (type = \"fs\", from = \"Textures\") })\n"
			"\t\t(type = \"fs\", from = \"Locales\")\n"
			"\t})\n"
			"})\n");

		writeFile(root / "bin" / "Game.exe", "hello");
		writeFile(root / "Data" / "Locales" / "enUS" / "Localization.txt", "KEY = \"Value\"\n");

		for (int folder = 0; folder < 8; ++folder)
		{
			for (int file = 0; file < 40; ++file)
			{
				std::string content(static_cast<size_t>(file * 997 + folder * 31), static_cast<char>('a' + file % 26));
				content += std::to_string(folder) + "/" + std::to_string(file);

				writeFile(
					root / "Data" / "Textures" / ("Folder" + std::to_string(folder)) / ("Texture" + std::to_string(file) + ".htex"),
					content);
			}
		}
	}

	void compile(const std::filesystem::path &source, const std::filesystem::path &output, unsigned threadCount)
	{
		virtual_dir::FileSystemReader reader(source);
		virtual_dir::FileSystemWriter writer(output);
		updating::compileDirectory(reader, writer, true, threadCount);
	}
}

TEST_CASE("Compiling with many threads produces the same update as a single thread", "[update_compilation]")
{
	TempDirectory temp;
	createSourceTree(temp.path / "source");

	compile(temp.path / "source", temp.path / "single", 1);
	compile(temp.path / "source", temp.path / "parallel", 8);

	const auto singleList = readFile(temp.path / "single" / "list.txt");
	REQUIRE_FALSE(singleList.empty());
	CHECK(readFile(temp.path / "parallel" / "list.txt") == singleList);

	size_t compressedFiles = 0;
	for (const auto &entry : std::filesystem::recursive_directory_iterator(temp.path / "single"))
	{
		if (!entry.is_regular_file())
		{
			continue;
		}

		const auto relative = std::filesystem::relative(entry.path(), temp.path / "single");
		INFO(relative.string());
		CHECK(readFile(temp.path / "parallel" / relative) == readFile(entry.path()));

		if (entry.path().extension() == ".z")
		{
			++compressedFiles;
		}
	}

	CHECK(compressedFiles == 8 * 40 + 2);
}

TEST_CASE("Compiled files carry the source size and hash and decompress to the source", "[update_compilation]")
{
	TempDirectory temp;
	createSourceTree(temp.path / "source");
	compile(temp.path / "source", temp.path / "output", 4);

	const auto list = readFile(temp.path / "output" / "list.txt");
	CHECK(list.find("name = \"Game.exe\", compressedName = \"Game.exe.z\", originalSize = 5, "
		"sha1 = \"aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d\", compression = \"zlib\"") != std::string::npos);
	CHECK(list.find("(type = \"hpak2\", name = \"Textures.hpak\"") != std::string::npos);

	const auto compressedPath = temp.path / "output" / "Data" / "Textures.hpak" / "Textures" / "Folder3" / "Texture7.htex.z";
	REQUIRE(std::filesystem::exists(compressedPath));

	std::ifstream compressedFile(compressedPath, std::ios::binary);
	zstr::istream decompressed(compressedFile);
	std::ostringstream content;
	content << decompressed.rdbuf();

	CHECK(content.str() == readFile(temp.path / "source" / "Data" / "Textures" / "Folder3" / "Texture7.htex"));
	CHECK(list.find("compressedSize = " + std::to_string(std::filesystem::file_size(compressedPath))) != std::string::npos);
}
