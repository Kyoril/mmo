#pragma once

#include "field_map.h"
#include "base/non_copyable.h"
#include "scene_graph/entity.h"
#include "scene_graph/scene_node.h"
#include "object_type_id.h"

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
		explicit GameObjectC(Scene& scene);
		~GameObjectC() override;

	public:
		virtual void Deserialize(io::Reader& reader);
		
		[[nodiscard]] SceneNode* GetSceneNode() const noexcept { return m_sceneNode; }

		virtual void Update(float deltaTime);

		virtual void SetMovementPath(const std::vector<Vector3>& points);

	protected:
		virtual void InitializeFieldMap();

		virtual void SetupSceneObjects();

	public:
		[[nodiscard]] ObjectGuid GetGuid() const noexcept { return m_fieldMap.GetFieldValue<ObjectGuid>(object_fields::Guid); }
		
	protected:
		Scene& m_scene;
		Entity* m_entity { nullptr };
		SceneNode* m_sceneNode { nullptr };
		FieldMap<uint32> m_fieldMap;

		float m_movementAnimationTime = 0.0f;
		std::unique_ptr<Animation> m_movementAnimation;
		Vector3 m_movementStart;
		Vector3 m_movementEnd;
	};
}