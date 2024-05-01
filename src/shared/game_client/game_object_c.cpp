
#include "game_object_c.h"

#include "game/movement_info.h"
#include "game/object_type_id.h"

#include "binary_io/reader.h"
#include "log/default_log_levels.h"
#include "scene_graph/scene.h"

namespace mmo
{
	GameObjectC::GameObjectC(Scene& scene)
		: m_scene(scene)
		, m_sceneNode(&m_scene.CreateSceneNode())
	{
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
		m_entity = m_scene.CreateEntity(std::to_string(GetGuid()), "Models/Mannequin_Edit.hmsh");
		m_entity->SetUserObject(this);

		//Quaternion rotationOffset;
		//rotationOffset.FromAngleAxis(Degree(90), Vector3::UnitY);
		m_entityOffsetNode = m_sceneNode->CreateChildSceneNode(/*Vector3::Zero, rotationOffset*/);
		m_entityOffsetNode->AttachObject(*m_entity);

		m_scene.GetRootSceneNode().AddChild(*m_sceneNode);
	}

	void GameObjectC::Update(float deltaTime)
	{
		
	}

	void GameObjectC::Deserialize(io::Reader& reader, bool creation)
	{
		uint32 updateFlags = 0;
		if (!(reader >> io::read<uint32>(updateFlags)))
		{
			return;
		}

		if (creation)
		{
			if (!(m_fieldMap.DeserializeComplete(reader)))
			{
				ASSERT(false);
			}
		}
		else
		{
			if (!(m_fieldMap.DeserializeChanges(reader)))
			{
				ASSERT(false);
			}
		}

		ASSERT(GetGuid() > 0);

		SetupSceneObjects();
	}
}
