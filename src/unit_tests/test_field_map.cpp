// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "catch.hpp"
#include "memory_source.h"
#include "vector_sink.h"


class AssertException final : std::runtime_error
{
public:
	explicit AssertException(const std::string& _Message)
		: runtime_error(_Message)
	{
	}

	explicit AssertException(const char* _Message)
		: runtime_error(_Message)
	{
	}
};

#include <exception>

#define ASSERT(expr) if (!(expr)) throw AssertException(#expr);

// Include after ASSERT macro definition to prevent setting this macro
#include "game/field_map.h"
#include "game/game_object_s.h"

using namespace mmo;


TEST_CASE("SerializeCompleteIsDeserializable", "[field_map]")
{
	FieldMap<uint32> fieldMap;
	fieldMap.Initialize(3);
	fieldMap.SetFieldValue<uint32>(0, 1);
	fieldMap.SetFieldValue<float>(2, 3.0f);
	
	REQUIRE(fieldMap.GetFieldValue<uint32>(0) == 1);
	REQUIRE(fieldMap.GetFieldValue<uint32>(1) == 0);
	REQUIRE(fieldMap.GetFieldValue<float>(2) == 3.0f);

	std::vector<char> buffer;
	io::VectorSink sink { buffer };
	io::Writer writer { sink };
	fieldMap.SerializeComplete(writer);

	FieldMap<uint32> deserializedMap;
	deserializedMap.Initialize(3);
	
	REQUIRE(deserializedMap.GetFieldValue<uint32>(0) == 0);
	REQUIRE(deserializedMap.GetFieldValue<uint32>(1) == 0);
	REQUIRE(deserializedMap.GetFieldValue<float>(2) == 0.0f);

	io::MemorySource source { buffer };
	io::Reader reader { source };
	deserializedMap.DeserializeComplete(reader);

	CHECK(deserializedMap.GetFieldValue<uint32>(0) == 1);
	CHECK(deserializedMap.GetFieldValue<uint32>(1) == 0);
	CHECK(deserializedMap.GetFieldValue<float>(2) == 3.0f);
}

TEST_CASE("SerializeChangesOnlySerializesChangedFields", "[field_map]")
{
	FieldMap<uint32> fieldMap;
	fieldMap.Initialize(3);
	fieldMap.SetFieldValue<float>(2, 3.0f);
	
	std::vector<char> buffer;
	io::VectorSink sink { buffer };
	io::Writer writer { sink };
	fieldMap.SerializeChanges(writer);
	writer.Sink().Flush();

	// One byte for change set bitmask, 4 bytes for float value
	CHECK(buffer.size() == 5);
	CHECK(buffer[0] == (uint8)(1 << 2));

	const float* changeValue = reinterpret_cast<float*>(&buffer[1]);
	CHECK(*changeValue == 3.0f);
}

TEST_CASE("DeserializeChangesSerializesExpectedFields", "[field_map]")
{
	std::vector<char> buffer;
	io::VectorSink sink { buffer };
	io::Writer writer { sink };
	writer << io::write<uint8>(1 << 2);
	writer << io::write<float>(3.0f);
	
	REQUIRE(buffer.size() == 5);

	io::MemorySource source { buffer };
	io::Reader reader { source };
	
	FieldMap<uint32> fieldMap;
	fieldMap.Initialize(3);
	fieldMap.DeserializeChanges(reader);
	
	CHECK(fieldMap.GetFieldValue<uint32>(0) == 0);
	CHECK(fieldMap.GetFieldValue<uint32>(1) == 0);
	CHECK(fieldMap.GetFieldValue<float>(2) == 3.0f);
}

TEST_CASE("SetAndGet64BitUnsignedFieldValue", "[field_map]")
{
	FieldMap<uint32> fieldMap;
	fieldMap.Initialize(2);

	const uint64 guid = 0xF1000000000012;
	fieldMap.SetFieldValue(0, guid);
	
	CHECK(fieldMap.GetFieldValue<uint64>(0) == guid);
}

TEST_CASE("PreventOverwriteOnSet", "[field_map]")
{
	FieldMap<uint32> fieldMap;
	fieldMap.Initialize(1);

	const uint64 guid = 0xF1000000000012;
	CHECK_THROWS_AS(fieldMap.SetFieldValue(0, guid), AssertException);
}

TEST_CASE("CheckIndexBoundsOnSet", "[field_map]")
{
	FieldMap<uint32> fieldMap;
	fieldMap.Initialize(1);

	const uint64 guid = 0xF1000000000012;
	CHECK_THROWS_AS(fieldMap.SetFieldValue(2, guid), AssertException);
}

TEST_CASE("CheckIndexBoundsOnGet", "[field_map]")
{
	FieldMap<uint32> fieldMap;
	fieldMap.Initialize(1);

	uint64 guid = 0xF1000000000012;
	CHECK_THROWS_AS(fieldMap.GetFieldValue<uint32>(2), AssertException);
}

TEST_CASE("SetAndGet64BitSignedFieldValue", "[field_map]")
{
	FieldMap<uint32> fieldMap;
	fieldMap.Initialize(2);

	const int64 guid = -5;
	fieldMap.SetFieldValue(0, guid);

	CHECK(fieldMap.GetFieldValue<int64>(0) == guid);
}

TEST_CASE("SetAndGet32BitUnsignedFieldValue", "[field_map]")
{
	FieldMap<uint32> fieldMap;
	fieldMap.Initialize(1);

	const uint32 guid = 0x12345678;
	fieldMap.SetFieldValue(0, guid);

	CHECK(fieldMap.GetFieldValue<uint32>(0) == guid);
}

TEST_CASE("SetAndGet32BitSignedFieldValue", "[field_map]")
{
	FieldMap<uint32> fieldMap;
	fieldMap.Initialize(1);

	const int32 guid = -5;
	fieldMap.SetFieldValue(0, guid);

	CHECK(fieldMap.GetFieldValue<int32>(0) == guid);
}

TEST_CASE("SetFieldsMarkFieldsAsChanged", "[field_map]")
{
	FieldMap<uint32> fieldMap;
	fieldMap.Initialize(2);

	REQUIRE_FALSE(fieldMap.IsFieldMarkedAsChanged(0));
	REQUIRE_FALSE(fieldMap.IsFieldMarkedAsChanged(1));

	const uint64 guid = 0xF1000000000012;
	fieldMap.SetFieldValue(0, guid);

	CHECK(fieldMap.IsFieldMarkedAsChanged(0));
	CHECK(fieldMap.IsFieldMarkedAsChanged(1));
}