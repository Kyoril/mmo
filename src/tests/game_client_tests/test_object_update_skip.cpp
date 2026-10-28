// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// A value update for an object the client does not know used to drop the session. The client now
// reads past such a block, which only works if it consumes exactly what the server wrote for it:
// the next block in the packet starts right after.

#include "catch.hpp"

#include "game_client/object_update_skip.h"
#include "game/field_map.h"
#include "game/movement_info.h"
#include "game/movement_type.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "binary_io/memory_source.h"

#include <vector>

using namespace mmo;

namespace
{
	constexpr uint32 Sentinel = 0xC0FFEE42;

	/// Writes what GameObjectS::WriteObjectUpdateBlock (and GameUnitS for units) writes after the
	/// guid of a value update, followed by a sentinel.
	std::vector<char> WriteValueBlock(const size_t fieldCount, const bool isUnit, const bool withMovement)
	{
		std::vector<char> buffer;
		io::VectorSink sink(buffer);
		io::Writer writer(sink);

		writer << io::write<uint32>(withMovement ? object_update_flags::HasMovementInfo : object_update_flags::None);
		if (withMovement)
		{
			MovementInfo info;
			info.position = Vector3(1.0f, 2.0f, 3.0f);
			writer << info;
		}

		FieldMap<uint32> fields;
		fields.Initialize(fieldCount);
		fields.SetFieldValue<uint32>(object_fields::Entry, 42);
		fields.SetFieldValue<uint32>(static_cast<FieldMap<uint32>::FieldIndexType>(fieldCount - 1), 7);
		fields.SerializeChanges(writer);

		if (isUnit)
		{
			for (int i = 0; i < movement_type::Count; ++i)
			{
				writer << io::write<float>(1.5f);
			}
		}

		writer << io::write<uint32>(Sentinel);
		return buffer;
	}

	bool SkipsToSentinel(const std::vector<char>& buffer, const ObjectTypeId typeId)
	{
		io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
		io::Reader reader(source);

		if (!SkipObjectValueUpdate(reader, typeId))
		{
			return false;
		}

		uint32 next = 0;
		return (reader >> io::read<uint32>(next)) && next == Sentinel;
	}
}

TEST_CASE("Skipping a unit value update lands on the next block", "[object_update]")
{
	CHECK(SkipsToSentinel(WriteValueBlock(object_fields::UnitFieldCount, true, false), ObjectTypeId::Unit));
}

TEST_CASE("Skipping a player value update lands on the next block", "[object_update]")
{
	CHECK(SkipsToSentinel(WriteValueBlock(object_fields::PlayerFieldCount, true, false), ObjectTypeId::Player));
}

TEST_CASE("Skipping a world object value update lands on the next block", "[object_update]")
{
	CHECK(SkipsToSentinel(WriteValueBlock(object_fields::WorldObjectFieldCount, false, false), ObjectTypeId::Object));
}

TEST_CASE("Skipping an item and a container value update lands on the next block", "[object_update]")
{
	CHECK(SkipsToSentinel(WriteValueBlock(object_fields::ItemFieldCount, false, false), ObjectTypeId::Item));
	CHECK(SkipsToSentinel(WriteValueBlock(object_fields::BagFieldCount, false, false), ObjectTypeId::Container));
}

TEST_CASE("Skipping a value update that carries movement info lands on the next block", "[object_update]")
{
	CHECK(SkipsToSentinel(WriteValueBlock(object_fields::UnitFieldCount, true, true), ObjectTypeId::Unit));
}

TEST_CASE("A truncated value update cannot be skipped", "[object_update]")
{
	std::vector<char> buffer = WriteValueBlock(object_fields::UnitFieldCount, true, false);
	buffer.resize(buffer.size() - sizeof(uint32) - sizeof(float));

	io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
	io::Reader reader(source);
	CHECK_FALSE(SkipObjectValueUpdate(reader, ObjectTypeId::Unit));
}

TEST_CASE("A value update of an unknown object type cannot be skipped", "[object_update]")
{
	const std::vector<char> buffer = WriteValueBlock(object_fields::ObjectFieldCount, false, false);

	io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
	io::Reader reader(source);
	CHECK_FALSE(SkipObjectValueUpdate(reader, ObjectTypeId::DynamicObject));
}
