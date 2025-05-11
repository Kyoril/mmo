#include "color_curve_editor.h"
#include "color_curve_editor_instance.h"
#include "editor_host.h"

#include <imgui.h>

#include "stream_sink.h"
#include "assets/asset_registry.h"
#include "imgui/misc/cpp/imgui_stdlib.h"
#include "log/default_log_levels.h"

namespace mmo
{    bool ColorCurveEditor::CanLoadAsset(const String& extension) const
    {
        return extension == ".hccv";
    }

    void ColorCurveEditor::DrawImpl()
    {
        // Show dialog for creating a new color curve asset if needed
        if (m_showColorCurveNameDialog)
        {
            ImGui::OpenPopup("Create New Color Curve");
            m_showColorCurveNameDialog = false;
        }

        if (ImGui::BeginPopupModal("Create New Color Curve", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {            ImGui::Text("Enter a name for the new color curve:");

            ImGui::InputText("##field", &m_colorCurveName);
            ImGui::SameLine();
            ImGui::Text(".hccv");

            if (ImGui::Button("Create"))
            {
                CreateNewColorCurve();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    std::shared_ptr<EditorInstance> ColorCurveEditor::OpenAssetImpl(const Path& asset)
    {
        try
        {
            auto instance = std::make_shared<ColorCurveEditorInstance>(*this, m_host, asset);
            m_instances[asset] = instance;
            return instance;
        }
        catch (const std::exception& e)
        {
            ELOG("Failed to open color curve: " << e.what());
            return nullptr;
        }
    }

    void ColorCurveEditor::CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance)
    {
        const auto it = m_instances.find(instance->GetAssetPath());
        if (it != m_instances.end())
        {
            m_instances.erase(it);
        }
    }

    void ColorCurveEditor::AddCreationContextMenuItems()
    {
        if (ImGui::MenuItem("Color Curve"))
        {
            m_colorCurveName = "";
            m_showColorCurveNameDialog = true;
        }
    }

    void ColorCurveEditor::AddAssetActions(const String& asset)
    {
        // Optional - Add asset-specific context menu actions here
    }    void ColorCurveEditor::CreateNewColorCurve()
    {
        if (m_colorCurveName.empty())
        {
            return;
        }

        // Determine the current path based on the host
        auto currentPath = m_host.GetCurrentPath();
        currentPath /= m_colorCurveName + ".hccv";

        // Create the file
        const auto file = AssetRegistry::CreateNewFile(currentPath.string());
        if (!file)
        {
            ELOG("Failed to create color curve file: " << currentPath);
            return;
        }

        // Create a default color curve with black to white gradient
        ColorCurve curve;
        curve.AddKey(0.0f, Vector4(0.0f, 0.0f, 0.0f, 1.0f)); // Black
        curve.AddKey(1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)); // White
        curve.CalculateTangents();

        // Serialize the curve
        io::StreamSink sink{ *file };
        io::Writer writer{ sink };
        curve.Serialize(writer);
        
        file->flush();

        // Notify the host that an asset was imported
        m_host.assetImported(m_host.GetCurrentPath());
        
        // Open the new file in the editor
        OpenAsset(currentPath);

        ILOG("Created new color curve: " << currentPath);
    }
}
