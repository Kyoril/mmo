
#include "game_unit_c.h"

#include "object_mgr.h"
#include "base/clock.h"
#include "log/default_log_levels.h"
#include "scene_graph/scene.h"
#include "shared/client_data/model_data.pb.h"

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
		}
		else
		{
			if (!(m_fieldMap.DeserializeChanges(reader)))
			{
				ASSERT(false);
			}

			const int32 startIndex = m_fieldMap.GetFirstChangedField();
			const int32 endIndex = m_fieldMap.GetLastChangedField();
			ASSERT(endIndex >= startIndex);
			if (startIndex >= 0 && endIndex >= 0)
			{
				fieldsChanged(GetGuid(), startIndex, (endIndex - startIndex) + 1);
			}

			m_fieldMap.MarkAllAsUnchanged();
		}

		reader
			>> io::read<float>(m_unitSpeed[movement_type::Walk])
			>> io::read<float>(m_unitSpeed[movement_type::Run])
			>> io::read<float>(m_unitSpeed[movement_type::Backwards])
			>> io::read<float>(m_unitSpeed[movement_type::Swim])
			>> io::read<float>(m_unitSpeed[movement_type::SwimBackwards])
			>> io::read<float>(m_unitSpeed[movement_type::Flight])
			>> io::read<float>(m_unitSpeed[movement_type::FlightBackwards])
			>> io::read<float>(m_unitSpeed[movement_type::Turn]);

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

		// TODO: Get npc name
		int32 entryId = Get<int32>(object_fields::Entry);
		if (entryId != -1)
		{
			m_netDriver.GetCreatureData(entryId, std::static_pointer_cast<GameUnitC>(shared_from_this()));
		}
	}

	void GameUnitC::Update(float deltaTime)
	{
		GameObjectC::Update(deltaTime);

		if (m_movementAnimation)
		{
			bool animationFinished = false;

			SetTargetAnimState(m_runAnimState);

			m_movementAnimationTime += deltaTime;
			if (m_movementAnimationTime >= m_movementAnimation->GetDuration())
			{
				m_movementAnimationTime = m_movementAnimation->GetDuration();
				animationFinished = true;
			}

			m_sceneNode->SetPosition(m_movementStart);
			m_sceneNode->SetOrientation(m_movementStartRot);
			m_movementAnimation->Apply(m_movementAnimationTime);

			if (animationFinished)
			{
				SetTargetAnimState(m_idleAnimState);
				m_sceneNode->SetDerivedPosition(m_movementEnd);

				// End animation
				m_movementAnimation.reset();
				m_movementAnimationTime = 0.0f;
			}
		}
		else
		{
			// TODO: This needs to be managed differently or it will explode in complexity here!
			if (m_movementInfo.IsMoving())
			{
				SetTargetAnimState(m_runAnimState);
			}
			else
			{
				SetTargetAnimState(m_idleAnimState);
			}
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
	}

	void GameUnitC::ApplyMovementInfo(const MovementInfo& movementInfo)
	{
		m_movementInfo = movementInfo;

		// Instantly apply movement data for now
		GetSceneNode()->SetDerivedPosition(movementInfo.position);
		GetSceneNode()->SetDerivedOrientation(Quaternion(Radian(movementInfo.facing), Vector3::UnitY));

	}

	void GameUnitC::InitializeFieldMap()
	{
		m_fieldMap.Initialize(object_fields::UnitFieldCount);
	}

	void GameUnitC::SetupSceneObjects()
	{
		GameObjectC::SetupSceneObjects();

		// Load display id value and resolve it
		const uint32 displayId = Get<uint32>(object_fields::DisplayId);
		const proto_client::ModelDataEntry* modelEntry = ObjectMgr::GetModelData(displayId);
		if (!modelEntry)
		{
			ELOG("Unable to resolve display id " << displayId << " for unit");
			return;
		}

		m_entity = m_scene.CreateEntity(std::to_string(GetGuid()), modelEntry->filename());
		m_entity->SetUserObject(this);
		m_entityOffsetNode->AttachObject(*m_entity);

		if (m_entity->HasAnimationState("Idle"))
		{
			m_idleAnimState = m_entity->GetAnimationState("Idle");
		}

		if (m_entity->HasAnimationState("Run"))
		{
			m_runAnimState = m_entity->GetAnimationState("Run");
		}
	}

	void GameUnitC::StartMove(const bool forward)
	{
		if (forward)
		{
			m_movementInfo.movementFlags |= movement_flags::Forward;
			m_movementInfo.movementFlags &= ~movement_flags::Backward;
		}
		else
		{
			m_movementInfo.movementFlags |= movement_flags::Backward;
			m_movementInfo.movementFlags &= ~movement_flags::Forward;
		}
	}

	void GameUnitC::StartStrafe(bool left)
	{
		if (left)
		{
			m_movementInfo.movementFlags |= movement_flags::StrafeLeft;
			m_movementInfo.movementFlags &= ~movement_flags::StrafeRight;
		}
		else
		{
			m_movementInfo.movementFlags |= movement_flags::StrafeRight;
			m_movementInfo.movementFlags &= ~movement_flags::StrafeLeft;
		}
	}

	void GameUnitC::StopMove()
	{
		m_movementInfo.movementFlags &= ~movement_flags::Moving;
	}

	void GameUnitC::StopStrafe()
	{
		m_movementInfo.movementFlags &= ~movement_flags::Strafing;
	}

	void GameUnitC::StartTurn(const bool left)
	{
		if (left)
		{
			m_movementInfo.movementFlags |= movement_flags::TurnLeft;
			m_movementInfo.movementFlags &= ~movement_flags::TurnRight;
		}
		else
		{
			m_movementInfo.movementFlags |= movement_flags::TurnRight;
			m_movementInfo.movementFlags &= ~movement_flags::TurnLeft;
		}
	}

	void GameUnitC::StopTurn()
	{
		m_movementInfo.movementFlags &= ~movement_flags::Turning;
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

		const Vector3 targetPos = m_movementStart - points[0];
		const Radian targetAngle = GetAngle(targetPos.x, targetPos.z);

		const Quaternion prevRotation = Quaternion(targetAngle, Vector3::UnitY);
		m_movementStartRot = prevRotation;
		m_sceneNode->SetOrientation(prevRotation);

		// First point
		positions.emplace_back(0.0f, 0.0f, 0.0f);
		keyFrameTimes.push_back(0.0f);
		float totalDuration = 0.0f;

		for (const auto& point : points)
		{
			Vector3 diff = point - prevPosition;
			const float distance = diff.GetLength();
			const float duration = distance / m_unitSpeed[movement_type::Run];

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
			const auto frame = track->CreateNodeKeyFrame(keyFrameTimes[i]);
			frame->SetTranslate(positions[i]);
		}

		m_movementEnd = prevPosition;
	}

	void GameUnitC::SetTargetUnit(const std::shared_ptr<GameUnitC>& targetUnit)
	{
		m_targetUnit = targetUnit;
	}

	void GameUnitC::SetInitialSpells(const std::vector<const proto_client::SpellEntry*>& spells)
	{
		m_spells = spells;
	}

	void GameUnitC::LearnSpell(const proto_client::SpellEntry* spell)
	{
		const auto it = std::find_if(m_spells.begin(), m_spells.end(), [spell](const proto_client::SpellEntry* entry) { return entry->id() == spell->id(); });
		if (it == m_spells.end())
		{
			m_spells.push_back(spell);
		}
	}

	void GameUnitC::UnlearnSpell(const uint32 spellId)
	{
		std::erase_if(m_spells, [spellId](const proto_client::SpellEntry* entry) { return entry->id() == spellId; });
	}

	const proto_client::SpellEntry* GameUnitC::GetSpell(uint32 index) const
	{
		if (index < m_spells.size())
		{
			return m_spells[index];
		}

		return nullptr;
	}

	void GameUnitC::Attack(GameUnitC& victim)
	{
		// Don't do anything if the victim is already the current target
		if (IsAttacking(victim))
		{
			return;
		}

		// We cant attack ourselves
		if (&victim == this)
		{
			return;
		}

		// Ensure that we are targeting the victim right now
		// TODO

		// Send attack
		m_victim = victim.GetGuid();
		m_netDriver.SendAttackStart(victim.GetGuid(), GetAsyncTimeMs());
	}

	void GameUnitC::StopAttack()
	{
		if (!IsAttacking())
		{
			return;
		}

		// Send stop attack
		m_victim = 0;
		m_netDriver.SendAttackStop(GetAsyncTimeMs());
	}

	const String& GameUnitC::GetName() const
	{
		if (m_creatureInfo.name.empty())
		{
			return GameObjectC::GetName();
		}

		return m_creatureInfo.name;
	}

	void GameUnitC::SetTargetAnimState(AnimationState* newTargetState)
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
}
