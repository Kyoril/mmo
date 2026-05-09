// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_ping_visualizer.h"

#include "scene_graph/camera.h"
#include "scene_graph/material_manager.h"
#include "frame_ui/color.h"
#include "log/default_log_levels.h"

#include "game_client/object_mgr.h"
#include "game_client/game_unit_c.h"

#include <algorithm>

namespace mmo
{
	WorldPingVisualizer::WorldPingVisualizer(Scene& scene)
		: m_scene(scene)
	{
		m_lineMaterial = MaterialManager::Get().Load("Editor/Wireframe.hmat");
		m_iconMaterial = MaterialManager::Get().Load("Editor/Wireframe.hmat");
	}

	WorldPingVisualizer::~WorldPingVisualizer()
	{
		Clear();
	}

	void WorldPingVisualizer::AddPositionPing(const uint64 senderGuid, const Vector3& groundPosition)
	{
		WorldPingEntry& entry = AcquireSlot(senderGuid);
		entry.senderGuid = senderGuid;
		entry.type = PingTargetType::Position;
		entry.groundPosition = groundPosition;
		entry.targetUnitGuid = 0;
		entry.remainingTime = kWorldPingDuration;

		// Create scene node if not already allocated
		if (!entry.node)
		{
			entry.node = m_scene.GetRootSceneNode().CreateChildSceneNode();
		}

		entry.node->SetPosition(groundPosition);

		// Ensure scene objects exist
		if (!entry.lineObject)
		{
			static uint32 s_counter = 0;
			const String name = "PingLine_" + std::to_string(s_counter++);
			entry.lineObject = m_scene.CreateManualRenderObject(name);
			if (entry.lineObject)
			{
				entry.lineObject->SetCastShadows(false);
				entry.lineObject->SetQueryFlags(0);
				entry.node->AttachObject(*entry.lineObject);
			}
		}

		if (!entry.iconObject)
		{
			static uint32 s_counter = 0;
			const String name = "PingIcon_" + std::to_string(s_counter++);
			entry.iconObject = m_scene.CreateManualRenderObject(name);
			if (entry.iconObject)
			{
				entry.iconObject->SetCastShadows(false);
				entry.iconObject->SetQueryFlags(0);
				entry.node->AttachObject(*entry.iconObject);
			}
		}

		// Draw the vertical line immediately (it doesn't need per-frame rebuild)
		if (entry.lineObject && m_lineMaterial)
		{
			entry.lineObject->Clear();
			auto lineOp = entry.lineObject->AddLineListOperation(m_lineMaterial);
			// Vertical line from ground up to icon height, in local space (node is at groundPosition)
			auto& line = lineOp->AddLine(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, kPingLineLength, 0.0f));
			line.SetColor(Color(0.2f, 0.6f, 1.0f, 1.0f).GetARGB());
			lineOp->Finish();
		}
	}

	void WorldPingVisualizer::AddUnitPing(const uint64 senderGuid, const uint64 unitGuid)
	{
		WorldPingEntry& entry = AcquireSlot(senderGuid);
		entry.senderGuid = senderGuid;
		entry.type = PingTargetType::Unit;
		entry.targetUnitGuid = unitGuid;
		entry.groundPosition = Vector3::Zero;
		entry.remainingTime = kWorldPingDuration;

		if (!entry.node)
		{
			entry.node = m_scene.GetRootSceneNode().CreateChildSceneNode();
		}

		// No line for unit pings — just icon
		if (entry.lineObject)
		{
			entry.node->DetachObject(*entry.lineObject);
			m_scene.DestroyManualRenderObject(*entry.lineObject);
			entry.lineObject = nullptr;
		}

		if (!entry.iconObject)
		{
			static uint32 s_counter = 0;
			const String name = "PingIcon_" + std::to_string(s_counter++);
			entry.iconObject = m_scene.CreateManualRenderObject(name);
			if (entry.iconObject)
			{
				entry.iconObject->SetCastShadows(false);
				entry.iconObject->SetQueryFlags(0);
				entry.node->AttachObject(*entry.iconObject);
			}
		}
	}

	void WorldPingVisualizer::Update(const float deltaSeconds, const Camera& camera)
	{
		// Compute camera right and up in world space for billboard orientation
		const Quaternion& camOri = camera.GetDerivedOrientation();
		const Vector3 camRight = camOri * Vector3::UnitX;
		const Vector3 camUp = camOri * Vector3::UnitY;

		for (auto& entry : m_pings)
		{
			if (entry.remainingTime <= 0.0f)
			{
				continue;
			}

			entry.remainingTime -= deltaSeconds;

			const float alpha = std::clamp(entry.remainingTime / kWorldPingDuration, 0.0f, 1.0f);
			const Color pingColor(0.2f, 0.6f, 1.0f, alpha);
			const uint32 colorARGB = pingColor.GetARGB();

			// For unit pings: track the unit's current position
			if (entry.type == PingTargetType::Unit && entry.node)
			{
				auto unit = ObjectMgr::Get<GameUnitC>(entry.targetUnitGuid);
				if (unit && unit->GetSceneNode())
				{
					const Vector3& unitPos = unit->GetPosition();
					entry.node->SetPosition(unitPos);
				}
			}

			// Rebuild billboard icon geometry (camera-facing quad, 2 triangles)
			if (entry.iconObject && m_iconMaterial)
			{
				entry.iconObject->Clear();
				auto triOp = entry.iconObject->AddTriangleListOperation(m_iconMaterial);

				// Icon floats kPingIconHeight above the node origin (local space)
				const Vector3 iconCenter(0.0f, kPingIconHeight, 0.0f);

				const Vector3 r = camRight * kPingIconHalfSize;
				const Vector3 u = camUp * kPingIconHalfSize;

				const Vector3 tl = iconCenter - r + u;
				const Vector3 tr = iconCenter + r + u;
				const Vector3 bl = iconCenter - r - u;
				const Vector3 br = iconCenter + r - u;

				auto& t1 = triOp->AddTriangle(tl, tr, bl);
				t1.SetColor(colorARGB);

				auto& t2 = triOp->AddTriangle(tr, br, bl);
				t2.SetColor(colorARGB);

				triOp->Finish();
			}
		}

		// Remove expired entries
		for (auto& entry : m_pings)
		{
			if (entry.remainingTime <= 0.0f)
			{
				DestroyEntryObjects(entry);
			}
		}
		m_pings.erase(
			std::remove_if(m_pings.begin(), m_pings.end(),
				[](const WorldPingEntry& e) { return e.remainingTime <= 0.0f; }),
			m_pings.end());
	}

	void WorldPingVisualizer::Clear()
	{
		for (auto& entry : m_pings)
		{
			DestroyEntryObjects(entry);
		}
		m_pings.clear();
	}

	WorldPingEntry& WorldPingVisualizer::AcquireSlot(const uint64 senderGuid)
	{
		// Replace existing ping from same sender
		for (auto& entry : m_pings)
		{
			if (entry.senderGuid == senderGuid)
			{
				return entry;
			}
		}

		// Room for a new slot?
		if (static_cast<int32>(m_pings.size()) < kMaxWorldPings)
		{
			m_pings.emplace_back();
			return m_pings.back();
		}

		// Evict oldest (smallest remainingTime)
		auto oldest = std::min_element(m_pings.begin(), m_pings.end(),
			[](const WorldPingEntry& a, const WorldPingEntry& b)
			{ return a.remainingTime < b.remainingTime; });

		DestroyEntryObjects(*oldest);
		*oldest = WorldPingEntry{};
		return *oldest;
	}

	void WorldPingVisualizer::DestroyEntryObjects(WorldPingEntry& entry)
	{
		if (entry.node)
		{
			if (entry.lineObject)
			{
				entry.node->DetachObject(*entry.lineObject);
				m_scene.DestroyManualRenderObject(*entry.lineObject);
				entry.lineObject = nullptr;
			}

			if (entry.iconObject)
			{
				entry.node->DetachObject(*entry.iconObject);
				m_scene.DestroyManualRenderObject(*entry.iconObject);
				entry.iconObject = nullptr;
			}

			// Remove node from parent and delete it
			if (entry.node->GetParent())
			{
				entry.node->GetParent()->RemoveChild(*entry.node);
			}
			m_scene.DestroySceneNode(*entry.node);
			entry.node = nullptr;
		}
	}
}
