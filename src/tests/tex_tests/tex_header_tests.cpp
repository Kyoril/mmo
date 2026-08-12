// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "tex/magic.h"
#include "tex/pre_header.h"
#include "tex/pre_header_load.h"
#include "tex/pre_header_save.h"
#include "tex_v1_0/header.h"
#include "tex_v1_0/header_load.h"
#include "tex_v1_0/header_save.h"

#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"

#include <vector>

using namespace mmo;
using namespace mmo::tex;

namespace
{
	v1_0::Header MakeHeader()
	{
		v1_0::Header header(Version_1_0);
		header.format = v1_0::DXT5;
		header.hasMips = true;
		header.width = 512;
		header.height = 256;

		// Plausible mip chain: each level a quarter of the previous one's byte count.
		uint32 offset = 1024;
		uint32 length = 65536;
		for (size_t i = 0; i < header.mipmapOffsets.size(); ++i)
		{
			header.mipmapOffsets[i] = offset;
			header.mipmapLengths[i] = length;
			offset += length;
			length = (length > 4) ? (length / 4) : 4;
		}

		return header;
	}
}

TEST_CASE("tex pre-header round-trips", "[tex][pre_header]")
{
	std::vector<char> buffer;
	{
		io::VectorSink sink(buffer);
		io::Writer writer(sink);
		savePreHeader(PreHeader(Version_1_0), writer);
	}

	// Four magic bytes plus a uint32 version.
	REQUIRE(buffer.size() == 8);

	io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
	io::Reader reader(source);

	PreHeader preHeader;
	REQUIRE(loadPreHeader(preHeader, reader));
	CHECK(preHeader.version == Version_1_0);
}

TEST_CASE("tex pre-header starts with the HTEX magic", "[tex][pre_header]")
{
	std::vector<char> buffer;
	{
		io::VectorSink sink(buffer);
		io::Writer writer(sink);
		savePreHeader(PreHeader(Version_1_0), writer);
	}

	REQUIRE(buffer.size() >= 4);
	CHECK(buffer[0] == 'H');
	CHECK(buffer[1] == 'T');
	CHECK(buffer[2] == 'E');
	CHECK(buffer[3] == 'X');
}

TEST_CASE("tex pre-header rejects a foreign magic", "[tex][pre_header]")
{
	// An .hpak fed to the texture loader has to be refused rather than misread.
	const std::string raw("HPAK\x00\x01\x00\x00", 8);

	io::MemorySource source(raw.data(), raw.data() + raw.size());
	io::Reader reader(source);

	PreHeader preHeader;
	CHECK_FALSE(loadPreHeader(preHeader, reader));
}

TEST_CASE("tex pre-header rejects a truncated file", "[tex][pre_header]")
{
	SECTION("magic cut short")
	{
		const std::string raw("HT", 2);
		io::MemorySource source(raw.data(), raw.data() + raw.size());
		io::Reader reader(source);

		PreHeader preHeader;
		CHECK_FALSE(loadPreHeader(preHeader, reader));
	}

	SECTION("version cut short")
	{
		const std::string raw("HTEX\x00", 5);
		io::MemorySource source(raw.data(), raw.data() + raw.size());
		io::Reader reader(source);

		PreHeader preHeader;
		CHECK_FALSE(loadPreHeader(preHeader, reader));
	}
}

TEST_CASE("tex v1.0 header round-trips through the saver", "[tex][header]")
{
	const v1_0::Header written = MakeHeader();

	std::vector<char> buffer;
	{
		io::VectorSink sink(buffer);
		v1_0::HeaderSaver saver(sink, written);
		saver.finish();
	}

	io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
	io::Reader reader(source);

	// The saver writes the pre-header too, so it has to be consumed first.
	PreHeader preHeader;
	REQUIRE(loadPreHeader(preHeader, reader));
	CHECK(preHeader.version == Version_1_0);

	v1_0::Header read(Version_1_0);
	REQUIRE(loadHeader(read, reader));

	CHECK(read.format == written.format);
	CHECK(read.hasMips == written.hasMips);
	CHECK(read.width == written.width);
	CHECK(read.height == written.height);
	CHECK(read.mipmapOffsets == written.mipmapOffsets);
	CHECK(read.mipmapLengths == written.mipmapLengths);
}

TEST_CASE("tex v1.0 finish rewrites mip offsets settled after the header was written",
	"[tex][header]")
{
	// This is the whole point of the saver: the mip table is written as placeholders, the
	// content is appended, and finish() goes back and patches in the real offsets. If it
	// stopped doing that, every mip after the first would be read from the wrong place.
	v1_0::Header header(Version_1_0);
	header.format = v1_0::RGBA;
	header.hasMips = true;
	header.width = 4;
	header.height = 4;
	header.mipmapOffsets.fill(0);
	header.mipmapLengths.fill(0);

	std::vector<char> buffer;
	{
		io::VectorSink sink(buffer);
		v1_0::HeaderSaver saver(sink, header);

		// Only now is the real layout known.
		header.mipmapOffsets[0] = 4242;
		header.mipmapLengths[0] = 64;
		header.mipmapOffsets[1] = 4306;
		header.mipmapLengths[1] = 16;

		saver.finish();
	}

	io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
	io::Reader reader(source);

	PreHeader preHeader;
	REQUIRE(loadPreHeader(preHeader, reader));

	v1_0::Header read(Version_1_0);
	REQUIRE(loadHeader(read, reader));

	CHECK(read.mipmapOffsets[0] == 4242);
	CHECK(read.mipmapLengths[0] == 64);
	CHECK(read.mipmapOffsets[1] == 4306);
	CHECK(read.mipmapLengths[1] == 16);
	CHECK(read.mipmapOffsets[2] == 0);
}

TEST_CASE("tex v1.0 header has a fixed on-disk size", "[tex][header]")
{
	const v1_0::Header header = MakeHeader();

	std::vector<char> buffer;
	{
		io::VectorSink sink(buffer);
		// Named local, not a temporary: HeaderSaver keeps a reference to the header and
		// reads it again in finish().
		v1_0::HeaderSaver saver(sink, header);
		saver.finish();
	}

	// 8 pre-header + 1 format + 1 hasMips + 2 width + 2 height + 16*4 offsets + 16*4
	// lengths. Pinned because the mip arrays are indexed by absolute file offset at load
	// time, so a size change silently invalidates every existing .htex.
	CHECK(buffer.size() == 8 + 1 + 1 + 2 + 2 + 64 + 64);
}

TEST_CASE("tex v1.0 header round-trips every pixel format", "[tex][header]")
{
	const v1_0::PixelFormat formats[] = {
		v1_0::RGB, v1_0::RGBA, v1_0::DXT1, v1_0::DXT5,
		v1_0::FLOAT_RGB, v1_0::FLOAT_RGBA,
		v1_0::R8, v1_0::RG8, v1_0::BC4, v1_0::BC5,
		v1_0::Unknown,
	};

	for (const auto format : formats)
	{
		v1_0::Header written(Version_1_0);
		written.format = format;
		written.hasMips = false;
		written.width = 1;
		written.height = 1;
		written.mipmapOffsets.fill(0);
		written.mipmapLengths.fill(0);

		std::vector<char> buffer;
		{
			io::VectorSink sink(buffer);
			v1_0::HeaderSaver saver(sink, written);
			saver.finish();
		}

		io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
		io::Reader reader(source);

		PreHeader preHeader;
		REQUIRE(loadPreHeader(preHeader, reader));

		v1_0::Header read(Version_1_0);
		REQUIRE(loadHeader(read, reader));

		// Stored as a uint8, so Unknown (0xff) is the value that would break first.
		CHECK(read.format == format);
	}
}

TEST_CASE("tex v1.0 header rejects a truncated mip table", "[tex][header]")
{
	const v1_0::Header header = MakeHeader();

	std::vector<char> buffer;
	{
		io::VectorSink sink(buffer);
		v1_0::HeaderSaver saver(sink, header);
		saver.finish();
	}

	buffer.resize(buffer.size() - 8);

	io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
	io::Reader reader(source);

	PreHeader preHeader;
	REQUIRE(loadPreHeader(preHeader, reader));

	v1_0::Header read(Version_1_0);
	CHECK_FALSE(loadHeader(read, reader));
}
