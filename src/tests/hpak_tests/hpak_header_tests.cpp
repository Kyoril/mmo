// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "hpak/magic.h"
#include "hpak/pre_header.h"
#include "hpak/pre_header_load.h"
#include "hpak/pre_header_save.h"
#include "hpak_v1_0/header.h"
#include "hpak_v1_0/header_load.h"
#include "hpak_v1_0/header_save.h"

#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"

#include <cstring>
#include <string>
#include <vector>

using namespace mmo;
using namespace mmo::hpak;

namespace
{
	SHA1Hash MakeDigest(uint8 seed)
	{
		SHA1Hash digest;
		for (size_t i = 0; i < digest.size(); ++i)
		{
			digest[i] = static_cast<uint8>(seed + i);
		}

		return digest;
	}
}

TEST_CASE("hpak pre-header round-trips", "[hpak][pre_header]")
{
	std::vector<char> buffer;
	{
		io::VectorSink sink(buffer);
		io::Writer writer(sink);
		savePreHeader(PreHeader(Version_1_0), writer);
	}

	REQUIRE(buffer.size() == 8);
	CHECK(buffer[0] == 'H');
	CHECK(buffer[1] == 'P');
	CHECK(buffer[2] == 'A');
	CHECK(buffer[3] == 'K');

	io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
	io::Reader reader(source);

	PreHeader preHeader;
	REQUIRE(loadPreHeader(preHeader, reader));
	CHECK(preHeader.version == Version_1_0);
}

TEST_CASE("hpak pre-header rejects a foreign magic", "[hpak][pre_header]")
{
	// An .htex fed to the archive loader has to be refused rather than misread.
	const std::string raw("HTEX\x00\x01\x00\x00", 8);

	io::MemorySource source(raw.data(), raw.data() + raw.size());
	io::Reader reader(source);

	PreHeader preHeader;
	CHECK_FALSE(loadPreHeader(preHeader, reader));
}

TEST_CASE("hpak v1.0 header round-trips an empty archive", "[hpak][header]")
{
	std::vector<char> buffer;
	{
		io::VectorSink sink(buffer);
		v1_0::HeaderSaver saver(sink);
		saver.finish(0);
	}

	io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
	io::Reader reader(source);

	PreHeader preHeader;
	REQUIRE(loadPreHeader(preHeader, reader));

	v1_0::Header header(Version_1_0);
	REQUIRE(loadHeader(header, reader));
	CHECK(header.files.empty());
}

TEST_CASE("hpak v1.0 header round-trips multiple file entries", "[hpak][header]")
{
	std::vector<char> buffer;
	{
		io::VectorSink sink(buffer);
		v1_0::HeaderSaver saver(sink);

		{
			v1_0::FileEntrySaver entry(sink, "Interface/Button.htex", v1_0::CompressionType::ZLibCompressed);
			entry.finish(4096, 512, 2048, MakeDigest(1));
		}
		{
			v1_0::FileEntrySaver entry(sink, "Models/Tree.hmsh", v1_0::CompressionType::NotCompressed);
			entry.finish(8192, 1024, 1024, MakeDigest(100));
		}

		saver.finish(2);
	}

	io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
	io::Reader reader(source);

	PreHeader preHeader;
	REQUIRE(loadPreHeader(preHeader, reader));

	v1_0::Header header(Version_1_0);
	REQUIRE(loadHeader(header, reader));

	REQUIRE(header.files.size() == 2);

	CHECK(header.files[0].name == "Interface/Button.htex");
	CHECK(header.files[0].compression == v1_0::CompressionType::ZLibCompressed);
	CHECK(header.files[0].contentOffset == 4096);
	CHECK(header.files[0].size == 512);
	CHECK(header.files[0].originalSize == 2048);
	CHECK(header.files[0].digest == MakeDigest(1));

	CHECK(header.files[1].name == "Models/Tree.hmsh");
	CHECK(header.files[1].compression == v1_0::CompressionType::NotCompressed);
	CHECK(header.files[1].contentOffset == 8192);
	CHECK(header.files[1].size == 1024);
	CHECK(header.files[1].originalSize == 1024);
	CHECK(header.files[1].digest == MakeDigest(100));
}

TEST_CASE("hpak v1.0 entry sizes survive values beyond 32 bits", "[hpak][header]")
{
	// Offsets and sizes are uint64 on purpose -- an archive can exceed 4 GiB. A narrowing
	// change would show up here and nowhere else until someone built a large pack.
	const uint64 largeOffset = 5ull * 1024 * 1024 * 1024;
	const uint64 largeSize = 3ull * 1024 * 1024 * 1024;

	std::vector<char> buffer;
	{
		io::VectorSink sink(buffer);
		v1_0::HeaderSaver saver(sink);

		{
			v1_0::FileEntrySaver entry(sink, "Big.dat", v1_0::CompressionType::NotCompressed);
			entry.finish(largeOffset, largeSize, largeSize, MakeDigest(7));
		}

		saver.finish(1);
	}

	io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
	io::Reader reader(source);

	PreHeader preHeader;
	REQUIRE(loadPreHeader(preHeader, reader));

	v1_0::Header header(Version_1_0);
	REQUIRE(loadHeader(header, reader));

	REQUIRE(header.files.size() == 1);
	CHECK(header.files[0].contentOffset == largeOffset);
	CHECK(header.files[0].size == largeSize);
}

TEST_CASE("hpak v1.0 header finish patches the file count written as a placeholder",
	"[hpak][header]")
{
	// The count is written as zero up front and rewritten by finish(), because the number
	// of entries is not known until they have all been appended.
	std::vector<char> buffer;
	{
		io::VectorSink sink(buffer);
		v1_0::HeaderSaver saver(sink);

		v1_0::FileEntrySaver entry(sink, "Only.dat", v1_0::CompressionType::NotCompressed);
		entry.finish(0, 0, 0, MakeDigest(0));

		saver.finish(1);
	}

	// The count lives immediately after the 8-byte pre-header.
	REQUIRE(buffer.size() > 12);
	uint32 storedCount = 0;
	std::memcpy(&storedCount, buffer.data() + 8, sizeof(storedCount));
	CHECK(storedCount == 1);
}

TEST_CASE("hpak v1.0 header stops when an entry is truncated", "[hpak][header]")
{
	std::vector<char> buffer;
	{
		io::VectorSink sink(buffer);
		v1_0::HeaderSaver saver(sink);

		v1_0::FileEntrySaver entry(sink, "Truncated.dat", v1_0::CompressionType::NotCompressed);
		entry.finish(1, 2, 3, MakeDigest(9));

		saver.finish(1);
	}

	buffer.resize(buffer.size() - 10);

	io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
	io::Reader reader(source);

	PreHeader preHeader;
	REQUIRE(loadPreHeader(preHeader, reader));

	v1_0::Header header(Version_1_0);
	CHECK_FALSE(loadHeader(header, reader));
}

TEST_CASE("hpak v1.0 header round-trips an empty file name", "[hpak][header]")
{
	// Names are length-prefixed, so a zero-length one is representable and must not be
	// mistaken for the end of the table.
	std::vector<char> buffer;
	{
		io::VectorSink sink(buffer);
		v1_0::HeaderSaver saver(sink);

		{
			v1_0::FileEntrySaver entry(sink, "", v1_0::CompressionType::NotCompressed);
			entry.finish(1, 2, 3, MakeDigest(4));
		}
		{
			v1_0::FileEntrySaver entry(sink, "After.dat", v1_0::CompressionType::NotCompressed);
			entry.finish(5, 6, 7, MakeDigest(8));
		}

		saver.finish(2);
	}

	io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
	io::Reader reader(source);

	PreHeader preHeader;
	REQUIRE(loadPreHeader(preHeader, reader));

	v1_0::Header header(Version_1_0);
	REQUIRE(loadHeader(header, reader));

	REQUIRE(header.files.size() == 2);
	CHECK(header.files[0].name.empty());
	CHECK(header.files[1].name == "After.dat");
}
