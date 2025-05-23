#pragma once

#include "graphics/color_curve.h"
#include <imgui.h>
#include <imgui_internal.h> // Needed for additional ImGui features

namespace mmo
{
    // Constants for the editor
    constexpr float KEY_SIZE = 8.0f;
    constexpr float TANGENT_SIZE = 6.0f;

    /**
     * @class ColorCurveImGuiEditor
     * @brief ImGui-based editor widget for ColorCurve objects.
     * 
     * Provides a GUI for editing and visualizing color curves directly in the editor.
     * Supports adding, removing, and adjusting keyframes as well as tangents.
     */
    class ColorCurveImGuiEditor
    {
    public:
        /**
         * @brief Constructor
         * @param label The label for this editor instance
         * @param colorCurve Reference to the color curve being edited
         */
        ColorCurveImGuiEditor(const char* label, ColorCurve& colorCurve);
        
        /**
         * @brief Renders the color curve editor UI
         * @param width The width of the editor widget
         * @param height The height of the editor widget
         * @return True if the curve was modified, false otherwise
         */
        bool Draw(float width = 0.0f, float height = 280.0f);
          /**
         * @brief Sets whether the alpha channel should be visible
         * @param showAlpha True to show alpha channel, false to hide it
         */
        void SetShowAlpha(bool showAlpha) { m_showAlpha = showAlpha; }
        
        /**
         * @brief Gets whether the alpha channel is visible
         * @return True if alpha channel is visible, false otherwise
         */
        bool GetShowAlpha() const { return m_showAlpha; }
        
        /**
         * @brief Sets whether the tangent handles should be visible
         * @param showTangents True to show tangent handles, false to hide them
         */
        void SetShowTangents(bool showTangents) { m_showTangents = showTangents; }
        
        /**
         * @brief Gets whether the tangent handles are visible
         * @return True if tangent handles are visible, false otherwise
         */
        bool GetShowTangents() const { return m_showTangents; }
        
        /**
         * @brief Sets the editor's background color
         * @param color The background color
         */
        void SetBackgroundColor(const ImVec4& color) { m_backgroundColor = color; }
        
        /**
         * @brief Gets the editor's background color
         * @return The background color
         */
        const ImVec4& GetBackgroundColor() const { return m_backgroundColor; }
        
        /**
         * @brief Sets the grid color
         * @param color The grid color
         */
        void SetGridColor(const ImVec4& color) { m_gridColor = color; }
        
        /**
         * @brief Gets the grid color
         * @return The grid color
         */
        const ImVec4& GetGridColor() const { return m_gridColor; }
        
        /**
         * @brief Sets whether the horizontal grid should be visible
         * @param showHorizontalGrid True to show horizontal grid lines, false to hide them
         */
        void SetShowHorizontalGrid(bool showHorizontalGrid) { m_showHorizontalGrid = showHorizontalGrid; }
        
        /**
         * @brief Gets whether the horizontal grid is visible
         * @return True if horizontal grid is visible, false otherwise
         */
        bool GetShowHorizontalGrid() const { return m_showHorizontalGrid; }
        
        /**
         * @brief Sets whether the vertical grid should be visible
         * @param showVerticalGrid True to show vertical grid lines, false to hide them
         */
        void SetShowVerticalGrid(bool showVerticalGrid) { m_showVerticalGrid = showVerticalGrid; }
        
        /**
         * @brief Gets whether the vertical grid is visible
         * @return True if vertical grid is visible, false otherwise
         */
        bool GetShowVerticalGrid() const { return m_showVerticalGrid; }
        
        /**
         * @brief Returns the label of this editor instance
         * @return The label string
         */
        const char* GetLabel() const { return m_label.c_str(); }

        /**
         * @brief Enables or disables the color preview strip
         * @param showPreview True to show the preview, false to hide it
         */
        void SetShowColorPreview(bool showPreview) { m_showColorPreview = showPreview; }

        /**
         * @brief Gets whether the color preview strip is visible
         * @return True if color preview is visible, false otherwise
         */
        bool GetShowColorPreview() const { return m_showColorPreview; }

        /**
         * @brief Sets the thickness of the curve lines
         * @param thickness The thickness in pixels
         */
        void SetCurveThickness(float thickness) { m_curveThickness = thickness; }
        
        /**
         * @brief Gets the thickness of the curve lines
         * @return The thickness in pixels
         */
        float GetCurveThickness() const { return m_curveThickness; }

        /**
         * @brief Resets the view to the default position and zoom level
         */
        void ResetView();        /**
         * @brief Sets the snap increment for time values when dragging keys
         * @param increment The snap increment value (0 to disable snapping)
         */
        void SetTimeSnap(float increment) { m_timeSnapIncrement = increment; }
        
        /**
         * @brief Gets the snap increment for time values
         * @return The time snap increment value
         */
        float GetTimeSnap() const { return m_timeSnapIncrement; }

        /**
         * @brief Sets the snap increment for color values when dragging keys
         * @param increment The snap increment value (0 to disable snapping)
         */
        void SetValueSnap(float increment) { m_valueSnapIncrement = increment; }
        
        /**
         * @brief Gets the snap increment for color values
         * @return The value snap increment value
         */
        float GetValueSnap() const { return m_valueSnapIncrement; }
        
    private:
        /**
         * @brief Renders the color curve in the editor
         * @param drawList ImGui draw list to render to
         * @param canvasPos Top-left position of the canvas
         * @param canvasSize Size of the canvas
         * @param curveThickness Thickness of the curve line
         */
        void DrawCurve(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize, float curveThickness = 2.0f);
        
        /**
         * @brief Renders the curve's keyframes
         * @param drawList ImGui draw list to render to
         * @param canvasPos Top-left position of the canvas
         * @param canvasSize Size of the canvas
         */
        void DrawKeys(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize);
        
        /**
         * @brief Renders the curve's tangent handles
         * @param drawList ImGui draw list to render to
         * @param canvasPos Top-left position of the canvas
         * @param canvasSize Size of the canvas
         */
        void DrawTangents(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize);
          /**
         * @brief Renders the background grid
         * @param drawList ImGui draw list to render to
         * @param canvasPos Top-left position of the canvas
         * @param canvasSize Size of the canvas
         */
        void DrawGrid(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize);
        
        /**
         * @brief Renders time labels at the top of the canvas
         * @param drawList ImGui draw list to render to
         * @param canvasPos Top-left position of the canvas
         * @param canvasSize Size of the canvas
         */
        void DrawTimeLabels(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize);
        
        /**
         * @brief Renders a curve preview showing the color gradient
         * @param drawList ImGui draw list to render to
         * @param canvasPos Top-left position of the canvas
         * @param canvasSize Size of the canvas
         */
        void DrawColorPreview(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize);

        /**
         * @brief Renders a tooltip with curve information at the current mouse position
         * @param drawList ImGui draw list to render to
         * @param canvasPos Top-left position of the canvas
         * @param canvasSize Size of the canvas
         */
        void DrawTooltip(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize);
        
        /**
         * @brief Maps a time value to an X-coordinate in the canvas
         * @param time Time value [0..1]
         * @param canvasPos Top-left position of the canvas
         * @param canvasSize Size of the canvas
         * @return X-coordinate in canvas space
         */
        float TimeToX(float time, const ImVec2& canvasPos, const ImVec2& canvasSize) const;
        
        /**
         * @brief Maps a color component value to a Y-coordinate in the canvas
         * @param value Color component value [0..1]
         * @param canvasPos Top-left position of the canvas
         * @param canvasSize Size of the canvas
         * @return Y-coordinate in canvas space
         */
        float ValueToY(float value, const ImVec2& canvasPos, const ImVec2& canvasSize) const;
        
        /**
         * @brief Maps an X-coordinate in the canvas to a time value
         * @param x X-coordinate in canvas space
         * @param canvasPos Top-left position of the canvas
         * @param canvasSize Size of the canvas
         * @return Time value [0..1]
         */
        float XToTime(float x, const ImVec2& canvasPos, const ImVec2& canvasSize) const;
        
        /**
         * @brief Maps a Y-coordinate in the canvas to a color component value
         * @param y Y-coordinate in canvas space
         * @param canvasPos Top-left position of the canvas
         * @param canvasSize Size of the canvas
         * @return Color component value [0..1]
         */
        float YToValue(float y, const ImVec2& canvasPos, const ImVec2& canvasSize) const;
          /**
         * @brief Handles mouse interactions with the curve editor
         * @param canvasPos Top-left position of the canvas
         * @param canvasSize Size of the canvas
         * @param modified Output parameter set to true if the curve was modified
         */
        void HandleInteraction(const ImVec2& canvasPos, const ImVec2& canvasSize, bool& modified);
        
        /**
         * @brief Handles right-click context menu
         * @param canvasPos Top-left position of the canvas
         * @param canvasSize Size of the canvas
         * @param modified Output parameter set to true if the curve was modified
         */
        void HandleContextMenu(const ImVec2& canvasPos, const ImVec2& canvasSize, bool& modified);
        
        /**
         * @brief Converts a Vector4 color to ImGui color format
         * @param color The Vector4 color to convert
         * @return The ImVec4 color
         */
        ImVec4 ColorToImVec4(const Vector4& color) const;
        
        /**
         * @brief Finds the closest key to the given mouse position
         * @param mousePos Mouse position in canvas space
         * @param canvasPos Top-left position of the canvas
         * @param canvasSize Size of the canvas
         * @param outKeyIndex Output parameter for the index of the closest key
         * @param maxDistance Maximum distance to consider
         * @return True if a key was found within maxDistance, false otherwise
         */
        bool FindClosestKey(const ImVec2& mousePos, const ImVec2& canvasPos, 
                            const ImVec2& canvasSize, size_t& outKeyIndex, 
                            float maxDistance = 10.0f) const;
        
        /**
         * @brief Finds if the mouse is hovering over a tangent handle
         * @param mousePos Mouse position in canvas space
         * @param canvasPos Top-left position of the canvas
         * @param canvasSize Size of the canvas
         * @param outKeyIndex Output parameter for the index of the key
         * @param outIsInTangent Output parameter indicating if it's an in-tangent (true) or out-tangent (false)
         * @param outComponent Output parameter for the color component (0=R, 1=G, 2=B, 3=A)
         * @param maxDistance Maximum distance to consider
         * @return True if a tangent handle was found within maxDistance, false otherwise
         */
        bool FindHoveredTangent(const ImVec2& mousePos, const ImVec2& canvasPos, 
                               const ImVec2& canvasSize, size_t& outKeyIndex, 
                               bool& outIsInTangent, int& outComponent, 
                               float maxDistance = 8.0f) const;

        /**
         * @brief Snaps a time value to the nearest increment
         * @param time The time value to snap
         * @return The snapped time value
         */
        float SnapTime(float time) const;

        /**
         * @brief Snaps a color component value to the nearest increment
         * @param value The value to snap
         * @return The snapped value
         */
        float SnapValue(float value) const;

        /**
         * @brief Resets all tangent handles to auto-calculated values
         * @return True if any tangents were modified, false otherwise
         */
        bool ResetAllTangents();

        /**
         * @brief Distributes selected keys evenly in time
         * @return True if any keys were modified, false otherwise
         */
        bool DistributeKeysEvenly();        /**
         * @brief Handles zoom and pan interactions
         * @param canvasPos Top-left position of the canvas
         * @param canvasSize Size of the canvas
         */
        void HandleZoomAndPan(const ImVec2& canvasPos, const ImVec2& canvasSize);
        
        /**
         * @brief Draws visual indicators for keys that are outside the visible area
         * @param drawList ImGui draw list to render to
         * @param canvasPos Top-left position of the canvas
         * @param canvasSize Size of the canvas
         */
        void DrawOffscreenIndicators(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize);
        
        /**
         * @brief Draws an indicator for an off-screen curve component
         * @param drawList ImGui draw list to render to
         * @param value The component value
         * @param x The x-coordinate for the indicator
         * @param canvasPos Top-left position of the canvas
         * @param canvasSize Size of the canvas
         * @param color The color of the indicator
         * @param arrowSize The size of the indicator arrow
         */
        void DrawComponentOffscreenIndicator(ImDrawList* drawList, float value, float x, 
                                            const ImVec2& canvasPos, const ImVec2& canvasSize, 
                                            const ImVec4& color, float arrowSize);

        /**
         * @brief Renders value labels on the left side of the canvas
         * @param drawList ImGui draw list to render to
         * @param canvasPos Top-left position of the canvas
         * @param canvasSize Size of the canvas
         */
        void DrawValueLabels(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize);

    private:
        std::string m_label;         ///< The label for this editor instance
        ColorCurve& m_colorCurve;    ///< Reference to the color curve being edited
        
        size_t m_selectedKeyIndex;   ///< Index of the currently selected key (-1 if none)
        size_t m_hoveredKeyIndex;    ///< Index of the currently hovered key (-1 if none)
        bool m_draggingKey;          ///< Whether a key is currently being dragged
        bool m_draggingTangent;      ///< Whether a tangent handle is currently being dragged
        bool m_tangentIsIn;          ///< Whether the dragged tangent is an in-tangent
        int m_draggedComponent;      ///< The color component being dragged (0=R, 1=G, 2=B, 3=A)
        bool m_draggingCanvas;       ///< Whether the canvas is being panned
        ImVec2 m_dragStartPos;       ///< Starting position for dragging operations
          float m_viewMinX;            ///< Minimum X value in view (time)
        float m_viewMaxX;            ///< Maximum X value in view (time)
        float m_viewMinY;            ///< Minimum Y value in view (value)
        float m_viewMaxY;            ///< Maximum Y value in view (value)
        float m_zoomLevel;           ///< Current zoom level
        
        bool m_showAlpha;            ///< Whether to show the alpha channel
        bool m_showTangents;         ///< Whether to show tangent handles
        bool m_showHorizontalGrid;   ///< Whether to show horizontal grid lines
        bool m_showVerticalGrid;     ///< Whether to show vertical grid lines
        bool m_showColorPreview;     ///< Whether to show the color preview strip
        
        float m_curveThickness;      ///< Thickness of curve lines
        float m_timeSnapIncrement;   ///< Snap increment for time values (0 = no snapping)
        float m_valueSnapIncrement;  ///< Snap increment for color values (0 = no snapping)
        
        ImVec4 m_backgroundColor;    ///< Background color of the editor
        ImVec4 m_gridColor;          ///< Color of the grid lines
        
        // Component colors for visualization
        ImVec4 m_redColor;           ///< Red component color
        ImVec4 m_greenColor;         ///< Green component color
        ImVec4 m_blueColor;          ///< Blue component color
        ImVec4 m_alphaColor;         ///< Alpha component color
        ImVec4 m_keyColor;           ///< Color for key points
        ImVec4 m_selectedKeyColor;   ///< Color for selected key points
        ImVec4 m_tangentHandleColor; ///< Color for tangent handles
    };
}
