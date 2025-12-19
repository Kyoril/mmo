// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include <functional>

namespace mmo
{
    class Selection;
    class WorldEditMode;
    class SelectableVisitor;

    /// @brief Manages the details panel UI for the world editor.
    /// Displays the edit mode selector, mode-specific details, and selection properties.
    class DetailsPanel final : public NonCopyable
    {
    public:
        /// @brief Constructs the details panel.
        /// @param selection Reference to the current selection.
        /// @param visitor Reference to the visitor for handling selection-specific UI.
        /// @param saveCallback Callback invoked when the save button is pressed.
        explicit DetailsPanel(
            Selection &selection,
            SelectableVisitor &visitor,
            std::function<void()> saveCallback);

        ~DetailsPanel() override = default;

    public:
        /// @brief Draws the details panel.
        /// @param id The ImGui window ID for the panel.
        /// @param currentEditMode The currently active edit mode (can be nullptr).
        /// @param availableEditModes Array of available edit modes to choose from.
        /// @param editModeCount Number of available edit modes.
        /// @param setEditModeCallback Callback to change the current edit mode.
        void Draw(
            const String &id,
            WorldEditMode *currentEditMode,
            WorldEditMode **availableEditModes,
            size_t editModeCount,
            std::function<void(WorldEditMode *)> setEditModeCallback);

    private:
        void DrawEditModeSelector(
            WorldEditMode *currentEditMode,
            WorldEditMode **availableEditModes,
            size_t editModeCount,
            std::function<void(WorldEditMode *)> setEditModeCallback);

        void DrawSelectionDetails();

    private:
        Selection &m_selection;
        SelectableVisitor &m_visitor;
        std::function<void()> m_saveCallback;
    };
}
