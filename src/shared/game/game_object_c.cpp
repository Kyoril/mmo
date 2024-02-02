
#include "game_object_c.h"
#include "object_type_id.h"

#include "binary_io/reader.h"
#include "log/default_log_levels.h"
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
		m_entity = m_scene.CreateEntity(std::to_string(GetGuid()), "Models/Mannequin_Edit.hmsh");
		m_sceneNode->AttachObject(*m_entity);

		m_scene.GetRootSceneNode().AddChild(*m_sceneNode);
	}

	void GameObjectC::Update(float deltaTime)
	{
		if (m_movementAnimation)
		{
			bool animationFinished = false;

			m_movementAnimationTime += deltaTime;
			if (m_movementAnimationTime >= m_movementAnimation->GetDuration())
			{
				m_movementAnimationTime = m_movementAnimation->GetDuration();
				animationFinished = true;
			}

			m_sceneNode->SetPosition(m_movementStart);
			m_movementAnimation->Apply(m_movementAnimationTime);

			if (animationFinished)
			{
				m_sceneNode->SetDerivedPosition(m_movementEnd);

				// End animation
				m_movementAnimation.reset();
				m_movementAnimationTime = 0.0f;
			}
		}
	}

	void GameObjectC::SetMovementPath(const std::vector<Vector3>& points)
	{
		m_movementAnimationTime = 0.0f;
		m_movementAnimation.reset();

		if (points.empty())
		{
			return;
		}

		std::vector<Vector3> positions;
		positions.reserve(points.size() + 1);
		std::vector<float> keyFrameTimes;
		keyFrameTimes.reserve(points.size() + 1);

		Vector3 prevPosition = m_sceneNode->GetDerivedPosition();
		m_movementStart = prevPosition;
		DLOG("Movement from " << prevPosition << " to " << points.back());

		// First point
		positions.emplace_back(0.0f, 0.0f, 0.0f);
		keyFrameTimes.push_back(0.0f);
		float totalDuration = 0.0f;

		for(const auto& point : points)
		{
			Vector3 diff = point - prevPosition;
			DLOG("\tDelta: " << diff);
			const float distance = diff.GetLength();
			const float duration = distance / 7.0f;			// TODO: Speed!

			positions.push_back(diff);
			keyFrameTimes.push_back(totalDuration + duration);
			totalDuration += duration;
			prevPosition = point;
		}

		ASSERT(positions.size() == keyFrameTimes.size());

		m_movementAnimation = std::make_unique<Animation>("Movement", totalDuration);
		NodeAnimationTrack* track = m_movementAnimation->CreateNodeTrack(0, m_sceneNode);

		for (size_t i = 0; i < positions.size(); ++i)
		{
			track->CreateNodeKeyFrame(keyFrameTimes[i])->SetTranslate(positions[i]);
		}

		m_movementEnd = prevPosition;
	}

	void GameObjectC::Deserialize(io::Reader& reader)
	{
		// Read movement info


		if (!(m_fieldMap.DeserializeComplete(reader)))
		{
			ASSERT(false);
		}

		ASSERT(GetGuid() > 0);

		SetupSceneObjects();
	}
}
