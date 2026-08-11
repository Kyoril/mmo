// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "memory_source.h"
#include "vector_sink.h"
#include "base/typedefs.h"
#include "writer.h"
#include "math/aabb.h"

#include <vector>
#include <cstring>
#include <algorithm>

using namespace mmo;


TEST_CASE("Can serialize and deserialize full packed guid", "[binaryio]")
{
	const uint64 originalGuid = 0x123456789abcdef1;

	std::vector<char> buffer;
	io::VectorSink sink{ buffer };
	io::Writer writer{sink};
	writer << io::write_packed_guid(originalGuid);

	// Read back from buffer
	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	uint64 deserializedGuid = 0;
	reader >> io::read_packed_guid(deserializedGuid);

	CHECK(reader);
	CHECK(originalGuid == deserializedGuid);
}

TEST_CASE("Can serialize and deserialize partial packed guid", "[binaryio]")
{
	const uint64 originalGuid = 0xF130000000000001;

	std::vector<char> buffer;
	io::VectorSink sink{ buffer };
	io::Writer writer{ sink };
	writer << io::write_packed_guid(originalGuid);

	// Read back from buffer
	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	uint64 deserializedGuid = 0;
	reader >> io::read_packed_guid(deserializedGuid);

	CHECK(reader);
	CHECK(originalGuid == deserializedGuid);
}


class MockSink : public io::ISink
{
public:
    std::vector<char> buffer;
    std::size_t position = 0;

    std::size_t Write(const char* src, std::size_t size) override
	{
        buffer.insert(buffer.end(), src, src + size);
        position += size;
        return size;
    }

    std::size_t Overwrite(std::size_t pos, const char* src, std::size_t size) override
	{
        if (pos + size > buffer.size())
        {
            buffer.resize(pos + size);
        }

        std::memcpy(buffer.data() + pos, src, size);
        return size;
    }

    std::size_t Position() override
	{
        return position;
    }

    void Flush() override
	{
        // No-op for mock
    }
};

TEST_CASE("Writer::WritePOD writes plain old data to the sink", "[binaryio]")
{
    MockSink sink;
    io::Writer writer(sink);

    SECTION("Write integer")
	{
        int value = 42;
        writer.WritePOD(value);
        REQUIRE(sink.buffer.size() == sizeof(value));
        REQUIRE(std::memcmp(sink.buffer.data(), &value, sizeof(value)) == 0);
    }

    SECTION("Write float")
	{
        float value = 3.14f;
        writer.WritePOD(value);
        REQUIRE(sink.buffer.size() == sizeof(value));
        REQUIRE(std::memcmp(sink.buffer.data(), &value, sizeof(value)) == 0);
    }
}

TEST_CASE("Writer::WritePOD with position overwrites data in the sink", "[binaryio]")
{
    MockSink sink;
    io::Writer writer(sink);

    int initialValue = 42;
    writer.WritePOD(initialValue);

    int newValue = 84;
    writer.WritePOD(0, newValue);
    REQUIRE(sink.buffer.size() == sizeof(newValue));
    REQUIRE(std::memcmp(sink.buffer.data(), &newValue, sizeof(newValue)) == 0);
}

TEST_CASE("Writer operator<< writes data to the sink", "[binaryio]")
{
    MockSink sink;
    io::Writer writer(sink);

    SECTION("Write integer")
	{
        int value = 42;
        writer << value;
        REQUIRE(sink.buffer.size() == sizeof(value));
        REQUIRE(std::memcmp(sink.buffer.data(), &value, sizeof(value)) == 0);
    }

    SECTION("Write float")
	{
        float value = 3.14f;
        writer << value;
        REQUIRE(sink.buffer.size() == sizeof(value));
        REQUIRE(std::memcmp(sink.buffer.data(), &value, sizeof(value)) == 0);
    }
}



class MockSource : public io::ISource
{
public:
    MockSource(const std::vector<char>& data)
		: data(data)
		, pos(0)
	{}

    std::size_t read(char* dest, std::size_t size) override
	{
        std::size_t bytesToRead = std::min(size, data.size() - pos);
        std::copy(data.begin() + pos, data.begin() + pos + bytesToRead, dest);
        pos += bytesToRead;
        return bytesToRead;
    }

    std::size_t skip(std::size_t size) override
	{
        std::size_t bytesToSkip = std::min(size, data.size() - pos);
        pos += bytesToSkip;
        return bytesToSkip;
    }

    bool end() const override
    {
		return pos >= data.size();
    }

    void seek(std::size_t pos) override
    {
        this->pos = pos;
    }

    std::size_t size() const override
    {
        return data.size();
    }

    std::size_t position() const override
    {
        return pos;
    }

private:
    std::vector<char> data;
    std::size_t pos;
};

TEST_CASE("Reader reads POD types correctly", "[binaryio]")
{
    std::vector<char> data = { 0x01, 0x00, 0x00, 0x00 }; // int 1
    MockSource source(data);
    io::Reader reader(source);

    int value;
    reader.readPOD(value);

    REQUIRE(value == 1);
    REQUIRE(reader);
}

TEST_CASE("Reader reads float correctly", "[binaryio]")
{
    std::vector<char> data = { 0x00, 0x00, (char)0x80, 0x3F }; // float 1.0
    MockSource source(data);
    io::Reader reader(source);

    float value;
    reader.readPOD(value);

    REQUIRE(value == 1.0f);
    REQUIRE(reader);
}

TEST_CASE("Reader reads double correctly", "[binaryio]")
{
    std::vector<char> data = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, (char)0xF0, 0x3F }; // double 1.0
    MockSource source(data);
    io::Reader reader(source);

    double value;
    reader.readPOD(value);

    REQUIRE(value == 1.0);
    REQUIRE(reader);
}

TEST_CASE("Reader handles NaN float correctly", "[binaryio]")
{
    std::vector<char> data = { 0x00, 0x00, (char)0xC0, 0x7F }; // float NaN
    MockSource source(data);
    io::Reader reader(source);

    float value;
    reader.readPOD(value);

    REQUIRE(std::isnan(value));
    REQUIRE_FALSE(reader);
}

TEST_CASE("Reader handles NaN double correctly", "[binaryio]")
{
    std::vector<char> data = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, (char)0xF8, 0x7F }; // double NaN
    MockSource source(data);
    io::Reader reader(source);

    double value;
    reader.readPOD(value);

    REQUIRE(std::isnan(value));
    REQUIRE_FALSE(reader);
}

TEST_CASE("Reader reads string correctly", "[binaryio]")
{
    std::vector<char> data = { 'H', 'e', 'l', 'l', 'o', '\0' };
    MockSource source(data);
    io::Reader reader(source);

    std::string value;
    reader >> io::read_string(value);

    REQUIRE(value == "Hello");
    REQUIRE(reader);
}

TEST_CASE("Reader reads limited string correctly", "[binaryio]")
{
    std::vector<char> data = { 'H', 'e', 'l', 'l', 'o', '\0' };
    MockSource source(data);
    io::Reader reader(source);

    std::string value;
    reader >> io::read_limited_string<3>(value);

    REQUIRE(value == "Hel");
    REQUIRE(reader);
}

