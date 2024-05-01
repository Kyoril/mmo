
#include "game_player_c.h"

#include "log/default_log_levels.h"

namespace mmo
{
	void GamePlayerC::Deserialize(io::Reader& reader, bool complete)
	{
		GameUnitC::Deserialize(reader, complete);
	}

	void GamePlayerC::Update(float deltaTime)
	{
		GameObjectC::Update(deltaTime);

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

	void GamePlayerC::InitializeFieldMap()
	{
		m_fieldMap.Initialize(object_fields::PlayerFieldCount);
	}
}
