#pragma once

#include "editors/editor_instance.h"
#include "editors/color_curve_editor/color_curve_editor.h"
#include "graphics/color_curve.h"
#include <imgui.h>

namespace mmo
{
    class ColorCurveImGuiEditor;

    /// @brief An editor instance for editing a color curve.
    class ColorCurveEditorInstance final : public EditorInstance
    {
    public:
        ColorCurveEditorInstance(ColorCurveEditor& editor, EditorHost& host, const Path& assetPath);
        ~ColorCurveEditorInstance() override;

    public:
        bool Save() override;

    public:
        /// @copydoc EditorInstance::Draw
        void Draw() override;

    private:
        // Init dock layout
        void InitializeDockLayout(ImGuiID dockspaceId, const String& editorId, const String& previewId);

        // Draw panels
        void DrawEditorPanel(const String& panelId);
        void DrawPreviewPanel(const String& panelId);

    private:
        ColorCurveEditor& m_editor;
        ColorCurve m_colorCurve;
        std::unique_ptr<ColorCurveImGuiEditor> m_colorCurveEditor;
        bool m_modified { false };
    };
}
