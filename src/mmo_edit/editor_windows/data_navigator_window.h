// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editor_window_base.h"
#include "proto_data/project.h"

#include <vector>
#include <string>
#include <functional>
#include <typeindex>
#include <typeinfo>

namespace mmo
{
    class EditorHost;

    /**
     * @brief A window that provides a categorized list of all data assets that can be edited.
     * This window serves as a central navigation hub for all data editors.
     */
    class DataNavigatorWindow final : public EditorWindowBase
    {
    public:
        /**
         * @brief Signal type for requesting window visibility changes by type
         */
        using OpenEditorWindowSignalType = std::function<void(std::type_index)>;

        /**
         * @brief Constructs a DataNavigatorWindow.
         * @param name The window name.
         * @param project Reference to the project data.
         * @param host Reference to the editor host.
         */
        DataNavigatorWindow(const String& name, proto::Project& project, EditorHost& host);
        ~DataNavigatorWindow() override = default;

    public:
        /**
         * @brief Draws the window content.
         * @return False as this window doesn't capture input.
         */
        bool Draw() override;

        /**
         * @brief Indicates that this window should be docked.
         * @return True as this window is dockable.
         */
        [[nodiscard]] bool IsDockable() const noexcept override { return true; }

        /**
         * @brief Gets the default dock direction for this window.
         * @return Left dock direction.
         */
        [[nodiscard]] DockDirection GetDefaultDockDirection() const noexcept override { return DockDirection::Left; }

        /**
         * @brief Gets the default dock size for this window.
         * @return A default width value.
         */
        [[nodiscard]] float GetDefaultDockSize() const noexcept override { return 300.0f; }

        /**
         * @brief Sets the callback for window visibility change requests
         * @param callback Function to call when window visibility changes are requested
         */
        void SetOpenEditorWindowCallback(OpenEditorWindowSignalType callback) { m_openEditorWindowSignal = std::move(callback); }

    private:
        /**
         * @brief Structure representing a data category with all its editors.
         */
        struct DataCategory {
            String name;
            bool isOpen = true;
            
            struct DataEditor {
                std::type_index typeIndex;  // Type index for explicit type identification
                String displayName;
                std::function<void()> openCallback;
                int count = 0;
            };
            
            std::vector<DataEditor> editors;
        };

        proto::Project& m_project;
        EditorHost& m_host;
        std::vector<DataCategory> m_categories;
        OpenEditorWindowSignalType m_openEditorWindowSignal;
        
        /**
         * @brief Initializes the categories and their editors.
         */
        void InitializeCategories();

        /**
         * @brief Requests that an editor window be opened based on its type
         * @param typeIndex Type index of the window to open
         */
        void OpenEditorWindow(const std::type_index& typeIndex);
    };
}