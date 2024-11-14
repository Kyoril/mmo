
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
		Quaternion rotationOffset;
		rotationOffset.FromAngleAxis(Degree(90), Vector3::UnitY);
		m_entityOffsetNode = m_sceneNode->CreateChildSceneNode(Vector3::Zero, rotationOffset);

		m_scene.GetRootSceneNode().AddChild(*m_sceneNode);

		const float scale = Get<float>(object_fields::Scale);
		m_sceneNode->SetScale(Vector3(scale, scale, scale));

	}

	void GameObjectC::Update(float deltaTime)
	{
		
	}

	bool GameObjectC::CanBeLooted() const
	{
		return false;
	}

	bool GameObjectC::IsWithinRange(GameObjectC& other, float range) const
	{
		const auto diff = other.GetPosition() - GetPosition();
		return diff.GetSquaredLength() <= range * range;
	}

	const String& GameObjectC::GetName() const
	{
		static String unknown = "Unknown";
		return unknown;
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

			SetupSceneObjects();
		}
		else
		{
			if (!(m_fieldMap.DeserializeChanges(reader)))
			{
				ASSERT(false);
			}
		}

		ASSERT(GetGuid() > 0);
	}
}
