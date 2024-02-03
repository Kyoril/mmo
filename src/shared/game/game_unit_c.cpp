
#include "game_unit_c.h"

#include "log/default_log_levels.h"

namespace mmo
{
	void GameUnitC::Deserialize(io::Reader& reader, bool complete)
	{
		uint32 updateFlags = 0;
		if (!(reader >> io::read<uint32>(updateFlags)))
		{
			return;
		}

		ASSERT(!complete || (updateFlags & object_update_flags::HasMovementInfo) != 0);
		if (updateFlags & object_update_flags::HasMovementInfo)
		{
			if (!(reader >> m_movementInfo))
			{
				return;
			}
		}

		if (complete)
		{
			if (!(m_fieldMap.DeserializeComplete(reader)))
			{
				ASSERT(false);
			}

			// TODO: Trigger registered field observers
		}
		else
		{
			if (!(m_fieldMap.DeserializeChanges(reader)))
			{
				ASSERT(false);
			}

			// TODO: Trigger registered field observers
			m_fieldMap.MarkAllAsUnchanged();
		}
		

		ASSERT(GetGuid() > 0);
		if (complete)
		{
			SetupSceneObjects();
		}

		if (updateFlags & object_update_flags::HasMovementInfo)
		{
			m_sceneNode->SetDerivedPosition(m_movementInfo.position);
			m_sceneNode->SetDerivedOrientation(Quaternion(m_movementInfo.facing, Vector3::UnitY));
		}
	}

	void GameUnitC::Update(float deltaTime)
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

	void GameUnitC::InitializeFieldMap()
	{
		m_fieldMap.Initialize(object_fields::UnitFieldCount);
	}

	void GameUnitC::StartMove(const bool forward)
	{
		if (forward)
		{
			m_movementInfo.movementFlags |= MovementFlags::Forward;
			m_movementInfo.movementFlags &= ~MovementFlags::Backward;
		}
		else
		{
			m_movementInfo.movementFlags |= MovementFlags::Backward;
			m_movementInfo.movementFlags &= ~MovementFlags::Forward;
		}
	}

	void GameUnitC::StartStrafe(bool left)
	{
		if (left)
		{
			m_movementInfo.movementFlags |= MovementFlags::StrafeLeft;
			m_movementInfo.movementFlags &= ~MovementFlags::StrafeRight;
		}
		else
		{
			m_movementInfo.movementFlags |= MovementFlags::StrafeRight;
			m_movementInfo.movementFlags &= ~MovementFlags::StrafeLeft;
		}
	}

	void GameUnitC::StopMove()
	{
		m_movementInfo.movementFlags &= ~MovementFlags::Moving;
	}

	void GameUnitC::StopStrafe()
	{
		m_movementInfo.movementFlags &= ~MovementFlags::Strafing;
	}

	void GameUnitC::StartTurn(const bool left)
	{
		if (left)
		{
			m_movementInfo.movementFlags |= MovementFlags::TurnLeft;
			m_movementInfo.movementFlags &= ~MovementFlags::TurnRight;
		}
		else
		{
			m_movementInfo.movementFlags |= MovementFlags::TurnRight;
			m_movementInfo.movementFlags &= ~MovementFlags::TurnLeft;
		}
	}

	void GameUnitC::StopTurn()
	{
		m_movementInfo.movementFlags &= ~MovementFlags::Turning;
	}

	void GameUnitC::SetFacing(const Radian& facing)
	{
		m_movementInfo.facing = facing;
	}

	void GameUnitC::SetMovementPath(const std::vector<Vector3>& points)
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

		// First point
		positions.emplace_back(0.0f, 0.0f, 0.0f);
		keyFrameTimes.push_back(0.0f);
		float totalDuration = 0.0f;

		for (const auto& point : points)
		{
			Vector3 diff = point - prevPosition;
			const float distance = diff.GetLength();
			const float duration = distance / 7.0f;			// TODO: Speed!

			positions.push_back(point - m_movementStart);
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
}
