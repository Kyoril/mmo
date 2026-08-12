// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "updater/parse_entry.h"
#include "updater/file_entry_handler.h"
#include "updater/prepare_parameters.h"
#include "updater/prepare_progress_handler.h"
// unique_ptr<IUpdateSource> needs the complete type wherever one is destroyed.
#include "updater/update_source.h"
#include "updater/update_list_properties.h"

#include "simple_file_format/sff_load_file.h"

#include <stdexcept>
#include <string>
#include <vector>

using namespace mmo;
using namespace mmo::updating;

namespace
{
	typedef std::string::const_iterator SffIterator;

	struct NullPrepareProgressHandler final : IPrepareProgressHandler
	{
		void beginCheckLocalCopy(const std::string&) override {}
	};

	/// Records what parseEntry dispatched, so a test can assert on the arguments it derived
	/// rather than on whatever the real handlers would go on to do with them.
	struct RecordingEntryHandler final : IFileEntryHandler
	{
		struct DirectoryCall
		{
			std::string type;
			std::string source;
			std::string destination;
			size_t entryCount = 0;
		};

		struct FileCall
		{
			std::string source;
			std::string destination;
			std::uintmax_t originalSize = 0;
			std::string compression;
			std::uintmax_t compressedSize = 0;
			SHA1Hash sha1{};
		};

		std::vector<DirectoryCall> directories;
		std::vector<FileCall> files;

		PreparedUpdate handleDirectory(
			const PrepareParameters&,
			const UpdateListProperties&,
			const sff::read::tree::Array<SffIterator>& entries,
			const std::string& type,
			const std::string& source,
			const std::string& destination) override
		{
			DirectoryCall call;
			call.type = type;
			call.source = source;
			call.destination = destination;
			call.entryCount = entries.getSize();
			directories.push_back(std::move(call));

			return PreparedUpdate();
		}

		PreparedUpdate handleFile(
			const PrepareParameters&,
			const sff::read::tree::Table<SffIterator>&,
			const std::string& source,
			const std::string& destination,
			std::uintmax_t originalSize,
			const SHA1Hash& sha1,
			const std::string& compression,
			std::uintmax_t compressedSize) override
		{
			FileCall call;
			call.source = source;
			call.destination = destination;
			call.originalSize = originalSize;
			call.sha1 = sha1;
			call.compression = compression;
			call.compressedSize = compressedSize;
			files.push_back(std::move(call));

			return PreparedUpdate();
		}

		PreparedUpdate finish(const PrepareParameters&) override
		{
			return PreparedUpdate();
		}
	};

	/// Holds the parsed table together with the string it borrows iterators from -- the
	/// table is non-owning, so the content has to outlive it.
	struct ParsedEntry
	{
		std::string content;
		sff::read::tree::Table<SffIterator> table;

		explicit ParsedEntry(std::string source)
			: content(std::move(source))
		{
			sff::loadTableFromMemory(table, content);
		}
	};

	const char* const ValidSha1Hex = "da39a3ee5e6b4b0d3255bfef95601890afd80709";
}

TEST_CASE("parseEntry dispatches a directory entry", "[updater][parse_entry]")
{
	ParsedEntry entry(R"(type = "directory"
name = "Interface"
entries = {}
)");

	NullPrepareProgressHandler progress;
	const PrepareParameters parameters(nullptr, {}, false, progress);
	UpdateListProperties listProperties;
	listProperties.version = 1;

	RecordingEntryHandler handler;
	parseEntry(parameters, listProperties, entry.table, "srcRoot", "dstRoot", handler);

	REQUIRE(handler.directories.size() == 1);
	CHECK(handler.files.empty());
	CHECK(handler.directories[0].type == "directory");
	CHECK(handler.directories[0].source == "srcRoot/Interface");
	CHECK(handler.directories[0].destination == "dstRoot/Interface");
}

TEST_CASE("parseEntry dispatches a file entry", "[updater][parse_entry]")
{
	ParsedEntry entry(std::string(R"(type = "file"
name = "list.txt"
originalSize = 1234
compression = ""
sha1 = ")") + ValidSha1Hex + "\"\n");

	NullPrepareProgressHandler progress;
	const PrepareParameters parameters(nullptr, {}, false, progress);
	UpdateListProperties listProperties;
	listProperties.version = 1;

	RecordingEntryHandler handler;
	parseEntry(parameters, listProperties, entry.table, "srcRoot", "dstRoot", handler);

	REQUIRE(handler.files.size() == 1);
	CHECK(handler.directories.empty());
	CHECK(handler.files[0].source == "srcRoot/list.txt");
	CHECK(handler.files[0].destination == "dstRoot/list.txt");
	CHECK(handler.files[0].originalSize == 1234);

	// With no compression the compressed size is not read at all; it mirrors the original.
	CHECK(handler.files[0].compression.empty());
	CHECK(handler.files[0].compressedSize == 1234);
}

TEST_CASE("parseEntry reads compressedSize only when a compression is named",
	"[updater][parse_entry]")
{
	ParsedEntry entry(std::string(R"(type = "file"
name = "list.txt"
originalSize = 1234
compression = "zlib"
compressedSize = 400
sha1 = ")") + ValidSha1Hex + "\"\n");

	NullPrepareProgressHandler progress;
	const PrepareParameters parameters(nullptr, {}, false, progress);
	UpdateListProperties listProperties;
	listProperties.version = 1;

	RecordingEntryHandler handler;
	parseEntry(parameters, listProperties, entry.table, "srcRoot", "dstRoot", handler);

	REQUIRE(handler.files.size() == 1);
	CHECK(handler.files[0].compression == "zlib");
	CHECK(handler.files[0].originalSize == 1234);
	CHECK(handler.files[0].compressedSize == 400);
}

TEST_CASE("parseEntry uses compressedName for the source but not the destination",
	"[updater][parse_entry]")
{
	// The download is fetched under its compressed name and lands under its real one.
	// Getting this backwards would write .zlib files into the game directory.
	ParsedEntry entry(std::string(R"(type = "file"
name = "list.txt"
compressedName = "list.txt.zlib"
originalSize = 1234
compression = "zlib"
compressedSize = 400
sha1 = ")") + ValidSha1Hex + "\"\n");

	NullPrepareProgressHandler progress;
	const PrepareParameters parameters(nullptr, {}, false, progress);
	UpdateListProperties listProperties;
	listProperties.version = 1;

	RecordingEntryHandler handler;
	parseEntry(parameters, listProperties, entry.table, "srcRoot", "dstRoot", handler);

	REQUIRE(handler.files.size() == 1);
	CHECK(handler.files[0].source == "srcRoot/list.txt.zlib");
	CHECK(handler.files[0].destination == "dstRoot/list.txt");
}

TEST_CASE("parseEntry reads the size field its list version dictates",
	"[updater][parse_entry]")
{
	SECTION("version 0 reads size")
	{
		ParsedEntry entry(std::string(R"(type = "file"
name = "list.txt"
size = 777
sha1 = ")") + ValidSha1Hex + "\"\n");

		NullPrepareProgressHandler progress;
		const PrepareParameters parameters(nullptr, {}, false, progress);
		UpdateListProperties listProperties;
		listProperties.version = 0;

		RecordingEntryHandler handler;
		parseEntry(parameters, listProperties, entry.table, "src", "dst", handler);

		REQUIRE(handler.files.size() == 1);
		CHECK(handler.files[0].originalSize == 777);
	}

	SECTION("version 1 reads originalSize and ignores size")
	{
		ParsedEntry entry(std::string(R"(type = "file"
name = "list.txt"
size = 111
originalSize = 777
compression = ""
sha1 = ")") + ValidSha1Hex + "\"\n");

		NullPrepareProgressHandler progress;
		const PrepareParameters parameters(nullptr, {}, false, progress);
		UpdateListProperties listProperties;
		listProperties.version = 1;

		RecordingEntryHandler handler;
		parseEntry(parameters, listProperties, entry.table, "src", "dst", handler);

		REQUIRE(handler.files.size() == 1);
		CHECK(handler.files[0].originalSize == 777);
	}
}

TEST_CASE("parseEntry follows an if entry whose condition is set", "[updater][parse_entry]")
{
	ParsedEntry entry(R"(type = "if"
condition = "windows"
value =
(
	type = "directory"
	name = "Win"
	entries = {}
)
)");

	NullPrepareProgressHandler progress;
	const PrepareParameters parameters(nullptr, { "windows" }, false, progress);
	UpdateListProperties listProperties;
	listProperties.version = 1;

	RecordingEntryHandler handler;
	parseEntry(parameters, listProperties, entry.table, "srcRoot", "dstRoot", handler);

	REQUIRE(handler.directories.size() == 1);
	CHECK(handler.directories[0].destination == "dstRoot/Win");
}

TEST_CASE("parseEntry skips an if entry whose condition is not set",
	"[updater][parse_entry]")
{
	ParsedEntry entry(R"(type = "if"
condition = "macos"
value =
(
	type = "directory"
	name = "Mac"
	entries = {}
)
)");

	NullPrepareProgressHandler progress;
	const PrepareParameters parameters(nullptr, { "windows" }, false, progress);
	UpdateListProperties listProperties;
	listProperties.version = 1;

	RecordingEntryHandler handler;
	const PreparedUpdate result =
		parseEntry(parameters, listProperties, entry.table, "srcRoot", "dstRoot", handler);

	// Nothing is dispatched and nothing is prepared -- the branch is simply not taken.
	CHECK(handler.directories.empty());
	CHECK(handler.files.empty());
	CHECK(result.steps.empty());
}

TEST_CASE("parseEntry rejects a file entry missing its size", "[updater][parse_entry]")
{
	ParsedEntry entry(std::string(R"(type = "file"
name = "list.txt"
compression = ""
sha1 = ")") + ValidSha1Hex + "\"\n");

	NullPrepareProgressHandler progress;
	const PrepareParameters parameters(nullptr, {}, false, progress);
	UpdateListProperties listProperties;
	listProperties.version = 1;

	RecordingEntryHandler handler;
	CHECK_THROWS_AS(
		parseEntry(parameters, listProperties, entry.table, "src", "dst", handler),
		std::runtime_error);
}

TEST_CASE("parseEntry rejects a file entry missing its digest", "[updater][parse_entry]")
{
	ParsedEntry entry(R"(type = "file"
name = "list.txt"
originalSize = 1234
compression = ""
)");

	NullPrepareProgressHandler progress;
	const PrepareParameters parameters(nullptr, {}, false, progress);
	UpdateListProperties listProperties;
	listProperties.version = 1;

	RecordingEntryHandler handler;
	CHECK_THROWS_AS(
		parseEntry(parameters, listProperties, entry.table, "src", "dst", handler),
		std::runtime_error);
}

TEST_CASE("parseEntry rejects an if entry with no value", "[updater][parse_entry]")
{
	ParsedEntry entry(R"(type = "if"
condition = "windows"
)");

	NullPrepareProgressHandler progress;
	const PrepareParameters parameters(nullptr, { "windows" }, false, progress);
	UpdateListProperties listProperties;
	listProperties.version = 1;

	RecordingEntryHandler handler;
	CHECK_THROWS_AS(
		parseEntry(parameters, listProperties, entry.table, "src", "dst", handler),
		std::runtime_error);
}
