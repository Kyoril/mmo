// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <bitset>
#include <array>
#include <vector>
#include <memory>
#include <variant>

#include "each_tile_in_sight.h"
#include "game/field_map.h"
#include "base/typedefs.h"
#include "base/signal.h"

#include "math/vector3.h"
#include "math/angle.h"
#include "math/degree.h"

#include "binary_io/reader.h"
#include "binary_io/writer.h"

#include "game/movement_info.h"
#include "game/object_type_id.h"
#include "world_instance.h"
#include "shared/proto_data/variables.pb.h"

namespace mmo
{
	class GamePlayerS;

	enum class GuidType
	{
		Player = 0,
		Object = 1,
		Transport = 2,
		Unit = 3,
		Pet = 4,
		Item = 5
	};

	struct VariableInstance
	{
		proto::VariableEntry::DataCase dataCase;
		std::variant<String, int64, float> value;
	};

	/// Gets the high part of a guid which can be used to determine the object type by it's GUID.
	inline GuidType GuidTypeID(uint64 guid) {
		return static_cast<GuidType>(static_cast<uint32>((guid >> 52) & 0xF));
	}
	/// Gets the realm id of a guid.
	inline uint16 GuidRealmID(uint64 guid) {
		return static_cast<uint16>((guid >> 56) & 0xFF);
	}

	/// Determines whether the given GUID belongs to a creature.
	inline bool IsCreatureGUID(uint64 guid) {
		return GuidTypeID(guid) == GuidType::Unit;
	}

	/// Determines whether the given GUID belongs to a pet.
	inline bool IsPetGUID(uint64 guid) {
		return GuidTypeID(guid) == GuidType::Pet;
	}

	/// Determines whether the given GUID belongs to a player.
	inline bool IsPlayerGUID(uint64 guid) {
		return GuidTypeID(guid) == GuidType::Player;
	}

	/// Determines whether the given GUID belongs to a unit.
	inline bool IsUnitGUID(uint64 guid) {
		return IsPlayerGUID(guid) || IsCreatureGUID(guid) || IsPetGUID(guid);
	}

	/// Determines whether the given GUID belongs to an item.
	inline bool IsItemGUID(uint64 guid) {
		return GuidTypeID(guid) == GuidType::Item;
	}

	/// Determines whether the given GUID belongs to a game object (chest for example).
	inline bool IsGameObjectGUID(uint64 guid) {
		return GuidTypeID(guid) == GuidType::Object;
	}

	/// Creates a GUID based on some settings.
	inline uint64_t CreateRealmGUID(uint64 low, uint64 realm, GuidType type) {
		return static_cast<uint64>(low | (realm << 56) | (static_cast<uint64_t>(type) << 52));
	}

	inline uint64_t CreateEntryGUID(uint64 low, uint64 entry, GuidType type) {
		return static_cast<uint64>(low | (entry << 24) | (static_cast<uint64_t>(type) << 52) | 0xF100000000000000);
	}

	/// Determines if a GUID has an entry part based on it's type.
	inline bool GuidHasEntryPart(uint64 guid)
	{
		switch (static_cast<GuidType>(guid))
		{
		case GuidType::Item:
		case GuidType::Player:
			return false;
		default:
			return true;
		}
	}
	/// Gets the entry part of a GUID or 0 if the GUID does not have an entry part.
	inline uint32 GuidEntryPart(uint64 guid)
	{
		if (GuidHasEntryPart(guid))
		{
			return static_cast<uint32>((guid >> 24) & static_cast<uint64>(0x0000000000FFFFFF));
		}

		return 0;
	}
	/// Gets the lower part of a GUID based on it's type.
	inline uint32 guidLowerPart(uint64 guid)
	{
		const uint64 low2 = 0x00000000FFFFFFFF;
		const uint64 low3 = 0x0000000000FFFFFF;

		return GuidHasEntryPart(guid) ?
			static_cast<uint32>(guid & low3) : static_cast<uint32>(guid & low2);
	}

	enum class ObjectUpdateType
	{
		CreateObject,
		CreatePlayer,
	};

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
	class GameObjectS : public std::enable_shared_from_this<GameObjectS>, public NonCopyable
	{
		friend void CreateUpdateBlocks(const GameObjectS &object, std::vector<std::vector<char>> &outBlocks);

	public:
		signal<void(WorldInstance&)> spawned;
		signal<void(GameObjectS&)> despawned;
		/// Fired when the object should be destroyed. The object should be destroyed after this call.
		std::function<void(GameObjectS&)> destroy;
		signal<void(VisibilityTile&, VisibilityTile&)> tileChangePending;

	public:
		explicit GameObjectS(const proto::Project& project);
		virtual ~GameObjectS();

	public:
		virtual ObjectTypeId GetTypeId() const;

		virtual void Initialize();

		bool IsUnit() const { return GetTypeId() == ObjectTypeId::Unit || IsPlayer(); }

		bool IsPlayer() const { return GetTypeId() == ObjectTypeId::Player; }

		GamePlayerS& AsPlayer() const;

		GameUnitS& AsUnit() const;

		GamePlayerS& AsPlayer();

		GameUnitS& AsUnit();

		bool IsItem() const { return GetTypeId() == ObjectTypeId::Item || IsContainer(); }

		bool IsContainer() const { return GetTypeId() == ObjectTypeId::Container; }

		bool IsCorpse() const { return GetTypeId() == ObjectTypeId::Corpse; }

		bool IsWorldObject() const { return GetTypeId() == ObjectTypeId::Object; }

		virtual bool IsInteractable(const GameUnitS& interactor) const { return false; };

		template<class T>
		void Set(ObjectFieldMap::FieldIndexType index, T value, bool notify = true)
		{
			const bool updated = m_fields.SetFieldValue(index, value);

			if (m_worldInstance && notify && updated)
			{
				m_worldInstance->AddObjectUpdate(*this);
			}
		}

		template<class T>
		T Get(ObjectFieldMap::FieldIndexType index) const
		{
			return m_fields.GetFieldValue<T>(index);
		}

		template<class T>
		void AddFlag(ObjectFieldMap::FieldIndexType index, T flag)
		{
			const T flags = Get<T>(index);
			Set<T>(index, flags | flag);
		}

		template<class T>
		void RemoveFlag(ObjectFieldMap::FieldIndexType index, T flag)
		{
			const T flags = Get<T>(index);
			Set<T>(index, flags & ~flag);
		}

		void Invalidate(ObjectFieldMap::FieldIndexType index)
		{
			m_fields.MarkAsChanged(index);
		}

		template<class OnSubscriber>
		void ForEachSubscriberInSight(OnSubscriber callback)
		{
			if (!m_worldInstance)
			{
				return;
			}

			TileIndex2D tileIndex;
			m_worldInstance->GetGrid().GetTilePosition(GetPosition(), tileIndex[0], tileIndex[1]);

			::mmo::ForEachSubscriberInSight(m_worldInstance->GetGrid(), tileIndex, callback);
		}

		const proto::Project& GetProject() const { return m_project; }

	protected:
		virtual void PrepareFieldMap()
		{
			m_fields.Initialize(object_fields::ObjectFieldCount);
		}

	public:
		/// Gets the objects globally unique identifier value.
		uint64 GetGuid() const { return m_fields.GetFieldValue<uint64>(object_fields::Guid); }

		/// Gets the position of this object.
		virtual const Vector3& GetPosition() const noexcept { return m_movementInfo.position; }

		/// Gets the facing of this object.
		const Radian& GetFacing() const noexcept { return m_movementInfo.facing; }

		uint32 GetMapId() const;

		virtual void Relocate(const Vector3& position, const Radian& facing)
		{
			const MovementInfo oldInfo = m_movementInfo;

			m_movementInfo.position = position;
			m_movementInfo.facing = facing;
			m_movementInfo.timestamp = GetAsyncTimeMs();

			if (m_worldInstance)
			{
				m_worldInstance->NotifyObjectMoved(*this, oldInfo, m_movementInfo);
			}
		}

		bool GetTileIndex(TileIndex2D& out_index) const
		{
			// This object has to be in a world instance
			if (!m_worldInstance)
			{
				return false;
			}

			// Try to resolve the objects position
			return m_worldInstance->GetGrid().GetTilePosition(m_movementInfo.position, out_index[0], out_index[1]);
		}

		Radian GetAngle(const GameObjectS& other) const
		{
			return GetAngle(other.GetPosition().x, other.GetPosition().z);
		}

		Radian GetAngle(const float x, const float z) const
		{
			const auto& position = GetPosition();
			const float dx = x - position.x;
			const float dz = z - position.z;

			float ang = ::atan2(-dz, dx);

			ang = (ang >= 0) ? ang : 2 * Pi + ang;
			return Radian(ang);
		}

		/// Returns true if this object acts as a quest giver and provides the given quest.
		virtual bool ProvidesQuest(uint32 questId) const
		{
			// By default, objects do not provide quests
			return false;
		}

		/// Returns true if this object acts as a quest giver and ends the given quest.
		virtual bool EndsQuest(uint32 questId) const
		{
			// By default, objects do not provide quests
			return false;
		}

		/// @brief Gets the movement info.
		[[nodiscard]] MovementInfo GetMovementInfo() { return m_movementInfo; }

		Vector3 GetPredictedPosition();

		virtual void ApplyMovementInfo(const MovementInfo& info);

		virtual void WriteObjectUpdateBlock(io::Writer &writer, bool creation = true) const;

		virtual void WriteValueUpdateBlock(io::Writer& writer, bool creation = true) const;

		bool HasFieldChanges() const;

		void ClearFieldChanges();

		float GetSquaredDistanceTo(const Vector3& position, bool withHeight) const;

		Vector3 GetForwardVector() const;

		bool IsInArc(const GameObjectS& other, const Radian& arc) const;

		bool IsInArc(const Vector3& position, const Radian& arc) const;

		bool IsFacingTowards(const GameObjectS& other) const;

		bool IsFacingAwayFrom(const GameObjectS& other) const;

		bool IsFacingTowards(const Vector3& position) const;

		bool IsFacingAwayFrom(const Vector3& position) const;

		/// Gets the world instance of this object. May be nullptr, if the object is
		/// not in any world.
		WorldInstance* GetWorldInstance()
		{
			return m_worldInstance;
		}

		/// Sets the world instance of this object. nullptr is valid here, if the object
		/// is not in any world.
		void SetWorldInstance(WorldInstance* instance);

		virtual bool HasMovementInfo() const { return false; }

	public:

		/// Adds a new variable to the list of variables of this object.
		/// @param entry Entry id of the variable.
		void AddVariable(uint32 entry);

		/// Determines if this object has an instance of the specified variable id.
		/// @param entry Entry id of the variable.
		/// @returns True, if this object owns an instance of this variable, false otherwise.
		const bool HasVariable(uint32 entry) const;

		/// Sets the value of a specific variable.
		/// @param entry Entry id of the variable.
		/// @param value Value to set.
		/// @returns false if the object does not own an instance of the variable or the type is not supported.
		template<typename T>
		bool SetVariable(uint32 entry, const T& value)
		{
			auto it = m_variables.find(entry);
			if (it == m_variables.end())
				return false;

			try
			{
				it->second.value = value;
				return true;
			}
			catch (...)
			{
				return false;
			}
		}

		/// Gets the value of a specific variable instance of this object.
		/// @param entry Entry id of the variable.
		/// @param out_value Reference of an object where the value will be copied to.
		/// @returns false if this object does not own an instance of the variable.
		template<typename T>
		bool GetVariable(uint32 entry, T& out_value)
		{
			auto it = m_variables.find(entry);
			if (it == m_variables.end())
				return false;

			try
			{
				out_value = std::get<T>(it->second.value);
				return true;
			}
			catch (...)
			{
				return false;
			}
		}

		/// Removes an instance of a variable from this object.
		/// @param entry Entry id of the variable.
		void RemoveVariable(uint32 entry);

	protected:
		const proto::Project& m_project;
		ObjectFieldMap m_fields;
		MovementInfo m_movementInfo;
		WorldInstance* m_worldInstance { nullptr };
		std::map<uint32, VariableInstance> m_variables;

	private:
		friend io::Writer& operator << (io::Writer& w, GameObjectS const& object);
		friend io::Reader& operator >> (io::Reader& r, GameObjectS& object);
	};

	io::Writer& operator << (io::Writer& w, GameObjectS const& object);
	io::Reader& operator >> (io::Reader& r, GameObjectS& object);

	void CreateUpdateBlocks(const GameObjectS &object, std::vector<std::vector<char>> &outBlocks);


}
