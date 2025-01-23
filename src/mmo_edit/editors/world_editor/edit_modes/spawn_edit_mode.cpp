// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spawn_edit_mode.h"

#include <imgui.h>

#include "base/typedefs.h"

namespace mmo
{
	SpawnEditMode::SpawnEditMode(IWorldEditor& worldEditor, proto::MapManager& maps)
		: WorldEditMode(worldEditor)
		, m_maps(maps)
	{
	}

	const char* SpawnEditMode::GetName() const
	{
		static const char* s_name = "Spawn Editor";
		return s_name;
	}

	void SpawnEditMode::DrawDetails()
	{
		static const char* s_noneMap = "<None>";

		if (ImGui::BeginCombo("Map", m_mapEntry ? m_mapEntry->name().c_str() : s_noneMap, ImGuiComboFlags_None))
		{
			for (::uint32 i = 0; i < static_cast<uint32>(m_maps.count()); ++i)
			{
				ImGui::PushID(i);
				if (ImGui::Selectable(m_maps.getTemplates().entry(i).name().c_str(), m_maps.getTemplates().mutable_entry(i) == m_mapEntry))
				{
					proto::MapEntry* entry = m_maps.getTemplates().mutable_entry(i);
					if (m_mapEntry != entry)
					{
						m_mapEntry = entry;
						m_worldEditor.RemoveAllUnitSpawns();

						if (m_mapEntry)
						{
							// For each unit spawn on the map
							for (auto& unitSpawn : *m_mapEntry->mutable_unitspawns())
							{
								m_worldEditor.AddUnitSpawn(unitSpawn, false);
							}
						}
					}

				}
				ImGui::PopID();
			}

			ImGui::EndCombo();
		}
	}

	void SpawnEditMode::OnDeactivate()
	{
		WorldEditMode::OnDeactivate();

		m_worldEditor.RemoveAllUnitSpawns();
		m_worldEditor.ClearSelection();
	}
}
