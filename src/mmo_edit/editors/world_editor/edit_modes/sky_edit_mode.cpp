// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "sky_edit_mode.h"
#include "editors/world_editor/world_editor_instance.h"
#include "deferred_shading/deferred_renderer.h"

namespace mmo
{
    SkyEditMode::SkyEditMode(WorldEditorInstance& editorInstance, SkyComponent& skyComponent)
        : WorldEditMode(editorInstance)
        , m_editorInstance(editorInstance)
        , m_skyComponent(skyComponent)
    {
        // Initialize values from current sky component state
        m_timeSpeed = m_skyComponent.GetTimeSpeed();

        // Initialize time of day to noon
        m_hour = 12;
        m_minute = 0;
        m_second = 0;
        m_skyComponent.SetTime(m_hour, m_minute, m_second);
        m_normalizedTimeOfDay = m_skyComponent.GetNormalizedTimeOfDay();

        // Initialize shadow settings from renderer if available
        if (DeferredRenderer* renderer = m_editorInstance.GetDeferredRenderer())
        {
            m_shadowBias = renderer->GetShadowBias();
            m_normalBiasScale = renderer->GetNormalBiasScale();
            m_shadowSoftness = renderer->GetShadowSoftness();
            m_blockerSearchRadius = renderer->GetBlockerSearchRadius();
            m_lightSize = renderer->GetLightSize();
            m_useCascadedShadows = renderer->IsCascadedShadowsEnabled();
            m_debugCascades = renderer->IsCascadeDebugVisualizationEnabled();
            m_shadowMapSize = renderer->GetShadowMapSize();
        }
    }

	void SkyEditMode::DrawDetails()
	{
        ImGui::Text("Sky Settings");
        ImGui::Separator();

        // Manual time control toggle
        if (ImGui::Checkbox("Manual Time Control", &m_manualTimeControl))
        {
            // When switching to manual, pause the time progression
            if (m_manualTimeControl)
            {
                m_skyComponent.SetTimeSpeed(0.0f);
            }
            else
            {
                // Restore time speed when switching back to automatic
                m_skyComponent.SetTimeSpeed(m_timeSpeed);
            }
        }

        // Normalized time slider (0.0 to 1.0 for a full day cycle)
        if (ImGui::SliderFloat("Time of Day", &m_normalizedTimeOfDay, 0.0f, 1.0f, "%.3f"))
        {
            if (m_manualTimeControl)
            {
                m_skyComponent.SetNormalizedTimeOfDay(m_normalizedTimeOfDay);

                // Update hour/minute/second fields to match
                m_hour = m_skyComponent.GetHour();
                m_minute = m_skyComponent.GetMinute();
                m_second = m_skyComponent.GetSecond();
            }
        }

        // Time input fields (hour, minute, second)
        bool timeChanged = false;

        // Hour input (0-23)
        int hour = static_cast<int>(m_hour);
        if (ImGui::InputInt("Hour", &hour, 1, 1))
        {
            // Clamp to valid range
            hour = std::max(0, std::min(23, hour));
            m_hour = static_cast<uint32>(hour);
            timeChanged = true;
        }

        // Minute input (0-59)
        int minute = static_cast<int>(m_minute);
        if (ImGui::InputInt("Minute", &minute, 1, 5))
        {
            // Clamp to valid range
            minute = std::max(0, std::min(59, minute));
            m_minute = static_cast<uint32>(minute);
            timeChanged = true;
        }

        // Second input (0-59)
        int second = static_cast<int>(m_second);
        if (ImGui::InputInt("Second", &second, 1, 5))
        {
            // Clamp to valid range
            second = std::max(0, std::min(59, second));
            m_second = static_cast<uint32>(second);
            timeChanged = true;
        }

        // Apply time changes if needed
        if (timeChanged && m_manualTimeControl)
        {
            m_skyComponent.SetTime(m_hour, m_minute, m_second);
            m_normalizedTimeOfDay = m_skyComponent.GetNormalizedTimeOfDay();
        }

        ImGui::Separator();

        // Time speed control
        if (ImGui::SliderFloat("Time Speed", &m_timeSpeed, 0.0f, 100.0f, "%.1f"))
        {
            // Only apply time speed if not in manual mode
            if (!m_manualTimeControl)
            {
                m_skyComponent.SetTimeSpeed(m_timeSpeed);
            }
        }

        // Preset time buttons
        ImGui::Text("Presets:");
        if (ImGui::Button("Dawn (6:00)"))
        {
            m_hour = 6;
            m_minute = 0;
            m_second = 0;
            m_skyComponent.SetTime(m_hour, m_minute, m_second);
            m_normalizedTimeOfDay = m_skyComponent.GetNormalizedTimeOfDay();
        }
        ImGui::SameLine();
        if (ImGui::Button("Noon (12:00)"))
        {
            m_hour = 12;
            m_minute = 0;
            m_second = 0;
            m_skyComponent.SetTime(m_hour, m_minute, m_second);
            m_normalizedTimeOfDay = m_skyComponent.GetNormalizedTimeOfDay();
        }
        ImGui::SameLine();
        if (ImGui::Button("Dusk (18:00)"))
        {
            m_hour = 18;
            m_minute = 0;
            m_second = 0;
            m_skyComponent.SetTime(m_hour, m_minute, m_second);
            m_normalizedTimeOfDay = m_skyComponent.GetNormalizedTimeOfDay();
        }
        ImGui::SameLine();
        if (ImGui::Button("Midnight (0:00)"))
        {
            m_hour = 0;
            m_minute = 0;
            m_second = 0;
            m_skyComponent.SetTime(m_hour, m_minute, m_second);
            m_normalizedTimeOfDay = m_skyComponent.GetNormalizedTimeOfDay();
        }

        // Display current time string
        ImGui::Text("Current Time: %s", m_skyComponent.GetTimeString().c_str());

        // Draw shadow settings section
        DrawShadowSettings();
}

    void SkyEditMode::DrawShadowSettings()
    {
        DeferredRenderer* renderer = m_editorInstance.GetDeferredRenderer();
        if (!renderer)
        {
            return;
        }

        ImGui::Separator();
        ImGui::Text("Shadow Settings");
        ImGui::Separator();

        // Cascaded Shadow Maps toggle
        if (ImGui::Checkbox("Use Cascaded Shadows (CSM)", &m_useCascadedShadows))
        {
            renderer->SetCascadedShadowsEnabled(m_useCascadedShadows);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Cascaded Shadow Maps provide better shadow quality\nat different distances from the camera.");
            ImGui::EndTooltip();
        }

        // Cascade debug visualization (only show if CSM is enabled)
        if (m_useCascadedShadows)
        {
            if (ImGui::Checkbox("Debug Cascade Colors", &m_debugCascades))
            {
                renderer->SetCascadeDebugVisualization(m_debugCascades);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Shows cascade boundaries with colored tints:\nRed=Near, Green=Mid1, Blue=Mid2, Yellow=Far");
                ImGui::EndTooltip();
            }
        }

        ImGui::Separator();
        ImGui::Text("Bias Settings");

        // Shadow Bias (depth bias in shadow space)
        if (ImGui::SliderFloat("Shadow Bias", &m_shadowBias, 0.0f, 0.01f, "%.6f"))
        {
            renderer->SetShadowBias(m_shadowBias);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Depth bias applied in shadow space.\nIncrease to reduce shadow acne.\nToo high causes peter panning (shadows detach from objects).");
            ImGui::EndTooltip();
        }

        // Normal Bias Scale
        if (ImGui::SliderFloat("Normal Bias Scale", &m_normalBiasScale, 0.0f, 0.2f, "%.4f"))
        {
            renderer->SetNormalBiasScale(m_normalBiasScale);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Offsets shadow sample position along surface normal.\nHelps with shadow acne on curved surfaces.");
            ImGui::EndTooltip();
        }

        // Depth Bias (hardware rasterizer bias)
        if (ImGui::SliderFloat("Depth Bias", &m_depthBias, 0.0f, 1000.0f, "%.1f"))
        {
            renderer->SetDepthBias(m_depthBias, m_slopeScaledDepthBias, 0.0f);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Hardware depth bias applied during shadow map rendering.\nHelps prevent z-fighting artifacts.");
            ImGui::EndTooltip();
        }

        // Slope Scaled Depth Bias
        if (ImGui::SliderFloat("Slope Scaled Bias", &m_slopeScaledDepthBias, 0.0f, 10.0f, "%.2f"))
        {
            renderer->SetDepthBias(m_depthBias, m_slopeScaledDepthBias, 0.0f);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Scales bias based on polygon slope relative to light.\nHelps with shadow acne on angled surfaces.");
            ImGui::EndTooltip();
        }

        ImGui::Separator();
        ImGui::Text("Soft Shadow Settings (PCSS)");

        // Shadow Softness
        if (ImGui::SliderFloat("Shadow Softness", &m_shadowSoftness, 0.0f, 5.0f, "%.2f"))
        {
            renderer->SetShadowSoftness(m_shadowSoftness);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Overall shadow softness multiplier.\nHigher = softer shadows everywhere.");
            ImGui::EndTooltip();
        }

        // Light Size
        if (ImGui::SliderFloat("Light Size", &m_lightSize, 0.0001f, 0.1f, "%.4f", ImGuiSliderFlags_Logarithmic))
        {
            renderer->SetLightSize(m_lightSize);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Virtual light source size for PCSS.\nLarger = softer shadows that vary with distance from occluder.");
            ImGui::EndTooltip();
        }

        // Blocker Search Radius
        if (ImGui::SliderFloat("Blocker Search Radius", &m_blockerSearchRadius, 0.001f, 0.1f, "%.4f", ImGuiSliderFlags_Logarithmic))
        {
            renderer->SetBlockerSearchRadius(m_blockerSearchRadius);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Search radius for finding shadow blockers in PCSS.\nLarger = wider soft shadow penumbras.");
            ImGui::EndTooltip();
        }

        ImGui::Separator();
        ImGui::Text("Quality Settings");

        // Shadow Map Size
        const char* shadowMapSizes[] = { "512", "1024", "2048", "4096" };
        int shadowMapSizeIndex = 0;
        if (m_shadowMapSize == 512) shadowMapSizeIndex = 0;
        else if (m_shadowMapSize == 1024) shadowMapSizeIndex = 1;
        else if (m_shadowMapSize == 2048) shadowMapSizeIndex = 2;
        else if (m_shadowMapSize == 4096) shadowMapSizeIndex = 3;

        if (ImGui::Combo("Shadow Map Size", &shadowMapSizeIndex, shadowMapSizes, 4))
        {
            int sizes[] = { 512, 1024, 2048, 4096 };
            m_shadowMapSize = sizes[shadowMapSizeIndex];
            renderer->SetShadowMapSize(static_cast<uint16>(m_shadowMapSize));
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Resolution of shadow map textures.\nHigher = sharper shadows but more memory and GPU cost.");
            ImGui::EndTooltip();
        }

        // Preset buttons
        ImGui::Separator();
        ImGui::Text("Presets:");

        if (ImGui::Button("Sharp Shadows"))
        {
            m_shadowBias = 0.0001f;
            m_normalBiasScale = 0.01f;
            m_shadowSoftness = 0.3f;
            m_lightSize = 0.0005f;
            m_blockerSearchRadius = 0.002f;
            renderer->SetShadowBias(m_shadowBias);
            renderer->SetNormalBiasScale(m_normalBiasScale);
            renderer->SetShadowSoftness(m_shadowSoftness);
            renderer->SetLightSize(m_lightSize);
            renderer->SetBlockerSearchRadius(m_blockerSearchRadius);
        }
        ImGui::SameLine();

        if (ImGui::Button("Soft Shadows"))
        {
            m_shadowBias = 0.0002f;
            m_normalBiasScale = 0.02f;
            m_shadowSoftness = 2.0f;
            m_lightSize = 0.02f;
            m_blockerSearchRadius = 0.02f;
            renderer->SetShadowBias(m_shadowBias);
            renderer->SetNormalBiasScale(m_normalBiasScale);
            renderer->SetShadowSoftness(m_shadowSoftness);
            renderer->SetLightSize(m_lightSize);
            renderer->SetBlockerSearchRadius(m_blockerSearchRadius);
        }
        ImGui::SameLine();

        if (ImGui::Button("No Peter Panning"))
        {
            m_shadowBias = 0.00005f;
            m_normalBiasScale = 0.005f;
            m_depthBias = 50.0f;
            m_slopeScaledDepthBias = 1.0f;
            renderer->SetShadowBias(m_shadowBias);
            renderer->SetNormalBiasScale(m_normalBiasScale);
            renderer->SetDepthBias(m_depthBias, m_slopeScaledDepthBias, 0.0f);
        }

        if (ImGui::Button("Reset to Defaults"))
        {
            m_shadowBias = 0.0001f;
            m_normalBiasScale = 0.02f;
            m_shadowSoftness = 1.0f;
            m_lightSize = 0.001f;
            m_blockerSearchRadius = 0.005f;
            m_depthBias = 100.0f;
            m_slopeScaledDepthBias = 2.0f;
            renderer->SetShadowBias(m_shadowBias);
            renderer->SetNormalBiasScale(m_normalBiasScale);
            renderer->SetShadowSoftness(m_shadowSoftness);
            renderer->SetLightSize(m_lightSize);
            renderer->SetBlockerSearchRadius(m_blockerSearchRadius);
            renderer->SetDepthBias(m_depthBias, m_slopeScaledDepthBias, 0.0f);
        }
    }

    void SkyEditMode::Update(const float deltaSeconds)
    {
    }

    void SkyEditMode::Draw()
    {
    }
}
