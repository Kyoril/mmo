// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "viewport_panel.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "edit_modes/world_edit_mode.h"
#include "transform_widget.h"
#include "selection.h"
#include "scene_graph/world_grid.h"
#include "deferred_shading/deferred_renderer.h"
#include "graphics/texture.h"
#include "scene_outline_window.h"

namespace mmo
{
    // UI transform mode button styles
    static const ImVec4 ButtonSelected = ImVec4(0.15f, 0.55f, 0.83f, 0.78f);
    static const ImVec4 ButtonHovered = ImVec4(0.24f, 0.52f, 0.88f, 0.40f);
    static const ImVec4 ButtonNormal = ImVec4(0.20f, 0.41f, 0.68f, 0.31f);

    // Static icon pointers
    Texture *ViewportPanel::s_translateIcon = nullptr;
    Texture *ViewportPanel::s_rotateIcon = nullptr;
    Texture *ViewportPanel::s_scaleIcon = nullptr;

    ViewportPanel::ViewportPanel(
        DeferredRenderer &deferredRenderer,
        WorldGrid &worldGrid,
        TransformWidget &transformWidget,
        GridSnapSettings &gridSnapSettings,
        Selection &selection,
        SceneOutlineWindow &sceneOutlineWindow,
        bool &hovering,
        bool &leftButtonPressed,
        bool &rightButtonPressed,
        float &cameraSpeed,
        ImVec2 &lastAvailViewportSize,
        ImVec2 &lastContentRectMin,
        std::function<void()> renderCallback,
        std::function<void()> generateMinimapsCallback)
        : m_deferredRenderer(deferredRenderer), m_worldGrid(worldGrid), m_transformWidget(transformWidget), m_gridSnapSettings(gridSnapSettings), m_selection(selection), m_sceneOutlineWindow(sceneOutlineWindow), m_hovering(hovering), m_leftButtonPressed(leftButtonPressed), m_rightButtonPressed(rightButtonPressed), m_cameraSpeed(cameraSpeed), m_lastAvailViewportSize(lastAvailViewportSize), m_lastContentRectMin(lastContentRectMin), m_renderCallback(std::move(renderCallback)), m_generateMinimapsCallback(std::move(generateMinimapsCallback))
    {
    }

    void ViewportPanel::Draw(const String &id, WorldEditMode *currentEditMode)
    {
        if (ImGui::Begin(id.c_str()))
        {
            // Determine the current viewport position
            auto viewportPos = ImGui::GetWindowContentRegionMin();
            viewportPos.x += ImGui::GetWindowPos().x;
            viewportPos.y += ImGui::GetWindowPos().y;

            // Determine the available size for the viewport window and either create the render target
            // or resize it if needed
            const auto availableSpace = ImGui::GetContentRegionAvail();

            if (m_lastAvailViewportSize.x != availableSpace.x || m_lastAvailViewportSize.y != availableSpace.y)
            {
                m_deferredRenderer.Resize(availableSpace.x, availableSpace.y);
                m_lastAvailViewportSize = availableSpace;

                if (m_renderCallback)
                {
                    m_renderCallback();
                }
            }

            // Render the render target content into the window as image object
            ImGui::Image(m_deferredRenderer.GetFinalRenderTarget()->GetTextureObject(), availableSpace);
            ImGui::SetItemUsingMouseWheel();

            HandleViewportDragDrop(currentEditMode);

            HandleViewportInteractions(availableSpace);
            DrawViewportToolbar(availableSpace);
        }
        ImGui::End();
    }

    void ViewportPanel::SetTransformIcons(Texture *translateIcon, Texture *rotateIcon, Texture *scaleIcon)
    {
        s_translateIcon = translateIcon;
        s_rotateIcon = rotateIcon;
        s_scaleIcon = scaleIcon;
    }

    void ViewportPanel::HandleViewportInteractions(const ImVec2 &availableSpace)
    {
        m_hovering = ImGui::IsItemHovered();
        if (m_hovering)
        {
            m_cameraSpeed = std::max(std::min(m_cameraSpeed + ImGui::GetIO().MouseWheel * 5.0f, 200.0f), 1.0f);

            m_leftButtonPressed = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            m_rightButtonPressed = ImGui::IsMouseDown(ImGuiMouseButton_Right);

            const auto mousePos = ImGui::GetMousePos();
            const auto contentRectMin = ImGui::GetWindowPos();
            m_lastContentRectMin = contentRectMin;

            if (ImGui::IsKeyPressed(ImGuiKey_Delete))
            {
                if (!m_selection.IsEmpty())
                {
                    for (auto &selected : m_selection.GetSelectedObjects())
                    {
                        selected->Remove();
                    }

                    m_selection.Clear();

                    // Update scene outline when objects are deleted
                    m_sceneOutlineWindow.Update();
                }
            }
        }
    }

    void ViewportPanel::DrawViewportToolbar(const ImVec2 &availableSpace)
    {
        ImGui::SetItemAllowOverlap();
        ImGui::SetCursorPos(ImVec2(16, 16));

        // Left side: Grid toggle, snap settings, minimap
        if (ImGui::Button("Toggle Grid"))
        {
            m_worldGrid.SetVisible(!m_worldGrid.IsVisible());
        }
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        bool snapEnabled = m_gridSnapSettings.IsEnabled();
        if (ImGui::Checkbox("Snap", &snapEnabled))
        {
            m_gridSnapSettings.SetEnabled(snapEnabled);
            m_transformWidget.SetSnapping(snapEnabled);
        }
        ImGui::SameLine();

        if (m_gridSnapSettings.IsEnabled())
        {
            DrawSnapSettings();
            ImGui::SameLine();
        }

        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        if (ImGui::Button("Generate Minimaps"))
        {
            if (m_generateMinimapsCallback)
            {
                m_generateMinimapsCallback();
            }
        }

        // Position transform buttons at the right edge with margin
        const float buttonWidth = 16.0f * 3; // Three 16x16 buttons
        const float rightMargin = 64.0f;
        ImGui::SameLine(availableSpace.x - buttonWidth - rightMargin);

        // Right side: Transform mode buttons
        DrawTransformButtons(availableSpace);
    }

    void ViewportPanel::DrawSnapSettings()
    {
        const auto &translateLabels = GridSnapSettings::GetTranslateSizeLabels();
        const auto &rotateLabels = GridSnapSettings::GetRotateSizeLabels();

        const char *previewValue = nullptr;

        switch (m_transformWidget.GetTransformMode())
        {
        case TransformMode::Translate:
        case TransformMode::Scale:
            previewValue = translateLabels[m_gridSnapSettings.GetCurrentTranslateIndex()];
            break;
        case TransformMode::Rotate:
            previewValue = rotateLabels[m_gridSnapSettings.GetCurrentRotateIndex()];
            break;
        }

        ImGui::SetNextItemWidth(50.0f);

        if (ImGui::BeginCombo("##snapSizes", previewValue, ImGuiComboFlags_None))
        {
            switch (m_transformWidget.GetTransformMode())
            {
            case TransformMode::Translate:
            case TransformMode::Scale:
                for (int i = 0; i < static_cast<int>(translateLabels.size()); ++i)
                {
                    const bool isSelected = i == m_gridSnapSettings.GetCurrentTranslateIndex();
                    if (ImGui::Selectable(translateLabels[i], isSelected))
                    {
                        m_gridSnapSettings.SetCurrentTranslateIndex(i);
                        m_transformWidget.SetTranslateSnapSize(m_gridSnapSettings.GetCurrentTranslateSnap());
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                break;
            case TransformMode::Rotate:
                for (int i = 0; i < static_cast<int>(rotateLabels.size()); ++i)
                {
                    const bool isSelected = i == m_gridSnapSettings.GetCurrentRotateIndex();
                    if (ImGui::Selectable(rotateLabels[i], isSelected))
                    {
                        m_gridSnapSettings.SetCurrentRotateIndex(i);
                        m_transformWidget.SetRotateSnapSize(m_gridSnapSettings.GetCurrentRotateSnap());
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }

            ImGui::EndCombo();
        }
    }

    void ViewportPanel::DrawTransformButtons(const ImVec2 &availableSpace)
    {
        // Transform mode buttons - now inline, no need for SetCursorPos
        ImGui::PushStyleColor(ImGuiCol_Button, m_transformWidget.GetTransformMode() == TransformMode::Translate ? ButtonSelected : ButtonNormal);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ButtonHovered);
        if (ImGui::ImageButton(s_translateIcon ? s_translateIcon->GetTextureObject() : nullptr, ImVec2(16.0f, 16.0f)))
        {
            m_transformWidget.SetTransformMode(TransformMode::Translate);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Translate selected objects along X, Y and Z axis.");
            ImGui::Text("Keyboard Shortcut:");
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
            ImGui::SameLine();
            ImGui::Text("1");
            ImGui::PopStyleColor();
            ImGui::EndTooltip();
        }

        ImGui::PopStyleColor(2);
        ImGui::SameLine(0, 0);

        ImGui::PushStyleColor(ImGuiCol_Button, m_transformWidget.GetTransformMode() == TransformMode::Rotate ? ButtonSelected : ButtonNormal);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ButtonHovered);
        if (ImGui::ImageButton(s_rotateIcon ? s_rotateIcon->GetTextureObject() : nullptr, ImVec2(16.0f, 16.0f)))
        {
            m_transformWidget.SetTransformMode(TransformMode::Rotate);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Rotate selected objects.");
            ImGui::Text("Keyboard Shortcut:");
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
            ImGui::SameLine();
            ImGui::Text("2");
            ImGui::PopStyleColor();
            ImGui::EndTooltip();
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine(0, 0);

        ImGui::BeginDisabled(true);
        ImGui::PushStyleColor(ImGuiCol_Button, m_transformWidget.GetTransformMode() == TransformMode::Scale ? ButtonSelected : ButtonNormal);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ButtonHovered);
        if (ImGui::ImageButton(s_scaleIcon ? s_scaleIcon->GetTextureObject() : nullptr, ImVec2(16.0f, 16.0f)))
        {
            // m_transformWidget.SetTransformMode(TransformMode::Scale);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Scale selected objects.");
            ImGui::Text("Keyboard Shortcut:");
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
            ImGui::SameLine();
            ImGui::Text("3");
            ImGui::PopStyleColor();
            ImGui::EndTooltip();
        }
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
    }

    void ViewportPanel::HandleViewportDragDrop(WorldEditMode *currentEditMode)
    {
        if (currentEditMode && currentEditMode->SupportsViewportDrop())
        {
            if (ImGui::BeginDragDropTarget())
            {
                const ImVec2 mousePos = ImGui::GetMousePos();
                const float x = (mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x;
                const float y = (mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y;
                currentEditMode->OnViewportDrop(x, y);

                ImGui::EndDragDropTarget();
            }
        }
    }
}
