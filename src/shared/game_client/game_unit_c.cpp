
#include "game_unit_c.h"

#include "object_mgr.h"
#include "base/clock.h"
#include "client_data/project.h"
#include "game/spell.h"
#include "log/default_log_levels.h"
#include "math/collision.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/scene.h"
#include "shared/client_data/proto_client/factions.pb.h"
#include "shared/client_data/proto_client/faction_templates.pb.h"
#include "shared/client_data/proto_client/model_data.pb.h"

namespace mmo
{
	GameAuraC::GameAuraC(GameUnitC& owner, const proto_client::SpellEntry& spell, const uint64 caster, const GameTime expiration)
		: m_spell(&spell)
		, m_expiration(0)
		, m_casterId(caster)
		, m_targetId(owner.GetGuid())
	{
		if (expiration > 0)
		{
			m_expiration = GetAsyncTimeMs() + expiration;
		}

		m_onOwnerRemoved = owner.removed.connect([this]() { removed(); });
	}

	GameAuraC::~GameAuraC()
	{
		removed();
	}

	bool GameAuraC::IsExpired() const
	{
		if (!CanExpire())
		{
			return false;
		}

		return GetAsyncTimeMs() >= m_expiration;
	}

	void GameUnitC::Deserialize(io::Reader& reader, const bool complete)
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

			OnEntryChanged();
		}
		else
		{
			if (!(m_fieldMap.DeserializeChanges(reader)))
			{
				ASSERT(false);
			}

			if (!complete && m_fieldMap.IsFieldMarkedAsChanged(object_fields::DisplayId))
			{
				OnDisplayIdChanged();
			}

			if (!complete && m_fieldMap.IsFieldMarkedAsChanged(object_fields::Scale))
			{
				OnScaleChanged();
			}

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

		if (complete || m_fieldMap.IsFieldMarkedAsChanged(object_fields::FactionTemplate))
		{
			OnFactionTemplateChanged();
		}

		if (complete || m_fieldMap.IsFieldMarkedAsChanged(object_fields::Entry))
		{
			OnEntryChanged();
		}

		m_fieldMap.MarkAllAsUnchanged();

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
	}

	void GameUnitC::Update(const float deltaTime)
	{
		GameObjectC::Update(deltaTime);

		if (m_questGiverNode != nullptr)
		{
			// TODO: Get rotation to the current camera and yaw the icon to face it!
			Camera* cam = m_scene.GetCamera(0);
			if (cam)
			{
				m_questGiverNode->SetFixedYawAxis(true);
				m_questGiverNode->LookAt(cam->GetDerivedPosition(), TransformSpace::World);
			}
		}

		const bool isDead = GetHealth() <= 0;
		if (m_movementAnimation)
		{
			bool animationFinished = false;
			if (!isDead)
			{
				SetTargetAnimState(m_casting ? m_castingState : m_runAnimState);
			}

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
				if (!isDead)
				{
					const bool isAttacking = (Get<uint32>(object_fields::Flags) & unit_flags::Attacking) != 0;

					AnimationState* idleAnim = isAttacking ? m_readyAnimState : m_idleAnimState;
					SetTargetAnimState(m_casting ? m_castingState : idleAnim);
				}

				m_sceneNode->SetDerivedPosition(m_movementEnd);

				// End animation
				m_movementAnimation.reset();
				m_movementAnimationTime = 0.0f;
			}
		}
		else if (!isDead)
		{
			ApplyLocalMovement(deltaTime);

			// TODO: This needs to be managed differently or it will explode in complexity here!
			if (m_movementInfo.IsMoving())
			{
				SetTargetAnimState(m_casting ? m_castingState : m_runAnimState);
			}
			else
			{
				const bool isAttacking = (Get<uint32>(object_fields::Flags) & unit_flags::Attacking) != 0;
				AnimationState* idleAnim = isAttacking ? m_readyAnimState : m_idleAnimState;

				SetTargetAnimState(m_casting ? m_castingState : idleAnim);
			}
		}

		// Play back one shot animations
		if (m_oneShotState)
		{
			if (m_currentState) m_currentState->SetWeight(0.0f);
			if (m_targetState) m_targetState->SetWeight(0.0f);

			if (m_oneShotState->HasEnded())
			{
				// Transition back to current state
				m_oneShotState->SetWeight(m_oneShotState->GetWeight() - deltaTime * 4.0f);
				if (m_targetState)
				{
					m_targetState->SetWeight(1.0f - m_oneShotState->GetWeight());
				}
				else if (m_currentState)
				{
					m_currentState->SetWeight(1.0f - m_oneShotState->GetWeight());
				}

				if (m_oneShotState->GetWeight() <= 0.0f)
				{
					if (m_targetState)
					{
						m_targetState->SetWeight(1.0f);
					}
					else if (m_currentState)
					{
						m_currentState->SetWeight(1.0f);
					}

					m_oneShotState->SetEnabled(false);
					m_oneShotState->SetWeight(0.0f);
					m_oneShotState = nullptr;
				}
			}
		}

		if (m_currentState != nullptr)
		{
			m_currentState->SetEnabled(m_oneShotState == nullptr || m_oneShotState->HasEnded());
		}
		if (m_targetState != nullptr)
		{
			m_targetState->SetEnabled(m_oneShotState == nullptr || m_oneShotState->HasEnded());
		}

		// Always force dead state
		if (isDead)
		{
			SetTargetAnimState(m_deathState);
		}

		// Interpolate
		if (!m_oneShotState)
		{
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
					m_currentState->SetWeight(0.0f);
					m_currentState->SetEnabled(false);

					m_currentState = m_targetState;
					m_targetState = nullptr;
				}
			}
		}

		// Update animation states
		if (m_currentState && m_currentState->IsEnabled())
		{
			m_currentState->AddTime(deltaTime);
		}
		if (m_targetState && m_targetState->IsEnabled())
		{
			m_targetState->AddTime(deltaTime);
		}
		if (m_oneShotState && m_oneShotState->IsEnabled())
		{
			m_oneShotState->AddTime(deltaTime);
		}
	}

	void GameUnitC::ApplyLocalMovement(const float deltaTime)
	{
		auto* playerNode = GetSceneNode();

		if (m_movementInfo.IsTurning())
		{
			if (m_movementInfo.movementFlags & movement_flags::TurnLeft)
			{
				playerNode->Yaw(Radian(GetSpeed(movement_type::Turn)) * deltaTime, TransformSpace::World);
			}
			else if (m_movementInfo.movementFlags & movement_flags::TurnRight)
			{
				playerNode->Yaw(Radian(-GetSpeed(movement_type::Turn)) * deltaTime, TransformSpace::World);
			}
			m_movementInfo.facing = GetSceneNode()->GetDerivedOrientation().GetYaw();
		}

		if (m_movementInfo.IsMoving())
		{
			Vector3 movementVector;

			if (m_movementInfo.movementFlags & movement_flags::Forward)
			{
				movementVector.x += 1.0f;
			}
			if (m_movementInfo.movementFlags & movement_flags::Backward)
			{
				movementVector.x -= 1.0f;
			}
			if (m_movementInfo.movementFlags & movement_flags::StrafeLeft)
			{
				movementVector.z -= 1.0f;
			}
			if (m_movementInfo.movementFlags & movement_flags::StrafeRight)
			{
				movementVector.z += 1.0f;
			}

			MovementType movementType = movement_type::Run;
			if (movementVector.x < 0.0)
			{
				movementType = movement_type::Backwards;
			}

			playerNode->Translate(movementVector.NormalizedCopy() * GetSpeed(movementType) * deltaTime, TransformSpace::Local);
			m_movementInfo.position = playerNode->GetDerivedPosition();
			UpdateCollider();
		}

		if (m_movementInfo.movementFlags & movement_flags::Falling)
		{
			constexpr float gravity = 19.291105f;
			m_movementInfo.jumpVelocity -= gravity * deltaTime;
			m_movementInfo.position.y += m_movementInfo.jumpVelocity * deltaTime;
			playerNode->SetPosition(m_movementInfo.position);
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

	void GameUnitC::SetQuestgiverStatus(const QuestgiverStatus status)
	{
		if (status == questgiver_status::None)
		{
			if (m_questGiverEntity)
			{
				m_scene.DestroyEntity(*m_questGiverEntity);
				m_questGiverEntity = nullptr;
			}

			if (m_questGiverNode)
			{
				m_scene.DestroySceneNode(*m_questGiverNode);
				m_questGiverNode = nullptr;
			}

			return;
		}

		const String exclamationMesh = "Models/QuestExclamationMark.hmsh";
		const String rewardMesh = "Models/QuestCompleteMark.hmsh";

		switch(status)
		{
		case questgiver_status::Unavailable:
			SetQuestGiverMesh(exclamationMesh);
			m_questGiverEntity->SetMaterial(MaterialManager::Get().Load("Models/QuestInactive_Inst.hmi"));
			break;
		case questgiver_status::Available:
			SetQuestGiverMesh(exclamationMesh);
			m_questGiverEntity->SetMaterial(MaterialManager::Get().Load("Models/QuestMaterialBase.hmat"));
			break;
		case questgiver_status::AvailableRep:
			SetQuestGiverMesh(exclamationMesh);
			m_questGiverEntity->SetMaterial(MaterialManager::Get().Load("Models/QuestRepeatable_Inst.hmat"));
			break;
		case questgiver_status::Incomplete:
			SetQuestGiverMesh(rewardMesh);
			m_questGiverEntity->SetMaterial(MaterialManager::Get().Load("Models/QuestInactive_Inst.hmi"));
			break;
		case questgiver_status::Reward:
		case questgiver_status::RewardNoDot:
			SetQuestGiverMesh(rewardMesh);
			m_questGiverEntity->SetMaterial(MaterialManager::Get().Load("Models/QuestMaterialBase.hmat"));
			break;
		case questgiver_status::RewardRep:
			SetQuestGiverMesh(rewardMesh);
			m_questGiverEntity->SetMaterial(MaterialManager::Get().Load("Models/QuestRepeatable_Inst.hmat"));
			break;
		}
	}

	bool GameUnitC::OnAuraUpdate(io::Reader& reader)
	{
		uint32 visibleAuraCount = 0;
		if (!(reader >> io::read<uint32>(visibleAuraCount)))
		{
			return false;
		}

		m_auras.clear();

		for (uint32 i = 0; i < visibleAuraCount; ++i)
		{
			uint32 spellId, duration;
			uint64 casterId;
			uint8 auraTypeCount;

			if (!(reader >> io::read<uint32>(spellId)
				>> io::read<uint32>(duration)
				>> io::read_packed_guid(casterId)
				>> io::read<uint8>(auraTypeCount)))
			{
				ELOG("Failed to read aura data for unit " << log_hex_digit(GetGuid()));
				return false;
			}

			std::vector<int32> basePoints;
			basePoints.resize(auraTypeCount);
			if (!(reader >> io::read_range(basePoints)))
			{
				ELOG("Failed to read aura base points");
				return false;
			}

			const proto_client::SpellEntry* spell = m_project.spells.getById(spellId);
			if (!spell)
			{
				ELOG("Failed to find spell for aura!");
				continue;
			}

			// Add aura
			m_auras.push_back(std::make_unique<GameAuraC>(*this, *spell, casterId, duration));
		}

		return reader;
	}

	void GameUnitC::SetupSceneObjects()
	{
		GameObjectC::SetupSceneObjects();

		// Attach text component
		m_nameComponentNode = m_sceneNode->CreateChildSceneNode(Vector3::UnitZ * 2.0f);
		m_nameComponent = std::make_unique<WorldTextComponent>(nullptr, GetName());
		m_nameComponentNode->AttachObject(*m_nameComponent);

		// Setup object display
		OnDisplayIdChanged();
	}

	bool GameUnitC::CanStepUp(const Vector3& collisionNormal, float penetrationDepth)
	{
		// Only attempt to step up if the obstacle is in front
		if (collisionNormal.y > 0.0f)
		{
			// The collision is with the ground or a slope, not a vertical obstacle
			return false;
		}

		// TODO: Raycast up?

		// No obstacle above, can attempt to step up
		return true;
	}

	void GameUnitC::OnEntryChanged()
	{
		int32 entryId = Get<int32>(object_fields::Entry);
		if (entryId != -1)
		{
			m_netDriver.GetCreatureData(entryId, std::static_pointer_cast<GameUnitC>(shared_from_this()));
		}
	}

	void GameUnitC::OnScaleChanged() const
	{
		if (!m_sceneNode)
		{
			return;
		}

		const float scale = Get<float>(object_fields::Scale);
		m_sceneNode->SetScale(Vector3(scale, scale, scale));
	}

	void GameUnitC::OnFactionTemplateChanged()
	{
		m_faction = nullptr;

		const uint32 factionTemplateId = Get<uint32>(object_fields::FactionTemplate);
		m_factionTemplate = m_project.factionTemplates.getById(factionTemplateId);
		ASSERT(m_factionTemplate);

		if (m_factionTemplate)
		{
			m_faction = m_project.factions.getById(m_factionTemplate->faction());
		}
	}

	void GameUnitC::SetQuestGiverMesh(const String& meshName)
	{
		if (!m_questGiverEntity)
		{
			m_questGiverEntity = m_scene.CreateEntity(GetSceneNode()->GetName() + "_QuestStatus", meshName);
			ASSERT(m_questGiverEntity);
		}
		else
		{
			m_questGiverEntity->SetMesh(MeshManager::Get().Load(meshName));
		}

		// Ideal size is a unit with a size of 2 units in height, but if the unit is bigger we want to offset the icon position as well as scale it up
		// so that for very big models its not just that tiny icon floating in the sky above some giant head or something of that
		float height = 2.0f;
		float scale = 1.0f;
		if (m_entity)
		{
			height = m_entity->GetBoundingBox().GetExtents().y * 2.2f;
			scale = height / 2.0f;
		}

		if (!m_questGiverNode)
		{
			m_questGiverNode = m_sceneNode->CreateChildSceneNode(Vector3::UnitY * height);
			ASSERT(m_questGiverNode);

			m_questGiverNode->SetScale(Vector3::UnitScale * scale);
			m_questGiverNode->AttachObject(*m_questGiverEntity);
		}
		else
		{
			m_questGiverNode->SetPosition(Vector3::UnitY * height);
			m_questGiverNode->SetScale(Vector3::UnitScale * scale);
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
		m_movementInfo.movementFlags &= ~(movement_flags::Forward | movement_flags::Backward | movement_flags::StrafeLeft | movement_flags::StrafeRight );
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

		if (m_sceneNode)
		{
			m_sceneNode->SetOrientation(Quaternion(facing, Vector3::UnitY));
		}
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

		const Vector3 targetPos = points.back();
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

	static void SetQueryMask(SceneNode* node, const uint32 mask)
	{
		for (uint32 i = 0; i < node->GetNumAttachedObjects(); ++i)
		{
			node->GetAttachedObject(i)->SetQueryFlags(mask);
		}

		for (uint32 i = 0; i < node->GetNumChildren(); ++i)
		{
			SetQueryMask(static_cast<SceneNode*>(node->GetChild(i)), mask);
		}
	}

	void GameUnitC::SetQueryMask(const uint32 mask)
	{
		mmo::SetQueryMask(m_sceneNode, mask);
	}

	bool GameUnitC::CanBeLooted() const
	{
		// Check if lootable flag is set!
		return (Get<uint32>(object_fields::Flags) & unit_flags::Lootable) != 0;
	}

	void GameUnitC::NotifySpellCastStarted()
	{
		m_casting = true;
	}

	void GameUnitC::NotifySpellCastCancelled()
	{
		// Interrupt the one shot state if there is any
		if (m_oneShotState)
		{
			m_oneShotState->SetEnabled(false);
			m_oneShotState->SetWeight(0.0f);
			m_oneShotState = nullptr;
		}

		m_casting = false;
	}

	void GameUnitC::NotifySpellCastSucceeded()
	{
		PlayOneShotAnimation(m_castReleaseState);
		m_casting = false;
	}

	int32 GameUnitC::GetPower(const int32 powerType) const
	{
		if (powerType < 0 || powerType >= power_type::Health)
		{
			return 0;
		}

		return Get<int32>(object_fields::Mana + powerType);
	}

	int32 GameUnitC::GetMaxPower(const int32 powerType) const
	{
		if (powerType < 0 || powerType >= power_type::Health)
		{
			return 0;
		}

		return Get<int32>(object_fields::MaxMana + powerType);
	}

	int32 GameUnitC::GetStat(int32 statId) const
	{
		if (statId < 0 || statId >= 5)
		{
			return 0;
		}

		return Get<int32>(object_fields::StatStamina + statId);
	}

	int32 GameUnitC::GetPosStat(int32 statId) const
	{
		if (statId < 0 || statId >= 5)
		{
			return 0;
		}

		return Get<int32>(object_fields::PosStatStamina + statId);
	}

	int32 GameUnitC::GetNegStat(int32 statId) const
	{
		if (statId < 0 || statId >= 5)
		{
			return 0;
		}

		return Get<int32>(object_fields::NegStatStamina + statId);
	}

	float GameUnitC::GetArmorReductionFactor() const
	{
		float armor = static_cast<float>(GetArmor());
		if (armor < 0.0f)
		{
			armor = 0.0f;
		}

		// If factor is 0.6, damage is reduced by 60%
		const float factor = armor / (armor + 400.0f + GetLevel() * 85.0f);
		return Clamp(factor, 0.0f, 0.75f);
	}

	GameAuraC* GameUnitC::GetAura(uint32 index) const
	{
		if (index < m_auras.size())
		{
			return m_auras[index].get();
		}

		return nullptr;
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
		NotifyAttackStopped();
		m_netDriver.SendAttackStop(GetAsyncTimeMs());
	}

	void GameUnitC::NotifyAttackStopped()
	{
		m_victim = 0;
	}

	void GameUnitC::SetCreatureInfo(const CreatureInfo& creatureInfo)
	{
		m_creatureInfo = creatureInfo;

		// Ensure name component is updated to display the correct name
		if (m_nameComponent)
		{
			m_nameComponent->SetText(GetName());
		}
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
			// Cancel any ongoing transition
			if (m_targetState)
			{
				m_targetState->SetWeight(0.0f);
				m_targetState->SetEnabled(false);
				m_targetState = nullptr;
			}

			// Ensure current state's weight is 1.0f
			m_currentState->SetWeight(1.0f);
			return;
		}

		// Reset any existing target state
		if (m_targetState)
		{
			m_targetState->SetWeight(0.0f);
			m_targetState->SetEnabled(false);
		}

		m_targetState = newTargetState;
		if (m_targetState)
		{
			m_targetState->SetWeight(m_currentState ? (1.0f - m_currentState->GetWeight()) : 0.0f);
			m_targetState->SetEnabled(true);
		}
	}

	void GameUnitC::PlayOneShotAnimation(AnimationState* animState)
	{
		if (!animState)
		{
			return;
		}

		if (animState->IsLoop())
		{
			WLOG("One shot animation has loop flag set to true, not playing!");
			return;
		}

		if (m_oneShotState != nullptr)
		{
			m_oneShotState->SetEnabled(false);
			m_oneShotState->SetWeight(0.0f);
		}

		m_oneShotState = animState;
		m_oneShotState->SetEnabled(true);
		m_oneShotState->SetWeight(1.0f);
		m_oneShotState->SetTimePosition(0.0f);
	}

	void GameUnitC::NotifyAttackSwingEvent()
	{
		PlayOneShotAnimation(m_unarmedAttackState);
	}

	bool GameUnitC::IsFriendlyTo(const GameUnitC& other) const
	{
		if (m_factionTemplate == nullptr || other.m_factionTemplate == nullptr)
		{
			return false;
		}

		if (m_faction == nullptr || other.m_faction == nullptr)
		{
			return false;
		}

		// Same faction template should always be friendly
		if (m_factionTemplate == other.m_factionTemplate)
		{
			return true;
		}

		if (m_faction == other.m_faction)
		{
			return true;
		}

		return std::find_if(m_factionTemplate->friends().begin(), m_factionTemplate->friends().end(), [&other](uint32 factionId) { return factionId == other.GetFaction()->id(); }) != m_factionTemplate->friends().end();
	}

	bool GameUnitC::IsHostileTo(const GameUnitC& other) const
	{
		if (m_factionTemplate == nullptr || other.m_factionTemplate == nullptr)
		{
			return false;
		}

		if (m_faction == nullptr || other.m_faction == nullptr)
		{
			return false;
		}

		// Same faction template should always never be hostile
		if (m_factionTemplate == other.m_factionTemplate)
		{
			return false;
		}

		if (m_faction == other.m_faction)
		{
			return false;
		}

		return std::find_if(m_factionTemplate->enemies().begin(), m_factionTemplate->enemies().end(), [&other](uint32 factionId) { return factionId == other.GetFaction()->id(); }) != m_factionTemplate->enemies().end();
	}

	void GameUnitC::OnDisplayIdChanged()
	{
		const uint32 displayId = Get<uint32>(object_fields::DisplayId);
		const proto_client::ModelDataEntry* modelEntry = ObjectMgr::GetModelData(displayId);
		if (m_entity) m_entity->SetVisible(modelEntry != nullptr);
		if (!modelEntry)
		{
			return;
		}

		// Reset animation states
		m_idleAnimState = nullptr;
		m_runAnimState = nullptr;
		m_readyAnimState = nullptr;
		m_castingState = nullptr;
		m_castReleaseState = nullptr;
		m_unarmedAttackState = nullptr;
		m_deathState = nullptr;
		m_targetState = nullptr;
		m_currentState = nullptr;
		m_oneShotState = nullptr;

		// Update or create entity
		if (!m_entity)
		{
			m_entity = m_scene.CreateEntity(std::to_string(GetGuid()), modelEntry->filename());
			m_entity->SetUserObject(this);
			m_entity->SetQueryFlags(0x00000002);
			m_entityOffsetNode->AttachObject(*m_entity);
		}
		else
		{
			// Just update the mesh
			m_entity->SetMesh(MeshManager::Get().Load(modelEntry->filename()));
		}

		m_collider.radius = 0.5f;

		// Initialize animation states from new mesh
		if (m_entity->HasAnimationState("Idle"))
		{
			m_idleAnimState = m_entity->GetAnimationState("Idle");
		}

		if (m_entity->HasAnimationState("Run"))
		{
			m_runAnimState = m_entity->GetAnimationState("Run");
		}

		if (m_entity->HasAnimationState("UnarmedReady"))
		{
			m_readyAnimState = m_entity->GetAnimationState("UnarmedReady");
		}

		if (!m_readyAnimState)
		{
			m_readyAnimState = m_idleAnimState;
		}

		if (m_entity->HasAnimationState("CastLoop"))
		{
			m_castingState = m_entity->GetAnimationState("CastLoop");
		}

		if (m_entity->HasAnimationState("CastRelease"))
		{
			m_castReleaseState = m_entity->GetAnimationState("CastRelease");
			m_castReleaseState->SetLoop(false);
			m_castReleaseState->SetPlayRate(2.0f);
		}

		if (m_entity->HasAnimationState("UnarmedAttack01"))
		{
			m_unarmedAttackState = m_entity->GetAnimationState("UnarmedAttack01");
			m_unarmedAttackState->SetLoop(false);
		}

		if (m_entity->HasAnimationState("Death"))
		{
			m_deathState = m_entity->GetAnimationState("Death");
			m_deathState->SetLoop(false);
			m_deathState->SetTimePosition(0.0f);
		}
	}

	void GameUnitC::UpdateCollider()
	{
		constexpr float halfHeight = 1.0f;

		m_collider.pointA = GetPosition() + Vector3(0.0f, 0.5f, 0.0f);
		m_collider.pointB = GetPosition() + Vector3(0.0f, halfHeight * 2.0f, 0.0f);
		m_collider.radius = 0.5f;
	}

	void GameUnitC::PerformGroundCheck()
	{
	}
}
