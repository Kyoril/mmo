// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "details_panel.h"

#include <imgui.h>

#include "edit_modes/world_edit_mode.h"
#include "selection.h"
#include "math/quaternion.h"
#include "math/vector3.h"
#include "world_editor_instance.h"

namespace mmo
{
    DetailsPanel::DetailsPanel(
        Selection &selection,
        SelectableVisitor &visitor,
        std::function<void()> saveCallback)
        : m_selection(selection), m_visitor(visitor), m_saveCallback(std::move(saveCallback))
    {
    }

    void DetailsPanel::Draw(
        const String &id,
        WorldEditMode *currentEditMode,
        WorldEditMode **availableEditModes,
        const size_t editModeCount,
        std::function<void(WorldEditMode *)> setEditModeCallback)
    {
        if (ImGui::Begin(id.c_str()))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
            if (ImGui::BeginTable("split", 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable))
            {
                ImGui::EndTable();
            }
            ImGui::PopStyleVar();

            ImGui::Separator();

            if (ImGui::Button("Save"))
            {
                if (m_saveCallback)
                {
                    m_saveCallback();
                }
            }

            ImGui::Separator();

            DrawEditModeSelector(currentEditMode, availableEditModes, editModeCount, setEditModeCallback);

            if (currentEditMode)
            {
                currentEditMode->DrawDetails();
            }

            ImGui::Separator();

            if (!m_selection.IsEmpty())
            {
                DrawSelectionDetails();
            }
        }
        ImGui::End();
    }

    void DetailsPanel::DrawEditModeSelector(
        WorldEditMode *currentEditMode,
        WorldEditMode **availableEditModes,
        const size_t editModeCount,
        std::function<void(WorldEditMode *)> setEditModeCallback)
    {
        const char *editModePreview = "None";
        if (ImGui::BeginCombo("Mode", currentEditMode ? currentEditMode->GetName() : editModePreview, ImGuiComboFlags_None))
        {
            if (ImGui::Selectable("None", currentEditMode == nullptr))
            {
                if (setEditModeCallback)
                {
                    setEditModeCallback(nullptr);
                }
            }

            for (size_t i = 0; i < editModeCount; ++i)
            {
                if (availableEditModes[i] && ImGui::Selectable(availableEditModes[i]->GetName(), currentEditMode == availableEditModes[i]))
                {
                    if (setEditModeCallback)
                    {
                        setEditModeCallback(availableEditModes[i]);
                    }
                }
            }

            ImGui::EndCombo();
        }
    }

    void DetailsPanel::DrawSelectionDetails()
    {
        Selectable *selected = m_selection.GetSelectedObjects().back().get();
        selected->Visit(m_visitor);

        if (selected->SupportsTranslate() || selected->SupportsRotate() || selected->SupportsScale())
        {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (selected->SupportsTranslate())
                {
                    if (Vector3 position = selected->GetPosition(); ImGui::InputFloat3("Position", position.Ptr()))
                    {
                        selected->SetPosition(position);
                    }
                }

                if (selected->SupportsRotate())
                {
                    Rotator rotation = selected->GetOrientation().ToRotator();
                    if (float angles[3] = {rotation.roll.GetValueDegrees(), rotation.yaw.GetValueDegrees(), rotation.pitch.GetValueDegrees()}; ImGui::InputFloat3("Rotation", angles, "%.3f"))
                    {
                        rotation.roll = angles[0];
                        rotation.pitch = angles[2];
                        rotation.yaw = angles[1];

                        Quaternion quaternion = Quaternion::FromRotator(rotation);
                        quaternion.Normalize();

                        selected->SetOrientation(quaternion);
                    }
                }

                if (selected->SupportsScale())
                {
                    Vector3 scale = selected->GetScale();
                    if (ImGui::InputFloat3("Scale", scale.Ptr()))
                    {
                        selected->SetScale(scale);
                    }
                }
            }
        }
    }
}
