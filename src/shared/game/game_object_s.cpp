// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "game_object_s.h"

#include "base/clock.h"
#include "binary_io/vector_sink.h"

namespace mmo
{
	GameObjectS::GameObjectS(const proto::Project& project)
		: m_project(project)
	{
	}

	GameObjectS::~GameObjectS() = default;

	ObjectTypeId GameObjectS::GetTypeId() const
	{
		return ObjectTypeId::Object;
	}

	void GameObjectS::Initialize()
	{
		PrepareFieldMap();
	}

	Vector3 GameObjectS::GetPredictedPosition()
	{
		Vector3 position = m_movementInfo.position;

		// TODO: Predict position based on movement flags

		return position;
	}

	void GameObjectS::ApplyMovementInfo(const MovementInfo& info)
	{
		const MovementInfo previousMovement = m_movementInfo;
		m_movementInfo = info;

		if (m_worldInstance)
		{
			m_worldInstance->NotifyObjectMoved(*this, previousMovement, info);
		}
	}

	void GameObjectS::WriteObjectUpdateBlock(io::Writer& writer, bool creation) const
	{
		writer
			<< io::write<uint8>(GetTypeId())
			<< io::write<uint8>(creation);

		if (!creation)
		{
			writer
				<< io::write_packed_guid(GetGuid());
		}

		uint32 flags = object_update_flags::None;
		if (HasMovementInfo() && creation)
		{
			flags |= object_update_flags::HasMovementInfo;
		}

		writer << io::write<uint32>(flags);
		if (flags & object_update_flags::HasMovementInfo)
		{
			writer << m_movementInfo;
		}

		if (creation)
		{
			m_fields.SerializeComplete(writer);
		}
		else
		{
			ASSERT(m_fields.HasChanges());
			m_fields.SerializeChanges(writer);
		}
	}

	void GameObjectS::WriteValueUpdateBlock(io::Writer& writer, bool creation) const
	{
		m_fields.SerializeChanges(writer);
	}

	void GameObjectS::ClearFieldChanges()
	{
		m_fields.MarkAsUnchanged();

		if (m_worldInstance)
		{
			m_worldInstance->RemoveObjectUpdate(*this);
		}
	}

	float GameObjectS::GetSquaredDistanceTo(const Vector3& position, bool withHeight) const
	{
		if (withHeight)
		{
			return position.GetSquaredDistanceTo(position);
		}

		Vector3 flatPosition = GetPosition();
		flatPosition.y = 0.0f;

		return flatPosition.GetSquaredDistanceTo(Vector3(position.x, 0.0f, position.z));
	}

	void GameObjectS::SetWorldInstance(WorldInstance* instance)
	{
		// Use new instance
		m_worldInstance = instance;
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
