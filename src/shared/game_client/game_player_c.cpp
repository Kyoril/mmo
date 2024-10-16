
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
			SetTargetAnimState(m_runAnimState);
		}
		else
		{
			SetTargetAnimState(m_idleAnimState);
		}

		// Interpolate
		if (m_targetState != m_currentState)
		{
			if (m_targetState && !m_currentState)
			{
				m_currentState = m_targetState;
				m_targetState = nullptr;

				m_currentState->SetWeight(1.0f);
				m_currentState->SetEnabled(true);
			}
		}

		if (m_currentState && m_targetState)
		{
			m_targetState->SetWeight(m_targetState->GetWeight() + deltaTime * 4.0f);
			m_currentState->SetWeight(1.0f - m_targetState->GetWeight());

			if (m_targetState->GetWeight() >= 1.0f)
			{
				m_targetState->SetWeight(1.0f);
				m_currentState->SetWeight(0.0f);
				m_currentState->SetEnabled(false);

				m_currentState = m_targetState;
				m_targetState = nullptr;
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

	void GamePlayerC::SetTargetAnimState(AnimationState* newTargetState)
	{
		if (m_targetState == newTargetState)
		{
			// Nothing to do here, we are already there
			return;
		}

		if (m_currentState == newTargetState)
		{
			m_targetState = nullptr;
			return;
		}

		m_targetState = newTargetState;
		if (m_targetState)
		{
			m_targetState->SetWeight(m_currentState ? (1.0f - m_currentState->GetWeight()) : 0.0f);
			m_targetState->SetEnabled(true);
		}
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
