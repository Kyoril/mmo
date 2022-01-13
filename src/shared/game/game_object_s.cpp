// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "game_object_s.h"

#include "base/clock.h"
#include "binary_io/vector_sink.h"

namespace mmo
{
	GameObjectS::GameObjectS(const uint64 guid)
	{
		GameObjectS::PrepareFieldMap();

		// Setup fields
		m_fields.SetFieldValue(object_fields::Guid, guid);
		m_fields.SetFieldValue(object_fields::Type, ObjectTypeId::Object);
		m_fields.SetFieldValue(object_fields::Entry, 0);
		m_fields.SetFieldValue(object_fields::Scale, 1.0f);
		m_fields.MarkAsUnchanged();
	}

	GameObjectS::~GameObjectS() = default;

	ObjectTypeId GameObjectS::GetTypeId() const
	{
		return ObjectTypeId::Object;
	}

	void GameObjectS::WriteObjectUpdateBlock(io::Writer& writer, bool creation) const
	{
		writer
			<< io::write<uint8>(GetTypeId());

		if (creation)
		{
			m_fields.SerializeComplete(writer);
		}
		else
		{
			m_fields.SerializeChanges(writer);
		}
	}

	void CreateUpdateBlocks(const GameObjectS& object, std::vector<std::vector<char>>& outBlocks)
	{
		// Write create object packet
		std::vector<char> createBlock;
		io::VectorSink sink(createBlock);
		io::Writer writer(sink);
		
		constexpr uint8 updateFlags = object_update_flags::None;

		writer
			<< io::write<uint8>(ObjectUpdateType::CreateObject)
			<< io::write_packed_guid(object.GetGuid())
			<< io::write<uint8>(object.GetTypeId())
			<< io::write<uint8>(updateFlags);

		object.m_fields.SerializeComplete(writer);
	}
}
