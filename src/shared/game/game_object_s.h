// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <bitset>
#include <array>
#include <vector>
#include <memory>

#include "field_map.h"
#include "base/typedefs.h"
#include "base/signal.h"

#include "math/vector3.h"
#include "math/angle.h"

#include "binary_io/reader.h"
#include "binary_io/writer.h"

#include "movement_info.h"
#include "object_type_id.h"
#include "world_instance.h"

namespace mmo
{
	enum class GuidType
	{
		Player = 0,
		Object = 1,
		Transport = 2,
		Unit = 3,
		Pet = 4,
		Item = 5
	};

	enum class ObjectUpdateType
	{
		CreateObject,
		CreatePlayer,
	};

	namespace object_update_flags
	{
		enum Type
		{
			None = 0,

			HasMovementInfo = 1 << 0
		};
	}

	/// Defines object field visibility modifiers.
	enum class FieldVisibilityModifier : uint8
	{
		/// The field is ownly visible to the owning client.
		Private,
		/// The field is visible for everyone.
		Public,
	};
	
	typedef FieldMap<uint32> ObjectFieldMap;
	
	class VisibilityTile;

	/// This is the base class of server side object, spawned on the world server.
	class GameObjectS : public std::enable_shared_from_this<GameObjectS>
	{
		friend void CreateUpdateBlocks(const GameObjectS &object, std::vector<std::vector<char>> &outBlocks);

	public:
		signal<void(WorldInstance&)> spawned;
		signal<void(GameObjectS&)> despawned;
		signal<void(VisibilityTile&, VisibilityTile&)> tileChangePending;

	public:
		explicit GameObjectS(uint64 guid);
		virtual ~GameObjectS();

	public:
		virtual ObjectTypeId GetTypeId() const;

	protected:
		virtual void PrepareFieldMap()
		{
			m_fields.Initialize(object_fields::ObjectFieldCount);
		}
		
	public:
		/// Gets the objects globally unique identifier value.
		uint64 GetGuid() const { return m_fields.GetFieldValue<uint64>(object_fields::Guid); }

		/// Gets the position of this object.
		const Vector3& GetPosition() const noexcept { return m_movementInfo.position; }

		/// Gets the facing of this object.
		const Radian& GetFacing() const noexcept { return m_movementInfo.facing; }
		
		/// @brief Gets the movement info.
		[[nodiscard]] MovementInfo GetMovementInfo() { return m_movementInfo; }

		Vector3 GetPredictedPosition();

		void ApplyMovementInfo(const MovementInfo& info);

		virtual void WriteObjectUpdateBlock(io::Writer &writer, bool creation = true) const;

	protected:
		ObjectFieldMap m_fields;
		MovementInfo m_movementInfo;
	};
	
	void CreateUpdateBlocks(const GameObjectS &object, std::vector<std::vector<char>> &outBlocks);
}
