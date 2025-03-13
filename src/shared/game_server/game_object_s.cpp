// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_object_s.h"
#include "game_player_s.h"

#include "base/clock.h"
#include "binary_io/vector_sink.h"

#include <cmath>

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

	GamePlayerS& GameObjectS::AsPlayer() const
	{
		ASSERT(IsPlayer());
		return *static_cast<GamePlayerS*>(const_cast<GameObjectS*>(this));
	}

	GameUnitS& GameObjectS::AsUnit() const
	{
		ASSERT(IsUnit());
		return *static_cast<GameUnitS*>(const_cast<GameObjectS*>(this));
	}

	uint32 GameObjectS::GetMapId() const
	{
		if (!m_worldInstance)
		{
			return 0;
		}

		return m_worldInstance->GetMapId();
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

	bool GameObjectS::HasFieldChanges() const
	{
		return m_fields.HasChanges();
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
			return position.GetSquaredDistanceTo(GetPosition());
		}

		Vector3 flatPosition = GetPosition();
		flatPosition.y = 0.0f;

		return flatPosition.GetSquaredDistanceTo(Vector3(position.x, 0.0f, position.z));
	}

	Vector3 GameObjectS::GetForwardVector() const
	{
		const float facing = GetFacing().GetValueRadians();
		const float cosYaw = cos(facing);
		const float sinYaw = sin(facing);

		return Vector3(cosYaw, 0.0f, -sinYaw).NormalizedCopy();
	}

	bool GameObjectS::IsInArc(const GameObjectS& other, const Radian& arc) const
	{
		return IsInArc(other.GetPosition(), arc);
	}

	bool GameObjectS::IsInArc(const Vector3& position, const Radian& arcRadian) const
	{
		const auto myPosition = GetPosition();

		// Get the direction between the current position and the target position
		const Vector3 direction = (position - myPosition).NormalizedCopy();
		const Vector3 forward = GetForwardVector();

		const float dotProduct = direction.Dot(forward);
		const float angle = acos(dotProduct);

		return angle <= (arcRadian.GetValueRadians() / 2);
	}

	bool GameObjectS::IsFacingTowards(const GameObjectS& other) const
	{
		if (&other == this)
		{
			return true;
		}

		return IsFacingTowards(other.GetPosition());
	}

	bool GameObjectS::IsFacingAwayFrom(const GameObjectS& other) const
	{
		if (&other == this)
		{
			return true;
		}

		return IsFacingAwayFrom(other.GetPosition());
	}

	bool GameObjectS::IsFacingTowards(const Vector3& position) const
	{
		// 120 degrees view cone in total
		return IsInArc(position, Radian(Pi * 2.0f / 3.0f));
	}

	bool GameObjectS::IsFacingAwayFrom(const Vector3& position) const
	{
		return IsInArc(position, Radian(Pi));
	}

	void GameObjectS::SetWorldInstance(WorldInstance* instance)
	{
		// Use new instance
		m_worldInstance = instance;
	}

	io::Writer& operator<<(io::Writer& w, GameObjectS const& object)
	{
		object.m_fields.SerializeComplete(w);
		return w << object.m_movementInfo;
	}

	io::Reader& operator>>(io::Reader& r, GameObjectS& object)
	{
		object.m_fields.DeserializeComplete(r);
		return r >> object.m_movementInfo;
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
