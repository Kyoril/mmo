// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "talent_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"

namespace mmo
{
    TalentEditorWindow::TalentEditorWindow(const String& name, proto::Project& project, EditorHost& host)
        : EditorEntryWindowBase(project, project.talentTabs, name)
        , m_host(host)
    {
        // Register the window with the editor host
        m_hasToolbarButton = false;
    }

	void TalentEditorWindow::OnNewEntry(proto::TemplateManager<proto::TalentTabs, proto::TalentTabEntry>::EntryType& entry)
    {
        EditorEntryWindowBase::OnNewEntry(entry);
        
        // Generate a new unique ID for the entry - m_manager will auto-assign the next available ID
        entry.set_name("New Talent Tab");
        entry.set_class_id(0); // Default to first class
    }

    void TalentEditorWindow::DrawDetailsImpl(proto::TalentTabEntry& currentEntry)
    {
        // Basic tab properties
        if (ImGui::CollapsingHeader("Talent Tab Properties", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("ID:"); ImGui::SameLine();
            ImGui::Text("%u", currentEntry.id());
            
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Name:"); ImGui::SameLine();
            std::string name = currentEntry.name();
            if (ImGui::InputText("##Name", &name))
            {
                currentEntry.set_name(name);
            }
            
            // Class dropdown
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Class:"); ImGui::SameLine();
            
            int classId = static_cast<int>(currentEntry.class_id());
            if (ImGui::BeginCombo("##Class", m_project.classes.getById(currentEntry.class_id())->name().c_str()))
            {
                for (int i = 0; i < m_project.classes.count(); ++i)
                {
                    const auto& classEntry = m_project.classes.getTemplates().entry(i);
                    const bool isSelected = (classId == classEntry.id());
                    if (ImGui::Selectable(classEntry.name().c_str(), isSelected))
                    {
                        classId = classEntry.id();
                        currentEntry.set_class_id(classId);
                    }
                    
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            
            // Icon selection
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Icon:"); ImGui::SameLine();
            std::string icon = currentEntry.has_icon() ? currentEntry.icon() : "";
            if (ImGui::InputText("##Icon", &icon))
            {
                if (icon.empty())
                {
                    if (currentEntry.has_icon())
                        currentEntry.clear_icon();
                }
                else
                {
                    currentEntry.set_icon(icon);
                }
            }
            
            // Background image selection
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Background:"); ImGui::SameLine();
            std::string background = currentEntry.has_background() ? currentEntry.background() : "";
            if (ImGui::InputText("##Background", &background))
            {
                if (background.empty())
                {
                    if (currentEntry.has_background())
                        currentEntry.clear_background();
                }
                else
                {
                    currentEntry.set_background(background);
                }
            }
        }
        
        ImGui::Separator();
        
        // Talent Tree Grid
        if (ImGui::CollapsingHeader("Talent Tree", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawTalentTreeGrid(currentEntry);
        }
    }

    void TalentEditorWindow::DrawTalentTreeGrid(const proto::TalentTabEntry& currentTab)
    {
        const float nodeSize = 64.0f; // Size of each talent node
        const float nodeSpacingX = 32.0f; // Horizontal spacing between nodes
        const float nodeSpacingY = 48.0f; // Vertical spacing between tiers
        
        // Background for the talent grid
        ImVec2 gridSize((nodeSize + nodeSpacingX) * MAX_GRID_WIDTH + nodeSpacingX,
                        (nodeSize + nodeSpacingY) * MAX_GRID_HEIGHT + nodeSpacingY);
        
        // Draw the grid as a scrollable region
        ImGui::BeginChild("TalentGridRegion", ImVec2(0, 500), true, ImGuiWindowFlags_HorizontalScrollbar);
        
        // Start of the drawing region
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        
        // Draw grid lines
        const ImU32 gridColor = IM_COL32(80, 80, 80, 100);
        for (uint32_t col = 0; col <= MAX_GRID_WIDTH; col++) {
            float x = canvasPos.x + (nodeSize + nodeSpacingX) * col + nodeSpacingX/2;
            drawList->AddLine(
                ImVec2(x, canvasPos.y),
                ImVec2(x, canvasPos.y + gridSize.y),
                gridColor
            );
        }
        
        for (uint32_t row = 0; row <= MAX_GRID_HEIGHT; row++) {
            float y = canvasPos.y + (nodeSize + nodeSpacingY) * row + nodeSpacingY/2;
            drawList->AddLine(
                ImVec2(canvasPos.x, y),
                ImVec2(canvasPos.x + gridSize.x, y),
                gridColor
            );
        }
        
        // Draw talent nodes from the talent entries that belong to this tab
        const uint32_t tabId = currentTab.id();
        
        // Track positions that are already occupied by talents
        std::vector<std::pair<uint32_t, uint32_t>> occupiedPositions;
        
        // First pass: Draw existing talents
        for (int i = 0; i < m_project.talents.count(); ++i) {
            const auto& talent = m_project.talents.getTemplates().entry(i);
            
            if (talent.tab() == tabId) {
                uint32_t row = talent.row();
                uint32_t column = talent.column();
                
                if (row < MAX_GRID_HEIGHT && column < MAX_GRID_WIDTH) {
                    occupiedPositions.push_back(std::make_pair(row, column));
                    
                    float x = canvasPos.x + (nodeSize + nodeSpacingX) * column + nodeSpacingX;
                    float y = canvasPos.y + (nodeSize + nodeSpacingY) * row + nodeSpacingY;
                    
                    bool isSelected = (m_selectedTalentId == talent.id());
                    ImU32 nodeColor = isSelected ? IM_COL32(100, 150, 250, 255) : IM_COL32(70, 70, 70, 255);
                    ImU32 borderColor = isSelected ? IM_COL32(200, 200, 255, 255) : IM_COL32(130, 130, 130, 255);
                    
                    // Draw node background
                    drawList->AddRectFilled(
                        ImVec2(x, y),
                        ImVec2(x + nodeSize, y + nodeSize),
                        nodeColor,
                        4.0f
                    );
                    
                    // Draw node border
                    drawList->AddRect(
                        ImVec2(x, y),
                        ImVec2(x + nodeSize, y + nodeSize),
                        borderColor,
                        4.0f,
                        0,
                        2.0f
                    );
                    
                    // If the talent has spell ranks, try to draw the icon
                    if (talent.ranks_size() > 0) {
                        uint32_t spellId = talent.ranks(0); // Get first rank spell
                        auto spellEntry = m_project.spells.getById(spellId);
                        
                        if (spellEntry && spellEntry->has_icon()) {
                            std::string iconPath = spellEntry->icon();
                            
                            // Try to get the icon texture from cache or load it                            if (m_iconCache.find(iconPath) == m_iconCache.end()) {
                            try {
                                m_iconCache[iconPath] = TextureManager::Get().CreateOrRetrieve(iconPath);
                            }
                            catch (...) {
                                ELOG("Failed to load texture: " << iconPath);
                            }
                            
                            // Draw the icon if available
                            if (m_iconCache.contains(iconPath)) {
                                TexturePtr texture = m_iconCache[iconPath];
                                if (texture) {
                                    ImGui::SetCursorScreenPos(ImVec2(x, y));                                    ImGui::Image(
                                        texture->GetTextureObject(),
                                        ImVec2(nodeSize, nodeSize)
                                    );
                                }
                            }
                        }
                        
                        // Draw max ranks
                        std::string rankText = std::to_string(talent.ranks_size());
                        ImVec2 textSize = ImGui::CalcTextSize(rankText.c_str());
                        drawList->AddText(
                            ImVec2(x + nodeSize - textSize.x - 4, y + nodeSize - textSize.y - 2),
                            IM_COL32(255, 255, 255, 255),
                            rankText.c_str()
                        );
                    }
                    
                    // Handle node selection
                    ImGui::SetCursorScreenPos(ImVec2(x, y));
                    ImGui::InvisibleButton(
                        ("talent_" + std::to_string(talent.id())).c_str(),
                        ImVec2(nodeSize, nodeSize)
                    );
                    
                    if (ImGui::IsItemClicked()) {
                        m_selectedTalentId = talent.id();
                    }
                }
            }
        }
        
        // Second pass: Draw buttons for empty positions
        for (uint32_t row = 0; row < MAX_GRID_HEIGHT; ++row) {
            for (uint32_t col = 0; col < MAX_GRID_WIDTH; ++col) {
                // Check if this position is already occupied
                bool isOccupied = false;
                for (const auto& pos : occupiedPositions) {
                    if (pos.first == row && pos.second == col) {
                        isOccupied = true;
                        break;
                    }
                }
                
                if (!isOccupied) {
                    // Position for the add button
                    float x = canvasPos.x + (nodeSize + nodeSpacingX) * col + nodeSpacingX;
                    float y = canvasPos.y + (nodeSize + nodeSpacingY) * row + nodeSpacingY;
                    
                    ImGui::SetCursorScreenPos(ImVec2(x + nodeSize/4, y + nodeSize/4));
                    
                    std::string buttonId = "add_talent_" + std::to_string(row) + "_" + std::to_string(col);
                    if (ImGui::Button(("+##" + buttonId).c_str(), ImVec2(nodeSize/2, nodeSize/2))) {
                        CreateNewTalent(tabId, row, col);
                    }
                    
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Add talent at row %d, column %d", row, col);
                    }
                }
            }
        }
        
        // If a talent is selected, show its properties
        if (m_selectedTalentId >= 0) {
            auto* talent = m_project.talents.getById(m_selectedTalentId);
            if (talent != nullptr) {
                ImGui::EndChild();  // End the grid child window
                
                ImGui::Separator();
                ImGui::Text("Selected Talent (ID: %u)", talent->id());
                
                DrawTalentNodeEditor(*talent);
            }
            else {
                m_selectedTalentId = -1;
                ImGui::EndChild();
            }
        }
        else {
            ImGui::EndChild();
        }
    }

    void TalentEditorWindow::DrawTalentNodeEditor(proto::TalentEntry& talent)
    {
        ImGui::BeginChild("TalentNodeEditor", ImVec2(0, 300), true);
        
        // Basic properties
        ImGui::Text("Position: Row %u, Column %u", talent.row(), talent.column());
        
        // Spell ranks editor
        ImGui::Text("Spell Ranks:");
        
        // List all current ranks
        for (int i = 0; i < talent.ranks_size(); ++i) {
            uint32_t spellId = talent.ranks(i);
            ImGui::PushID(i);
            
            auto spellEntry = m_project.spells.getById(spellId);
            std::string spellName = spellEntry ? spellEntry->name() : "Unknown Spell";
            
            ImGui::Text("Rank %d: [%u] %s", i+1, spellId, spellName.c_str());
            
            ImGui::SameLine();
            if (ImGui::Button("Change")) {
                // Open a spell selection modal
                // For now, we'll just allow directly editing the spell ID
                uint32_t newSpellId = spellId;
                if (ImGui::InputScalar("Spell ID", ImGuiDataType_U32, &newSpellId)) {
                    talent.set_ranks(i, newSpellId);
                }
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Remove")) {
                // Remove this rank
                for (int j = i; j < talent.ranks_size() - 1; ++j) {
                    talent.set_ranks(j, talent.ranks(j + 1));
                }
                talent.mutable_ranks()->RemoveLast();
                --i; // Adjust index since we removed an element
            }
            
            ImGui::PopID();
        }
        
        // Add new rank button
        if (ImGui::Button("Add New Rank")) {
            talent.add_ranks(0); // Add a default spell ID, user will need to select the appropriate one
        }
        
        ImGui::Separator();
        
        // Delete talent button
        if (ImGui::Button("Delete Talent", ImVec2(150, 0))) {
            m_project.talents.remove(talent.id());
            m_selectedTalentId = -1;
        }
        
        ImGui::EndChild();
    }    void TalentEditorWindow::CreateNewTalent(uint32_t tabId, uint32_t row, uint32_t column)
    {
        auto* newTalent = m_project.talents.add();
        
        // Set basic properties
        // ID is automatically assigned by the add() method
        newTalent->set_tab(tabId);
        newTalent->set_row(row);
        newTalent->set_column(column);
        
        // Select the newly created talent
        m_selectedTalentId = newTalent->id();
    }
}
