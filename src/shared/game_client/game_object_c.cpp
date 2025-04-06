
#include "game_object_c.h"

#include "game/movement_info.h"
#include "game/object_type_id.h"

#include "binary_io/reader.h"
#include "log/default_log_levels.h"
#include "scene_graph/scene.h"
#include "game_unit_c.h"

namespace mmo
{
	GameObjectC::GameObjectC(Scene& scene, const proto_client::Project& project, uint32 map)
		: m_scene(scene)
		, m_project(project)
		, m_sceneNode(&m_scene.CreateSceneNode())
		, m_mapId(map)
	{
	}

	GameObjectC::~GameObjectC()
	{
		if (m_entity)
		{
			m_scene.DestroyEntity(*m_entity);
			m_entity = nullptr;
		}

		if (m_sceneNode)
		{
			m_scene.DestroySceneNode(*m_sceneNode);
			m_sceneNode = nullptr;
		}

		removed();
	}

	GameUnitC& GameObjectC::AsUnit()
	{
		ASSERT(IsUnit());
		return static_cast<GameUnitC&>(*this);
	}

	const GameUnitC& GameObjectC::AsUnit() const
	{
		ASSERT(IsUnit());

		const auto unit = static_cast<const GameUnitC*>(this);
		return *unit;
	}

	void GameObjectC::InitializeFieldMap()
	{
		m_fieldMap.Initialize(object_fields::ObjectFieldCount);
	}

	void GameObjectC::HandleFieldMapChanges()
	{
		uint32 fieldStartIndex = 0;
		uint32 fieldCount = 0;

		// Iterate through field map and search for changed field blocks
		for (size_t i = 0; i < m_fieldMap.GetFieldCount(); ++i)
		{
			// This field was changed?
			if (m_fieldMap.IsFieldMarkedAsChanged(i))
			{
				// Check if it is the start of a block of consecutive changed fields
				if (fieldCount == 0)
				{
					// It is, mark this as the start of the block.
					fieldStartIndex = i;
					fieldCount = 1;
				}
				else
				{
					// Not the start, but another element in a sequence of changed fields
					++fieldCount;
				}
			}
			else
			{
				// Check if we had a changed field before and if so, trigger the end of the block
				if (fieldCount > 0)
				{
					fieldsChanged(GetGuid(), fieldStartIndex, fieldCount);
					fieldCount = 0;
				}
			}
		}

		// One last time: Could be that the last field was changed so we need to check if thats the case and trigger again
		if (fieldCount > 0)
		{
			fieldsChanged(GetGuid(), fieldStartIndex, fieldCount);
			fieldCount = 0;
		}

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

			HandleFieldMapChanges();

			m_fieldMap.MarkAllAsUnchanged();
		}

		ASSERT(GetGuid() > 0);
	}
}
