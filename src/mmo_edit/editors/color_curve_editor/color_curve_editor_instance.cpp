#include "color_curve_editor_instance.h"
#include "color_curve_imgui_editor.h"
#include "editor_host.h"
#include "log/default_log_levels.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "assets/asset_registry.h"

#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>

#include "stream_sink.h"
#include "stream_source.h"

namespace mmo
{
    ColorCurveEditorInstance::ColorCurveEditorInstance(ColorCurveEditor& editor, EditorHost& host, const Path& assetPath)
        : EditorInstance(host, assetPath)
        , m_editor(editor)
    {
        if (const auto file = AssetRegistry::OpenFile(GetAssetPath().string()))
        {
            io::StreamSource source{ *file };
            io::Reader reader{ source };
            if (!m_colorCurve.Deserialize(reader))
            {
                throw std::runtime_error("Failed to deserialize color curve from file: " + assetPath.string());
            }
        }

        // Create the ImGui editor for this color curve
        m_colorCurveEditor = std::make_unique<ColorCurveImGuiEditor>(assetPath.filename().string().c_str(), m_colorCurve);
    }

    ColorCurveEditorInstance::~ColorCurveEditorInstance() = default;

    void ColorCurveEditorInstance::Draw()
    {
        if (ImGui::GetCurrentContext()->FrameCount == 1)
        {
            ImGuiID dockspaceId = ImGui::GetID("ColorCurveDockSpace");
            InitializeDockLayout(dockspaceId, "ColorCurveEditor", "ColorCurvePreview");
        }

        ImGuiID dockspaceId = ImGui::GetID("ColorCurveDockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        DrawEditorPanel("ColorCurveEditor");
        DrawPreviewPanel("ColorCurvePreview");
    }

    bool ColorCurveEditorInstance::Save()
    {
        if (!m_modified)
        {
            return true;
        }

        const auto file = AssetRegistry::CreateNewFile(GetAssetPath().string());
        if (!file)
        {
            ELOG("Failed to open material file " << GetAssetPath() << " for writing!");
            return false;
        }

        io::StreamSink sink{ *file };
        io::Writer writer{ sink };

        // Serialize the color curve to the file
        m_colorCurve.Serialize(writer);

        m_modified = false;
        return true;
    }

    void ColorCurveEditorInstance::InitializeDockLayout(ImGuiID dockspaceId, const String& editorId, const String& previewId)
    {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);

        ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

        ImGuiID editorDock;
        ImGuiID previewDock;

        ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.7f, &editorDock, &previewDock);

        ImGui::DockBuilderDockWindow(editorId.c_str(), editorDock);
        ImGui::DockBuilderDockWindow(previewId.c_str(), previewDock);

        ImGui::DockBuilderFinish(dockspaceId);
    }

    void ColorCurveEditorInstance::DrawEditorPanel(const String& panelId)
    {
        if (ImGui::Begin(panelId.c_str()))
        {
            ImGui::Text("Color Curve Editor - %s", m_assetPath.filename().string().c_str());
            ImGui::Separator();

            // Create the editor UI
            if (m_colorCurveEditor->Draw())
            {
                m_modified = true;
            }
        }
        ImGui::End();
    }

    void ColorCurveEditorInstance::DrawPreviewPanel(const String& panelId)
    {
        if (ImGui::Begin(panelId.c_str()))
        {
            ImGui::Text("Color Preview");
            ImGui::Separator();

            // Draw a color strip to visualize the curve
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImVec2 size(ImGui::GetContentRegionAvail().x, 80.0f);

            // Draw a background grid for the preview
            const ImU32 gridColor = IM_COL32(80, 80, 80, 100);
            const float gridStep = size.x / 10.0f;
            for (int i = 0; i <= 10; ++i)
            {
                const float x = pos.x + i * gridStep;
                drawList->AddLine(ImVec2(x, pos.y), ImVec2(x, pos.y + size.y), gridColor);
            }

            // Draw the color preview
            const int segments = static_cast<int>(size.x);
            for (int i = 0; i < segments; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(segments - 1);
                Vector4 color = m_colorCurve.Evaluate(t);
                
                const float x0 = pos.x + i * (size.x / segments);
                const float x1 = pos.x + (i + 1) * (size.x / segments);
                
                ImU32 imColor = IM_COL32(
                    static_cast<int>(color.x * 255.0f),
                    static_cast<int>(color.y * 255.0f),
                    static_cast<int>(color.z * 255.0f),
                    static_cast<int>(color.w * 255.0f)
                );
                
                drawList->AddRectFilled(
                    ImVec2(x0, pos.y),
                    ImVec2(x1, pos.y + size.y),
                    imColor
                );
            }

            // Draw border around the preview
            drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(255, 255, 255, 100));

            ImGui::Dummy(size);

            // Show a legend for the color components
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "Red");
            ImGui::SameLine(80.0f);
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "Green");
            ImGui::SameLine(160.0f);
            ImGui::TextColored(ImVec4(0.2f, 0.4f, 0.9f, 1.0f), "Blue");
            ImGui::SameLine(240.0f);
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Alpha");

            // Show editor options
            ImGui::Separator();
            ImGui::Text("Editor Options");

            bool showAlpha = true;
            if (ImGui::Checkbox("Show Alpha Channel", &showAlpha))
            {
                m_colorCurveEditor->SetShowAlpha(showAlpha);
            }

            bool showTangents = true;
            if (ImGui::Checkbox("Show Tangent Handles", &showTangents))
            {
                m_colorCurveEditor->SetShowTangents(showTangents);
            }

            bool showPreview = true;
            if (ImGui::Checkbox("Show Color Preview", &showPreview))
            {
                m_colorCurveEditor->SetShowColorPreview(showPreview);
            }

            // Add curve thickness control
            float thickness = 2.0f;
            if (ImGui::SliderFloat("Curve Thickness", &thickness, 1.0f, 5.0f, "%.1f"))
            {
                m_colorCurveEditor->SetCurveThickness(thickness);
            }

            // Add snapping options
            float timeSnap = 0.0f;
            if (ImGui::SliderFloat("Time Snapping", &timeSnap, 0.0f, 0.25f, timeSnap > 0.0f ? "%.3f" : "Off"))
            {
                m_colorCurveEditor->SetTimeSnap(timeSnap);
            }

            float valueSnap = 0.0f;
            if (ImGui::SliderFloat("Value Snapping", &valueSnap, 0.0f, 0.25f, valueSnap > 0.0f ? "%.3f" : "Off"))
            {
                m_colorCurveEditor->SetValueSnap(valueSnap);
            }
        }
        ImGui::End();
    }
}
