// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_settings_panel.h"

#include <imgui.h>

#include "edit_modes/world_edit_mode.h"
#include "terrain/terrain.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/scene.h"

namespace mmo
{
    WorldSettingsPanel::WorldSettingsPanel(
        terrain::Terrain &terrain,
        bool &hasTerrain,
        WorldEditMode *&currentEditMode,
        WorldEditMode *terrainEditMode,
        std::function<void(WorldEditMode *)> setEditModeCallback)
        : m_terrain(terrain), m_hasTerrain(hasTerrain), m_currentEditMode(currentEditMode), m_terrainEditMode(terrainEditMode), m_setEditModeCallback(std::move(setEditModeCallback))
    {
    }

    void WorldSettingsPanel::Draw(const String &id)
    {
        if (ImGui::Begin(id.c_str()))
        {
            if (ImGui::CollapsingHeader("World Settings", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::Checkbox("Has Terrain", &m_hasTerrain))
                {
                    m_terrain.SetVisible(m_hasTerrain);

                    // Ensure terrain mode is not selectable if there is no terrain
                    if (!m_hasTerrain && m_currentEditMode == m_terrainEditMode)
                    {
                        if (m_setEditModeCallback)
                        {
                            m_setEditModeCallback(nullptr);
                        }
                    }
                }

                ImGui::BeginDisabled(!m_hasTerrain);

                static const char *s_noMaterialPreview = "<None>";

                const char *previewString = s_noMaterialPreview;
                if (m_terrain.GetDefaultMaterial())
                {
                    previewString = m_terrain.GetDefaultMaterial()->GetName().data();
                }

                if (ImGui::BeginCombo("Terrain Default Material", previewString))
                {
                    ImGui::EndCombo();
                }

                bool wireframe = m_terrain.IsWireframeVisible();
                if (ImGui::Checkbox("Show Wireframe on Top", &wireframe))
                {
                    m_terrain.SetWireframeVisible(wireframe);
                }

                // TODO: Hack: Use terrain to get scene
                bool showFog = m_terrain.GetScene().IsFogEnabled();
                if (ImGui::Checkbox("Show Fog", &showFog))
                {
                    m_terrain.GetScene().SetFogEnabled(showFog);
                }

                if (m_hasTerrain)
                {
                    if (ImGui::BeginDragDropTarget())
                    {
                        // We only accept material file drops
                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(".hmat"))
                        {
                            m_terrain.SetDefaultMaterial(MaterialManager::Get().Load(*static_cast<String *>(payload->Data)));
                        }

                        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(".hmi"))
                        {
                            m_terrain.SetDefaultMaterial(MaterialManager::Get().Load(*static_cast<String *>(payload->Data)));
                        }

                        ImGui::EndDragDropTarget();
                    }
                }

                ImGui::EndDisabled();
            }
        }
        ImGui::End();
    }
}
