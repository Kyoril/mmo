#include "game_unit_c.h"
#include "game_aura_c.h"
#include "spell_visualization_service.h"

#include <algorithm>
#include <set>

#include "debug_interface.h"
#include "net_client.h"
#include "object_mgr.h"
#include "unit_movement.h"
#include "base/clock.h"
#include "client_data/project.h"
#include "frame_ui/font_mgr.h"
#include "game/spell.h"
#include "game/character_customization/avatar_definition_mgr.h"
#include "log/default_log_levels.h"
#include "math/collision.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/scene.h"
#include "shared/client_data/proto_client/factions.pb.h"
#include "shared/client_data/proto_client/faction_templates.pb.h"
#include "shared/client_data/proto_client/model_data.pb.h"
#include "shared/client_data/proto_client/spells.pb.h"

namespace mmo
{
	GameUnitC::GameUnitC(Scene &scene, NetClient &netDriver, const proto_client::Project &project, uint32 map) : GameObjectC(scene, project, map), m_netDriver(netDriver), m_unitSpeed{0.0f}, m_creatureInfo()
	{
		m_unitMovement = std::make_unique<UnitMovement>(*this);
	}

	GameUnitC::~GameUnitC()
	{
		// Ensure quest giver status is removed
		SetQuestGiverStatus(questgiver_status::None);

		if (m_capsuleDebugObject)
		{
			DestroyCapsuleDebugVisualization();
		}
	}

	void GameUnitC::QueueMovementEvent(MovementEventType eventType, GameTime timestamp,
									   const MovementInfo &movementInfo)
	{
		m_movementEventQueue.emplace(eventType, timestamp, movementInfo);
	}

	void GameUnitC::AddYawInput(const Radian &value)
	{
		m_yawInput += value;
	}

	void GameUnitC::OnMovementModeChanged(MovementMode previousMovementMode, MovementMode newMovementMode)
	{
		if (!m_pressedJump || !m_unitMovement->IsFalling())
		{
			ResetJumpState();
		}

		if (previousMovementMode == MovementMode::Falling && newMovementMode != MovementMode::Falling)
		{
			UpdateMovementInfo();
			m_movementInfo.movementFlags &= ~movement_flags::Falling;

			m_netDriver.OnMoveEvent(*this, MovementEvent(movement_event_type::Land, m_movementInfo.timestamp, m_movementInfo));
		}
	}

	Vector3 GameUnitC::GetForwardVector() const
	{
		return m_sceneNode->GetOrientation() * Vector3::UnitX;
	}

	Vector3 GameUnitC::GetRightVector() const
	{
		return m_sceneNode->GetOrientation() * Vector3::UnitZ;
	}

	Vector3 GameUnitC::GetUpVector() const
	{
		return m_sceneNode->GetOrientation() * Vector3::UnitY;
	}

	void GameUnitC::Deserialize(io::Reader &reader, const bool complete)
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

			HandleFieldMapChanges();
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

		reader >> io::read<float>(m_unitSpeed[movement_type::Walk]) >> io::read<float>(m_unitSpeed[movement_type::Run]) >> io::read<float>(m_unitSpeed[movement_type::Backwards]) >> io::read<float>(m_unitSpeed[movement_type::Swim]) >> io::read<float>(m_unitSpeed[movement_type::SwimBackwards]) >> io::read<float>(m_unitSpeed[movement_type::Flight]) >> io::read<float>(m_unitSpeed[movement_type::FlightBackwards]) >> io::read<float>(m_unitSpeed[movement_type::Turn]);

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

		// Get current game time
		const GameTime now = GetAsyncTimeMs();
		while (!m_movementEventQueue.empty())
		{
			// Get next event (we expect events are in order, so no future event is in front of a past event)
			const auto &moveEvent = m_movementEventQueue.front();
			if (moveEvent.timestamp > now)
			{
				// Event is in the future, stop processing
				break;
			}

			if (ObjectMgr::GetActivePlayerGuid() == GetGuid())
			{
				m_netDriver.OnMoveEvent(*this, moveEvent);
			}
			else
			{
				// Adjust timestamp
				ApplyMovementInfo(moveEvent.movementInfo);
			}

			// Remove processed event
			m_movementEventQueue.pop();
		}

		if (IsControlledByLocalPlayer())
		{
			UpdateMovementInfo();

			if (m_movementInfo.IsChangingPosition() && now > m_lastHeartbeat + (constants::OneSecond / 2))
			{
				m_lastHeartbeat = now;
				m_netDriver.OnMoveEvent(*this, MovementEvent(movement_event_type::Heartbeat, now, m_movementInfo));
			}
		}

		// Based on current movement info, update movement states
		if (m_movementInfo.movementFlags & movement_flags::Forward)
		{
			AddInputVector(GetForwardVector() * GetSpeed(movement_type::Run));
		}
		if (m_movementInfo.movementFlags & movement_flags::Backward)
		{
			AddInputVector(-GetForwardVector() * GetSpeed(movement_type::Backwards));
		}
		if (m_movementInfo.movementFlags & movement_flags::StrafeLeft)
		{
			AddInputVector(-GetRightVector() * GetSpeed(movement_type::Run));
		}
		if (m_movementInfo.movementFlags & movement_flags::StrafeRight)
		{
			AddInputVector(GetRightVector() * GetSpeed(movement_type::Run));
		}
		if (m_movementInfo.movementFlags & movement_flags::TurnLeft)
		{
			AddYawInput(Radian(GetSpeed(movement_type::Turn) * 1.0f));
		}
		if (m_movementInfo.movementFlags & movement_flags::TurnRight)
		{
			AddYawInput(Radian(GetSpeed(movement_type::Turn) * -1.0f));
		}

		UpdateQuestGiverAndNameVisuals(deltaTime);

		const bool isDead = (GetHealth() <= 0) || GetStandState() == unit_stand_state::Dead;
		if (!isDead)
		{
			UpdateNormalMovement(deltaTime);
		}

		UpdateAnimationStates(deltaTime, isDead);

		m_unitMovement->Tick(deltaTime);
	}

	void GameUnitC::UpdateQuestGiverAndNameVisuals(const float deltaTime)
	{
		// Update quest giver icon
		if (m_questGiverNode != nullptr)
		{
			if (!m_questOffset.IsNearlyEqual(m_questGiverNode->GetPosition()))
			{
				// Lerp questgivernode position to quest offset node
				m_questGiverNode->SetPosition(m_questGiverNode->GetPosition().Lerp(m_questOffset, deltaTime * 5.0f));
			}

			// TODO: Get rotation to the current camera and yaw the icon to face it!
			const Camera *cam = m_scene.GetCamera((uint32)0);
			if (cam)
			{
				m_questGiverNode->SetFixedYawAxis(true);
				m_questGiverNode->LookAt(cam->GetDerivedPosition(), TransformSpace::World, Vector3::NegativeUnitZ);
			}
		}

		// Update name component to face camera
		if (m_nameComponent && m_nameComponent->IsVisible())
		{
			const Camera *cam = m_scene.GetCamera((uint32)0);
			if (cam)
			{
				m_nameComponentNode->SetFixedYawAxis(true);
				m_nameComponentNode->LookAt(cam->GetDerivedPosition(), TransformSpace::World, Vector3::UnitZ);
			}
		}
	}

	void GameUnitC::UpdateNormalMovement(const float deltaTime)
	{
		// Update target tracking for NPCs
		UpdateTargetTracking();

		// Update path-based movement if we have an active path
		if (!m_movementPath.empty() && !m_pathCompleted)
		{
			UpdatePathMovement(deltaTime);
		}

		// Update animation state based on movement
		// Skip normal animation system if following a path (path system handles animations manually)
		if (m_movementPath.empty() || m_pathCompleted)
		{
			UpdateMovementBasedAnimation();
		}
	}

	void GameUnitC::UpdateTargetTracking() const
	{
		// Check if we should track a target
		if (const auto target = m_targetUnit.lock(); !IsPlayer() && target)
		{
			// Only rotate around the Y axis (yaw) to face the target
			// Calculate the direction vector to the target in the horizontal plane
			const Vector3 targetPos = target->GetSceneNode()->GetDerivedPosition();
			const Vector3 myPos = GetSceneNode()->GetDerivedPosition();

			// Calculate the angle to face the target
			const Radian yawToTarget = GetAngle(myPos.x, myPos.z, targetPos.x, targetPos.z);

			// Set orientation using only the yaw component
			GetSceneNode()->SetOrientation(Quaternion(yawToTarget, Vector3::UnitY));
		}
	}

	void GameUnitC::UpdateMovementBasedAnimation()
	{
		if (!m_unitMovement)
		{
			return;
		}

		// Handle jumping animations first
		if (m_unitMovement->IsFalling() && std::abs(m_unitMovement->GetVelocity().y) > 0.05f)
		{
			// If the jumping velocity is positive, we're in the jump up phase
			if (m_unitMovement->GetVelocity().y > 0.0f)
			{
				// If we have a jump start animation, use it. Otherwise, use falling
				if (m_jumpStartState && !m_jumpStartState->HasEnded())
				{
					SetTargetAnimState(m_jumpStartState);
				}
				else if (m_fallingState)
				{
					SetTargetAnimState(m_fallingState);
				}
			}
			// If velocity is negative, we're falling
			else if (m_fallingState)
			{
				SetTargetAnimState(m_fallingState);
			}
			return;
		}

		const float inputVector2DSize = (m_inputVector * Vector3(1.0f, 0.0f, 1.0f)).Normalize();

		// Regular movement animations
		if (m_unitMovement->IsMovingOnGround() && inputVector2DSize > 0.1f)
		{
			SetTargetAnimState(m_runAnimState);
		}
		else
		{
			const bool isAttacking = (Get<uint32>(object_fields::Flags) & unit_flags::Attacking) != 0;
			AnimationState *idleAnim = isAttacking ? m_readyAnimState : m_idleAnimState;

			SetTargetAnimState(idleAnim);
		}
	}

	void GameUnitC::RefreshMovementAnimation()
	{
		// Force refresh of movement-based animation state
		// This is useful after canceling spell animations to return to idle/run state
		UpdateMovementBasedAnimation();
	}

	void GameUnitC::UpdateAnimationStates(const float deltaTime, const bool isDead)
	{
		// Handle one-shot animations
		if (m_oneShotState)
		{
			UpdateOneShotAnimation(deltaTime);
		}

		// Control animation state visibility based on one-shot state
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
			if (m_oneShotState && m_oneShotState->IsEnabled())
			{
				m_oneShotState->SetTimePosition(m_oneShotState->GetLength());
			}

			SetTargetAnimState(m_deathState);
		}

		// Handle animation transitions
		UpdateAnimationTransitions(deltaTime);

		// Update animation time positions
		AdvanceAnimationTimes(deltaTime);
	}

	void GameUnitC::UpdateOneShotAnimation(const float deltaTime)
	{
		// Hide regular animations while one-shot is playing
		if (m_currentState)
			m_currentState->SetWeight(0.0f);
		if (m_targetState)
			m_targetState->SetWeight(0.0f);

		// Handle transition back after one-shot animation has ended
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

			// Once transition is complete, disable one-shot animation
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

	void GameUnitC::UpdateAnimationTransitions(const float deltaTime)
	{
		// Skip transitions if one-shot animation is playing
		if (m_oneShotState)
		{
			return;
		}

		// Handle transition to target state
		if (m_targetState != m_currentState)
		{
			// If we have a target but no current state, just set target as current
			if (m_targetState && !m_currentState)
			{
				m_currentState = m_targetState;
				m_targetState = nullptr;

				m_currentState->SetWeight(1.0f);
				m_currentState->SetEnabled(true);
			}
		}

		// Handle crossfade between current and target states
		if (m_currentState && m_targetState)
		{
			m_targetState->SetWeight(m_targetState->GetWeight() + deltaTime * 4.0f);
			m_currentState->SetWeight(1.0f - m_targetState->GetWeight());

			// Once transition is complete, make target the new current state
			if (m_targetState->GetWeight() >= 1.0f)
			{
				m_currentState->SetWeight(0.0f);
				m_currentState->SetEnabled(false);

				m_currentState = m_targetState;
				m_targetState = nullptr;
			}
		}
	}

	void GameUnitC::AdvanceAnimationTimes(const float deltaTime) const
	{
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

	void GameUnitC::ApplyMovementInfo(const MovementInfo &movementInfo)
	{
		m_movementInfo = movementInfo;

		// Instantly apply movement data for now
		GetSceneNode()->SetDerivedPosition(movementInfo.position);
		GetSceneNode()->SetDerivedOrientation(Quaternion(Radian(movementInfo.facing), Vector3::UnitY));

		if (m_movementInfo.IsFalling())
		{
			m_unitMovement->SetVelocity(m_movementInfo.jumpVelocity);
			m_unitMovement->SetMovementMode(MovementMode::Falling);
		}

		UpdateCollider();
	}

	void GameUnitC::InitializeFieldMap()
	{
		m_fieldMap.Initialize(object_fields::UnitFieldCount);
	}

	void GameUnitC::SetQuestGiverStatus(const QuestgiverStatus status)
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

		switch (status)
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

	bool GameUnitC::OnAuraUpdate(io::Reader &reader)
	{
		uint32 visibleAuraCount = 0;
		if (!(reader >> io::read<uint32>(visibleAuraCount)))
		{
			return false;
		}

		// Track old auras for diffing (simple approach: remember spell ids)
		std::set<uint32> oldAuraSpellIds;
		for (const auto &aura : m_auras)
		{
			if (const auto *spell = aura->GetSpell())
			{
				oldAuraSpellIds.insert(spell->id());
			}
		}

		m_auras.clear();

		std::set<uint32> newAuraSpellIds;
		for (uint32 i = 0; i < visibleAuraCount; ++i)
		{
			uint32 spellId, duration;
			uint64 casterId;
			uint8 auraTypeCount;

			if (!(reader >> io::read<uint32>(spellId) >> io::read<uint32>(duration) >> io::read_packed_guid(casterId) >> io::read<uint8>(auraTypeCount)))
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

			const proto_client::SpellEntry *spell = m_project.spells.getById(spellId);
			if (!spell)
			{
				ELOG("Failed to find spell for aura!");
				continue;
			}

			// Add aura
			m_auras.push_back(std::make_unique<GameAuraC>(*this, *spell, casterId, duration));
			newAuraSpellIds.insert(spellId);
		}

		// Trigger visualization events for added/removed auras
		for (uint32 spellId : newAuraSpellIds)
		{
			if (oldAuraSpellIds.find(spellId) == oldAuraSpellIds.end())
			{
				// Newly applied aura
				if (const auto *spell = m_project.spells.getById(spellId))
				{
					NotifyAuraVisualizationApplied(*spell, this);
				}
			}
		}
		for (uint32 spellId : oldAuraSpellIds)
		{
			if (newAuraSpellIds.find(spellId) == newAuraSpellIds.end())
			{
				// Removed aura
				if (const auto *spell = m_project.spells.getById(spellId))
				{
					NotifyAuraVisualizationRemoved(*spell, this);
				}
			}
		}

		return reader;
	}

	const proto_client::ModelDataEntry *GameUnitC::GetDisplayModel() const
	{
		const uint32 displayId = Get<uint32>(object_fields::DisplayId);
		if (!displayId)
		{
			return nullptr;
		}

		return m_project.models.getById(displayId);
	}

	void GameUnitC::SetupSceneObjects()
	{
		GameObjectC::SetupSceneObjects();

		// These need to be set before!
		ASSERT(ObjectMgr::GetUnitNameFont());
		ASSERT(ObjectMgr::GetUnitNameFontMaterial());

		// Attach text component
		m_nameComponentNode = m_sceneNode->CreateChildSceneNode(Vector3::UnitY * 2.0f);
		m_nameComponent = std::make_unique<WorldTextComponent>(ObjectMgr::GetUnitNameFont(), ObjectMgr::GetUnitNameFontMaterial(), "");
		m_nameComponentNode->AttachObject(*m_nameComponent);
		m_nameComponent->SetVisible(ObjectMgr::GetSelectedObjectGuid() == GetGuid());

		static uint64 s_counter = 0;
		m_normalDebugObject = m_scene.CreateManualRenderObject("UnitDebug_" + std::to_string(s_counter++));
		m_sceneNode->AttachObject(*m_normalDebugObject);

		RefreshUnitName();

		// Setup object display
		OnDisplayIdChanged();
	}

	void GameUnitC::OnEntryChanged()
	{
		const int32 entryId = Get<int32>(object_fields::Entry);
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

	void GameUnitC::SetQuestGiverMesh(const String &meshName)
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

		if (!m_questGiverNode)
		{
			// Ideal size is a unit with a size of 2 units in height, but if the unit is bigger we want to offset the icon position as well as scale it up
			// so that for very big models it's not just that tiny icon floating in the sky above some giant head or something of that

			auto offset = GetDefaultQuestGiverOffset();
			float scale = 1.0f;
			if (m_entity)
			{
				offset.y = m_entity->GetBoundingBox().GetExtents().y * 2.2f;
				scale = offset.y / 2.0f;
			}

			// Hack to ensure the offset is set correctly initialized
			if (m_questOffset.y <= 0.0f)
			{
				m_questOffset = offset;
			}

			m_questGiverNode = m_sceneNode->CreateChildSceneNode(m_questOffset);
			ASSERT(m_questGiverNode);

			m_questGiverNode->SetScale(Vector3::UnitScale * scale);
			m_questGiverNode->AttachObject(*m_questGiverEntity);

			if (m_nameComponent)
			{
				SetUnitNameVisible(m_nameComponent->IsVisible());
			}
		}
	}

	void GameUnitC::RefreshUnitName()
	{
		// Ensure name component is updated to display the correct name
		if (m_nameComponent)
		{
			std::ostringstream strm;
			strm << GetName();

			if (!m_creatureInfo.subname.empty())
			{
				strm << "\n<" << m_creatureInfo.subname << ">";
			}

			m_nameComponent->SetText(strm.str());

			// Set the text color based on the unit's relationship to the player
			Color textColor = Color::White; // Default color

			// Check if this is a party member
			bool isPartyMember = false;
			if (IsPlayer())
			{
				// Check if this player is in the active player's party
				const auto activePlayer = ObjectMgr::GetActivePlayer();
				if (activePlayer && activePlayer->GetGuid() != GetGuid())
				{
					// TODO: Check if this player is in the active player's party
					// For now, we'll just check if it's a friendly player
					if (activePlayer->IsFriendlyTo(*this))
					{
						isPartyMember = true;
					}
				}
			}

			if (isPartyMember || (IsPlayer() && IsFriendly()))
			{
				// Blue for party members and friendly players
				textColor.Set(0.0f, 0.5f, 1.0f, 1.0f);
			}
			else if (IsHostile())
			{
				// Red for hostile units
				textColor.Set(1.0f, 0.0f, 0.0f, 1.0f);
			}
			else if (IsFriendly())
			{
				// Green for friendly units
				textColor.Set(0.0f, 1.0f, 0.0f, 1.0f);
			}
			else
			{
				// Yellow for neutral units
				textColor.Set(1.0f, 1.0f, 0.0f, 1.0f);
			}

			m_nameComponent->SetFontColor(textColor);
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

		UpdateMovementInfo();
		m_lastHeartbeat = m_movementInfo.timestamp;

		QueueMovementEvent(forward ? movement_event_type::StartMoveForward : movement_event_type::StartMoveBackward, m_movementInfo.timestamp, m_movementInfo);
	}

	void GameUnitC::StartStrafe(const bool left)
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

		UpdateMovementInfo();
		m_lastHeartbeat = m_movementInfo.timestamp;

		QueueMovementEvent(left ? movement_event_type::StartStrafeLeft : movement_event_type::StartStrafeRight, m_movementInfo.timestamp, m_movementInfo);
	}

	void GameUnitC::StopMove()
	{
		m_movementInfo.movementFlags &= ~(movement_flags::Forward | movement_flags::Backward);

		// Stop movement
		UpdateMovementInfo();
		m_lastHeartbeat = m_movementInfo.timestamp;

		QueueMovementEvent(movement_event_type::StopMove, m_movementInfo.timestamp, m_movementInfo);
	}

	void GameUnitC::StopStrafe()
	{
		m_movementInfo.movementFlags &= ~movement_flags::Strafing;

		// Stop movement
		UpdateMovementInfo();
		m_lastHeartbeat = m_movementInfo.timestamp;

		QueueMovementEvent(movement_event_type::StopStrafe, m_movementInfo.timestamp, m_movementInfo);
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

		// Stop movement
		UpdateMovementInfo();
		m_lastHeartbeat = m_movementInfo.timestamp;

		QueueMovementEvent(left ? movement_event_type::StartTurnLeft : movement_event_type::StartTurnRight, m_movementInfo.timestamp, m_movementInfo);
	}

	void GameUnitC::StopTurn()
	{
		m_movementInfo.movementFlags &= ~movement_flags::Turning;

		// Stop movement
		UpdateMovementInfo();
		m_lastHeartbeat = m_movementInfo.timestamp;

		QueueMovementEvent(movement_event_type::StopTurn, m_movementInfo.timestamp, m_movementInfo);
	}

	void GameUnitC::SetFacing(const Radian &facing)
	{
		m_movementInfo.facing = facing;

		if (m_sceneNode)
		{
			m_sceneNode->SetOrientation(Quaternion(facing, Vector3::UnitY));

			UpdateMovementInfo();
			m_lastHeartbeat = m_movementInfo.timestamp;

			QueueMovementEvent(movement_event_type::SetFacing, m_movementInfo.timestamp, m_movementInfo);
		}
	}

	void GameUnitC::Jump()
	{
		m_pressedJump = true;
		m_jumpKeyHoldTime = 0.0f;
	}

	void GameUnitC::StopJumping()
	{
		m_pressedJump = false;
		ResetJumpState();
	}

	void GameUnitC::ClearJumpInput(const float deltaTime)
	{
		if (m_pressedJump)
		{
			m_jumpKeyHoldTime += deltaTime;

			if (m_jumpKeyHoldTime >= GetJumpMaxHoldTime())
			{
				m_pressedJump = false;
			}
		}
		else
		{
			m_jumpForceTimeRemaining = 0.0f;
			m_wasJumping = false;
		}
	}

	void GameUnitC::OnJumped()
	{
		const bool wasFalling = (m_movementInfo.movementFlags & movement_flags::Falling) != 0;

		m_lastHeartbeat = GetAsyncTimeMs();
		UpdateMovementInfo();
		m_movementInfo.movementFlags |= movement_flags::Falling;

		if (!wasFalling)
		{
			m_netDriver.OnMoveEvent(*this, MovementEvent(movement_event_type::Fall, m_movementInfo.timestamp, m_movementInfo));
		}
	}

	void GameUnitC::CheckJumpInput()
	{
		m_jumpCurrentCountPreJump = m_jumpCurrentCount;
		if (!m_unitMovement)
		{
			return;
		}

		if (!m_pressedJump)
		{
			return;
		}

		// If this is the first jump, and we're already falling,
		// then increment the JumpCount to compensate.
		const bool bFirstJump = m_jumpCurrentCount == 0;
		if (bFirstJump && m_unitMovement->IsFalling())
		{
			m_jumpCurrentCount++;
		}

		const bool bDidJump = CanJump() && m_unitMovement->DoJump();
		if (bDidJump)
		{
			// Transition from not (actively) jumping to jumping.
			if (!m_wasJumping)
			{
				m_jumpCurrentCount++;
				m_jumpForceTimeRemaining = GetJumpMaxHoldTime();
				OnJumped();
			}
		}

		m_wasJumping = bDidJump;
	}

	bool GameUnitC::CanJump() const
	{
		return JumpIsAllowed();
	}

	bool GameUnitC::JumpIsAllowed() const
	{
		bool jumpIsAllowed = m_unitMovement->CanAttemptJump();
		if (jumpIsAllowed)
		{
			// Ensure JumpHoldTime and JumpCount are valid.
			if (!m_wasJumping || GetJumpMaxHoldTime() <= 0.0f)
			{
				if (m_jumpCurrentCount == 0 && m_unitMovement->IsFalling())
				{
					jumpIsAllowed = m_jumpCurrentCount + 1 < m_jumpMaxCount;
				}
				else
				{
					jumpIsAllowed = m_jumpCurrentCount < m_jumpMaxCount;
				}
			}
			else
			{
				// Only consider JumpKeyHoldTime as long as:
				// A) The jump limit hasn't been met OR
				// B) The jump limit has been met AND we were already jumping
				const bool bJumpKeyHeld = (m_pressedJump && m_jumpKeyHoldTime < GetJumpMaxHoldTime());
				jumpIsAllowed = bJumpKeyHeld &&
								((m_jumpCurrentCount < m_jumpMaxCount) || (m_wasJumping && m_jumpCurrentCount == m_jumpMaxCount));
			}
		}

		return jumpIsAllowed;
	}

	void GameUnitC::SetMovementPath(const std::vector<Vector3> &points, GameTime moveTime, const std::optional<Radian> &targetRotation)
	{
		// Clear any existing animation-based movement
		m_movementAnimationTime = 0.0f;

		// Clear any existing path
		m_movementPath.clear();
		m_currentPathIndex = 0;
		m_targetRotation.reset();
		m_pathCompleted = false;

		if (points.empty())
		{
			return;
		}

		// Check if the new path start is close to our current position
		const Vector3 currentPosition = m_sceneNode->GetDerivedPosition();
		const Vector3 newPathStart = points[0];
		const float distanceToNewStart = (newPathStart - currentPosition).GetLength();

		constexpr float pathStartTolerance = 5.0f; // Allow 5 units tolerance for new path starts

		Vector3 actualStartPosition;
		if (distanceToNewStart <= pathStartTolerance)
		{
			// Close enough - start from current position
			actualStartPosition = currentPosition;
		}
		else
		{
			// Too far - use the path's start position but this might cause a small teleport
			actualStartPosition = newPathStart;
		}

		// Store the path points and target rotation
		m_movementPath = points;
		m_targetRotation = targetRotation;
		m_currentPathIndex = 0;
		m_pathCompleted = false;
		m_pathStartTime = GetAsyncTimeMs();
		m_pathStartPosition = actualStartPosition; // Use tolerance-checked start position

		// Initialize path gravity variables
		m_pathVerticalVelocity = 0.0f;
		m_pathOnGround = true;

		// Calculate path segment lengths and total length for time-based movement
		m_pathSegmentLengths.clear();
		m_pathTotalLength = 0.0f;

		Vector3 currentPos = m_pathStartPosition; // Start from actual starting position

		for (size_t i = 0; i < m_movementPath.size(); ++i)
		{
			Vector3 segmentEnd = m_movementPath[i];
			float segmentLength = (segmentEnd - currentPos).GetLength();
			m_pathSegmentLengths.push_back(segmentLength);
			m_pathTotalLength += segmentLength;
			currentPos = segmentEnd;
		}

		// For remote units, we don't use movement flags for physics - we use direct positioning
		// For local units, we might still want movement flags for network sync
		if (!IsControlledByLocalPlayer())
		{
			// Remote units use time-based direct positioning
			// Clear ALL movement flags to avoid physics interference and debug spam
			m_movementInfo.movementFlags &= ~movement_flags::PositionChanging;
			m_movementInfo.movementFlags &= ~movement_flags::Forward;
			m_movementInfo.movementFlags &= ~movement_flags::Backward;
			m_movementInfo.movementFlags &= ~movement_flags::StrafeLeft;
			m_movementInfo.movementFlags &= ~movement_flags::StrafeRight;
			m_movementInfo.movementFlags &= ~movement_flags::TurnLeft;
			m_movementInfo.movementFlags &= ~movement_flags::TurnRight;
		}
		else
		{
			// Local units could use movement flags for physics, but for now use direct positioning too
			m_movementInfo.movementFlags |= movement_flags::Forward;
			m_movementInfo.movementFlags |= movement_flags::PositionChanging;
		}
	}

	void GameUnitC::UpdatePathMovement(const float deltaTime)
	{
		if (m_movementPath.empty() || m_pathCompleted || m_pathTotalLength <= 0.0f)
		{
			return;
		}

		// Calculate how much time has passed since path started
		const GameTime currentTime = GetAsyncTimeMs();
		const float elapsedTime = static_cast<float>(currentTime - m_pathStartTime) / 1000.0f; // Convert to seconds

		// Calculate how far along the path we should be based on movement speed
		const float runSpeed = GetSpeed(movement_type::Run);
		const float targetDistance = runSpeed * elapsedTime;

		// Check if we're close enough to the final destination (ONLY distance-based completion)
		const Vector3 currentPosition = m_sceneNode->GetDerivedPosition();
		const Vector3 finalDestination = m_movementPath.back();
		const float distanceToFinalDestination = (finalDestination - currentPosition).GetLength();

		constexpr float arrivalThreshold = 1.0f; // Distance-based completion only

		// Complete path ONLY when we're actually close to destination (no time-based teleportation)
		if (distanceToFinalDestination <= arrivalThreshold)
		{
			// Apply target rotation if specified (but don't teleport to exact position)
			if (m_targetRotation.has_value())
			{
				SetFacing(m_targetRotation.value());
			}

			// Clear movement flags to stop animations
			m_movementInfo.movementFlags &= ~movement_flags::Forward;
			m_movementInfo.movementFlags &= ~movement_flags::Backward;
			m_movementInfo.movementFlags &= ~movement_flags::StrafeLeft;
			m_movementInfo.movementFlags &= ~movement_flags::StrafeRight;
			m_movementInfo.movementFlags &= ~movement_flags::PositionChanging;

			// Set idle animation for all units when path completes
			if (m_idleAnimState)
			{
				SetTargetAnimState(m_idleAnimState);
			}

			// Complete the path
			m_pathCompleted = true;
			m_movementPath.clear();
			m_pathSegmentLengths.clear();
			m_currentPathIndex = 0;

			// Reset gravity variables
			m_pathVerticalVelocity = 0.0f;
			m_pathOnGround = true;

			return;
		}

		// Continue moving along path even if we're past the calculated time
		// This allows units to finish paths naturally without teleportation
		Vector3 targetPosition;

		if (targetDistance >= m_pathTotalLength)
		{
			// We're past the calculated end time, but move towards final destination naturally
			targetPosition = finalDestination;
		}
		else
		{
			// Find which segment we should be on and calculate position along it
			targetPosition = CalculatePositionAlongPath(targetDistance);
		}

		// Move towards target position smoothly without large teleports
		const Vector3 unitCurrentPosition = m_sceneNode->GetDerivedPosition();
		Vector3 direction = targetPosition - unitCurrentPosition;
		const float distanceFromTarget = direction.GetLength();

		constexpr float maxMovePerFrame = 0.5f; // Maximum distance to move in one frame
		if (distanceFromTarget > 0.01f)
		{
			Vector3 newPosition;

			if (distanceFromTarget <= maxMovePerFrame)
			{
				// Small distance - move directly to target
				newPosition = targetPosition;
			}
			else
			{
				// Large distance - move gradually towards target
				direction.Normalize();
				newPosition = unitCurrentPosition + direction * maxMovePerFrame;
			}

			m_sceneNode->SetPosition(newPosition);

			// Set run animation for all units following paths
			if (m_runAnimState)
			{
				SetTargetAnimState(m_runAnimState);
			}
		}

		// Update facing direction towards movement direction
		if (!m_movementPath.empty())
		{
			const Vector3 currentPos = m_sceneNode->GetDerivedPosition();

			// Find the next waypoint we're moving towards
			size_t nextWaypointIndex = 0;
			float accumulatedDistance = 0.0f;

			for (size_t i = 0; i < m_pathSegmentLengths.size(); ++i)
			{
				if (targetDistance <= accumulatedDistance + m_pathSegmentLengths[i])
				{
					nextWaypointIndex = i;
					break;
				}
				accumulatedDistance += m_pathSegmentLengths[i];
				nextWaypointIndex = i + 1;
			}

			// Update current path index for other systems
			m_currentPathIndex = nextWaypointIndex;

			if (nextWaypointIndex < m_movementPath.size())
			{
				const Vector3 nextWaypoint = m_movementPath[nextWaypointIndex];
				const Vector3 facingDirection = nextWaypoint - currentPos;

				if (facingDirection.GetLength() > 0.01f)
				{
					const Radian targetYaw = GetAngle(currentPos.x, currentPos.z, nextWaypoint.x, nextWaypoint.z);
					SetFacing(targetYaw);
				}
			}
		}
	}

	Vector3 GameUnitC::CalculatePositionAlongPath(const float distance) const
	{
		if (m_movementPath.empty() || m_pathSegmentLengths.empty())
		{
			return m_sceneNode->GetDerivedPosition();
		}

		float remainingDistance = distance;
		Vector3 currentPos = m_pathStartPosition; // Start from where the path began

		// Walk through path segments until we find where we should be
		for (size_t i = 0; i < m_movementPath.size() && i < m_pathSegmentLengths.size(); ++i)
		{
			const float segmentLength = m_pathSegmentLengths[i];
			Vector3 segmentEnd = m_movementPath[i];

			if (remainingDistance <= segmentLength)
			{
				// We're somewhere along this segment
				if (segmentLength > 0.0f)
				{
					const float t = remainingDistance / segmentLength;
					return currentPos + (segmentEnd - currentPos) * t;
				}
				else
				{
					return segmentEnd;
				}
			}

			// Move to the end of this segment and continue
			remainingDistance -= segmentLength;
			currentPos = segmentEnd;
		}

		// If we get here, we've gone past the end of the path
		return m_movementPath.back();
	}

	bool GameUnitC::IsControlledByLocalPlayer() const
	{
		const auto activePlayer = ObjectMgr::GetActivePlayer();
		return activePlayer && activePlayer.get() == this;
	}

	static void SetQueryMask(SceneNode *node, const uint32 mask)
	{
		for (uint32 i = 0; i < node->GetNumAttachedObjects(); ++i)
		{
			node->GetAttachedObject(i)->SetQueryFlags(mask);
		}

		for (uint32 i = 0; i < node->GetNumChildren(); ++i)
		{
			SetQueryMask(static_cast<SceneNode *>(node->GetChild(i)), mask);
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
		// Legacy method - now handled by SpellVisualizationService
	}

	void GameUnitC::NotifySpellCastCancelled()
	{
		// Legacy method - now handled by SpellVisualizationService
	}

	void GameUnitC::NotifySpellCastSucceeded()
	{
		// Legacy method - now handled by SpellVisualizationService
	}

	bool GameUnitC::IsFriendly() const
	{
		if (ObjectMgr::GetActivePlayerGuid() == GetGuid())
		{
			return true;
		}

		if (ObjectMgr::GetActivePlayerGuid() == 0)
		{
			return false;
		}

		const auto player = ObjectMgr::GetActivePlayer();
		if (!player)
		{
			return false;
		}

		return IsFriendlyTo(*player);
	}

	bool GameUnitC::IsHostile() const
	{
		if (ObjectMgr::GetActivePlayerGuid() == GetGuid())
		{
			return true;
		}

		if (ObjectMgr::GetActivePlayerGuid() == 0)
		{
			return false;
		}

		const auto player = ObjectMgr::GetActivePlayer();
		if (!player)
		{
			return false;
		}

		return IsHostileTo(*player);
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

	int32 GameUnitC::GetHealthFromStat(const int32 statId) const
	{
		int32 stat = 0;

		if (IsPlayer())
		{
			// 20 is the base value
			const int32 statValue = GetStat(statId) - 20;

			const auto *classEntry = AsPlayer().GetClass();
			if (classEntry)
			{
				for (const auto &source : classEntry->healthstatsources())
				{
					if (source.statid() == statId)
					{
						stat += static_cast<int32>(source.factor() * static_cast<float>(statValue));
					}
				}
			}
		}

		return stat;
	}

	int32 GameUnitC::GetManaFromStat(int32 statId) const
	{
		int32 stat = 0;

		if (IsPlayer())
		{
			// 20 is the base value
			const int32 statValue = GetStat(statId) - 20;

			const auto *classEntry = AsPlayer().GetClass();
			if (classEntry)
			{
				for (const auto &source : classEntry->manastatsources())
				{
					if (source.statid() == statId)
					{
						stat += static_cast<int32>(source.factor() * static_cast<float>(statValue));
					}
				}
			}
		}

		return stat;
	}

	int32 GameUnitC::GetAttackPowerFromStat(int32 statId) const
	{
		int32 stat = 0;

		if (IsPlayer())
		{
			// 20 is the base value
			const int32 statValue = GetStat(statId);

			const auto *classEntry = AsPlayer().GetClass();
			if (classEntry)
			{
				for (const auto &source : classEntry->attackpowerstatsources())
				{
					if (source.statid() == statId)
					{
						stat += static_cast<int32>(source.factor() * static_cast<float>(statValue));
					}
				}
			}
		}

		return stat;
	}

	uint8 GameUnitC::GetAttributeCost(uint32 attribute) const
	{
		return 0;
	}

	GameAuraC *GameUnitC::GetAura(uint32 index) const
	{
		if (index < m_auras.size())
		{
			return m_auras[index].get();
		}

		return nullptr;
	}

	void GameUnitC::SetTargetUnit(const std::shared_ptr<GameUnitC> &targetUnit)
	{
		if (m_targetUnit.expired() && !targetUnit)
		{
			return;
		}

		// Check if the target unit actually changed
		{
			const auto prevUnit = m_targetUnit.lock();
			if (targetUnit && prevUnit && prevUnit->GetGuid() == targetUnit->GetGuid())
			{
				return;
			}
		}

		m_targetUnit = targetUnit;

		if (GetGuid() == ObjectMgr::GetActivePlayerGuid())
		{
			ObjectMgr::SetSelectedObjectGuid(targetUnit ? targetUnit->GetGuid() : 0);
			m_netDriver.SetSelectedTarget(targetUnit ? targetUnit->GetGuid() : 0);
		}
	}

	void GameUnitC::SetInitialSpells(const std::vector<const proto_client::SpellEntry *> &spells)
	{
		m_spells = spells;

		m_spellBookSpells.clear();
		for (const auto *spell : m_spells)
		{
			if ((spell->attributes(0) & static_cast<uint32>(spell_attributes::HiddenClientSide)) == 0)
			{
				m_spellBookSpells.push_back(spell);
			}
		}
	}

	void GameUnitC::LearnSpell(const proto_client::SpellEntry *spell)
	{
		const auto it = std::find_if(m_spells.begin(), m_spells.end(), [spell](const proto_client::SpellEntry *entry)
									 { return entry->id() == spell->id(); });
		if (it == m_spells.end())
		{
			m_spells.push_back(spell);

			// Visible?
			if ((spell->attributes(0) & static_cast<uint32>(spell_attributes::HiddenClientSide)) == 0)
			{
				m_spellBookSpells.push_back(spell);
			}
		}
	}

	void GameUnitC::UnlearnSpell(const uint32 spellId)
	{
		std::erase_if(m_spells, [spellId](const proto_client::SpellEntry *entry)
					  { return entry->id() == spellId; });
		std::erase_if(m_spellBookSpells, [spellId](const proto_client::SpellEntry *entry)
					  { return entry->id() == spellId; });
	}

	bool GameUnitC::HasSpell(uint32 spellId) const
	{
		return std::find_if(m_spells.begin(), m_spells.end(), [spellId](const proto_client::SpellEntry *entry)
							{ return entry->id() == spellId; }) != m_spells.end();
	}

	const proto_client::SpellEntry *GameUnitC::GetSpell(uint32 index) const
	{
		if (index < m_spells.size())
		{
			return m_spells[index];
		}

		return nullptr;
	}

	const proto_client::SpellEntry *GameUnitC::GetVisibleSpell(uint32 index) const
	{
		if (index < m_spellBookSpells.size())
		{
			return m_spellBookSpells[index];
		}

		return nullptr;
	}

	void GameUnitC::Attack(GameUnitC &victim)
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

	void GameUnitC::SetCreatureInfo(const CreatureInfo &creatureInfo)
	{
		m_creatureInfo = creatureInfo;
		RefreshUnitName();
	}

	const String &GameUnitC::GetName() const
	{
		if (m_creatureInfo.name.empty())
		{
			return GameObjectC::GetName();
		}

		return m_creatureInfo.name;
	}

	void GameUnitC::SetTargetAnimState(AnimationState *newTargetState)
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

	void GameUnitC::PlayOneShotAnimation(AnimationState *animState)
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

	void GameUnitC::CancelOneShotAnimation()
	{
		if (m_oneShotState != nullptr)
		{
			m_oneShotState->SetEnabled(false);
			m_oneShotState->SetWeight(0.0f);
			m_oneShotState = nullptr;
		}

		// Refresh movement animation to ensure proper state
		RefreshMovementAnimation();
	}

	void GameUnitC::NotifyAttackSwingEvent()
	{
		PlayOneShotAnimation(m_unarmedAttackState);
	}

	void GameUnitC::NotifyHitEvent()
	{
		PlayOneShotAnimation(m_damageHitState);
	}

	void GameUnitC::SetWeaponProficiency(const uint32 mask)
	{
		m_weaponProficiency = mask;
	}

	void GameUnitC::SetArmorProficiency(const uint32 mask)
	{
		m_armorProficiency = mask;
	}

	bool GameUnitC::IsFriendlyTo(const GameUnitC &other) const
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

		return std::find_if(m_factionTemplate->friends().begin(), m_factionTemplate->friends().end(), [&other](uint32 factionId)
							{ return factionId == other.GetFaction()->id(); }) != m_factionTemplate->friends().end();
	}

	bool GameUnitC::IsHostileTo(const GameUnitC &other) const
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

		return std::find_if(m_factionTemplate->enemies().begin(), m_factionTemplate->enemies().end(), [&other](uint32 factionId)
							{ return factionId == other.GetFaction()->id(); }) != m_factionTemplate->enemies().end();
	}

	void GameUnitC::OnDisplayIdChanged()
	{
		const uint32 displayId = Get<uint32>(object_fields::DisplayId);
		const proto_client::ModelDataEntry *modelEntry = ObjectMgr::GetModelData(displayId);
		if (m_entity)
			m_entity->SetVisible(modelEntry != nullptr);
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
		m_jumpStartState = nullptr;
		m_fallingState = nullptr;
		m_landState = nullptr;
		m_customizationDefinition = nullptr;

		String meshFile = modelEntry->filename();
		if (modelEntry->flags() & model_data_flags::IsCustomizable)
		{
			// Check if the model is a mesh file or a .char file
			m_customizationDefinition = AvatarDefinitionManager::Get().Load(modelEntry->filename());
			if (!m_customizationDefinition)
			{
				ELOG("Failed to find customizable avatar definition for " << modelEntry->filename());
				return;
			}

			meshFile = m_customizationDefinition->GetBaseMesh();

			if (!IsPlayer())
			{
				m_configuration.chosenOptionPerGroup.clear();
				m_configuration.scalarValues.clear();
				for (const auto &kvp : modelEntry->customizationproperties())
				{
					m_configuration.chosenOptionPerGroup[kvp.first] = kvp.second;
				}
			}
		}

		// Update or create entity
		if (!m_entity)
		{
			m_entity = m_scene.CreateEntity(std::to_string(GetGuid()), meshFile);
			m_entity->SetUserObject(this);
			m_entity->SetQueryFlags(0x00000002);
			m_entityOffsetNode->AttachObject(*m_entity);
		}
		else
		{
			// Just update the mesh
			m_entity->SetMesh(MeshManager::Get().Load(meshFile));
		}

		if (m_customizationDefinition)
		{
			m_configuration.Apply(*this, *m_customizationDefinition);
		}

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

		if (m_entity->HasAnimationState("Hit"))
		{
			m_damageHitState = m_entity->GetAnimationState("Hit");
			m_damageHitState->SetLoop(false);
			m_damageHitState->SetTimePosition(0.0f);
		}

		// Initialize jump animation states
		if (m_entity->HasAnimationState("JumpStart"))
		{
			m_jumpStartState = m_entity->GetAnimationState("JumpStart");
			m_jumpStartState->SetLoop(false);
		}

		if (m_entity->HasAnimationState("Falling"))
		{
			m_fallingState = m_entity->GetAnimationState("Falling");
			m_fallingState->SetLoop(true);
		}

		if (m_entity->HasAnimationState("Land"))
		{
			m_landState = m_entity->GetAnimationState("Land");
			m_landState->SetLoop(false);
		}

		if (m_entity)
		{
			m_nameComponentNode->SetPosition(Vector3::UnitY * (m_entity->GetBoundingRadius()));
		}

		OnScaleChanged();
	}

	void GameUnitC::UpdateCollider()
	{
		constexpr float radius = 0.25f;

		float halfHeight = 0.65f;
		if (m_entity && m_entity->GetMesh())
		{
			halfHeight = std::max(0.0f, m_entity->GetBoundingBox().GetExtents().y - radius);
		}

		m_collider.Update(
			GetPosition() + Vector3(0.0f, radius, 0.0f),
			GetPosition() + Vector3(0.0f, radius + halfHeight * 2.0f, 0.0f), radius);
	}

	Vector3 GameUnitC::GetDefaultQuestGiverOffset()
	{
		// Ideal size is a unit with a size of 2 units in height, but if the unit is bigger we want to offset the icon position as well as scale it up
		// so that for very big models its not just that tiny icon floating in the sky above some giant head or something of that
		float height = 2.0f;
		float scale = 1.0f;
		if (m_entity)
		{
			height = m_entity->GetBoundingBox().GetExtents().y * 2.2f;
			scale = height / 2.0f;
		}

		return Vector3::UnitY * height;
	}

	void GameUnitC::UpdateMovementInfo()
	{
		m_movementInfo.timestamp = GetAsyncTimeMs();
		m_movementInfo.position = m_sceneNode->GetPosition();
		m_movementInfo.facing = m_sceneNode->GetOrientation().GetYaw();

		// Ensure falling flags and properties are set
		if (m_unitMovement->IsFalling())
		{
			m_movementInfo.movementFlags |= movement_flags::Falling;
			m_movementInfo.jumpVelocity = m_unitMovement->GetVelocity();
		}
		else
		{
			m_movementInfo.movementFlags &= ~movement_flags::Falling;
			m_movementInfo.fallTime = 0;
			m_movementInfo.jumpVelocity = Vector3::Zero;
		}
	}

	void GameUnitC::ResetJumpState()
	{
		m_pressedJump = false;
		m_wasJumping = false;
		m_jumpKeyHoldTime = 0.0f;
		m_jumpForceTimeRemaining = 0.0f;

		if (m_unitMovement && !m_unitMovement->IsFalling())
		{
			m_jumpCurrentCount = 0;
			m_jumpCurrentCountPreJump = 0;
		}
	}

	void GameUnitC::Apply(const VisibilitySetPropertyGroup &group, const AvatarConfiguration &configuration)
	{
		// First, hide all sub entities with the given visibility set tag
		if (!group.subEntityTag.empty())
		{
			for (uint16 i = 0; i < m_entity->GetNumSubEntities(); ++i)
			{
				ASSERT(m_entity->GetMesh()->GetSubMeshCount() == m_entity->GetNumSubEntities());

				SubMesh &subMesh = m_entity->GetMesh()->GetSubMesh(i);
				if (subMesh.HasTag(group.subEntityTag))
				{
					SubEntity *subEntity = m_entity->GetSubEntity(i);
					ASSERT(subEntity);
					subEntity->SetVisible(false);
				}
			}
		}

		const auto it = configuration.chosenOptionPerGroup.find(group.GetId());
		if (it == configuration.chosenOptionPerGroup.end())
		{
			// Nothing to do here because we have no value set
			return;
		}

		// Now make each referenced sub entity visible
		for (const auto &value : group.possibleValues)
		{
			if (value.valueId == it->second)
			{
				for (const auto &subEntityName : value.visibleSubEntities)
				{
					if (SubEntity *subEntity = m_entity->GetSubEntity(subEntityName))
					{
						subEntity->SetVisible(true);
					}
				}
			}
		}
	}

	void GameUnitC::Apply(const MaterialOverridePropertyGroup &group, const AvatarConfiguration &configuration)
	{
		const auto it = configuration.chosenOptionPerGroup.find(group.GetId());
		if (it == configuration.chosenOptionPerGroup.end())
		{
			// Nothing to do here because we have no value set
			return;
		}

		// Now make each referenced sub entity visible
		for (const auto &value : group.possibleValues)
		{
			if (value.valueId == it->second)
			{
				for (const auto &pair : value.subEntityToMaterial)
				{
					if (SubEntity *subEntity = m_entity->GetSubEntity(pair.first))
					{
						MaterialPtr material = MaterialManager::Get().Load(pair.second);
						if (material)
						{
							subEntity->SetMaterial(material);
						}
					}
				}
			}
		}
	}

	void GameUnitC::Apply(const ScalarParameterPropertyGroup &group, const AvatarConfiguration &configuration)
	{
	}
	void GameUnitC::SetCollisionVisibility(bool show)
	{
		if (show)
		{
			if (!m_capsuleDebugObject)
			{
				CreateCapsuleDebugVisualization();
			}
			if (m_capsuleDebugObject)
			{
				m_capsuleDebugObject->SetVisible(true);
			}
		}
		else
		{
			if (m_capsuleDebugObject)
			{
				m_capsuleDebugObject->SetVisible(false);
			}
		}
	}

	void GameUnitC::CreateCapsuleDebugVisualization()
	{
		if (m_capsuleDebugObject || !m_sceneNode)
		{
			return;
		}

		// Get the unit's collision capsule
		const Capsule &capsule = GetCollider();

		// Create manual render object
		static uint64 s_counter = 0;
		m_capsuleDebugObject = m_scene.CreateManualRenderObject(m_entity->GetName() + "_DEBUGCAPSULE_" + std::to_string(s_counter++));
		m_capsuleDebugObject->SetCastShadows(false);
		m_capsuleDebugObject->SetQueryFlags(0);

		{
			// Set up material for wireframe rendering
			auto lineOp = m_capsuleDebugObject->AddLineListOperation(MaterialManager::Get().Load("Models/Engine/ColorDebug.hmat"));

			// Capsule parameters
			const float radius = capsule.GetRadius();
			const Vector3 bottomHemisphereCenter = capsule.GetPointA(); // Center of bottom hemisphere
			const Vector3 topHemisphereCenter = capsule.GetPointB();	// Center of top hemisphere

			// Number of segments for circles and hemispheres
			constexpr int segments = 8;
			constexpr int hemisphereRings = 4;
			const float angleStep = (2.0f * Pi) / segments;
			const float ringStep = (Pi * 0.5f) / hemisphereRings;

			// Draw the cylindrical body (vertical lines)
			for (int i = 0; i < segments; ++i)
			{
				const float angle = i * angleStep;
				const Vector3 topPoint = topHemisphereCenter + Vector3(std::cos(angle) * radius, 0, std::sin(angle) * radius);
				const Vector3 bottomPoint = bottomHemisphereCenter + Vector3(std::cos(angle) * radius, 0, std::sin(angle) * radius);

				lineOp->AddLine(topPoint, bottomPoint).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
			}

			// Draw horizontal circles at top and bottom of cylinder
			for (int i = 0; i < segments; ++i)
			{
				const float angle1 = i * angleStep;
				const float angle2 = ((i + 1) % segments) * angleStep;

				// Top circle (at top hemisphere center level)
				const Vector3 topP1 = topHemisphereCenter + Vector3(std::cos(angle1) * radius, 0, std::sin(angle1) * radius);
				const Vector3 topP2 = topHemisphereCenter + Vector3(std::cos(angle2) * radius, 0, std::sin(angle2) * radius);

				lineOp->AddLine(topP1, topP2).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));

				// Bottom circle (at bottom hemisphere center level)
				const Vector3 bottomP1 = bottomHemisphereCenter + Vector3(std::cos(angle1) * radius, 0, std::sin(angle1) * radius);
				const Vector3 bottomP2 = bottomHemisphereCenter + Vector3(std::cos(angle2) * radius, 0, std::sin(angle2) * radius);

				lineOp->AddLine(bottomP1, bottomP2).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
			}

			// Draw top hemisphere
			for (int ring = 0; ring < hemisphereRings; ++ring)
			{
				const float ringAngle1 = ring * ringStep;
				const float ringAngle2 = (ring + 1) * ringStep;

				const float y1 = std::sin(ringAngle1) * radius;
				const float y2 = std::sin(ringAngle2) * radius;
				const float ringRadius1 = std::cos(ringAngle1) * radius;
				const float ringRadius2 = std::cos(ringAngle2) * radius;

				for (int i = 0; i < segments; ++i)
				{
					const float angle = i * angleStep;
					const float nextAngle = ((i + 1) % segments) * angleStep;

					// Horizontal lines for this ring
					const Vector3 p1 = topHemisphereCenter + Vector3(std::cos(angle) * ringRadius1, y1, std::sin(angle) * ringRadius1);
					const Vector3 p2 = topHemisphereCenter + Vector3(std::cos(nextAngle) * ringRadius1, y1, std::sin(nextAngle) * ringRadius1);

					lineOp->AddLine(p1, p2).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));

					// Vertical lines connecting rings
					if (ring < hemisphereRings - 1)
					{
						const Vector3 p3 = topHemisphereCenter + Vector3(std::cos(angle) * ringRadius2, y2, std::sin(angle) * ringRadius2);

						lineOp->AddLine(p1, p3).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
					}
				}
			}

			// Draw bottom hemisphere
			for (int ring = 0; ring < hemisphereRings; ++ring)
			{
				const float ringAngle1 = ring * ringStep;
				const float ringAngle2 = (ring + 1) * ringStep;

				const float y1 = -std::sin(ringAngle1) * radius;
				const float y2 = -std::sin(ringAngle2) * radius;
				const float ringRadius1 = std::cos(ringAngle1) * radius;
				const float ringRadius2 = std::cos(ringAngle2) * radius;

				for (int i = 0; i < segments; ++i)
				{
					const float angle = i * angleStep;
					const float nextAngle = ((i + 1) % segments) * angleStep;

					// Horizontal lines for this ring
					const Vector3 p1 = bottomHemisphereCenter + Vector3(std::cos(angle) * ringRadius1, y1, std::sin(angle) * ringRadius1);
					const Vector3 p2 = bottomHemisphereCenter + Vector3(std::cos(nextAngle) * ringRadius1, y1, std::sin(nextAngle) * ringRadius1);

					lineOp->AddLine(p1, p2).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));

					// Vertical lines connecting rings
					if (ring < hemisphereRings - 1)
					{
						const Vector3 p3 = bottomHemisphereCenter + Vector3(std::cos(angle) * ringRadius2, y2, std::sin(angle) * ringRadius2);

						lineOp->AddLine(p1, p3).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
					}
				}
			}

			// Draw meridian lines (vertical arcs from top to bottom through the hemispheres)
			for (int i = 0; i < segments; i += 4) // Only draw every 4th meridian to avoid clutter
			{
				const float angle = i * angleStep;

				// Top hemisphere meridian
				for (int ring = 0; ring < hemisphereRings; ++ring)
				{
					const float ringAngle1 = ring * ringStep;
					const float ringAngle2 = (ring + 1) * ringStep;

					const Vector3 p1 = topHemisphereCenter + Vector3(
																 std::cos(angle) * std::cos(ringAngle1) * radius,
																 std::sin(ringAngle1) * radius,
																 std::sin(angle) * std::cos(ringAngle1) * radius);
					const Vector3 p2 = topHemisphereCenter + Vector3(
																 std::cos(angle) * std::cos(ringAngle2) * radius,
																 std::sin(ringAngle2) * radius,
																 std::sin(angle) * std::cos(ringAngle2) * radius);

					lineOp->AddLine(p1, p2).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
				}

				// Bottom hemisphere meridian
				for (int ring = 0; ring < hemisphereRings; ++ring)
				{
					const float ringAngle1 = ring * ringStep;
					const float ringAngle2 = (ring + 1) * ringStep;

					const Vector3 p1 = bottomHemisphereCenter + Vector3(
																	std::cos(angle) * std::cos(ringAngle1) * radius,
																	-std::sin(ringAngle1) * radius,
																	std::sin(angle) * std::cos(ringAngle1) * radius);
					const Vector3 p2 = bottomHemisphereCenter + Vector3(
																	std::cos(angle) * std::cos(ringAngle2) * radius,
																	-std::sin(ringAngle2) * radius,
																	std::sin(angle) * std::cos(ringAngle2) * radius);

					lineOp->AddLine(p1, p2).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
				}
			}
		}

		// Attach to the unit's scene node
		m_sceneNode->AttachObject(*m_capsuleDebugObject);
		m_capsuleDebugObject->SetVisible(true);
	}

	void GameUnitC::DestroyCapsuleDebugVisualization()
	{
		if (!m_capsuleDebugObject)
		{
			return;
		}

		m_scene.DestroyManualRenderObject(*m_capsuleDebugObject);
		m_capsuleDebugObject = nullptr;
	}
	void GameUnitC::SetUnitNameVisible(const bool show)
	{
		m_questOffset = GetDefaultQuestGiverOffset();
		if (show)
		{
			m_questOffset.y += (m_nameComponent->GetBoundingBox().GetSize().y + 0.1f);
		}

		if (m_nameComponent)
		{
			m_nameComponent->SetVisible(show);
		}
	}

	const proto_client::SpellEntry *GameUnitC::GetOpenSpell() const
	{
		for (const auto *spell : m_spells)
		{
			for (const auto &effect : spell->effects())
			{
				if (effect.type() == spell_effects::OpenLock)
				{
					return spell;
				}
			}
		}

		return nullptr;
	}

	void GameUnitC::OnLanded()
	{
		PlayLandAnimation();

		m_movementInfo.position = m_sceneNode->GetDerivedPosition();
		m_movementInfo.facing = GetSceneNode()->GetOrientation().GetYaw();
		m_movementInfo.movementFlags &= ~movement_flags::Falling;
		m_movementInfo.timestamp = GetAsyncTimeMs();
	}

	void GameUnitC::OnStartFalling()
	{
		const bool wasFalling = (m_movementInfo.movementFlags & movement_flags::Falling) != 0;

		UpdateMovementInfo();
		m_movementInfo.movementFlags |= movement_flags::Falling;

		if (!wasFalling)
		{
			m_netDriver.OnMoveEvent(*this, MovementEvent(movement_event_type::Fall, m_movementInfo.timestamp, m_movementInfo));
		}
	}

	void GameUnitC::PlayLandAnimation()
	{
		// Play landing animation if available
		if (m_landState)
		{
			// Reset animation state to beginning
			m_landState->SetTimePosition(0.0f);
			PlayOneShotAnimation(m_landState);
		}

		// Also ensure jump start state is reset
		if (m_jumpStartState)
		{
			m_jumpStartState->SetTimePosition(0.0f);
		}
	}

}
