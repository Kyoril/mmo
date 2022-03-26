
#include "game_object_c.h"
#include "object_type_id.h"

#include "binary_io/reader.h"
#include "scene_graph/scene.h"

namespace mmo
{
	GameObjectC::GameObjectC(Scene& scene)
		: m_scene(scene)
		, m_sceneNode(&m_scene.CreateSceneNode())
	{
		InitializeFieldMap();
	}

	GameObjectC::~GameObjectC()
	{
		if (m_entity)
		{
			m_scene.DestroyEntity(*m_entity);	
		}

		if (m_sceneNode)
		{
			m_scene.DestroySceneNode(*m_sceneNode);	
		}
	}

	void GameObjectC::InitializeFieldMap()
	{
		m_fieldMap.Initialize(object_fields::ObjectFieldCount);
	}

	void GameObjectC::SetupSceneObjects()
	{
		m_entity = m_scene.CreateEntity(std::to_string(GetGuid()), "Models/Mannequin/Mannequin.hmsh");
		m_sceneNode->AttachObject(*m_entity);

		m_scene.GetRootSceneNode().AddChild(*m_sceneNode);
	}

	void GameObjectC::Deserialize(io::Reader& reader)
	{
		if (!(m_fieldMap.DeserializeComplete(reader)))
		{
			ASSERT(false);
		}

		ASSERT(GetGuid() > 0);

		SetupSceneObjects();
	}
}
