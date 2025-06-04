// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "graphics/texture.h"
#include "proto_data/project.h"

namespace mmo
{
    /// Talent editor window for managing talent trees and individual talents
    class TalentEditorWindow final
        : public EditorEntryWindowBase<proto::TalentTabs, proto::TalentTabEntry>
        , public NonCopyable
    {
    public:
        explicit TalentEditorWindow(const String& name, proto::Project& project, EditorHost& host);
        ~TalentEditorWindow() override = default;

    private:
        void DrawDetailsImpl(proto::TalentTabEntry& currentEntry) override;
        
        /// Draw the talent grid for the selected tab
        void DrawTalentTreeGrid(const proto::TalentTabEntry& currentTab);
        
        /// Create the talent node editor for a specific talent
        void DrawTalentNodeEditor(proto::TalentEntry& talent);
        
        /// Handle the creation of a new talent entry
        void CreateNewTalent(uint32_t tabId, uint32_t row, uint32_t column);
        
        void OnNewEntry(proto::TemplateManager<proto::TalentTabs, proto::TalentTabEntry>::EntryType& entry) override;

    public:
        bool IsDockable() const noexcept override { return true; }

        [[nodiscard]] DockDirection GetDefaultDockDirection() const noexcept override { return DockDirection::Center; }

    private:
        EditorHost& m_host;
        std::map<std::string, TexturePtr> m_iconCache;
        
        // Currently selected talent (if any)
        int32_t m_selectedTalentId = -1;
        
        // Maximum dimensions of the talent grid
        static constexpr uint32_t MAX_GRID_WIDTH = 4;
        static constexpr uint32_t MAX_GRID_HEIGHT = 7; // Reasonable maximum for talent tiers
    };
}
