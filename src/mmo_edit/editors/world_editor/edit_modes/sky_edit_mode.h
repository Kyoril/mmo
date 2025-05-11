// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_edit_mode.h"
#include "graphics/sky_component.h"

namespace mmo
{
    class WorldEditorInstance;
    
    class SkyEditMode final : public WorldEditMode    {
    public:
        SkyEditMode(WorldEditorInstance& editorInstance, SkyComponent& skyComponent);
        ~SkyEditMode() override = default;

    public:
        void OnActivate() override {}
        void OnDeactivate() override {}
        [[nodiscard]] const char* GetName() const override { return "Sky"; }
        void DrawDetails() override;
        void Update(const float deltaSeconds);
        void Draw();
        void LeftClick(Ray ray) {}
        void MiddleClick(Ray ray) {}
        void RightClick(Ray ray) {}
        void MouseMoved(Ray ray) {}

    private:
        SkyComponent& m_skyComponent;
        bool m_manualTimeControl{true};
        float m_normalizedTimeOfDay{0.5f};
        uint32 m_hour{12};
        uint32 m_minute{0};
        uint32 m_second{0};
        float m_timeSpeed{1.0f};
    };
}
