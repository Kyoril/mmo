// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_settings_panel.h"

#include <imgui.h>

#include "edit_modes/world_edit_mode.h"
#include "editor_windows/asset_picker_widget.h"
#include "terrain/terrain.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/scene.h"
#include "scene_graph/world_grid.h"

namespace mmo
{
    WorldSettingsPanel::WorldSettingsPanel(
        terrain::Terrain &terrain,
        WorldGrid &worldGrid,
        bool &hasTerrain,
        WorldEditMode *&currentEditMode,
        WorldEditMode *terrainEditMode,
        std::function<void(WorldEditMode *)> setEditModeCallback,
        bool &showFoliage,
        bool &showWater,
        std::function<void(bool)> setFoliageVisibleCallback,
        std::function<void(bool)> setWaterVisibleCallback)
        : m_terrain(terrain), m_worldGrid(worldGrid), m_hasTerrain(hasTerrain), m_currentEditMode(currentEditMode), m_terrainEditMode(terrainEditMode), m_setEditModeCallback(std::move(setEditModeCallback)), m_showFoliage(showFoliage), m_showWater(showWater), m_setFoliageVisibleCallback(std::move(setFoliageVisibleCallback)), m_setWaterVisibleCallback(std::move(setWaterVisibleCallback))
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
                if (ImGui::Checkbox("Show Wireframe on Top (G)", &wireframe))
                {
                    m_terrain.SetWireframeVisible(wireframe);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Toggle with G, except while typing in a field.");
                }

                // The world grid is a flat plane by default and simply intersects any ground that
                // is not at its own height, which makes it useless for reading where anything
                // sits. Draping it costs geometry, so it stays opt-in.
                bool followTerrain = m_worldGrid.IsFollowingTerrain();
                if (ImGui::Checkbox("Grid Follows Terrain", &followTerrain))
                {
                    m_worldGrid.SetFollowTerrain(followTerrain);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Drape the world grid over the terrain surface instead of "
                        "drawing it as a flat plane that cuts through hills.");
                }

                if (followTerrain)
                {
                    ImGui::Indent();

                    int subdivisions = m_worldGrid.GetTerrainSubdivisions();
                    if (ImGui::SliderInt("Grid Detail", &subdivisions, 1, 16))
                    {
                        m_worldGrid.SetTerrainSubdivisions(static_cast<uint8>(subdivisions));
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Segments each grid cell is split into. The default of 8 "
                            "puts a grid vertex on every terrain vertex, which is as closely as the "
                            "surface can be followed; lower it if the grid costs too much to rebuild.");
                    }

                    float offset = m_worldGrid.GetTerrainOffset();
                    if (ImGui::DragFloat("Grid Height Offset", &offset, 0.01f, 0.0f, 5.0f, "%.2f"))
                    {
                        m_worldGrid.SetTerrainOffset(offset);
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("How far above the surface the grid floats, to keep it out "
                            "of a z-fight with the ground.");
                    }

                    ImGui::Unindent();
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
