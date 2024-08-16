#pragma once

#include "game/field_map.h"
#include "base/non_copyable.h"
#include "scene_graph/entity.h"
#include "scene_graph/scene_node.h"
#include "game/object_type_id.h"

namespace io
{
	class Reader;
}

namespace mmo
{
	class Scene;

	/// @brief Represents a game object at the client.
	class GameObjectC : public NonCopyable
	{
	public:
		signal<void(uint64, uint16, uint16)> fieldsChanged;

	public:
		explicit GameObjectC(Scene& scene);
		~GameObjectC() override;

		template<typename T>
		T Get(const uint32 field) const
		{
			return m_fieldMap.GetFieldValue<T>(field);
		}

		virtual void InitializeFieldMap();

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
			const float dx = x - position.x;
			const float dz = z - position.z;

			float ang = ::atan2(dz, dx);

			ang = (ang >= 0) ? ang : 2 * Pi + ang;
			return Radian(ang);
		}

	public:
		virtual void Deserialize(io::Reader& reader, bool complete);
		
		[[nodiscard]] SceneNode* GetSceneNode() const noexcept { return m_sceneNode; }

		virtual void Update(float deltaTime);

	protected:
		virtual void SetupSceneObjects();

	public:
		[[nodiscard]] ObjectGuid GetGuid() const noexcept { return m_fieldMap.GetFieldValue<ObjectGuid>(object_fields::Guid); }
		
	protected:
		Scene& m_scene;
		Entity* m_entity { nullptr };
		SceneNode* m_sceneNode { nullptr };
		SceneNode* m_entityOffsetNode{ nullptr };
		FieldMap<uint32> m_fieldMap;
	};
}