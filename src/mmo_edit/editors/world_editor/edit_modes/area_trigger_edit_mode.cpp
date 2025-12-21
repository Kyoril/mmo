// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "area_trigger_edit_mode.h"

#include <imgui.h>

#include "base/typedefs.h"
#include "base/utilities.h"
#include "math/plane.h"
#include "math/ray.h"
#include "scene_graph/camera.h"
#include "scene_graph/scene.h"
#include "scene_graph/movable_object.h"
#include "terrain/terrain.h"
#include "editors/world_editor/world_editor_instance.h"

namespace mmo
{
    AreaTriggerEditMode::AreaTriggerEditMode(IWorldEditor &worldEditor, proto::MapManager &maps, proto::AreaTriggerManager &areaTriggers)
        : WorldEditMode(worldEditor), m_maps(maps), m_areaTriggers(areaTriggers)
    {
        DetectMapEntry();
    }

    const char *AreaTriggerEditMode::GetName() const
    {
        static const char *s_name = "Area Triggers";
        return s_name;
    }

    void AreaTriggerEditMode::DrawDetails()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

        // Map Status Section - Only show if map entry is missing
        if (!m_mapEntry)
        {
            if (ImGui::CollapsingHeader("Map Information", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent();

                const String worldName = ExtractWorldNameFromPath();

                ImGui::Text("World:");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "%s", worldName.c_str());

                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.3f, 1.0f));
                ImGui::TextWrapped("No map entry found for this world file. Please create a map entry first.");
                ImGui::PopStyleColor();

                ImGui::Unindent();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }

        // Trigger Creation Section
        if (ImGui::CollapsingHeader("Create Area Trigger", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            if (!m_mapEntry)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("A map entry is required to create area triggers.");
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::TextWrapped("Drag a trigger type into the viewport to create:");
                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.15f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.5f, 0.7f, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.4f, 0.6f, 0.8f, 0.7f));

                if (ImGui::BeginListBox("##triggerTypes", ImVec2(-1, 80)))
                {
                    // Sphere trigger type
                    ImGui::PushID(0);
                    ImGui::Selectable("Sphere Trigger", false);
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                    {
                        TriggerType type = TriggerType::Sphere;
                        ImGui::SetDragDropPayload("TRIGGER_TYPE", &type, sizeof(TriggerType));
                        ImGui::Text("Sphere Trigger");
                        ImGui::EndDragDropSource();
                    }
                    ImGui::PopID();

                    // Box trigger type
                    ImGui::PushID(1);
                    ImGui::Selectable("Box Trigger", false);
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                    {
                        TriggerType type = TriggerType::Box;
                        ImGui::SetDragDropPayload("TRIGGER_TYPE", &type, sizeof(TriggerType));
                        ImGui::Text("Box Trigger");
                        ImGui::EndDragDropSource();
                    }
                    ImGui::PopID();

                    ImGui::EndListBox();
                }

                ImGui::PopStyleColor(3);
            }

            ImGui::Unindent();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Existing Triggers Section
        if (ImGui::CollapsingHeader("Area Triggers", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            if (m_mapEntry)
            {
                // Filter input
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##TriggerFilter", "Filter triggers...", m_filterBuffer, sizeof(m_filterBuffer));
                ImGui::Spacing();

                int triggerCount = 0;
                for (const auto &trigger : m_areaTriggers.getTemplates().entry())
                {
                    if (trigger.map() == m_mapEntry->id())
                    {
                        triggerCount++;
                    }
                }

                ImGui::TextDisabled("Triggers on this map: %d", triggerCount);

                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.15f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.5f, 0.7f, 0.3f, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.6f, 0.8f, 0.4f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.7f, 0.9f, 0.5f, 0.9f));

                if (ImGui::BeginListBox("##triggers", ImVec2(-1, 250)))
                {
                    const String filterStr = String(m_filterBuffer);
                    const bool hasFilter = !filterStr.empty();

                    for (const auto &trigger : m_areaTriggers.getTemplates().entry())
                    {
                        if (trigger.map() == m_mapEntry->id())
                        {
                            std::ostringstream labelStream;
                            labelStream << "#" << std::setw(6) << std::setfill('0') << trigger.id() << " - " << trigger.name();
                            String displayName = labelStream.str();

                            // Apply filter
                            if (hasFilter)
                            {
                                String lowerDisplay = displayName;
                                String lowerFilter = filterStr;
                                std::transform(lowerDisplay.begin(), lowerDisplay.end(), lowerDisplay.begin(), ::tolower);
                                std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);

                                if (lowerDisplay.find(lowerFilter) == String::npos)
                                {
                                    continue;
                                }
                            }

                            ImGui::PushID(trigger.id());

                        // Make trigger clickable to select it
                        if (ImGui::Selectable(displayName.c_str(), false))
                        {
                            // Cast to WorldEditorInstance to access SelectAreaTrigger
                            WorldEditorInstance* editorInstance = dynamic_cast<WorldEditorInstance*>(&m_worldEditor);
                            if (editorInstance)
                            {
                                editorInstance->SelectAreaTrigger(const_cast<proto::AreaTriggerEntry&>(trigger));
                            }
                        }

                            if (ImGui::IsItemHovered())
                            {
                                ImGui::BeginTooltip();
                                ImGui::Text("ID: %u", trigger.id());
                                ImGui::Text("Position: (%.2f, %.2f, %.2f)", trigger.x(), trigger.y(), trigger.z());
                                if (trigger.has_radius())
                                {
                                    ImGui::Text("Type: Sphere (radius: %.2f)", trigger.radius());
                                }
                                else
                                {
                                    ImGui::Text("Type: Box (%.2f x %.2f x %.2f)",
                                                trigger.box_x(), trigger.box_y(), trigger.box_z());
                                }
                                ImGui::EndTooltip();
                            }

                            ImGui::PopID();
                        }
                    }

                    ImGui::EndListBox();
                }

                ImGui::PopStyleColor(4);
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("No map entry available.");
                ImGui::PopStyleColor();
            }

            ImGui::Unindent();
        }

        ImGui::PopStyleVar(2);
    }

    void AreaTriggerEditMode::OnActivate()
    {
        WorldEditMode::OnActivate();
        LoadAreaTriggersForMap();
    }

    void AreaTriggerEditMode::OnDeactivate()
    {
        WorldEditMode::OnDeactivate();
        m_worldEditor.ClearSelection();
        m_worldEditor.RemoveAllAreaTriggers();
    }

    void AreaTriggerEditMode::OnViewportDrop(float x, float y)
    {
        if (!m_mapEntry)
        {
            return;
        }

        // Check if we're receiving a trigger type drag-drop
        if (!ImGui::GetDragDropPayload())
        {
            return;
        }

        const ImGuiPayload *payload = ImGui::GetDragDropPayload();
        if (!payload || !payload->IsDataType("TRIGGER_TYPE"))
        {
            return;
        }

        // Extract the trigger type from the payload
        const TriggerType *droppedType = static_cast<const TriggerType *>(payload->Data);
        if (!droppedType)
        {
            return;
        }

        m_selectedTriggerType = *droppedType;

        // Calculate trigger position
        Vector3 position;
        bool hitFound = false;

        // Try raycast against scene geometry
        Scene *scene = m_worldEditor.GetCamera().GetScene();
        if (scene)
        {
            const Ray ray = m_worldEditor.GetCamera().GetCameraToViewportRay(x, y, 10000.0f);
            auto query = scene->CreateRayQuery(ray);
            query->SetSortByDistance(true);
            query->Execute();

            const auto &results = query->GetLastResult();
            float closestDist = std::numeric_limits<float>::max();

            for (const auto &entry : results)
            {
                if (hitFound && entry.distance > closestDist)
                {
                    break;
                }

                if (entry.movable)
                {
                    const ICollidable *collidable = entry.movable->GetCollidable();
                    if (collidable)
                    {
                        CollisionResult hit;
                        if (collidable->IsCollidable() && collidable->TestRayCollision(ray, hit))
                        {
                            if (hit.distance < closestDist)
                            {
                                closestDist = hit.distance;
                                position = hit.contactPoint;
                                hitFound = true;
                            }
                        }
                    }
                }
            }
        }

        if (!hitFound)
        {
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

        // Generate unique ID
        const uint32 triggerId = GenerateUniqueTriggerId();

        // Create new area trigger entry
        proto::AreaTriggerEntry *entry = m_areaTriggers.add(triggerId);
        entry->set_name("New Area Trigger");
        entry->set_map(m_mapEntry->id());
        entry->set_x(position.x);
        entry->set_y(position.y);
        entry->set_z(position.z);

        if (m_selectedTriggerType == TriggerType::Sphere)
        {
            entry->set_radius(5.0f);
        }
        else
        {
            entry->set_box_x(5.0f);
            entry->set_box_y(5.0f);
            entry->set_box_z(5.0f);
            entry->set_box_o(0.0f);
        }

        // Add visual representation
        m_worldEditor.AddAreaTrigger(*entry, true);
    }

    String AreaTriggerEditMode::ExtractWorldNameFromPath() const
    {
        const auto worldPath = m_worldEditor.GetWorldPath();

        // Expected format: Worlds/{name}/{name}.hwld
        // Extract the directory name (second to last component)
        if (worldPath.has_parent_path())
        {
            const auto parentPath = worldPath.parent_path();
            if (parentPath.has_filename())
            {
                return parentPath.filename().string();
            }
        }

        return "";
    }

    void AreaTriggerEditMode::LoadAreaTriggersForMap()
    {
        if (!m_mapEntry)
        {
            return;
        }

        // Load all area triggers for this map
        for (auto &trigger : *m_areaTriggers.getTemplates().mutable_entry())
        {
            if (trigger.map() == m_mapEntry->id())
            {
                m_worldEditor.AddAreaTrigger(trigger, false);
            }
        }
    }

    uint32 AreaTriggerEditMode::GenerateUniqueTriggerId()
    {
        uint32 maxId = 0;
        for (const auto &trigger : m_areaTriggers.getTemplates().entry())
        {
            if (trigger.id() > maxId)
            {
                maxId = trigger.id();
            }
        }
        return maxId + 1;
    }

    void AreaTriggerEditMode::DetectMapEntry()
    {
        const String worldName = ExtractWorldNameFromPath();

        // Find matching map entry
        for (auto &mapEntry : *m_maps.getTemplates().mutable_entry())
        {
            if (mapEntry.directory() == worldName)
            {
                m_mapEntry = &mapEntry;
                break;
            }
        }
    }
}