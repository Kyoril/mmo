// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "sky_edit_mode.h"
#include "editors/world_editor/world_editor_instance.h"

namespace mmo
{    SkyEditMode::SkyEditMode(WorldEditorInstance& editorInstance, SkyComponent& skyComponent)
        : WorldEditMode(editorInstance)
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
}

    void SkyEditMode::Update(const float deltaSeconds)
    {
    }

    void SkyEditMode::Draw()
    {
    }
}
