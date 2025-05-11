#include "color_curve_imgui_editor.h"

#include <vector>
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>

namespace mmo
{    ColorCurveImGuiEditor::ColorCurveImGuiEditor(const char* label, ColorCurve& colorCurve)
        : m_label(label)
        , m_colorCurve(colorCurve)
        , m_selectedKeyIndex(static_cast<size_t>(-1))
        , m_hoveredKeyIndex(static_cast<size_t>(-1))
        , m_draggingKey(false)
        , m_draggingTangent(false)
        , m_tangentIsIn(false)
        , m_draggedComponent(0)
        , m_draggingCanvas(false)
        , m_viewMinX(0.0f)
        , m_viewMaxX(1.0f)
        , m_viewMinY(0.0f)
        , m_viewMaxY(1.0f)
        , m_zoomLevel(1.0f)
        , m_showAlpha(true)
        , m_showTangents(true)
        , m_showHorizontalGrid(true)
        , m_showVerticalGrid(true)
        , m_showColorPreview(true)
        , m_curveThickness(2.0f)
        , m_timeSnapIncrement(0.0f)
        , m_valueSnapIncrement(0.0f)
        , m_backgroundColor(ImVec4(0.15f, 0.15f, 0.15f, 1.0f))
        , m_gridColor(ImVec4(0.4f, 0.4f, 0.4f, 0.25f))
        , m_redColor(ImVec4(0.9f, 0.2f, 0.2f, 1.0f))
        , m_greenColor(ImVec4(0.2f, 0.9f, 0.2f, 1.0f))
        , m_blueColor(ImVec4(0.2f, 0.4f, 0.9f, 1.0f))
        , m_alphaColor(ImVec4(0.8f, 0.8f, 0.8f, 1.0f))
        , m_keyColor(ImVec4(0.8f, 0.8f, 0.8f, 1.0f))
        , m_selectedKeyColor(ImVec4(1.0f, 0.9f, 0.2f, 1.0f))
        , m_tangentHandleColor(ImVec4(0.7f, 0.7f, 0.7f, 1.0f))
    {
        ResetView();
    }
    
    void ColorCurveImGuiEditor::ResetView()
    {
        m_viewMinX = -0.05f;  // Show a bit outside the 0-1 range for better visibility
        m_viewMaxX = 1.05f;
        m_viewMinY = -0.05f;
        m_viewMaxY = 1.05f;
        m_zoomLevel = 1.0f;
    }    bool ColorCurveImGuiEditor::Draw(float width, float height)
    {
        ImVec2 availSize = ImGui::GetContentRegionAvail();
        if (width <= 0.0f)
        {
            width = availSize.x;
        }        if (height <= 0.0f)
        {
            // Use almost all available height, leaving only enough for toolbar and controls
            float topToolbarHeight = ImGui::GetFrameHeightWithSpacing();
            float bottomButtonsHeight = ImGui::GetFrameHeightWithSpacing(); // Space for the Add Key button
            
            // If a key is selected, add space for the key property controls
            float keyPropertiesHeight = 0.0f;
            if (m_selectedKeyIndex != static_cast<size_t>(-1) && m_selectedKeyIndex < m_colorCurve.GetKeyCount())
            {
                // Space for Color + Time + Tangent Mode controls
                keyPropertiesHeight = ImGui::GetFrameHeightWithSpacing() * 2;
                // If manual tangent mode, add space for tangent controls
                const ColorKey& key = m_colorCurve.GetKey(m_selectedKeyIndex);
                if (key.tangentMode == 1)
                {
                    keyPropertiesHeight += ImGui::GetFrameHeightWithSpacing() * 3;
                }
            }
              height = availSize.y - (topToolbarHeight + bottomButtonsHeight + keyPropertiesHeight);
            
            // Ensure minimum reasonable height, but not too large
            height = std::max(200.0f, height);
        }
        
        bool modified = false;
        
        ImGui::PushID(m_label.c_str());
        
        // Create the frame for the curve editor
        ImGui::BeginGroup();
        
        // Add zoom/pan toolbar
        if (ImGui::Button("Reset View"))
        {
            ResetView();
        }
        ImGui::SameLine();
        ImGui::Text("Zoom: %.1fx", m_zoomLevel);        ImGui::SameLine();
        ImGui::TextDisabled("(Middle-click and drag to pan, scroll wheel to zoom)");
          // Reserve space for time labels above and value labels on the left of the canvas
        const float timeLabelsHeight = 20.0f;
        const float valueLabelsWidth = 40.0f;          ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        // Adjust canvasPos to make space for labels
        canvasPos.x += valueLabelsWidth;
        canvasPos.y += timeLabelsHeight;
        ImVec2 canvasSize = ImVec2(width - valueLabelsWidth, height - timeLabelsHeight);
        
        // Create the invisible button for interaction that spans the entire canvas + label areas
        // This needs to use the full width and height to capture all mouse events
        ImGui::InvisibleButton("canvas", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        
        // Handle zoom and pan
        HandleZoomAndPan(canvasPos, canvasSize);
        
        // Get the draw list to render our custom UI
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        
        // Only process interactions when mouse is hovering over the control
        // This reduces the amount of processing when the user is not interacting
        if (ImGui::IsItemHovered() || m_draggingKey || m_draggingTangent || m_draggingCanvas)
        {
            // Capture mouse interactions
            HandleInteraction(canvasPos, canvasSize, modified);
            
            // Handle context menu
            HandleContextMenu(canvasPos, canvasSize, modified);
        }
          // Draw time labels at the top of the canvas
        DrawTimeLabels(drawList, canvasPos, canvasSize);
        
        // Draw value labels on the left side of the canvas
        DrawValueLabels(drawList, canvasPos, canvasSize);
        
        // Draw the background
        drawList->AddRectFilled(
            canvasPos, 
            ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), 
            ImGui::ColorConvertFloat4ToU32(m_backgroundColor)
        );
        
        // Draw grid
        DrawGrid(drawList, canvasPos, canvasSize);
        
        // Draw color preview gradient if enabled
        if (m_showColorPreview)
        {
            DrawColorPreview(drawList, canvasPos, canvasSize);
        }
        
        // Draw the curve
        DrawCurve(drawList, canvasPos, canvasSize, m_curveThickness);
        
        // Draw the key points
        DrawKeys(drawList, canvasPos, canvasSize);
        
        // Draw indicators for off-screen keys
        DrawOffscreenIndicators(drawList, canvasPos, canvasSize);
        
        // Draw the tangent handles if enabled
        if (m_showTangents)
        {
            DrawTangents(drawList, canvasPos, canvasSize);
        }
        
        // Draw a border around the canvas
        drawList->AddRect(
            canvasPos, 
            ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), 
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.3f, 0.3f, 1.0f))
        );
        
        // Current time display
        if (ImGui::IsItemHovered() && !m_draggingKey && !m_draggingTangent)
        {
            DrawTooltip(drawList, canvasPos, canvasSize);
        }
        
        // Draw time and value indicators for the selected key
        if (m_selectedKeyIndex != static_cast<size_t>(-1) && m_selectedKeyIndex < m_colorCurve.GetKeyCount())
        {
            const ColorKey& key = m_colorCurve.GetKey(m_selectedKeyIndex);
            
            float timeX = TimeToX(key.time, canvasPos, canvasSize);
            
            // Draw time indicator
            drawList->AddLine(
                ImVec2(timeX, canvasPos.y),
                ImVec2(timeX, canvasPos.y + canvasSize.y),
                ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.7f, 0.7f, 0.5f)),
                1.0f
            );
        }
        
        // Add controls for the selected key point
        if (m_selectedKeyIndex != static_cast<size_t>(-1) && m_selectedKeyIndex < m_colorCurve.GetKeyCount())
        {
            ImGui::Spacing();
            
            ColorKey key = m_colorCurve.GetKey(m_selectedKeyIndex);
            
            ImGui::PushItemWidth(80);
            
            // Time control
            float time = key.time;
            if (ImGui::DragFloat("Time", &time, 0.01f, 0.0f, 1.0f))
            {
                // Don't allow first and last keys to change time
                if ((m_selectedKeyIndex > 0 && m_selectedKeyIndex < m_colorCurve.GetKeyCount() - 1) ||
                    (m_colorCurve.GetKeyCount() <= 2))
                {
                    key.time = time;
                    m_colorCurve.UpdateKey(m_selectedKeyIndex, key);
                    modified = true;
                }
            }
            
            ImGui::SameLine();
            
            // Color control
            float color[4] = { key.color.x, key.color.y, key.color.z, key.color.w };
            if (ImGui::ColorEdit4("Color", color, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar))
            {
                key.color = Vector4(color[0], color[1], color[2], color[3]);
                m_colorCurve.UpdateKey(m_selectedKeyIndex, key);
                modified = true;
            }
            
            // Tangent mode
            const char* modes[] = { "Auto", "Manual" };
            int mode = key.tangentMode;
            if (ImGui::Combo("Tangent Mode", &mode, modes, IM_ARRAYSIZE(modes)))
            {
                key.tangentMode = static_cast<uint8>(mode);
                m_colorCurve.UpdateKey(m_selectedKeyIndex, key);
                if (mode == 0) // Auto mode
                {
                    m_colorCurve.CalculateTangents();
                }
                modified = true;
            }
            
            // Tangent controls (only in manual mode)
            if (key.tangentMode == 1)
            {
                ImGui::Text("In Tangent:");
                float inTangent[4] = { key.inTangent.x, key.inTangent.y, key.inTangent.z, key.inTangent.w };
                if (ImGui::DragFloat4("##InTangent", inTangent, 0.01f))
                {
                    key.inTangent = Vector4(inTangent[0], inTangent[1], inTangent[2], inTangent[3]);
                    m_colorCurve.UpdateKey(m_selectedKeyIndex, key);
                    modified = true;
                }
                
                ImGui::Text("Out Tangent:");
                float outTangent[4] = { key.outTangent.x, key.outTangent.y, key.outTangent.z, key.outTangent.w };
                if (ImGui::DragFloat4("##OutTangent", outTangent, 0.01f))
                {
                    key.outTangent = Vector4(outTangent[0], outTangent[1], outTangent[2], outTangent[3]);
                    m_colorCurve.UpdateKey(m_selectedKeyIndex, key);
                    modified = true;
                }
            }
            
            ImGui::PopItemWidth();
            
            // Add/Remove key buttons
            ImGui::Spacing();
            
            // Don't allow removing first or last key if there are only two keys
            bool disableRemove = m_colorCurve.GetKeyCount() <= 2;
            
            if (disableRemove)
            {
                ImGui::BeginDisabled();
            }
            
            if (ImGui::Button("Remove Key"))
            {
                if (m_colorCurve.RemoveKey(m_selectedKeyIndex))
                {
                    m_selectedKeyIndex = static_cast<size_t>(-1);
                    modified = true;
                }
            }
            
            if (disableRemove)
            {
                ImGui::EndDisabled();
            }
        }
        
        // Add a key at the current mouse position button
        if (ImGui::Button("Add Key"))
        {
            ImVec2 mousePos = ImGui::GetMousePos();
            float time = XToTime(mousePos.x, canvasPos, canvasSize);
            
            // Clamp time to curve range
            time = std::max(0.0f, std::min(time, 1.0f));
            
            // Sample the curve at the mouse position to get the color
            Vector4 color = m_colorCurve.Evaluate(time);
            
            // Add the new key
            size_t newIndex = m_colorCurve.AddKey(time, color);
            m_selectedKeyIndex = newIndex;
            modified = true;
        }
        
        ImGui::EndGroup();
        ImGui::PopID();
        
        return modified;
    }    void ColorCurveImGuiEditor::DrawCurve(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize, float curveThickness)
    {
        const int numSamples = static_cast<int>(canvasSize.x);
        
        if (numSamples <= 1)
        {
            return;
        }
        
        // Maintain persistent vectors to avoid reallocating every frame
        static thread_local std::vector<ImVec2> redPoints;
        static thread_local std::vector<ImVec2> greenPoints;
        static thread_local std::vector<ImVec2> bluePoints;
        static thread_local std::vector<ImVec2> alphaPoints;
        
        // Resize vectors only when necessary
        if (redPoints.size() != static_cast<size_t>(numSamples)) 
        {
            redPoints.resize(numSamples);
            greenPoints.resize(numSamples);
            bluePoints.resize(numSamples);
            alphaPoints.resize(numSamples);
        }
          // Pre-calculate values that are constant throughout this loop
        const float xScale = 1.0f / static_cast<float>(numSamples - 1);
        const float timeRange = m_viewMaxX - m_viewMinX;
          for (int i = 0; i < numSamples; ++i)
        {
            // Map sample point from screen to view space (optimized calculation)
            float normalizedX = static_cast<float>(i) * xScale;
            float viewSpaceT = m_viewMinX + normalizedX * timeRange;
            
            // Clamp to valid curve range
            float t = std::max(0.0f, std::min(viewSpaceT, 1.0f));
            Vector4 color = m_colorCurve.Evaluate(t);
            
            // Map back to screen space
            float x = canvasPos.x + normalizedX * canvasSize.x;
            
            redPoints[i] = ImVec2(x, ValueToY(color.x, canvasPos, canvasSize));
            greenPoints[i] = ImVec2(x, ValueToY(color.y, canvasPos, canvasSize));
            bluePoints[i] = ImVec2(x, ValueToY(color.z, canvasPos, canvasSize));
            if (m_showAlpha)
            {
                alphaPoints[i] = ImVec2(x, ValueToY(color.w, canvasPos, canvasSize));
            }
        }
              // Clip polylines to canvas bounds
        ImVec2 clipMin = canvasPos;
        ImVec2 clipMax = ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);
        
        // Draw curve for each color component
        drawList->PushClipRect(clipMin, clipMax, true);
        
        // Draw all curves with a single clip rectangle push/pop for better performance
        drawList->AddPolyline(redPoints.data(), numSamples, 
                           ImGui::ColorConvertFloat4ToU32(m_redColor), 0, curveThickness);
        
        drawList->AddPolyline(greenPoints.data(), numSamples, 
                           ImGui::ColorConvertFloat4ToU32(m_greenColor), 0, curveThickness);
        
        drawList->AddPolyline(bluePoints.data(), numSamples, 
                           ImGui::ColorConvertFloat4ToU32(m_blueColor), 0, curveThickness);
        
        if (m_showAlpha)
        {
            drawList->AddPolyline(alphaPoints.data(), numSamples, 
                               ImGui::ColorConvertFloat4ToU32(m_alphaColor), 0, curveThickness);
        }
        
        drawList->PopClipRect();
    }

    void ColorCurveImGuiEditor::DrawKeys(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize)
    {
        const size_t keyCount = m_colorCurve.GetKeyCount();
        
        for (size_t i = 0; i < keyCount; ++i)
        {
            const ColorKey& key = m_colorCurve.GetKey(i);
            
            float x = TimeToX(key.time, canvasPos, canvasSize);
            
            bool isSelected = (i == m_selectedKeyIndex);
            bool isHovered = (i == m_hoveredKeyIndex);
            
            // Draw key points for each color component
            float redY = ValueToY(key.color.x, canvasPos, canvasSize);
            float greenY = ValueToY(key.color.y, canvasPos, canvasSize);
            float blueY = ValueToY(key.color.z, canvasPos, canvasSize);
            float alphaY = ValueToY(key.color.w, canvasPos, canvasSize);
            
            // Check if any component is in view before drawing
            bool redInView = (key.color.x >= m_viewMinY && key.color.x <= m_viewMaxY);
            bool greenInView = (key.color.y >= m_viewMinY && key.color.y <= m_viewMaxY);
            bool blueInView = (key.color.z >= m_viewMinY && key.color.z <= m_viewMaxY);
            bool alphaInView = (key.color.w >= m_viewMinY && key.color.w <= m_viewMaxY);
            
            ImU32 keyColorRed = ImGui::ColorConvertFloat4ToU32(isSelected ? m_selectedKeyColor : m_redColor);
            ImU32 keyColorGreen = ImGui::ColorConvertFloat4ToU32(isSelected ? m_selectedKeyColor : m_greenColor);
            ImU32 keyColorBlue = ImGui::ColorConvertFloat4ToU32(isSelected ? m_selectedKeyColor : m_blueColor);
            ImU32 keyColorAlpha = ImGui::ColorConvertFloat4ToU32(isSelected ? m_selectedKeyColor : m_alphaColor);
              // Draw the key points
            if (redInView)
            {
                drawList->AddCircleFilled(ImVec2(x, redY), KEY_SIZE/2, keyColorRed);
            }
            
            if (greenInView)
            {
                drawList->AddCircleFilled(ImVec2(x, greenY), KEY_SIZE/2, keyColorGreen);
            }
            
            if (blueInView)
            {
                drawList->AddCircleFilled(ImVec2(x, blueY), KEY_SIZE/2, keyColorBlue);
            }
            
            if (m_showAlpha && alphaInView)
            {
                drawList->AddCircleFilled(ImVec2(x, alphaY), KEY_SIZE/2, keyColorAlpha);
            }
            
            // Draw outlines for selected or hovered keys
            if (isSelected || isHovered)
            {
                float outlineSize = isSelected ? 2.0f : 1.0f;
                ImU32 outlineColor = ImGui::ColorConvertFloat4ToU32(isSelected ? m_selectedKeyColor : m_keyColor);
                
                drawList->AddCircle(ImVec2(x, redY), KEY_SIZE/2 + 1, outlineColor, 0, outlineSize);
                drawList->AddCircle(ImVec2(x, greenY), KEY_SIZE/2 + 1, outlineColor, 0, outlineSize);
                drawList->AddCircle(ImVec2(x, blueY), KEY_SIZE/2 + 1, outlineColor, 0, outlineSize);
                
                if (m_showAlpha)
                {
                    drawList->AddCircle(ImVec2(x, alphaY), KEY_SIZE/2 + 1, outlineColor, 0, outlineSize);
                }
            }
        }
    }

    void ColorCurveImGuiEditor::DrawTangents(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize)
    {
        const size_t keyCount = m_colorCurve.GetKeyCount();
        
        for (size_t i = 0; i < keyCount; ++i)
        {
            const ColorKey& key = m_colorCurve.GetKey(i);
            
            // Skip if not in manual tangent mode
            if (key.tangentMode != 1)
            {
                continue;
            }
            
            float x = TimeToX(key.time, canvasPos, canvasSize);
            
            bool isSelected = (i == m_selectedKeyIndex);
            
            // Get point positions
            float redY = ValueToY(key.color.x, canvasPos, canvasSize);
            float greenY = ValueToY(key.color.y, canvasPos, canvasSize);
            float blueY = ValueToY(key.color.z, canvasPos, canvasSize);
            float alphaY = ValueToY(key.color.w, canvasPos, canvasSize);
            
            // Calculate tangent handle positions
            // Scale by 0.1 to make the handles a reasonable distance
            float tangentScale = canvasSize.x * 0.1f;
            
            // In-tangent handles (left)
            float redInX = x - key.inTangent.x * tangentScale;
            float greenInX = x - key.inTangent.y * tangentScale;
            float blueInX = x - key.inTangent.z * tangentScale;
            float alphaInX = x - key.inTangent.w * tangentScale;
            
            float redInY = redY - key.inTangent.x * tangentScale;
            float greenInY = greenY - key.inTangent.y * tangentScale;
            float blueInY = blueY - key.inTangent.z * tangentScale;
            float alphaInY = alphaY - key.inTangent.w * tangentScale;
            
            // Out-tangent handles (right)
            float redOutX = x + key.outTangent.x * tangentScale;
            float greenOutX = x + key.outTangent.y * tangentScale;
            float blueOutX = x + key.outTangent.z * tangentScale;
            float alphaOutX = x + key.outTangent.w * tangentScale;
            
            float redOutY = redY + key.outTangent.x * tangentScale;
            float greenOutY = greenY + key.outTangent.y * tangentScale;
            float blueOutY = blueY + key.outTangent.z * tangentScale;
            float alphaOutY = alphaY + key.outTangent.w * tangentScale;
            
            // Draw tangent lines
            ImU32 redColor = ImGui::ColorConvertFloat4ToU32(m_redColor);
            ImU32 greenColor = ImGui::ColorConvertFloat4ToU32(m_greenColor);
            ImU32 blueColor = ImGui::ColorConvertFloat4ToU32(m_blueColor);
            ImU32 alphaColor = ImGui::ColorConvertFloat4ToU32(m_alphaColor);
            
            // In-tangents
            drawList->AddLine(ImVec2(x, redY), ImVec2(redInX, redInY), redColor, 1.0f);
            drawList->AddLine(ImVec2(x, greenY), ImVec2(greenInX, greenInY), greenColor, 1.0f);
            drawList->AddLine(ImVec2(x, blueY), ImVec2(blueInX, blueInY), blueColor, 1.0f);
            if (m_showAlpha)
            {
                drawList->AddLine(ImVec2(x, alphaY), ImVec2(alphaInX, alphaInY), alphaColor, 1.0f);
            }
            
            // Out-tangents
            drawList->AddLine(ImVec2(x, redY), ImVec2(redOutX, redOutY), redColor, 1.0f);
            drawList->AddLine(ImVec2(x, greenY), ImVec2(greenOutX, greenOutY), greenColor, 1.0f);
            drawList->AddLine(ImVec2(x, blueY), ImVec2(blueOutX, blueOutY), blueColor, 1.0f);
            if (m_showAlpha)
            {
                drawList->AddLine(ImVec2(x, alphaY), ImVec2(alphaOutX, alphaOutY), alphaColor, 1.0f);
            }
            
            // Draw tangent handles
            ImU32 handleColor = ImGui::ColorConvertFloat4ToU32(isSelected ? m_selectedKeyColor : m_tangentHandleColor);
            
            // In-tangent handles
            drawList->AddCircleFilled(ImVec2(redInX, redInY), TANGENT_SIZE/2, redColor);
            drawList->AddCircleFilled(ImVec2(greenInX, greenInY), TANGENT_SIZE/2, greenColor);
            drawList->AddCircleFilled(ImVec2(blueInX, blueInY), TANGENT_SIZE/2, blueColor);
            if (m_showAlpha)
            {
                drawList->AddCircleFilled(ImVec2(alphaInX, alphaInY), TANGENT_SIZE/2, alphaColor);
            }
            
            // Out-tangent handles
            drawList->AddCircleFilled(ImVec2(redOutX, redOutY), TANGENT_SIZE/2, redColor);
            drawList->AddCircleFilled(ImVec2(greenOutX, greenOutY), TANGENT_SIZE/2, greenColor);
            drawList->AddCircleFilled(ImVec2(blueOutX, blueOutY), TANGENT_SIZE/2, blueColor);
            if (m_showAlpha)
            {
                drawList->AddCircleFilled(ImVec2(alphaOutX, alphaOutY), TANGENT_SIZE/2, alphaColor);
            }
        }
    }    void ColorCurveImGuiEditor::DrawGrid(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize)
    {        ImU32 gridColor = ImGui::ColorConvertFloat4ToU32(m_gridColor);
        ImU32 mainAxisColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
            m_gridColor.x * 1.8f, 
            m_gridColor.y * 1.8f, 
            m_gridColor.z * 1.8f, 
            m_gridColor.w * 1.8f
        ));
        ImU32 secondaryAxisColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
            m_gridColor.x * 1.4f, 
            m_gridColor.y * 1.4f, 
            m_gridColor.z * 1.4f, 
            m_gridColor.w * 1.4f
        ));
        
        // Calculate the grid spacing based on zoom level
        const float baseGridStep = 0.1f; // Base grid step at zoom level 1
        float adaptiveStep = baseGridStep;
        
        // Adjust grid density based on zoom level
        if (m_zoomLevel < 0.5f)
            adaptiveStep = 0.25f;
        else if (m_zoomLevel > 3.0f)
            adaptiveStep = 0.05f;
        else if (m_zoomLevel > 6.0f)
            adaptiveStep = 0.025f;
            
        // Draw horizontal grid lines
        if (m_showHorizontalGrid)
        {
            // Calculate grid lines positions
            float viewHeight = m_viewMaxY - m_viewMinY;
            
            // Find the starting grid line position
            float startValue = std::floor(m_viewMinY / adaptiveStep) * adaptiveStep;
            
            for (float value = startValue; value <= m_viewMaxY; value += adaptiveStep)
            {
                if (value < m_viewMinY || value > m_viewMaxY)
                    continue;
                
                float y = ValueToY(value, canvasPos, canvasSize);
                  // Use main axis color for the 0.0, 0.5, and 1.0 lines
                bool isMainAxis = std::abs(value) < 0.001f || std::abs(value - 1.0f) < 0.001f;
                bool isSecondaryAxis = std::abs(value - 0.5f) < 0.001f || std::abs(value - 0.25f) < 0.001f || std::abs(value - 0.75f) < 0.001f;
                
                ImU32 lineColor;
                float lineThickness;
                
                if (isMainAxis) {
                    lineColor = mainAxisColor;
                    lineThickness = 1.5f;
                } else if (isSecondaryAxis) {
                    lineColor = secondaryAxisColor;
                    lineThickness = 1.2f;
                } else {
                    lineColor = gridColor;
                    lineThickness = 1.0f;
                }
                    
                drawList->AddLine(
                    ImVec2(canvasPos.x, y), 
                    ImVec2(canvasPos.x + canvasSize.x, y), 
                    lineColor,
                    lineThickness
                );
            }
        }
        
        // Draw vertical grid lines
        if (m_showVerticalGrid)
        {
            // Calculate grid lines positions
            float viewWidth = m_viewMaxX - m_viewMinX;
            
            // Find the starting grid line position
            float startTime = std::floor(m_viewMinX / adaptiveStep) * adaptiveStep;
            
            for (float time = startTime; time <= m_viewMaxX; time += adaptiveStep)
            {
                if (time < m_viewMinX || time > m_viewMaxX)
                    continue;
                    
                float x = TimeToX(time, canvasPos, canvasSize);
                
                // Use main axis color for the 0.0, 0.5, and 1.0 lines
                bool isMainAxis = std::abs(time) < 0.001f || std::abs(time - 0.5f) < 0.001f || std::abs(time - 1.0f) < 0.001f;
                
                drawList->AddLine(
                    ImVec2(x, canvasPos.y), 
                    ImVec2(x, canvasPos.y + canvasSize.y), 
                    isMainAxis ? mainAxisColor : gridColor,
                    isMainAxis ? 1.5f : 1.0f
                );
            }
        }
    }    void ColorCurveImGuiEditor::DrawColorPreview(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize)
    {
        // Draw preview at the bottom
        const float previewHeight = 20.0f;
        
        if (canvasSize.y <= previewHeight)
            return;
            
        const float previewY = canvasPos.y + canvasSize.y - previewHeight;
        
        // Use a lower number of segments for the preview to improve performance
        // We don't need as many segments for the preview as for the main curve
        const int numSegments = static_cast<int>(std::min(canvasSize.x / 4.0f, 128.0f));
        
        if (numSegments <= 1)
            return;
            
        // Precompute values that are constant within the loop
        const float segmentWidth = canvasSize.x / static_cast<float>(numSegments);
        const float timeRange = m_viewMaxX - m_viewMinX;
        const float invNumSegments = 1.0f / static_cast<float>(numSegments - 1);
        
        // Use push/pop clip rect only once for better performance
        drawList->PushClipRect(
            ImVec2(canvasPos.x, previewY),
            ImVec2(canvasPos.x + canvasSize.x, previewY + previewHeight),
            true
        );
        
        // For proper representation of color curve values in view space
        for (int i = 0; i < numSegments; ++i)
        {
            // Map sample point from screen to view space (optimized calculation)
            float normalizedX = static_cast<float>(i) * invNumSegments;
            float viewSpaceT = m_viewMinX + normalizedX * timeRange;
            
            // Clamp to valid curve range
            float t = std::max(0.0f, std::min(viewSpaceT, 1.0f));
            Vector4 color = m_colorCurve.Evaluate(t);
            
            float x0 = canvasPos.x + normalizedX * canvasSize.x;
            float x1 = x0 + segmentWidth + 0.5f; // Add 0.5 to avoid gaps between segments
            
            drawList->AddRectFilled(
                ImVec2(x0, previewY),
                ImVec2(x1, previewY + previewHeight),
                ImGui::ColorConvertFloat4ToU32(ColorToImVec4(color))
            );
        }
        
        // Draw a border around the preview
        drawList->AddRect(
            ImVec2(canvasPos.x, previewY),
            ImVec2(canvasPos.x + canvasSize.x, previewY + previewHeight),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.3f, 0.3f, 1.0f))
        );
        
        drawList->PopClipRect();
    }

    void ColorCurveImGuiEditor::DrawTooltip(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize)
    {
        ImVec2 mousePos = ImGui::GetMousePos();
        float time = XToTime(mousePos.x, canvasPos, canvasSize);
        
        // Clamp time to curve range
        time = std::max(0.0f, std::min(time, 1.0f));
        
        // Sample the curve at the mouse position
        Vector4 color = m_colorCurve.Evaluate(time);
        
        // Display a tooltip
        ImGui::BeginTooltip();
        ImGui::Text("Time: %.3f", time);
        ImGui::Text("R: %.3f", color.x);
        ImGui::Text("G: %.3f", color.y);
        ImGui::Text("B: %.3f", color.z);
        if (m_showAlpha)
        {
            ImGui::Text("A: %.3f", color.w);
        }
        
        // Draw color swatch in the tooltip
        ImVec2 swatchSize(80, 20);
        ImVec2 swatchPos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##colorswatch", swatchSize);
        
        drawList->AddRectFilled(
            swatchPos,
            ImVec2(swatchPos.x + swatchSize.x, swatchPos.y + swatchSize.y),
            ImGui::ColorConvertFloat4ToU32(ColorToImVec4(color))
        );
        
        ImGui::EndTooltip();
    }    float ColorCurveImGuiEditor::TimeToX(float time, const ImVec2& canvasPos, const ImVec2& canvasSize) const
    {
        if (m_viewMaxX == m_viewMinX) return canvasPos.x;
        return canvasPos.x + (time - m_viewMinX) / (m_viewMaxX - m_viewMinX) * canvasSize.x;
    }

    float ColorCurveImGuiEditor::ValueToY(float value, const ImVec2& canvasPos, const ImVec2& canvasSize) const
    {
        if (m_viewMaxY == m_viewMinY) return canvasPos.y;
        // Y is inverted in screen space (0 is top, canvasSize.y is bottom)
        return canvasPos.y + (1.0f - (value - m_viewMinY) / (m_viewMaxY - m_viewMinY)) * canvasSize.y;
    }
    
    float ColorCurveImGuiEditor::XToTime(float x, const ImVec2& canvasPos, const ImVec2& canvasSize) const
    {
        if (canvasSize.x <= 0.0f) return m_viewMinX;
        float t = (x - canvasPos.x) / canvasSize.x;
        return m_viewMinX + t * (m_viewMaxX - m_viewMinX);
    }
    
    float ColorCurveImGuiEditor::YToValue(float y, const ImVec2& canvasPos, const ImVec2& canvasSize) const
    {
        if (canvasSize.y <= 0.0f) return m_viewMinY;
        // Y is inverted in screen space (0 is top, canvasSize.y is bottom)
        float v = 1.0f - (y - canvasPos.y) / canvasSize.y;
        return m_viewMinY + v * (m_viewMaxY - m_viewMinY);
    }

    void ColorCurveImGuiEditor::HandleInteraction(const ImVec2& canvasPos, const ImVec2& canvasSize, bool& modified)
    {
        ImVec2 mousePos = ImGui::GetMousePos();
        bool isHovered = ImGui::IsItemHovered();
        
        // Reset hovered key index
        m_hoveredKeyIndex = static_cast<size_t>(-1);
        
        // Handle key selection and dragging
        if (isHovered)
        {
            // Find the closest key point to the mouse cursor
            size_t closestKeyIndex;
            bool foundKey = FindClosestKey(mousePos, canvasPos, canvasSize, closestKeyIndex);
            
            if (foundKey)
            {
                m_hoveredKeyIndex = closestKeyIndex;
                
                if (ImGui::IsMouseClicked(0))
                {
                    m_selectedKeyIndex = closestKeyIndex;
                    m_draggingKey = true;
                }
            }
            else if (ImGui::IsMouseClicked(0) && m_showTangents)
            {
                // Check if we clicked on a tangent handle
                size_t keyIndex;
                bool isInTangent;
                int component;
                if (FindHoveredTangent(mousePos, canvasPos, canvasSize, keyIndex, isInTangent, component))
                {
                    m_selectedKeyIndex = keyIndex;
                    m_draggingTangent = true;
                    m_tangentIsIn = isInTangent;
                    m_draggedComponent = component;
                }
            }
            else if (ImGui::IsMouseClicked(0) && !m_draggingKey && !m_draggingTangent)
            {
                // If clicking empty space, clear selection
                m_selectedKeyIndex = static_cast<size_t>(-1);
            }
        }
        
        // Handle dragging of keys
        if (m_draggingKey && ImGui::IsMouseDown(0))
        {
            if (m_selectedKeyIndex != static_cast<size_t>(-1) && m_selectedKeyIndex < m_colorCurve.GetKeyCount())
            {
                ColorKey key = m_colorCurve.GetKey(m_selectedKeyIndex);
                
                // Calculate new time value
                float time = XToTime(mousePos.x, canvasPos, canvasSize);
                time = std::max(0.0f, std::min(time, 1.0f));
                
                // Apply time snapping if enabled
                time = SnapTime(time);
                
                // Don't allow the first or last key to move horizontally if there are only 2 keys
                if ((m_colorCurve.GetKeyCount() > 2) || 
                    (m_selectedKeyIndex > 0 && m_selectedKeyIndex < m_colorCurve.GetKeyCount() - 1))
                {
                    key.time = time;
                }
                
                // Calculate which component is being dragged based on closest distance
                const float thresholdDistance = 10.0f;
                
                float redY = ValueToY(key.color.x, canvasPos, canvasSize);
                float greenY = ValueToY(key.color.y, canvasPos, canvasSize);
                float blueY = ValueToY(key.color.z, canvasPos, canvasSize);
                float alphaY = ValueToY(key.color.w, canvasPos, canvasSize);
                
                float redDist = std::abs(mousePos.y - redY);
                float greenDist = std::abs(mousePos.y - greenY);
                float blueDist = std::abs(mousePos.y - blueY);
                float alphaDist = std::abs(mousePos.y - alphaY);
                
                // Find the closest component
                float minDist = redDist;
                int component = 0;
                
                if (greenDist < minDist)
                {
                    minDist = greenDist;
                    component = 1;
                }
                
                if (blueDist < minDist)
                {
                    minDist = blueDist;
                    component = 2;
                }
                
                if (m_showAlpha && alphaDist < minDist)
                {
                    minDist = alphaDist;
                    component = 3;
                }
                
                // Only modify the component if it's close to the mouse
                if (minDist <= thresholdDistance)
                {
                    float value = YToValue(mousePos.y, canvasPos, canvasSize);
                    value = std::max(0.0f, std::min(value, 1.0f));
                    
                    // Apply value snapping if enabled
                    value = SnapValue(value);
                    
                    // Update the appropriate component
                    if (component == 0)
                    {
                        key.color.x = value;
                    }
                    else if (component == 1)
                    {
                        key.color.y = value;
                    }
                    else if (component == 2)
                    {
                        key.color.z = value;
                    }
                    else if (component == 3)
                    {
                        key.color.w = value;
                    }
                }
                
                m_colorCurve.UpdateKey(m_selectedKeyIndex, key);
                modified = true;
            }
        }
        else if (m_draggingTangent && ImGui::IsMouseDown(0))
        {
            if (m_selectedKeyIndex != static_cast<size_t>(-1) && m_selectedKeyIndex < m_colorCurve.GetKeyCount())
            {
                ColorKey key = m_colorCurve.GetKey(m_selectedKeyIndex);
                
                // Convert to manual mode if not already
                if (key.tangentMode != 1)
                {
                    key.tangentMode = 1;
                }
                
                // Calculate new tangent value
                float x = mousePos.x;
                float y = mousePos.y;
                
                float keyX = TimeToX(key.time, canvasPos, canvasSize);
                float keyY = 0.0f;
                
                // Get Y position of the key point for the component being dragged
                if (m_draggedComponent == 0)
                {
                    keyY = ValueToY(key.color.x, canvasPos, canvasSize);
                }
                else if (m_draggedComponent == 1)
                {
                    keyY = ValueToY(key.color.y, canvasPos, canvasSize);
                }
                else if (m_draggedComponent == 2)
                {
                    keyY = ValueToY(key.color.z, canvasPos, canvasSize);
                }
                else if (m_draggedComponent == 3)
                {
                    keyY = ValueToY(key.color.w, canvasPos, canvasSize);
                }
                
                // Calculate tangent vector
                float dx = (x - keyX) / (canvasSize.x * 0.1f); // Scale back to tangent space
                float dy = (keyY - y) / (canvasSize.y * 0.1f); // Y is flipped
                
                if (m_tangentIsIn)
                {
                    dx = -dx; // Invert X for in-tangent
                }
                
                // Set the tangent for the dragged component
                if (m_draggedComponent == 0)
                {
                    if (m_tangentIsIn)
                        key.inTangent.x = dx;
                    else
                        key.outTangent.x = dx;
                }
                else if (m_draggedComponent == 1)
                {
                    if (m_tangentIsIn)
                        key.inTangent.y = dx;
                    else
                        key.outTangent.y = dx;
                }
                else if (m_draggedComponent == 2)
                {
                    if (m_tangentIsIn)
                        key.inTangent.z = dx;
                    else
                        key.outTangent.z = dx;
                }
                else if (m_draggedComponent == 3)
                {
                    if (m_tangentIsIn)
                        key.inTangent.w = dx;
                    else
                        key.outTangent.w = dx;
                }
                
                m_colorCurve.UpdateKey(m_selectedKeyIndex, key);
                modified = true;
            }
        }
        
        // End dragging when mouse button is released
        if (!ImGui::IsMouseDown(0))
        {
            m_draggingKey = false;
            m_draggingTangent = false;
        }
    }

    ImVec4 ColorCurveImGuiEditor::ColorToImVec4(const Vector4& color) const
    {
        return ImVec4(color.x, color.y, color.z, color.w);
    }    bool ColorCurveImGuiEditor::FindClosestKey(const ImVec2& mousePos, const ImVec2& canvasPos, 
                                          const ImVec2& canvasSize, size_t& outKeyIndex, 
                                          float maxDistance) const
    {
        const size_t keyCount = m_colorCurve.GetKeyCount();
        
        // Early out if no keys
        if (keyCount == 0)
            return false;
            
        // Square the max distance for faster comparison (avoiding square root)
        float closestDist = maxDistance * maxDistance;
        size_t closestIndex = static_cast<size_t>(-1);
        
        // First check: do a quick test based on X coordinate only 
        // This avoids calculating Y coordinates for keys far away horizontally
        float mouseX = mousePos.x;
        float xThreshold = maxDistance * 2.0f; // Be a bit more generous with horizontal distance
        
        for (size_t i = 0; i < keyCount; ++i)
        {
            const ColorKey& key = m_colorCurve.GetKey(i);
            float x = TimeToX(key.time, canvasPos, canvasSize);
            
            // If key is too far horizontally, skip the detailed distance calculation
            if (std::abs(x - mouseX) > xThreshold)
                continue;
                
            // Calculate distance to red, green, blue, and alpha points
            float redY = ValueToY(key.color.x, canvasPos, canvasSize);
            float greenY = ValueToY(key.color.y, canvasPos, canvasSize);
            float blueY = ValueToY(key.color.z, canvasPos, canvasSize);
            float alphaY = ValueToY(key.color.w, canvasPos, canvasSize);
            
            // Use squared distance for efficiency (avoid square root)
            float dx = x - mousePos.x;
            float dx2 = dx * dx;
            
            float redDist = dx2 + std::pow(redY - mousePos.y, 2);
            float greenDist = dx2 + std::pow(greenY - mousePos.y, 2);
            float blueDist = dx2 + std::pow(blueY - mousePos.y, 2);
            float alphaDist = dx2 + std::pow(alphaY - mousePos.y, 2);
            
            // Find the minimum distance
            float minDist = redDist;
            minDist = std::min(minDist, greenDist);
            minDist = std::min(minDist, blueDist);
            
            if (m_showAlpha)
            {
                minDist = std::min(minDist, alphaDist);
            }
            
            // Update closest key if this one is closer
            if (minDist < closestDist)
            {
                closestDist = minDist;
                closestIndex = i;
            }
        }
        
        if (closestIndex != static_cast<size_t>(-1))
        {
            outKeyIndex = closestIndex;
            return true;
        }
        
        return false;
    }    bool ColorCurveImGuiEditor::FindHoveredTangent(const ImVec2& mousePos, const ImVec2& canvasPos, 
                               const ImVec2& canvasSize, size_t& outKeyIndex, 
                               bool& outIsInTangent, int& outComponent, 
                               float maxDistance) const
    {
        // Early out if not showing tangents
        if (!m_showTangents)
            return false;
            
        const size_t keyCount = m_colorCurve.GetKeyCount();
        
        // Early out if no keys
        if (keyCount == 0)
            return false;
            
        // Square the distance for faster comparison
        float closestDist = maxDistance * maxDistance;
        bool foundTangent = false;
        
        // Quick test based on screen area where tangents are likely to be
        float quickTestDist = maxDistance * 5.0f; // More generous for quick test
        
        for (size_t i = 0; i < keyCount; ++i)
        {
            const ColorKey& key = m_colorCurve.GetKey(i);
            
            // Skip if not in manual tangent mode
            if (key.tangentMode != 1)
                continue;
            
            float keyX = TimeToX(key.time, canvasPos, canvasSize);
            
            // Quick check if mouse is within reasonable distance of key's X position
            // Tangents are always horizontally aligned with keys
            if (std::abs(keyX - mousePos.x) > quickTestDist)
                continue;
                
            // Check for red, green, blue, and alpha tangent handles
            for (int comp = 0; comp < 4; ++comp)
            {
                // Skip alpha if not showing
                if (comp == 3 && !m_showAlpha)
                    continue;
                
                float keyY = 0.0f;
                Vector4 inTangent, outTangent;
                
                // Get key position and tangents for this component
                switch (comp)
                {
                    case 0: // Red
                        keyY = ValueToY(key.color.x, canvasPos, canvasSize);
                        inTangent = Vector4(key.inTangent.x, 0.0f, 0.0f, 0.0f);
                        outTangent = Vector4(key.outTangent.x, 0.0f, 0.0f, 0.0f);
                        break;
                    
                    case 1: // Green
                        keyY = ValueToY(key.color.y, canvasPos, canvasSize);
                        inTangent = Vector4(key.inTangent.y, 0.0f, 0.0f, 0.0f);
                        outTangent = Vector4(key.outTangent.y, 0.0f, 0.0f, 0.0f);
                        break;
                    
                    case 2: // Blue
                        keyY = ValueToY(key.color.z, canvasPos, canvasSize);
                        inTangent = Vector4(key.inTangent.z, 0.0f, 0.0f, 0.0f);
                        outTangent = Vector4(key.outTangent.z, 0.0f, 0.0f, 0.0f);
                        break;
                    
                    case 3: // Alpha
                        keyY = ValueToY(key.color.w, canvasPos, canvasSize);
                        inTangent = Vector4(key.inTangent.w, 0.0f, 0.0f, 0.0f);
                        outTangent = Vector4(key.outTangent.w, 0.0f, 0.0f, 0.0f);
                        break;
                }
                
                // Calculate in-tangent handle position
                float tangentScale = canvasSize.x * 0.1f; // Scale for display
                
                float inTangentX = keyX - inTangent.x * tangentScale;
                float inTangentY = keyY - inTangent.y * tangentScale;
                
                // Calculate distance to in-tangent handle
                float inDist = std::pow(inTangentX - mousePos.x, 2) + std::pow(inTangentY - mousePos.y, 2);
                
                if (inDist < closestDist)
                {
                    closestDist = inDist;
                    outKeyIndex = i;
                    outIsInTangent = true;
                    outComponent = comp;
                    foundTangent = true;
                }
                
                // Calculate out-tangent handle position
                float outTangentX = keyX + outTangent.x * tangentScale;
                float outTangentY = keyY - outTangent.y * tangentScale;
                
                // Calculate distance to out-tangent handle
                float outDist = std::pow(outTangentX - mousePos.x, 2) + std::pow(outTangentY - mousePos.y, 2);
                
                if (outDist < closestDist)
                {
                    closestDist = outDist;
                    outKeyIndex = i;
                    outIsInTangent = false;
                    outComponent = comp;
                    foundTangent = true;
                }
            }
        }
        
        return foundTangent;
    }

    float ColorCurveImGuiEditor::SnapTime(float time) const
    {
        if (m_timeSnapIncrement <= 0.0f)
        {
            return time;
        }
        
        return std::round(time / m_timeSnapIncrement) * m_timeSnapIncrement;
    }

    float ColorCurveImGuiEditor::SnapValue(float value) const
    {
        if (m_valueSnapIncrement <= 0.0f)
        {
            return value;
        }
        
        return std::round(value / m_valueSnapIncrement) * m_valueSnapIncrement;
    }

    bool ColorCurveImGuiEditor::ResetAllTangents()
    {
        if (m_colorCurve.GetKeyCount() <= 0)
        {
            return false;
        }
        
        // Set all keys to auto tangent mode
        bool modified = false;
        
        for (size_t i = 0; i < m_colorCurve.GetKeyCount(); ++i)
        {
            ColorKey key = m_colorCurve.GetKey(i);
            if (key.tangentMode != 0)
            {
                key.tangentMode = 0;
                m_colorCurve.UpdateKey(i, key);
                modified = true;
            }
        }
        
        if (modified)
        {
            m_colorCurve.CalculateTangents();
        }
        
        return modified;
    }

    bool ColorCurveImGuiEditor::DistributeKeysEvenly()
    {
        const size_t keyCount = m_colorCurve.GetKeyCount();
        if (keyCount <= 2)
        {
            return false;
        }
        
        // Get all keys
        std::vector<ColorKey> keys;
        keys.reserve(keyCount);
        
        for (size_t i = 0; i < keyCount; ++i)
        {
            keys.push_back(m_colorCurve.GetKey(i));
        }
        
        // Sort by time (should already be sorted, but just in case)
        std::sort(keys.begin(), keys.end(), [](const ColorKey& a, const ColorKey& b) {
            return a.time < b.time;
        });
        
        // Keep first and last key times fixed, redistribute others evenly
        const float startTime = keys.front().time;
        const float endTime = keys.back().time;
        const float timeRange = endTime - startTime;
        const float step = timeRange / (keyCount - 1);
        
        bool modified = false;
        
        for (size_t i = 1; i < keyCount - 1; ++i)
        {
            const float newTime = startTime + i * step;
            if (keys[i].time != newTime)
            {
                keys[i].time = newTime;
                m_colorCurve.UpdateKey(i, keys[i]);
                modified = true;
            }
        }
        
        if (modified)
        {
            m_colorCurve.CalculateTangents();
        }
        
        return modified;
    }    void ColorCurveImGuiEditor::HandleContextMenu(const ImVec2& canvasPos, const ImVec2& canvasSize, bool& modified)
    {
        if (ImGui::BeginPopupContextItem("ColorCurveContextMenu"))
        {
            ImGui::Text("Color Curve Actions");
            ImGui::Separator();
            
            if (ImGui::MenuItem("Add Key at Cursor"))
            {
                ImVec2 mousePos = ImGui::GetMousePos();
                float time = XToTime(mousePos.x, canvasPos, canvasSize);
                
                // Clamp time to curve range
                time = std::max(0.0f, std::min(time, 1.0f));
                
                // Sample the curve at the mouse position to get the color
                Vector4 color = m_colorCurve.Evaluate(time);
                
                // Add the new key
                size_t newIndex = m_colorCurve.AddKey(time, color);
                m_selectedKeyIndex = newIndex;
                modified = true;
            }
            
            ImGui::Separator();
            
            if (ImGui::MenuItem("Reset View"))
            {
                ResetView();
            }
            
            if (ImGui::MenuItem("Zoom to Selection", nullptr, false, m_selectedKeyIndex != static_cast<size_t>(-1)))
            {
                if (m_selectedKeyIndex != static_cast<size_t>(-1))
                {
                    const ColorKey& key = m_colorCurve.GetKey(m_selectedKeyIndex);
                    
                    // Zoom in on the selected key with padding
                    float padding = 0.2f;
                    m_viewMinX = key.time - padding;
                    m_viewMaxX = key.time + padding;
                    
                    // Calculate vertical range based on key colors
                    float minY = std::min({key.color.x, key.color.y, key.color.z, key.color.w}) - padding;
                    float maxY = std::max({key.color.x, key.color.y, key.color.z, key.color.w}) + padding;
                    
                    m_viewMinY = minY;
                    m_viewMaxY = maxY;
                    
                    // Update zoom level
                    m_zoomLevel = 4.0f;
                }
            }
            
            ImGui::Separator();
            
            if (ImGui::MenuItem("Reset All Tangents", nullptr, false, m_colorCurve.GetKeyCount() > 0))
            {
                if (ResetAllTangents())
                {
                    modified = true;
                }
            }
            
            if (ImGui::MenuItem("Distribute Keys Evenly", nullptr, false, m_colorCurve.GetKeyCount() > 2))
            {
                if (DistributeKeysEvenly())
                {
                    modified = true;
                }
            }
            
            ImGui::Separator();
            
            if (ImGui::BeginMenu("Display Options"))
            {
                ImGui::MenuItem("Show Alpha Channel", nullptr, &m_showAlpha);
                ImGui::MenuItem("Show Tangent Handles", nullptr, &m_showTangents);
                ImGui::MenuItem("Show Color Preview", nullptr, &m_showColorPreview);
                ImGui::MenuItem("Show Horizontal Grid", nullptr, &m_showHorizontalGrid);
                ImGui::MenuItem("Show Vertical Grid", nullptr, &m_showVerticalGrid);
                
                ImGui::Separator();
                
                // Curve thickness slider
                float thickness = m_curveThickness;
                if (ImGui::SliderFloat("Curve Thickness", &thickness, 1.0f, 5.0f, "%.1f"))
                {
                    m_curveThickness = thickness;
                }
                
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("Snapping"))
            {
                // Time snap settings
                float timeSnap = m_timeSnapIncrement;
                if (ImGui::SliderFloat("Time Snap", &timeSnap, 0.0f, 0.25f, timeSnap > 0.0f ? "%.3f" : "Off"))
                {
                    m_timeSnapIncrement = timeSnap;
                }
                
                // Value snap settings
                float valueSnap = m_valueSnapIncrement;
                if (ImGui::SliderFloat("Value Snap", &valueSnap, 0.0f, 0.25f, valueSnap > 0.0f ? "%.3f" : "Off"))
                {
                    m_valueSnapIncrement = valueSnap;
                }
                
                ImGui::EndMenu();
            }
            
            ImGui::EndPopup();
        }
    }

    void ColorCurveImGuiEditor::HandleZoomAndPan(const ImVec2& canvasPos, const ImVec2& canvasSize)
    {
        const ImVec2 mousePos = ImGui::GetMousePos();
        const bool isHovered = ImGui::IsItemHovered();
        
        // Handle panning with middle mouse button
        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        {
            m_draggingCanvas = true;
            m_dragStartPos = mousePos;
        }
        
        if (m_draggingCanvas && ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        {
            // Calculate delta in screen space
            const float deltaX = mousePos.x - m_dragStartPos.x;
            const float deltaY = mousePos.y - m_dragStartPos.y;
            
            // Convert to view space
            const float viewWidth = m_viewMaxX - m_viewMinX;
            const float viewHeight = m_viewMaxY - m_viewMinY;
            
            const float scaleX = viewWidth / canvasSize.x;
            const float scaleY = viewHeight / canvasSize.y;
            
            // Move the view (negative because drag moves the opposite way of the view)
            m_viewMinX -= deltaX * scaleX;
            m_viewMaxX -= deltaX * scaleX;
            m_viewMinY += deltaY * scaleY; // Y is flipped in screen space
            m_viewMaxY += deltaY * scaleY;
            
            // Update drag start position for next frame
            m_dragStartPos = mousePos;
        }
        
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        {
            m_draggingCanvas = false;
        }
        
        // Handle zooming with mouse wheel
        if (isHovered && ImGui::GetIO().MouseWheel != 0)
        {
            // Calculate cursor position in view space
            const float cursorTimePos = XToTime(mousePos.x, canvasPos, canvasSize);
            const float cursorValuePos = YToValue(mousePos.y, canvasPos, canvasSize);
            
            // Calculate zoom factor based on mouse wheel delta
            const float zoomFactor = ImGui::GetIO().MouseWheel > 0 ? 0.8f : 1.25f;
            
            // Update zoom level
            m_zoomLevel *= (ImGui::GetIO().MouseWheel > 0) ? 1.25f : 0.8f;
            m_zoomLevel = std::max(0.25f, std::min(m_zoomLevel, 10.0f)); // Limit zoom range
            
            // Adjust view size
            const float viewWidth = m_viewMaxX - m_viewMinX;
            const float viewHeight = m_viewMaxY - m_viewMinY;
            
            const float newViewWidth = viewWidth * zoomFactor;
            const float newViewHeight = viewHeight * zoomFactor;
            
            // Calculate position adjustment to zoom around cursor
            const float tRatio = (cursorTimePos - m_viewMinX) / viewWidth;
            const float vRatio = (cursorValuePos - m_viewMinY) / viewHeight;
            
            // Apply new view bounds, centered on cursor position
            m_viewMinX = cursorTimePos - tRatio * newViewWidth;
            m_viewMaxX = cursorTimePos + (1.0f - tRatio) * newViewWidth;
            m_viewMinY = cursorValuePos - vRatio * newViewHeight;
            m_viewMaxY = cursorValuePos + (1.0f - vRatio) * newViewHeight;
        }
    }

    void ColorCurveImGuiEditor::DrawOffscreenIndicators(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize)
    {
        const size_t keyCount = m_colorCurve.GetKeyCount();
        const float arrowSize = 10.0f;
        
        for (size_t i = 0; i < keyCount; ++i)
        {
            const ColorKey& key = m_colorCurve.GetKey(i);
            
            // Check if key is outside view
            bool isOutsideX = (key.time < m_viewMinX || key.time > m_viewMaxX);
            bool redOutsideY = (key.color.x < m_viewMinY || key.color.x > m_viewMaxY);
            bool greenOutsideY = (key.color.y < m_viewMinY || key.color.y > m_viewMaxY);
            bool blueOutsideY = (key.color.z < m_viewMinY || key.color.z > m_viewMaxY);
            bool alphaOutsideY = m_showAlpha && (key.color.w < m_viewMinY || key.color.w > m_viewMaxY);
            
            if (!isOutsideX && !redOutsideY && !greenOutsideY && !blueOutsideY && !alphaOutsideY)
            {
                continue; // Key is fully visible
            }
            
            // Calculate clamped position
            float x = TimeToX(key.time, canvasPos, canvasSize);
            
            // Clamp X position to canvas edges with a small margin
            x = std::max(canvasPos.x + arrowSize, std::min(x, canvasPos.x + canvasSize.x - arrowSize));
            
            // Draw indicators for each component that's off-screen
            DrawComponentOffscreenIndicator(drawList, key.color.x, x, canvasPos, canvasSize, m_redColor, arrowSize);
            DrawComponentOffscreenIndicator(drawList, key.color.y, x, canvasPos, canvasSize, m_greenColor, arrowSize);
            DrawComponentOffscreenIndicator(drawList, key.color.z, x, canvasPos, canvasSize, m_blueColor, arrowSize);
            
            if (m_showAlpha)
            {
                DrawComponentOffscreenIndicator(drawList, key.color.w, x, canvasPos, canvasSize, m_alphaColor, arrowSize);
            }
        }
    }

    void ColorCurveImGuiEditor::DrawComponentOffscreenIndicator(ImDrawList* drawList, float value, float x, 
                                                          const ImVec2& canvasPos, const ImVec2& canvasSize, 
                                                          const ImVec4& color, float arrowSize)
    {
        if (value >= m_viewMinY && value <= m_viewMaxY)
        {
            return; // Component is visible
        }
        
        ImU32 arrowColor = ImGui::ColorConvertFloat4ToU32(color);
        
        if (value < m_viewMinY)
        {
            // Component is below the visible area, draw down arrow
            float y = canvasPos.y + canvasSize.y - arrowSize * 2;
            
            drawList->AddTriangleFilled(
                ImVec2(x, y + arrowSize * 2),
                ImVec2(x - arrowSize, y),
                ImVec2(x + arrowSize, y),
                arrowColor
            );
        }
        else
        {
            // Component is above the visible area, draw up arrow
            float y = canvasPos.y + arrowSize;
            
            drawList->AddTriangleFilled(
                ImVec2(x, y - arrowSize),
                ImVec2(x - arrowSize, y + arrowSize),
                ImVec2(x + arrowSize, y + arrowSize),
                arrowColor
            );
        }
    }

    void ColorCurveImGuiEditor::DrawTimeLabels(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize)
    {
        ImU32 textColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
        
        // Calculate the grid spacing based on zoom level
        const float baseGridStep = 0.1f; // Base grid step at zoom level 1
        float adaptiveStep = baseGridStep;
        
        // Adjust grid density based on zoom level
        if (m_zoomLevel < 0.5f)
            adaptiveStep = 0.25f;
        else if (m_zoomLevel > 3.0f)
            adaptiveStep = 0.05f;
        else if (m_zoomLevel > 6.0f)
            adaptiveStep = 0.025f;
        
        // Find the starting grid line position
        float startTime = std::floor(m_viewMinX / adaptiveStep) * adaptiveStep;
        
        for (float time = startTime; time <= m_viewMaxX; time += adaptiveStep)
        {
            if (time < m_viewMinX || time > m_viewMaxX)
                continue;
                
            float x = TimeToX(time, canvasPos, canvasSize);
            
            // Only draw labels at key positions (0.0, 0.5, 1.0) or at major divisions
            bool isKeyPosition = 
                std::abs(time) < 0.001f || 
                std::abs(time - 0.5f) < 0.001f || 
                std::abs(time - 1.0f) < 0.001f ||
                std::abs(time + 0.5f) < 0.001f ||
                std::abs(time + 1.0f) < 0.001f ||
                std::abs(std::fmod(time, 0.5f)) < 0.001f;
            
            // Format time value string
            char label[16];
            if (isKeyPosition)
            {
                // Ticks at key positions
                drawList->AddLine(
                    ImVec2(x, canvasPos.y - 5),
                    ImVec2(x, canvasPos.y),
                    textColor,
                    1.5f
                );
                  // Format with better precision for key positions
                if (std::abs(time) < 0.001f)
                    sprintf(label, "0.0");
                else if (std::abs(time - 0.5f) < 0.001f)
                    sprintf(label, "0.5");
                else if (std::abs(time - 1.0f) < 0.001f)
                    sprintf(label, "1.0");
                else if (std::abs(time + 0.5f) < 0.001f)
                    sprintf(label, "-0.5");
                else if (std::abs(time + 1.0f) < 0.001f)
                    sprintf(label, "-1.0");
                else
                    sprintf(label, "%.2f", time);
                ImVec2 textSize = ImGui::CalcTextSize(label);
                drawList->AddText(
                    ImVec2(x - textSize.x / 2, canvasPos.y - textSize.y - 8),
                    textColor,
                    label
                );
            }
            else if (std::fmod(std::abs(time), 0.2f) < 0.001f)
            {
                // Smaller ticks at intermediate positions
                drawList->AddLine(
                    ImVec2(x, canvasPos.y - 3),
                    ImVec2(x, canvasPos.y),
                    textColor,
                    1.0f
                );
            }
        }
    }

    void ColorCurveImGuiEditor::DrawValueLabels(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize)
    {
        ImU32 textColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
        
        // Calculate the grid spacing based on zoom level
        const float baseGridStep = 0.1f; // Base grid step at zoom level 1
        float adaptiveStep = baseGridStep;
        
        // Adjust grid density based on zoom level
        if (m_zoomLevel < 0.5f)
            adaptiveStep = 0.25f;
        else if (m_zoomLevel > 3.0f)
            adaptiveStep = 0.05f;
        else if (m_zoomLevel > 6.0f)
            adaptiveStep = 0.025f;
        
        // Find the starting grid line position
        float startValue = std::floor(m_viewMinY / adaptiveStep) * adaptiveStep;
        
        for (float value = startValue; value <= m_viewMaxY; value += adaptiveStep)
        {
            if (value < m_viewMinY || value > m_viewMaxY)
                continue;
            
            float y = ValueToY(value, canvasPos, canvasSize);
              // Only draw labels at key positions (0.0, 0.5, 1.0) or at major divisions
            bool isKeyPosition = 
                std::abs(value) < 0.001f || 
                std::abs(value - 0.5f) < 0.001f || 
                std::abs(value - 1.0f) < 0.001f ||
                std::abs(std::fmod(value, 0.25f)) < 0.001f;
            
            // Format value string
            char label[16];
            if (isKeyPosition)
            {
                // Ticks at key positions
                drawList->AddLine(
                    ImVec2(canvasPos.x - 5, y),
                    ImVec2(canvasPos.x, y),
                    textColor,
                    1.5f
                );
                  // Format with better precision for key positions
                if (std::abs(value) < 0.001f)
                    sprintf(label, "0.0");
                else if (std::abs(value - 0.5f) < 0.001f)
                    sprintf(label, "0.5");
                else if (std::abs(value - 1.0f) < 0.001f)
                    sprintf(label, "1.0");
                else
                    sprintf(label, "%.2f", value);
                
                ImVec2 textSize = ImGui::CalcTextSize(label);
                drawList->AddText(
                    ImVec2(canvasPos.x - textSize.x - 8, y - textSize.y / 2),
                    textColor,
                    label
                );
            }
            else if (std::fmod(std::abs(value), 0.2f) < 0.001f)
            {
                // Smaller ticks at intermediate positions
                drawList->AddLine(
                    ImVec2(canvasPos.x - 3, y),
                    ImVec2(canvasPos.x, y),
                    textColor,
                    1.0f
                );
            }
        }
    }
}
