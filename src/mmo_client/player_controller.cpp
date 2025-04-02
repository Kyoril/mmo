// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "player_controller.h"

#include "cursor.h"
#include "event_loop.h"
#include "vector_sink.h"
#include "net/realm_connector.h"
#include "console/console_var.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "loot_client.h"
#include "trainer_client.h"

#include "platform.h"
#include "vendor_client.h"
#include "base/profiler.h"
#include "console/console.h"
#include "frame_ui/frame_mgr.h"
#include "game/loot.h"
#include "game_client/object_mgr.h"
#include "math/collision.h"

namespace mmo
{
	// Console variables for gameplay
	static ConsoleVar* s_mouseSensitivityCVar = nullptr;
	static ConsoleVar* s_invertVMouseCVar = nullptr;
	static ConsoleVar* s_maxCameraZoomCVar = nullptr;
	static ConsoleVar* s_cameraZoomCVar = nullptr;

	static ConsoleVar* s_resetCameraYawCVar = nullptr;
	static ConsoleVar* s_resetCameraPitchCVar = nullptr;

	extern Cursor g_cursor;

	PlayerController::PlayerController(Scene& scene, RealmConnector& connector, LootClient& lootClient, VendorClient& vendorClient, TrainerClient& trainerClient)
		: m_scene(scene)
		, m_lootClient(lootClient)
		, m_vendorClient(vendorClient)
		, m_trainerClient(trainerClient)
		, m_connector(connector)
	{
		if (!s_mouseSensitivityCVar)
		{
			s_mouseSensitivityCVar = ConsoleVarMgr::RegisterConsoleVar("MouseSensitivity", "Gets or sets the mouse sensitivity value", "0.25");
			s_invertVMouseCVar = ConsoleVarMgr::RegisterConsoleVar("InvertVMouse", "Whether the vertical camera rotation is inverted.", "1");
			s_maxCameraZoomCVar = ConsoleVarMgr::RegisterConsoleVar("MaxCameraZoom", "Gets or sets the maximum camera zoom value.", "8");
			s_cameraZoomCVar = ConsoleVarMgr::RegisterConsoleVar("CameraZoom", "Gets or sets the current camera zoom value.", "8");

			s_resetCameraYawCVar = ConsoleVarMgr::RegisterConsoleVar("ResetCameraHorizontally", "Gets or sets whether the camera yaw will be reset while moving.", "1");
			s_resetCameraPitchCVar = ConsoleVarMgr::RegisterConsoleVar("ResetCameraVertically", "Gets or sets whether the camera pitch will be reset while moving.", "1");
		}

		m_cvarConnections += {
			s_maxCameraZoomCVar->Changed += [this](ConsoleVar&, const std::string&)
				{
					NotifyCameraZoomChanged();
				},
				s_cameraZoomCVar->Changed += [this](ConsoleVar&, const std::string&)
				{
					NotifyCameraZoomChanged();
				}
		};

		m_selectionSceneQuery = m_scene.CreateRayQuery(Ray());
		m_selectionSceneQuery->SetQueryMask(0x00000002);

		SetupCamera();
	}

	PlayerController::~PlayerController()
	{
		// Ensure we reset the cursor type properly
		g_cursor.SetCursorType(CursorType::Pointer);
		Platform::ReleaseMouseCapture();

		m_controlledUnit.reset();

		if (m_defaultCamera)
		{
			m_scene.DestroyCamera(*m_defaultCamera);
			m_defaultCamera = nullptr;
		}

		if (m_cameraNode)
		{
			m_scene.DestroySceneNode(*m_cameraNode);
			m_cameraNode = nullptr;
		}

		if (m_cameraAnchorNode)
		{
			m_scene.DestroySceneNode(*m_cameraAnchorNode);
			m_cameraAnchorNode = nullptr;
		}

		if (m_cameraOffsetNode)
		{
			m_scene.DestroySceneNode(*m_cameraOffsetNode);
			m_cameraOffsetNode = nullptr;
		}
	}

	void PlayerController::StopAllMovement()
	{
		m_controlFlags = ControlFlags::None;
	}

	void PlayerController::MovePlayer()
	{
		int movementDirection = (m_controlFlags & ControlFlags::Autorun) != 0;

		if (m_controlFlags & ControlFlags::MoveForwardKey)
		{
			++movementDirection;
		}
		if (m_controlFlags & ControlFlags::MoveBackwardKey)
		{
			--movementDirection;
		}

		// If both flags are set, we also move the player forward (equal to holding both mouse buttons down at the same time)
		if ((m_controlFlags & ControlFlags::MoveAndTurnPlayer) == ControlFlags::MoveAndTurnPlayer)
		{
			++movementDirection;
		}

		if (!m_controlledUnit->IsAlive())
		{
			movementDirection = 0;
		}

		if (movementDirection != 0)
		{
			if (m_controlFlags & ControlFlags::MoveSent)
			{
				return;
			}

			const bool forward = movementDirection > 0;
			m_controlledUnit->StartMove(forward);
			SendMovementUpdate(forward ? game::client_realm_packet::MoveStartForward : game::client_realm_packet::MoveStartBackward);
			StartHeartbeatTimer();
			
			SetControlBit(ControlFlags::MoveSent, true);
			return;
		}

		if((m_controlFlags & ControlFlags::MoveSent) != 0)
		{
			m_controlledUnit->StopMove();
			SendMovementUpdate(game::client_realm_packet::MoveStop);
			StopHeartbeatTimer();

			SetControlBit(ControlFlags::MoveSent, false);
		}
	}

	void PlayerController::StrafePlayer()
	{
		int direction = (m_controlFlags & ControlFlags::StrafeLeftKey) != 0;
		if ((m_controlFlags & ControlFlags::TurnPlayer) != 0 && (m_controlFlags & ControlFlags::TurnLeftKey) != 0)
		{
			++direction;
		}

		if ((m_controlFlags & ControlFlags::StrafeRightKey) != 0)
		{
			--direction;
		}

		if ((m_controlFlags & ControlFlags::TurnPlayer) != 0 && (m_controlFlags & ControlFlags::TurnRightKey) != 0)
		{
			--direction;
		}


		if (!m_controlledUnit->IsAlive())
		{
			direction = 0;
		}

		if (direction != 0)
		{
			if ((m_controlFlags & ControlFlags::StrafeSent) != 0)
			{
				return;
			}

			const bool left = direction > 0;
			m_controlledUnit->StartStrafe(left);
			SendMovementUpdate(left ? game::client_realm_packet::MoveStartStrafeLeft : game::client_realm_packet::MoveStartStrafeRight);
			StartHeartbeatTimer();

			SetControlBit(ControlFlags::StrafeSent, true);
			return;
		}

		if (m_controlFlags & ControlFlags::StrafeSent)
		{
			m_controlledUnit->StopStrafe();
			SetControlBit(ControlFlags::StrafeSent, false);
		}
	}

	void PlayerController::TurnPlayer()
	{
		if ((m_controlFlags & ControlFlags::TurnPlayer) == 0 || (m_controlFlags & ControlFlags::TurnSent) != 0)
		{
			int direction = ((m_controlFlags & ControlFlags::TurnLeftKey) != 0);
			if ((m_controlFlags & ControlFlags::TurnRightKey))
			{
				--direction;
			}

			if (!m_controlledUnit->IsAlive())
			{
				direction = 0;
			}

			if (direction != 0)
			{
				if ((m_controlFlags & ControlFlags::TurnSent))
				{
					return;
				}
				
				const bool left = direction > 0;
				m_controlledUnit->StartTurn(left);
				SendMovementUpdate(left ? game::client_realm_packet::MoveStartTurnLeft : game::client_realm_packet::MoveStartTurnRight);
				SetControlBit(ControlFlags::TurnSent, true);
			}
			else if (m_controlFlags & ControlFlags::TurnSent)
			{
				m_controlledUnit->StopTurn();
				SendMovementUpdate(game::client_realm_packet::MoveStopTurn);
				SetControlBit(ControlFlags::TurnSent, false);
			}
		}
	}

	void PlayerController::ApplyLocalMovement(const float deltaSeconds) const
	{
		if (m_controlledUnit->IsBeingMoved())
		{
			return;
		}

		PROFILE_SCOPE("Local Player Collision");

		// Apply gravity to jump velocity
		MovementInfo info = m_controlledUnit->GetMovementInfo();

		// Are we somehow moving at all?
		if ((info.movementFlags & movement_flags::PositionChanging) == 0)
		{
			return;
		}

	}

	void PlayerController::SendMovementUpdate(const uint16 opCode) const
	{
		// Build the movement data
		MovementInfo info = m_controlledUnit->GetMovementInfo();
		info.timestamp = GetAsyncTimeMs();
		info.position = m_controlledUnit->GetSceneNode()->GetDerivedPosition();
		info.facing = m_controlledUnit->GetSceneNode()->GetDerivedOrientation().GetYaw();
		info.pitch = Radian(0);
		m_connector.SendMovementUpdate(m_controlledUnit->GetGuid(), opCode, info);
	}

	void PlayerController::StartHeartbeatTimer()
	{
		if (m_lastHeartbeat != 0)
		{
			return;
		}

		m_lastHeartbeat = GetAsyncTimeMs();
	}

	void PlayerController::StopHeartbeatTimer()
	{
		m_lastHeartbeat = 0;
	}

	void PlayerController::UpdateHeartbeat()
	{
		if (m_lastHeartbeat == 0)
		{
			return;
		}

		const auto now = GetAsyncTimeMs();
		if (now - m_lastHeartbeat >= 500)
		{
			SendMovementUpdate(game::client_realm_packet::MoveHeartBeat);
			m_lastHeartbeat = now;
		}
	}

	void PlayerController::NotifyCameraZoomChanged()
	{
		ASSERT(s_maxCameraZoomCVar);
		ASSERT(s_cameraZoomCVar);

		const float maxZoom = Clamp<float>(s_maxCameraZoomCVar->GetFloatValue(), 2.0f, 15.0f);
		const float newZoom = Clamp<float>(s_cameraZoomCVar->GetFloatValue(), 0.0f, maxZoom);
		m_desiredCameraLocation = m_cameraNode->GetOrientation() * (Vector3::UnitZ * newZoom);
	}

	void PlayerController::ClampCameraPitch()
	{
		const float clampDegree = 60.0f;

		// Ensure the camera pitch is clamped
		const Radian pitch = m_cameraPitchNode->GetOrientation().GetPitch();
		if (pitch < Degree(-clampDegree))
		{
			m_cameraPitchNode->Pitch(Degree(-clampDegree) - pitch, TransformSpace::Local);
		}
		if (pitch > Degree(clampDegree))
		{
			m_cameraPitchNode->Pitch(Degree(clampDegree) - pitch, TransformSpace::Local);
		}
	}

	void PlayerController::OnMovementCompleted(GameUnitC& unit, const MovementInfo& movementInfo)
	{
		SendMovementUpdate(game::client_realm_packet::MoveEnded);
	}

	void PlayerController::HandleCameraCollision()
	{
		float zoom = s_cameraZoomCVar->GetFloatValue();
		m_desiredCameraLocation = m_cameraNode->GetOrientation() * (Vector3::UnitZ * zoom);
		m_cameraNode->SetPosition(m_desiredCameraLocation);

		// Camera raycast
		m_selectionSceneQuery->ClearResult();
		m_selectionSceneQuery->SetSortByDistance(true);
		m_selectionSceneQuery->SetQueryMask(0xffffffff);
		Ray cameraRay(m_cameraAnchorNode->GetDerivedPosition(), m_cameraNode->GetDerivedPosition());
		m_selectionSceneQuery->SetRay(cameraRay);
		m_selectionSceneQuery->Execute();

		const auto& cameraHitResult = m_selectionSceneQuery->GetLastResult();
		for (auto& result : cameraHitResult)
		{
			if (const Entity* entity = static_cast<Entity*>(result.movable))
			{
				const Matrix4 inverse = entity->GetParentNodeFullTransform().Inverse();
				Ray localRay = Ray(inverse * cameraRay.origin, inverse * cameraRay.destination);
				localRay.hitDistance = std::numeric_limits<float>::max();

				const auto& tree = entity->GetMesh()->GetCollisionTree();
				if (tree.IsEmpty())
				{
					continue;
				}

				if (tree.IntersectRay(localRay, nullptr, raycast_flags::EarlyExit))
				{
					const float dist = localRay.hitDistance * localRay.GetLength() * 0.9f;
					if (zoom > dist)
					{
						zoom = std::max(0.0f, dist);
					}

					break;
				}

			}
		}

		m_desiredCameraLocation = m_cameraNode->GetOrientation() * (Vector3::UnitZ * zoom);
		m_cameraNode->SetPosition(m_desiredCameraLocation);
	}

	void PlayerController::SetControlBit(const ControlFlags::Type flag, bool set)
	{
		if (set)
		{
			m_controlFlags |= flag;

			if ((flag & ControlFlags::MovePlayer) != 0)
			{
				m_controlFlags &= ~ControlFlags::Autorun;
			}

			// If any relevant flag was added and we are moving using both mouse buttons now, remove auto run flag
			if (((flag & ControlFlags::MoveAndTurnPlayer) != 0) &&
				(m_controlFlags & ControlFlags::MoveAndTurnPlayer) == ControlFlags::MoveAndTurnPlayer)
			{
				m_controlFlags &= ~ControlFlags::Autorun;
			}
		}
		else
		{
			m_controlFlags &= ~flag;
		}
	}

	void PlayerController::Jump()
	{
		if (!m_controlledUnit)
		{
			return;
		}

		// Are we still falling? Then we can't jump!
		MovementInfo movementInfo = m_controlledUnit->GetMovementInfo();
		if ((movementInfo.movementFlags & movement_flags::Falling) || (movementInfo.movementFlags & movement_flags::FallingFar))
		{
			return;
		}

		// TODO
		movementInfo.jumpVelocity = 7.98f;
		movementInfo.movementFlags |= movement_flags::Falling;

		// Calculate jump velocity
		if (movementInfo.IsMoving() || movementInfo.IsStrafing())
		{
			Vector3 movementVector;
			if (movementInfo.movementFlags & movement_flags::Forward)
			{
				movementVector.x += 1.0f;
			}
			if (movementInfo.movementFlags & movement_flags::Backward)
			{
				movementVector.x -= 1.0f;
			}
			if (movementInfo.movementFlags & movement_flags::StrafeLeft)
			{
				movementVector.z -= 1.0f;
			}
			if (movementInfo.movementFlags & movement_flags::StrafeRight)
			{
				movementVector.z += 1.0f;
			}

			MovementType movementType = movement_type::Run;
			if (movementVector.x < 0.0)
			{
				movementType = movement_type::Backwards;
			}

			movementInfo.jumpXZSpeed = m_controlledUnit->GetSpeed(movementType);
		}

		m_controlledUnit->ApplyMovementInfo(movementInfo);
		SendMovementUpdate(game::client_realm_packet::MoveJump);
		StartHeartbeatTimer();
	}

	void PlayerController::OnHoveredUnitChanged(GameUnitC* previousHoveredUnit)
	{
		if (m_hoveredUnit)
		{
			if(m_hoveredUnit->CanBeLooted())
			{
				g_cursor.SetCursorType(CursorType::Loot);
			}
			else
			{
				if (m_hoveredUnit->IsAlive())
				{
					if (m_hoveredUnit->Get<uint32>(object_fields::NpcFlags) != 0)
					{
						g_cursor.SetCursorType(CursorType::Gossip);
					}
					else
					{
						g_cursor.SetCursorType(m_controlledUnit->IsFriendlyTo(*m_hoveredUnit) ? CursorType::Pointer : CursorType::Attack);
					}
					
				}
				else
				{
					g_cursor.SetCursorType(CursorType::Pointer);
				}
			}
		}
		else
		{
			g_cursor.SetCursorType(CursorType::Pointer);
		}

		if (m_hoveredUnit != previousHoveredUnit)
		{
			ObjectMgr::SetHoveredObject(m_hoveredUnit ? m_hoveredUnit->GetGuid() : 0);
			FrameManager::Get().TriggerLuaEvent("HOVERED_UNIT_CHANGED");
		}
	}

	void PlayerController::Update(const float deltaSeconds)
	{
		if (!m_controlledUnit)
		{
			return;
		}

		HandleCameraCollision();

		int32 w, h;
		GraphicsDevice::Get().GetViewport(nullptr, nullptr, &w, &h);
		m_defaultCamera->InvalidateView();

		if (!m_controlledUnit->IsBeingMoved())
		{
			MovePlayer();
			StrafePlayer();

			TurnPlayer();

			ApplyLocalMovement(deltaSeconds);
			UpdateHeartbeat();
		}

		if (!(m_controlFlags & ControlFlags::TurnCamera) && !(m_controlFlags & ControlFlags::TurnPlayer))
		{
			if (m_controlledUnit->GetMovementInfo().IsChangingPosition() || m_controlledUnit->GetMovementInfo().IsTurning())
			{
				if (s_resetCameraYawCVar->GetBoolValue())
				{
					// Slowly reset the camera yaw over time to the player's facing direction (yaw = 0.0f)
					const float yaw = m_cameraAnchorNode->GetOrientation().GetYaw().GetValueRadians();
					const float newYaw = 0.0f;
					m_cameraAnchorNode->Yaw(Radian((newYaw - yaw) * 4.0f * deltaSeconds), TransformSpace::Parent);
				}

				if (s_resetCameraPitchCVar->GetBoolValue())
				{
					// Get the current pitch in radians.
					const float currentPitch = m_cameraPitchNode->GetOrientation().GetPitch().GetValueRadians();
					// Define the target pitch (-15 degrees converted to radians).
					const float targetPitch = Degree(-15.0f).GetValueRadians();

					// Compute the shortest angular difference.
					const float deltaAngle = std::atan2(std::sin(targetPitch - currentPitch),
						std::cos(targetPitch - currentPitch));

					// Interpolate the pitch correction over time.
					m_cameraPitchNode->Pitch(Radian(deltaAngle * 4.0f * deltaSeconds), TransformSpace::Local);
				}
			}
		}

		// When we are looting, check the distance to the looted object on movement
		if (m_lootClient.IsLooting() && m_controlledUnit->GetMovementInfo().IsChangingPosition())
		{
			const auto lootedObject = ObjectMgr::Get<GameObjectC>(m_lootClient.GetLootedObjectGuid());
			if (lootedObject && !m_controlledUnit->IsWithinRange(*lootedObject, LootDistance))
			{
				m_lootClient.CloseLoot();
			}
		}

		if (m_controlledUnit->GetMovementInfo().IsChangingPosition())
		{
			if (m_vendorClient.HasVendor())
			{
				const auto vendorObject = ObjectMgr::Get<GameObjectC>(m_vendorClient.GetVendorGuid());
				if (vendorObject && !m_controlledUnit->IsWithinRange(*vendorObject, LootDistance))
				{
					m_vendorClient.CloseVendor();
				}
			}
			else if(m_trainerClient.HasTrainer())
			{
				const auto trainerObject = ObjectMgr::Get<GameObjectC>(m_trainerClient.GetTrainerGuid());
				if (trainerObject && !m_controlledUnit->IsWithinRange(*trainerObject, LootDistance))
				{
					m_trainerClient.CloseTrainer();
				}
			}
		}

		// Fire unit raycast
		m_selectionSceneQuery->ClearResult();
		m_selectionSceneQuery->SetSortByDistance(true);
		m_selectionSceneQuery->SetQueryMask(0x00000002);
		m_selectionSceneQuery->SetRay(m_defaultCamera->GetCameraToViewportRay(
			static_cast<float>(m_x) / static_cast<float>(w),
			static_cast<float>(m_y) / static_cast<float>(h), 1000.0f));
		m_selectionSceneQuery->Execute();

		const uint64 previousSelectedUnit = m_controlledUnit->Get<uint64>(object_fields::TargetUnit);

		GameUnitC* newHoveredUnit = nullptr;

		const auto& hitResult = m_selectionSceneQuery->GetLastResult();
		if (!hitResult.empty())
		{
			Entity* entity = static_cast<Entity*>(hitResult[0].movable);
			if (entity)
			{
				newHoveredUnit = entity->GetUserObject<GameUnitC>();
			}
		}

		GameUnitC* previousUnit = m_hoveredUnit;
		m_hoveredUnit = newHoveredUnit;
		OnHoveredUnitChanged(previousUnit);
	}

	void PlayerController::OnMouseDown(const MouseButton button, const int32 x, const int32 y)
	{
		m_x = x;
		m_y = y;

		if (!m_controlledUnit)
		{
			return;
		}

		m_mouseDownTime = GetAsyncTimeMs();
		m_mouseMoved = 0;

		if ((m_controlFlags & (ControlFlags::TurnCamera | ControlFlags::TurnPlayer)) == 0)
		{
            Platform::GetCursorPos(m_lastMousePosition[0], m_lastMousePosition[1]);
		}

		if (button == MouseButton_Left)
		{
			SetControlBit(ControlFlags::TurnCamera, true);
		}
		else if (button == MouseButton_Right)
		{
			SetControlBit(ControlFlags::TurnPlayer, true);
		}

		if (button == MouseButton_Left || button == MouseButton_Right)
		{
			Platform::CaptureMouse();
		}
	}

	void PlayerController::OnMouseUp(const MouseButton button, const int32 x, const int32 y)
	{
		m_x = x;
		m_y = y;

		if (!m_controlledUnit)
		{
			return;
		}

		// TODO: Drop items we are dragging
		if (g_cursor.GetItemType() == CursorItemType::Item)
		{
			// Drop the item entirely

		}

		if (button == MouseButton_Left)
		{
			SetControlBit(ControlFlags::TurnCamera, false);
		}
		else if (button == MouseButton_Right)
		{
			SetControlBit(ControlFlags::TurnPlayer, false);
		}

		if (m_mouseMoved <= 16)
		{
			const uint64 previousSelectedUnit = m_controlledUnit->Get<uint64>(object_fields::TargetUnit);

			if (m_hoveredUnit)
			{
				if (m_hoveredUnit->GetGuid() != previousSelectedUnit)
				{
					m_controlledUnit->SetTargetUnit(ObjectMgr::Get<GameUnitC>(m_hoveredUnit->GetGuid()));
				}

				if (button == MouseButton_Right)
				{
					if (m_hoveredUnit->CanBeLooted())
					{
						if (m_controlledUnit->IsWithinRange(*m_hoveredUnit, LootDistance))
						{
							// Open the loot dialog if we are close enough
							m_lootClient.LootObject(*m_hoveredUnit);
						}
						else
						{
							FrameManager::Get().TriggerLuaEvent("GAME_ERROR", "ERR_TOO_FAR_AWAY_TO_LOOT");
						}
					}
					else if (m_hoveredUnit->IsAlive())
					{
						if (m_controlledUnit->IsFriendlyTo(*m_hoveredUnit) && m_controlledUnit->IsWithinRange(*m_hoveredUnit, LootDistance))
						{
							const uint32 npcFlags = m_hoveredUnit->Get<uint32>(object_fields::NpcFlags);

							// Check for explicit flags so we can ask the server for a specific action
							switch(npcFlags)
							{
							case npc_flags::QuestGiver:
								m_connector.QuestGiverHello(m_hoveredUnit->GetGuid());
								break;
							case npc_flags::Trainer:
								m_connector.TrainerMenu(m_hoveredUnit->GetGuid());
								break;
							case npc_flags::Vendor:
								m_connector.ListInventory(m_hoveredUnit->GetGuid());
								break;
							default:
								// No specific npc flag set, so ask for the gossip dialog
								if (npcFlags != 0)
								{
									m_connector.GossipHello(m_hoveredUnit->GetGuid());
								}
								break;
							}
						}
						else
						{
							m_controlledUnit->Attack(*m_hoveredUnit);
						}
					}
				}
			}
			else
			{
				if (previousSelectedUnit != 0)
				{
					m_controlledUnit->SetTargetUnit(nullptr);
				}
			}
		}

		if ((button == MouseButton_Left || button == MouseButton_Right) && 
			(m_controlFlags & (ControlFlags::MoveAndTurnPlayer)) == 0)
		{
			Platform::ReleaseMouseCapture();
		}
	}

	void PlayerController::OnMouseMove(const int32 x, const int32 y)
	{
		if (!m_controlledUnit)
		{
			return;
		}

		m_x = x;
		m_y = y;

		if ((m_controlFlags & (ControlFlags::TurnCamera | ControlFlags::TurnPlayer)) == 0)
		{
			return;
		}

		// Get the local mouse position and calculate delta to the last mouse position
		int32 cursorX, cursorY;
		Platform::GetCursorPos(cursorX, cursorY);
		const int32 deltaX = cursorX - m_lastMousePosition.x();
		const int32 deltaY = cursorY - m_lastMousePosition.y();

		m_mouseMoved += std::abs(deltaX) + std::abs(deltaY);

		// Reset platform mouse cursor to captured position to prevent it from reaching the edge of the screen
		Platform::ResetCursorPosition();

		SceneNode* yawNode = m_cameraAnchorNode;
		if (std::abs(deltaX) > 0)
		{
			yawNode->Yaw(Degree(static_cast<float>(deltaX) * s_mouseSensitivityCVar->GetFloatValue() * -1.0f), TransformSpace::Parent);
		}
		
		if (std::abs(deltaY) > 0)
		{
			const float factor = s_invertVMouseCVar->GetBoolValue() ? -1.0f : 1.0f;

			const Radian deltaPitch = Degree(static_cast<float>(deltaY) * factor * s_mouseSensitivityCVar->GetFloatValue());
			m_cameraPitchNode->Pitch(deltaPitch, TransformSpace::Local);

			ClampCameraPitch();
		}

		if ((m_controlFlags & ControlFlags::TurnPlayer) != 0 && m_controlledUnit->IsAlive() && !m_controlledUnit->IsBeingMoved())
		{
			const Radian facing = (m_controlledUnit->GetSceneNode()->GetOrientation() * m_cameraAnchorNode->GetOrientation()).GetYaw();
			m_controlledUnit->GetSceneNode()->SetOrientation(Quaternion(facing, Vector3::UnitY));
			m_cameraAnchorNode->SetOrientation(Quaternion::Identity);

			m_controlledUnit->SetFacing(facing);

			// Limit to 10 per second
			if (GetAsyncTimeMs() >= m_nextSetFacing)
			{
				SendMovementUpdate(game::client_realm_packet::MoveSetFacing);
				m_nextSetFacing = GetAsyncTimeMs() + 100;
			}
			
		}
	}

	void PlayerController::OnMouseWheel(const int32 delta)
	{
		if (!m_controlledUnit)
		{
			return;
		}

		ASSERT(m_cameraNode);

		const float currentZoom = s_cameraZoomCVar->GetFloatValue();
		const float maxZoom = s_maxCameraZoomCVar->GetFloatValue();
		s_cameraZoomCVar->Set(std::max(0.0f, std::min(currentZoom - static_cast<float>(delta), maxZoom)));
	}
		
	void PlayerController::SetControlledUnit(const std::shared_ptr<GameUnitC>& controlledUnit)
	{
		m_moveCompleted.disconnect();
		m_cameraOffsetNode->RemoveFromParent();
		
		// Re-enable selection for previously controlled unit
		if (m_controlledUnit)
		{
			m_controlledUnit->SetQueryMask(0x00000002);
		}

		m_controlledUnit = controlledUnit;

		if (m_controlledUnit)
		{
			ASSERT(m_controlledUnit->GetSceneNode());
			m_controlledUnit->GetSceneNode()->AddChild(*m_cameraOffsetNode);

			// Not selectable via clicking when controlled
			m_controlledUnit->SetQueryMask(0);

			m_moveCompleted = m_controlledUnit->movementEnded.connect(this, &PlayerController::OnMovementCompleted);
		}
	}

	void PlayerController::OnMoveFallLand()
	{
		SendMovementUpdate(game::client_realm_packet::MoveFallLand);

		if ((m_controlFlags & ControlFlags::MoveSent) == 0)
		{
			StopHeartbeatTimer();
		}
	}

	void PlayerController::OnMoveFall()
	{
		SendMovementUpdate(game::client_realm_packet::MoveJump);
	}

	void PlayerController::SetupCamera()
	{
		// Default camera for the player
		m_defaultCamera = m_scene.CreateCamera("Default");
		
		// Camera node which will hold the camera but is a child of an anchor node
		m_cameraNode = &m_scene.CreateSceneNode("DefaultCamera");
		m_cameraNode->AttachObject(*m_defaultCamera);
		m_cameraNode->SetPosition(Vector3(0.0f, 0.0f, 3.0f));

		// Anchor node for the camera. This node is directly attached to the player node and marks
		// the target view point of the camera. By adding the camera node as a child, we can rotate
		// the anchor node which results in the camera orbiting around the player entity.
		m_cameraAnchorNode = &m_scene.CreateSceneNode("CameraAnchor");
		m_cameraAnchorNode->SetPosition(Vector3::UnitY);

		m_cameraPitchNode = m_cameraAnchorNode->CreateChildSceneNode("CameraPitch", Vector3::Zero, Quaternion(Degree(-15.0f), Vector3::UnitX));
		m_cameraPitchNode->AddChild(*m_cameraNode);

		m_cameraOffsetNode = &m_scene.CreateSceneNode("CameraOffset");
		m_cameraOffsetNode->AddChild(*m_cameraAnchorNode);
		m_cameraOffsetNode->Yaw(Degree(-90.0f), TransformSpace::Parent);

		NotifyCameraZoomChanged();
	}

	void PlayerController::ResetControls()
	{
		m_lastMousePosition.clear();
		m_leftButtonDown = false;
		m_rightButtonDown = false;
		m_controlFlags = ControlFlags::None;

		ASSERT(m_cameraNode);
		ASSERT(m_cameraAnchorNode);
		m_cameraNode->SetPosition(Vector3::UnitZ * 3.0f);

		const Quaternion defaultRotation(Degree(0.0f), Vector3::UnitY);
		m_cameraAnchorNode->SetOrientation(defaultRotation);

		NotifyCameraZoomChanged();
	}
}
