#pragma once

#include "field_map.h"
#include "game_object_s.h"
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
		signal<void(uint64, uint16, uint16)> fieldsChanged;

	public:
		explicit GameObjectC(Scene& scene);
		~GameObjectC() override;

		virtual void InitializeFieldMap();

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
		FieldMap<uint32> m_fieldMap;
	};
}