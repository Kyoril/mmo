#pragma once

#include "game/field_map.h"
#include "base/non_copyable.h"
#include "scene_graph/entity.h"
#include "scene_graph/scene_node.h"
#include "game/object_type_id.h"

#include <memory>

namespace io
{
	class Reader;
}

namespace mmo
{
	namespace proto_client
	{
		class Project;
	}

	class Scene;
	class GameUnitC;

	/// @brief Represents a game object at the client.
	class GameObjectC : public NonCopyable, public std::enable_shared_from_this<GameObjectC>
	{
	public:
		signal<void(uint64, uint16, uint16)> fieldsChanged;
		signal<void()> removed;

	public:
		explicit GameObjectC(Scene& scene, const proto_client::Project& project, uint32 map);

		~GameObjectC() override;

		[[nodiscard]] bool IsPlayer() const { return GetTypeId() == ObjectTypeId::Player; }

		[[nodiscard]] bool IsUnit() const { return IsPlayer() || GetTypeId() == ObjectTypeId::Unit; }

		[[nodiscard]] bool IsItem() const { return IsContainer() || GetTypeId() == ObjectTypeId::Item; }

		[[nodiscard]] bool IsContainer() const { return GetTypeId() == ObjectTypeId::Container; }

		[[nodiscard]] bool IsWorldObject() const { return GetTypeId() == ObjectTypeId::Object; }

		uint32 GetMapId() const { return m_mapId; }

		GameUnitC& AsUnit();

		const GameUnitC& AsUnit() const;

		virtual ObjectTypeId GetTypeId() const
		{
			return ObjectTypeId::Object;
		}

		template<typename T>
		T Get(const uint32 field) const
		{
			return m_fieldMap.GetFieldValue<T>(field);
		}
		
		virtual void InitializeFieldMap();

		/// Determines whether an object field in the synchronized field map was marked as changed. Useful in fieldsChanged callbacks or
		///	mirror handler connections created with the RegisterMirrorHandler method to check if a specific field was changed.
		[[nodiscard]] bool WasChanged(const FieldMap<uint32>::FieldIndexType field) const
		{
			return m_fieldMap.IsFieldMarkedAsChanged(field);
		}

		/// Registers a callback handler that is called when some object field values change.
		template <class Handler>
		connection RegisterMirrorHandler(uint32 field, uint32 fieldCount, Handler handler)
		{
			ASSERT(field < m_fieldMap.GetFieldCount());
			ASSERT(fieldCount > 0);
			ASSERT(field + fieldCount < m_fieldMap.GetFieldCount());

			return fieldsChanged.connect([monitoredField = field, monitoredFieldCount = fieldCount, handler](uint64 guid, uint16 fieldIndex, uint16 fieldCount)
				{
					ASSERT(fieldCount > 0);

					if (const bool rangesOverlap =
						static_cast<int32>(fieldIndex + fieldCount - 1) >= static_cast<int32>(monitoredField) &&
						fieldIndex <= monitoredField + monitoredFieldCount)
					{
						// The monitored field has changed, so we need to update the mirror field.
						// This will trigger the handler.
						handler(guid);
					}
				});
		}

		/// Registers a callback handler that is called when some object field values change.
		template <class Instance, class Class, class... Args1>
		connection RegisterMirrorHandler(uint32 field, uint32 fieldCount, Instance& object, void(Class::* method)(Args1...))
		{
			ASSERT(field < m_fieldMap.GetFieldCount());
			ASSERT(fieldCount > 0);
			ASSERT(field + fieldCount < m_fieldMap.GetFieldCount());

			const auto handler = [&object, method](Args1... args)
				{
					(object.*method)(args...);
				};

			return fieldsChanged.connect([monitoredField = field, monitoredFieldCount = fieldCount, handler](uint64 guid, uint16 fieldIndex, uint16 fieldCount)
				{
					ASSERT(fieldCount > 0);

					if (const bool rangesOverlap =
						static_cast<int32>(fieldIndex + fieldCount - 1) >= static_cast<int32>(monitoredField) &&
						fieldIndex <= monitoredField + monitoredFieldCount)
					{
						// The monitored field has changed, so we need to update the mirror field.
						// This will trigger the handler.
						handler(guid);
					}
				});
		}

	protected:
		void HandleFieldMapChanges();

	public:
		[[nodiscard]] const Vector3& GetPosition() const
		{
			return m_sceneNode->GetDerivedPosition();
		}

		[[nodiscard]] Radian GetFacing() const
		{
			return m_sceneNode->GetDerivedOrientation().GetYaw();
		}

		[[nodiscard]] Radian GetAngle(const GameObjectC& other) const
		{
			return GetAngle(other.GetPosition().x, other.GetPosition().z);
		}

		[[nodiscard]] Radian GetAngle(const float x, const float z) const
		{
			const auto& position = GetPosition();
			return GetAngle(position.x, position.z, x, z);
		}

		[[nodiscard]] Radian GetAngle(const float fromX, const float fromZ, const float toX, const float toZ) const
		{
			const float dx = toX - fromX;
			const float dz = toZ - fromZ;

			float ang = ::atan2(-dz, dx);

			if (ang < 0)
			{
				ang += 2 * Pi;
			}

			return Radian(ang);
		}

		bool IsWithinRange(GameObjectC& other, float range) const;

		virtual const String& GetName() const;

	public:
		virtual void Deserialize(io::Reader& reader, bool complete);
		
		[[nodiscard]] SceneNode* GetSceneNode() const { return m_sceneNode; }

		virtual void Update(float deltaTime);

		virtual bool CanBeLooted() const;

	protected:
		virtual void SetupSceneObjects();

	public:
		[[nodiscard]] ObjectGuid GetGuid() const { return m_fieldMap.GetFieldValue<ObjectGuid>(object_fields::Guid); }
		
	protected:
		Scene& m_scene;
		const proto_client::Project& m_project;
		Entity* m_entity { nullptr };
		SceneNode* m_sceneNode { nullptr };
		SceneNode* m_entityOffsetNode{ nullptr };
		FieldMap<uint32> m_fieldMap;
		uint32 m_mapId{ 0 };
	};
}