// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include <functional>

namespace mmo
{
    namespace terrain
    {
        class Terrain;
    }

    class WorldEditMode;

    /// @brief Manages the world settings panel UI.
    /// Displays world-level configuration such as terrain settings, default materials,
    /// and wireframe visualization options.
    class WorldSettingsPanel final : public NonCopyable
    {
    public:
        /// @brief Constructs the world settings panel.
        /// @param terrain Reference to the terrain system.
        /// @param hasTerrain Reference to the terrain enabled flag.
        /// @param currentEditMode Pointer to current edit mode (used to check terrain mode).
        /// @param terrainEditMode Pointer to terrain edit mode for comparison.
        /// @param setEditModeCallback Callback to change edit mode when terrain is disabled.
        explicit WorldSettingsPanel(
            terrain::Terrain &terrain,
            bool &hasTerrain,
            WorldEditMode *&currentEditMode,
            WorldEditMode *terrainEditMode,
            std::function<void(WorldEditMode *)> setEditModeCallback);

        ~WorldSettingsPanel() override = default;

    public:
        /// @brief Draws the world settings panel.
        /// @param id The ImGui window ID for the panel.
        void Draw(const String &id);

    private:
        terrain::Terrain &m_terrain;
        bool &m_hasTerrain;
        WorldEditMode *&m_currentEditMode;
        WorldEditMode *m_terrainEditMode;
        std::function<void(WorldEditMode *)> m_setEditModeCallback;
    };
}
