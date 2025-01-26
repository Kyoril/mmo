// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "entity_edit_mode.h"

#include <imgui.h>

#include "math/plane.h"
#include "math/ray.h"
#include "math/vector3.h"
#include "scene_graph/camera.h"

namespace mmo
{
	EntityEditMode::EntityEditMode(IWorldEditor& worldEditor)
		: WorldEditMode(worldEditor)
	{
	}

	const char* EntityEditMode::GetName() const
	{
		static const char* s_name = "Static Map Entities";
		return s_name;
	}

	void EntityEditMode::DrawDetails()
	{
	}

	void EntityEditMode::OnViewportDrop(float x, float y)
	{
		WorldEditMode::OnViewportDrop(x, y);

		// We only accept mesh file drops
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmsh"))
		{
			Vector3 position;

			const auto plane = Plane(Vector3::UnitY, Vector3::Zero);
			const Ray ray = m_worldEditor.GetCamera().GetCameraToViewportRay(x, y, 10000.0f);

			const auto hit = ray.Intersects(plane);
			if (hit.first)
			{
				position = ray.GetPoint(hit.second);
			}
			else
			{
				position = ray.GetPoint(10.0f);
			}

			// Snap to grid?
			if (m_worldEditor.IsGridSnapEnabled())
			{
				const float gridSize = m_worldEditor.GetTranslateGridSnapSize();

				// Snap position to grid size
				position.x = std::round(position.x / gridSize) * gridSize;
				position.y = std::round(position.y / gridSize) * gridSize;
				position.z = std::round(position.z / gridSize) * gridSize;
			}

			m_worldEditor.CreateMapEntity(*static_cast<String*>(payload->Data), position, Quaternion::Identity, Vector3::UnitScale, 0);
		}
	}
}
