// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "catch.hpp"


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

#include "game/game_object.h"

using namespace mmo;


TEST_CASE("SetAndGet64BitUnsignedFieldValue", "[field_map]")
{
	FieldMap<uint32> fieldMap;
	fieldMap.Initialize(2);

	uint64 guid = 0xF1000000000012;
	fieldMap.SetFieldValue(0, guid);
	
	CHECK(fieldMap.GetFieldValue<uint64>(0) == guid);
}

TEST_CASE("PreventOverwriteOnSet", "[field_map]")
{
	FieldMap<uint32> fieldMap;
	fieldMap.Initialize(1);

	uint64 guid = 0xF1000000000012;
	CHECK_THROWS_AS(fieldMap.SetFieldValue(0, guid), AssertException);
}

TEST_CASE("CheckIndexBoundsOnSet", "[field_map]")
{
	FieldMap<uint32> fieldMap;
	fieldMap.Initialize(1);

	uint64 guid = 0xF1000000000012;
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

	int64 guid = -5;
	fieldMap.SetFieldValue(0, guid);

	CHECK(fieldMap.GetFieldValue<int64>(0) == guid);
}

TEST_CASE("SetAndGet32BitUnsignedFieldValue", "[field_map]")
{
	FieldMap<uint32> fieldMap;
	fieldMap.Initialize(1);

	uint32 guid = 0x12345678;
	fieldMap.SetFieldValue(0, guid);

	CHECK(fieldMap.GetFieldValue<uint32>(0) == guid);
}

TEST_CASE("SetAndGet32BitSignedFieldValue", "[field_map]")
{
	FieldMap<uint32> fieldMap;
	fieldMap.Initialize(1);

	int32 guid = -5;
	fieldMap.SetFieldValue(0, guid);

	CHECK(fieldMap.GetFieldValue<int32>(0) == guid);
}

TEST_CASE("SetFieldsMarkFieldsAsChanged", "[field_map]")
{
	FieldMap<uint32> fieldMap;
	fieldMap.Initialize(2);

	REQUIRE_FALSE(fieldMap.IsFieldMarkedAsChanged(0));
	REQUIRE_FALSE(fieldMap.IsFieldMarkedAsChanged(1));
	
	uint64 guid = 0xF1000000000012;
	fieldMap.SetFieldValue(0, guid);

	CHECK(fieldMap.IsFieldMarkedAsChanged(0));
	CHECK(fieldMap.IsFieldMarkedAsChanged(1));
}