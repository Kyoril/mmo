// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "game_object.h"

#include "base/clock.h"
#include "binary_io/vector_sink.h"

namespace mmo
{
	GameObject::GameObject(const uint64 guid)
	{
		GameObject::PrepareFieldMap();

		// Setup fields
		m_fields.SetFieldValue(object_fields::Guid, guid);
		m_fields.SetFieldValue(object_fields::Type, ObjectTypeId::Object);
		m_fields.SetFieldValue(object_fields::Entry, 0);
		m_fields.SetFieldValue(object_fields::Scale, 1.0f);
	}

	GameObject::~GameObject()
	{
	}

	ObjectTypeId GameObject::GetTypeId() const
	{
		return ObjectTypeId::Object;
	}

	void GameObject::WriteValueUpdateBlock(io::Writer& writer, bool creation) const
	{
		// TODO
	}

	void CreateUpdateBlocks(GameObject& object, std::vector<std::vector<char>>& out_blocks)
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
	}
}
