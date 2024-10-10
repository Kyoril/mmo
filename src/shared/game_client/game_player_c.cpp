
#include "game_player_c.h"

#include "log/default_log_levels.h"

namespace mmo
{
	void GamePlayerC::Deserialize(io::Reader& reader, bool complete)
	{
		GameUnitC::Deserialize(reader, complete);

		m_netDriver.GetPlayerName(GetGuid(), std::static_pointer_cast<GamePlayerC>(shared_from_this()));
	}

	void GamePlayerC::Update(float deltaTime)
	{
		GameObjectC::Update(deltaTime);

		// TODO: This needs to be managed differently or it will explode in complexity here!
		if (m_movementInfo.IsMoving())
		{
			if (m_idleAnimState != nullptr)
			{
				m_idleAnimState->SetEnabled(false);
			}
			if (m_runAnimState != nullptr)
			{
				m_runAnimState->SetEnabled(true);
			}
		}
		else
		{
			if (m_idleAnimState != nullptr)
			{
				m_idleAnimState->SetEnabled(true);
			}
			if (m_runAnimState != nullptr)
			{
				m_runAnimState->SetEnabled(false);
			}
		}

		if (m_idleAnimState && m_idleAnimState->IsEnabled())
		{
			m_idleAnimState->AddTime(deltaTime);
		}
		if (m_runAnimState && m_runAnimState->IsEnabled())
		{
			m_runAnimState->AddTime(deltaTime);
		}

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

	const String& GamePlayerC::GetName() const
	{
		if (m_name.empty())
		{
			return GameObjectC::GetName();
		}

		return m_name;
	}

	void GamePlayerC::SetupSceneObjects()
	{
		GameUnitC::SetupSceneObjects();

		if (m_entity->HasAnimationState("Idle"))
		{
			m_idleAnimState = m_entity->GetAnimationState("Idle");
		}

		if (m_entity->HasAnimationState("Run"))
		{
			m_runAnimState = m_entity->GetAnimationState("Run");
		}
	}
}
