// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#include "world_ping_visualizer.h"

#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/manual_render_object.h"
#include "scene_graph/camera.h"
#include "scene_graph/material_manager.h"
#include "game_client/object_mgr.h"
#include "game_client/game_unit_c.h"

namespace mmo
{
	WorldPingVisualizer::WorldPingVisualizer(Scene& scene)
		: m_scene(scene)
	{
	}

	WorldPingVisualizer::~WorldPingVisualizer()
	{
		Clear();
	}

	void WorldPingVisualizer::AddPositionPing(const uint64 senderGuid, const Vector3& worldPos)
	{
		const int32 idx = AcquireSlot(senderGuid);
		if (idx < 0)
		{
			return;
		}

		PingSlot& slot = m_slots[idx];
		FreeSlot(slot);

		slot.active = true;
		slot.type = PingType::Position;
		slot.remainingTime = kPingDuration;
		slot.senderGuid = senderGuid;
		slot.targetGuid = 0;
		slot.worldPosition = worldPos;

		BuildIconMesh(slot);
		BuildLineMesh(slot);
	}

	void WorldPingVisualizer::AddUnitPing(const uint64 senderGuid, const uint64 targetGuid)
	{
		const int32 idx = AcquireSlot(senderGuid);
		if (idx < 0)
		{
			return;
		}

		PingSlot& slot = m_slots[idx];
		FreeSlot(slot);

		slot.active = true;
		slot.type = PingType::Unit;
		slot.remainingTime = kPingDuration;
		slot.senderGuid = senderGuid;
		slot.targetGuid = targetGuid;

		// Resolve initial position from the unit if available
		if (auto unit = ObjectMgr::Get<GameUnitC>(targetGuid))
		{
			slot.worldPosition = unit->GetPosition();
		}
		else
		{
			slot.worldPosition = Vector3::Zero;
		}

		BuildIconMesh(slot);
		// No vertical line for unit pings
	}

	void WorldPingVisualizer::Update(const float deltaSeconds, const Camera& camera)
	{
		const Quaternion camOrientation = camera.GetDerivedOrientation();

		for (auto& slot : m_slots)
		{
			if (!slot.active)
			{
				continue;
			}

			slot.remainingTime -= deltaSeconds;
			if (slot.remainingTime <= 0.0f)
			{
				FreeSlot(slot);
				continue;
			}

			// For unit pings: follow the unit's current world position
			if (slot.type == PingType::Unit && slot.targetGuid != 0)
			{
				if (auto unit = ObjectMgr::Get<GameUnitC>(slot.targetGuid))
				{
					slot.worldPosition = unit->GetPosition();
				}
			}

			// Compute alpha (fade out over last second)
			const float fadeStart = 1.0f;
			const float alpha = slot.remainingTime < fadeStart
				? slot.remainingTime / fadeStart
				: 1.0f;
			const uint8 a = static_cast<uint8>(alpha * 255.0f);

			// Update icon node: position + camera-facing orientation
			if (slot.iconNode)
			{
				const Vector3 iconWorldPos = slot.worldPosition + Vector3(0.0f, kIconHeight, 0.0f);
				slot.iconNode->SetPosition(iconWorldPos);
				slot.iconNode->SetOrientation(camOrientation);
			}

			// Rebuild geometry only when quantized alpha changes (max every ~4 frames at 60fps)
			const uint8 quantized = (a >> 2) << 2;  // 64 discrete steps
			if (quantized != slot.lastAlpha)
			{
				slot.lastAlpha = quantized;
				const uint32 iconColor = (static_cast<uint32>(quantized) << 24) | 0x00FFFFFF;
				const uint32 lineColor  = (static_cast<uint32>(quantized) << 24) | 0x004488FF;

				if (slot.iconMesh)
				{
					slot.iconMesh->Clear();
					auto tri = slot.iconMesh->AddTriangleListOperation(
						MaterialManager::Get().Load("Interface/WorldPingIcon.hmat"));

					const float h = kIconHalfSize;
					auto& t1 = tri->AddTriangle(
						Vector3(-h,  h, 0.0f),
						Vector3(-h, -h, 0.0f),
						Vector3( h,  h, 0.0f));
					t1.SetColor(iconColor);
					t1.SetUV(0, 0.0f, 0.0f);
					t1.SetUV(1, 0.0f, 1.0f);
					t1.SetUV(2, 1.0f, 0.0f);

					auto& t2 = tri->AddTriangle(
						Vector3(-h, -h, 0.0f),
						Vector3( h, -h, 0.0f),
						Vector3( h,  h, 0.0f));
					t2.SetColor(iconColor);
					t2.SetUV(0, 0.0f, 1.0f);
					t2.SetUV(1, 1.0f, 1.0f);
					t2.SetUV(2, 1.0f, 0.0f);
				}

				if (slot.lineMesh)
				{
					slot.lineMesh->Clear();
					auto line = slot.lineMesh->AddLineListOperation(
						MaterialManager::Get().Load("Models/Engine/Axis.hmat"));
					auto& seg = line->AddLine(
						slot.worldPosition + Vector3(0.0f, kIconHeight - kIconHalfSize, 0.0f),
						slot.worldPosition);
					seg.SetStartColor(lineColor);
					seg.SetEndColor(lineColor);
				}
			}
		}
	}

	void WorldPingVisualizer::Clear()
	{
		for (auto& slot : m_slots)
		{
			FreeSlot(slot);
		}
	}

	int32 WorldPingVisualizer::AcquireSlot(const uint64 senderGuid)
	{
		// Reuse existing slot for this sender
		for (int32 i = 0; i < kMaxPings; ++i)
		{
			if (m_slots[i].active && m_slots[i].senderGuid == senderGuid)
			{
				return i;
			}
		}

		// Use first inactive slot
		for (int32 i = 0; i < kMaxPings; ++i)
		{
			if (!m_slots[i].active)
			{
				return i;
			}
		}

		// Evict the slot with the least remaining time
		int32 oldest = 0;
		float minTime = m_slots[0].remainingTime;
		for (int32 i = 1; i < kMaxPings; ++i)
		{
			if (m_slots[i].remainingTime < minTime)
			{
				minTime = m_slots[i].remainingTime;
				oldest = i;
			}
		}
		return oldest;
	}

	void WorldPingVisualizer::BuildIconMesh(PingSlot& slot)
	{
		const std::string nodeName = "PingIcon_" + std::to_string(m_nameCounter++);
		slot.iconNode = m_scene.GetRootSceneNode().CreateChildSceneNode(nodeName);
		slot.iconNode->SetPosition(slot.worldPosition + Vector3(0.0f, kIconHeight, 0.0f));

		slot.iconMesh = m_scene.CreateManualRenderObject(nodeName + "_mesh");
		slot.iconMesh->SetCastShadows(false);
		slot.iconNode->AttachObject(*slot.iconMesh);

		auto tri = slot.iconMesh->AddTriangleListOperation(
			MaterialManager::Get().Load("Interface/WorldPingIcon.hmat"));

		const float h = kIconHalfSize;
		auto& t1 = tri->AddTriangle(
			Vector3(-h,  h, 0.0f),
			Vector3(-h, -h, 0.0f),
			Vector3( h,  h, 0.0f));
		t1.SetColor(0xFFFFFFFF);
		t1.SetUV(0, 0.0f, 0.0f);
		t1.SetUV(1, 0.0f, 1.0f);
		t1.SetUV(2, 1.0f, 0.0f);

		auto& t2 = tri->AddTriangle(
			Vector3(-h, -h, 0.0f),
			Vector3( h, -h, 0.0f),
			Vector3( h,  h, 0.0f));
		t2.SetColor(0xFFFFFFFF);
		t2.SetUV(0, 0.0f, 1.0f);
		t2.SetUV(1, 1.0f, 1.0f);
		t2.SetUV(2, 1.0f, 0.0f);

		slot.iconNode->UpdateBounds();
	}

	void WorldPingVisualizer::BuildLineMesh(PingSlot& slot)
	{
		if (slot.type != PingType::Position)
		{
			return;
		}

		const std::string nodeName = "PingLine_" + std::to_string(m_nameCounter++);
		slot.lineNode = m_scene.GetRootSceneNode().CreateChildSceneNode(nodeName);

		slot.lineMesh = m_scene.CreateManualRenderObject(nodeName + "_mesh");
		slot.lineMesh->SetCastShadows(false);
		slot.lineNode->AttachObject(*slot.lineMesh);

		auto line = slot.lineMesh->AddLineListOperation(
			MaterialManager::Get().Load("Models/Engine/Axis.hmat"));
		auto& seg = line->AddLine(
			slot.worldPosition + Vector3(0.0f, kIconHeight - kIconHalfSize, 0.0f),
			slot.worldPosition);
		seg.SetStartColor(0xFF4488FF);
		seg.SetEndColor(0xFF4488FF);

		slot.lineNode->UpdateBounds();
	}

	void WorldPingVisualizer::FreeSlot(PingSlot& slot)
	{
		if (slot.iconMesh)
		{
			if (slot.iconNode) { slot.iconNode->DetachObject(*slot.iconMesh); }
			m_scene.DestroyManualRenderObject(*slot.iconMesh);
			slot.iconMesh = nullptr;
		}
		if (slot.iconNode)
		{
			m_scene.DestroySceneNode(*slot.iconNode);
			slot.iconNode = nullptr;
		}

		if (slot.lineMesh)
		{
			if (slot.lineNode) { slot.lineNode->DetachObject(*slot.lineMesh); }
			m_scene.DestroyManualRenderObject(*slot.lineMesh);
			slot.lineMesh = nullptr;
		}
		if (slot.lineNode)
		{
			m_scene.DestroySceneNode(*slot.lineNode);
			slot.lineNode = nullptr;
		}

		slot.active = false;
		slot.senderGuid = 0;
		slot.targetGuid = 0;
		slot.remainingTime = 0.0f;
		slot.lastAlpha = 255;
	}
}
