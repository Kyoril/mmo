// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_settings_panel.h"

#include <imgui.h>

#include "edit_modes/world_edit_mode.h"
#include "editor_windows/asset_picker_widget.h"
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
        std::function<void(WorldEditMode *)> setEditModeCallback,
        bool &showFoliage,
        bool &showWater,
        std::function<void(bool)> setFoliageVisibleCallback,
        std::function<void(bool)> setWaterVisibleCallback)
        : m_terrain(terrain), m_hasTerrain(hasTerrain), m_currentEditMode(currentEditMode), m_terrainEditMode(terrainEditMode), m_setEditModeCallback(std::move(setEditModeCallback)), m_showFoliage(showFoliage), m_showWater(showWater), m_setFoliageVisibleCallback(std::move(setFoliageVisibleCallback)), m_setWaterVisibleCallback(std::move(setWaterVisibleCallback))
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

                String defaultMaterialName;
                if (m_terrain.GetDefaultMaterial())
                {
                    defaultMaterialName = m_terrain.GetDefaultMaterial()->GetName();
                }

                if (AssetPickerWidget::Draw("Terrain Default Material", defaultMaterialName, asset_extensions::Materials))
                {
                    if (!defaultMaterialName.empty())
                    {
                        m_terrain.SetDefaultMaterial(MaterialManager::Get().Load(defaultMaterialName));
                    }
                }

                bool wireframe = m_terrain.IsWireframeVisible();
                if (ImGui::Checkbox("Show Wireframe on Top", &wireframe))
                {
                    m_terrain.SetWireframeVisible(wireframe);
                }

                bool lodEnabled = m_terrain.IsLodEnabled();
                if (ImGui::Checkbox("Simulate Terrain LOD", &lodEnabled))
                {
                    m_terrain.SetLodEnabled(lodEnabled);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Enable terrain level of detail to simulate in-game appearance");
                }

                // TODO: Hack: Use terrain to get scene
                bool showFog = m_terrain.GetScene().IsFogEnabled();
                if (ImGui::Checkbox("Show Fog", &showFog))
                {
                    m_terrain.GetScene().SetFogEnabled(showFog);
                }

                // Foliage rendering (authored trees) — mirrors the in-game client appearance.
                // Hidden by default since dense foliage makes terrain editing harder.
                bool showFoliage = m_showFoliage;
                if (ImGui::Checkbox("Show Foliage", &showFoliage))
                {
                    m_showFoliage = showFoliage;
                    if (m_setFoliageVisibleCallback)
                    {
                        m_setFoliageVisibleCallback(showFoliage);
                    }
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Render authored foliage (trees) just as in the game client.");
                }

                // Water rendering — visible by default. Note that water is always shown while
                // the water edit mode is active, regardless of this setting.
                bool showWater = m_showWater;
                if (ImGui::Checkbox("Show Water", &showWater))
                {
                    m_showWater = showWater;
                    if (m_setWaterVisibleCallback)
                    {
                        m_setWaterVisibleCallback(showWater);
                    }
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Show water surfaces. Water is always visible while editing water.");
                }

                // Fog fade range controls
                float fogValues[2] = { m_terrain.GetScene().GetFogStart(), m_terrain.GetScene().GetFogEnd() };
                if (ImGui::DragFloat2("Fog Fade Range", fogValues, 1.0f, 0.0f, fogValues[1] - 0.1f))
                {
                    m_terrain.GetScene().SetFogRange(fogValues[0], fogValues[1]);
				}

                ImGui::EndDisabled();
            }
        }
        ImGui::End();
    }
}
