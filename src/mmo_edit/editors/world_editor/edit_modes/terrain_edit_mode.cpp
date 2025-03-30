// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "terrain_edit_mode.h"
#include "terrain/terrain.h"

#include <imgui.h>


namespace mmo
{
	static const char* s_terrainEditModeStrings[] = {
		"Select",
		"Deform",
		"Paint",
		"Area"
	};

	static_assert(std::size(s_terrainEditModeStrings) == static_cast<uint32>(TerrainEditType::Count_), "There needs to be one string per enum value to display!");

	static const char* s_terrainDeformModeStrings[] = {
		"Sculpt",
		"Smooth",
		"Flatten"
	};

	static_assert(std::size(s_terrainDeformModeStrings) == static_cast<uint32>(TerrainDeformMode::Count_), "There needs to be one string per enum value to display!");

	TerrainEditMode::TerrainEditMode(IWorldEditor& worldEditor, terrain::Terrain& terrain, const proto::ZoneManager& zones, Camera& camera)
		: WorldEditMode(worldEditor)
		, m_terrain(terrain)
		, m_zones(zones)
		, m_camera(camera)
	{
	}

	const char* TerrainEditMode::GetName() const
	{
		static const char* s_name = "Terrain";
		return s_name;
	}

	void TerrainEditMode::DrawDetails()
	{
		if (ImGui::BeginCombo("Terrain Edit Mode", s_terrainEditModeStrings[static_cast<uint32>(m_type)], ImGuiComboFlags_None))
		{
			for (uint32 i = 0; i < static_cast<uint32>(TerrainEditType::Count_); ++i)
			{
				ImGui::PushID(i);
				if (ImGui::Selectable(s_terrainEditModeStrings[i], i == static_cast<uint32>(m_type)))
				{
					m_type = static_cast<TerrainEditType>(i);
					m_worldEditor.ClearSelection();
				}
				ImGui::PopID();
			}

			ImGui::EndCombo();
		}

		if (m_type == TerrainEditType::Deform)
		{
			if (ImGui::BeginCombo("Deform Mode", s_terrainDeformModeStrings[static_cast<uint32>(m_deformMode)], ImGuiComboFlags_None))
			{
				for (uint32 i = 0; i < static_cast<uint32>(TerrainDeformMode::Count_); ++i)
				{
					ImGui::PushID(i);
					if (ImGui::Selectable(s_terrainDeformModeStrings[i], i == static_cast<uint32>(m_deformMode)))
					{
						m_deformMode = static_cast<TerrainDeformMode>(i);
						m_worldEditor.ClearSelection();
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}
		}
		else if (m_type == TerrainEditType::Paint)
		{
			static const char* s_layerNames[] = { "Layer 1", "Layer 2", "Layer 3", "Layer 4" };

			if (ImGui::BeginCombo("Layer", s_layerNames[m_terrainPaintLayer]))
			{
				for (uint32 i = 0; i < std::size(s_layerNames); ++i)
				{
					ImGui::PushID(i);
					if (ImGui::Selectable(s_layerNames[i], i == m_terrainPaintLayer))
					{
						m_terrainPaintLayer = i;
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}
		}

		ImGui::SliderFloat("Brush Radius", &m_terrainBrushSize, 0.01f, terrain::constants::VerticesPerTile);
		ImGui::SliderFloat("Brush Hardness", &m_terrainBrushHardness, 0.0f, 1.0f);
		ImGui::SliderFloat("Brush Power", &m_terrainBrushPower, 0.01f, 10.0f);

		ImGui::Separator();

		if (m_type == TerrainEditType::Area)
		{
			// Render a list of all zones
			if (ImGui::BeginListBox("##areas"))
			{
				if (ImGui::Selectable("(None)", 0 == m_selectedArea))
				{
					m_selectedArea = 0;
				}

				for (const auto& zone : m_zones.getTemplates().entry())
				{
					ImGui::PushID(zone.id());
					if (ImGui::Selectable(zone.name().c_str(), zone.id() == m_selectedArea))
					{
						m_selectedArea = zone.id();
					}
					ImGui::PopID();
				}

				ImGui::EndListBox();
			}
		}
	}

	void TerrainEditMode::OnMouseHold(const float deltaSeconds)
	{
		WorldEditMode::OnMouseHold(deltaSeconds);

		const float factor = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? -1.0f : 1.0f;

		const float outerRadius = m_terrainBrushSize;
		const float innerRadius = std::max(0.05f, m_terrainBrushSize * m_terrainBrushHardness);

		if (m_type == TerrainEditType::Deform)
		{
			if (m_deformMode == TerrainDeformMode::Flatten && ImGui::IsKeyDown(ImGuiKey_LeftControl))
			{
				m_deformFlattenHeight = m_terrain.GetSmoothHeightAt(m_brushPosition.x, m_brushPosition.z);
			}
			else
			{
				switch (m_deformMode)
				{
				case TerrainDeformMode::Sculpt:
				{
					m_terrain.Deform(m_brushPosition.x, m_brushPosition.z,
						innerRadius, outerRadius, m_terrainBrushPower * factor * deltaSeconds);
				} break;
				case TerrainDeformMode::Smooth:
				{
					m_terrain.Smooth(m_brushPosition.x, m_brushPosition.z,
						innerRadius, outerRadius, m_terrainBrushPower * factor * deltaSeconds);
				} break;
				case TerrainDeformMode::Flatten:
				{
					m_terrain.Flatten(m_brushPosition.x, m_brushPosition.z,
						innerRadius, outerRadius, m_terrainBrushPower * factor * deltaSeconds, m_deformFlattenHeight);
				} break;
				}
			}
		}
		else if (m_type == TerrainEditType::Paint)
		{
			m_terrain.Paint(m_terrainPaintLayer, m_brushPosition.x, m_brushPosition.z,
				innerRadius, outerRadius, m_terrainBrushPower * factor * deltaSeconds);
		}
		else if (m_type == TerrainEditType::Area)
		{
			m_terrain.SetArea(m_brushPosition, m_selectedArea);
		}
	}

	void TerrainEditMode::OnMouseMoved(float x, float y)
	{
		WorldEditMode::OnMouseMoved(x, y);


	}

	void TerrainEditMode::OnMouseUp(float x, float y)
	{
		WorldEditMode::OnMouseUp(x, y);
	}

	void TerrainEditMode::SetBrushPosition(const Vector3& position)
	{
		m_brushPosition = position;
	}
}
