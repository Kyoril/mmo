// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "talent_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"

namespace mmo
{    TalentEditorWindow::TalentEditorWindow(const String& name, proto::Project& project, EditorHost& host)
        : EditorEntryWindowBase(project, project.talentTabs, name)
        , m_host(host)
        , m_isDragging(false)
        , m_draggedTalentId(0)
        , m_dragStartPos(0, 0)
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
        }

        ImGui::Separator();

        // Talent Tree Grid
        if (ImGui::CollapsingHeader("Talent Tree", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Create a side-by-side layout with talent tree on the left and details on the right
            ImGui::Columns(2, "TalentEditorColumns", true);

            // Set column widths: 60% for tree, 40% for details
            static bool columnWidthSet = false;
            if (!columnWidthSet) {
                ImGui::SetColumnWidth(0, ImGui::GetWindowContentRegionWidth() * 0.6f);
                columnWidthSet = true;
            }

            // Left column - Talent Tree Grid
            {
                DrawTalentTreeGrid(currentEntry);
            }

            // Right column - Talent Details (if selected)
            ImGui::NextColumn();

            if (m_selectedTalentId >= 0)
            {
                auto* talent = m_project.talents.getById(m_selectedTalentId);
                if (talent != nullptr)
                {
                    ImGui::Text("Selected Talent (ID: %u)", talent->id());
                    ImGui::Separator();

                    DrawTalentNodeEditor(*talent);
                }
                else
                {
                    m_selectedTalentId = -1;
                    ImGui::Text("No talent selected");
                }
            }
            else
            {
                ImGui::Text("No talent selected");
            }

            ImGui::Columns(1);
        }
    }

	void TalentEditorWindow::DrawTalentTreeGrid(const proto::TalentTabEntry& currentTab)
    {
        const float nodeSize = 64.0f; // Size of each talent node
        const float nodeSpacingX = 32.0f; // Horizontal spacing between nodes
        const float nodeSpacingY = 48.0f; // Vertical spacing between tiers
        
        // Variables to track potential drop target position when dragging
        int hoverRow = -1;
        int hoverCol = -1;
        
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
                    ImU32 borderColor = isSelected ? IM_COL32(255, 215, 0, 255) : IM_COL32(130, 130, 130, 255); // Gold border for selected talent
                    
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

                        	// Try to get the icon texture from cache or load it
                            if (m_iconCache.find(iconPath) == m_iconCache.end()) {
                                try {
                                    m_iconCache[iconPath] = TextureManager::Get().CreateOrRetrieve(iconPath);
                                }
                                catch (...) {
                                    ELOG("Failed to load texture: " << iconPath);
                                }
                            }
                            
                            // Draw the icon if available
                            if (m_iconCache.contains(iconPath)) {
                                TexturePtr texture = m_iconCache[iconPath];
                                if (texture) {
                                    ImGui::SetCursorScreenPos(ImVec2(x, y));
                                    ImGui::Image(
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
                      // Handle node selection and drag & drop
                    ImGui::SetCursorScreenPos(ImVec2(x, y));
                    ImGui::InvisibleButton(
                        ("talent_" + std::to_string(talent.id())).c_str(),
                        ImVec2(nodeSize, nodeSize)
                    );
                    
                    if (ImGui::IsItemClicked()) {
                        m_selectedTalentId = talent.id();
                    }
                    
                    // Begin drag operation
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !m_isDragging) {
                        m_isDragging = true;
                        m_draggedTalentId = talent.id();
                        m_dragStartPos = ImGui::GetMousePos();
                    }
                    
                    // Store information about the dragged talent for later rendering
                    if (m_isDragging && m_draggedTalentId == talent.id()) {
                        ImVec2 mousePos = ImGui::GetMousePos();
                        
                        // Calculate hover row and column for highlighting drop target
                        hoverRow = -1;
                        hoverCol = -1;
                        
                        // Find which grid cell the mouse is currently over
                        for (uint32_t r = 0; r < MAX_GRID_HEIGHT; ++r) {
                            for (uint32_t c = 0; c < MAX_GRID_WIDTH; ++c) {
                                float cellX = canvasPos.x + (nodeSize + nodeSpacingX) * c + nodeSpacingX;
                                float cellY = canvasPos.y + (nodeSize + nodeSpacingY) * r + nodeSpacingY;
                                
                                if (mousePos.x >= cellX && mousePos.x < cellX + nodeSize &&
                                    mousePos.y >= cellY && mousePos.y < cellY + nodeSize) {
                                    hoverRow = r;
                                    hoverCol = c;
                                    break;
                                }
                            }
                            if (hoverRow != -1) break;
                        }
                        
                        // We'll draw the dragged talent at the end to ensure it's on top
                        // Just keep track of the dragged talent info for now
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

		// Handle dropping talents and ending drag operations
        if (m_isDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ImVec2 mousePos = ImGui::GetMousePos();
            bool dropped = false;

            // Calculate which grid cell the mouse is over
            int targetRow = -1;
            int targetCol = -1;
            
            // Calculate the grid cell based on mouse position
            for (uint32_t row = 0; row < MAX_GRID_HEIGHT; ++row) {
                for (uint32_t col = 0; col < MAX_GRID_WIDTH; ++col) {
                    float cellX = canvasPos.x + (nodeSize + nodeSpacingX) * col + nodeSpacingX;
                    float cellY = canvasPos.y + (nodeSize + nodeSpacingY) * row + nodeSpacingY;
                    
                    if (mousePos.x >= cellX && mousePos.x < cellX + nodeSize &&
                        mousePos.y >= cellY && mousePos.y < cellY + nodeSize) {
                        targetRow = row;
                        targetCol = col;
                        break;
                    }
                }
                if (targetRow != -1) break; // Found the cell, exit outer loop
            }

            if (targetRow != -1 && targetCol != -1) {
                // Check if the dragged talent is valid
                auto* draggedTalent = m_project.talents.getById(m_draggedTalentId);
                
                if (draggedTalent) {
                    // Only handle if the talent belongs to the current tab
                    if (draggedTalent->tab() == tabId) {
                        // Check if there's already a talent at the target position
                        proto::TalentEntry* targetTalent = nullptr;
                        
                        for (int i = 0; i < m_project.talents.count(); ++i) {
                            auto& talent = *m_project.talents.getTemplates().mutable_entry(i);
                            
                            if (talent.tab() == tabId && 
                                talent.row() == targetRow && 
                                talent.column() == targetCol &&
                                talent.id() != m_draggedTalentId) {
                                targetTalent = &talent;
                                break;
                            }
                        }
                        
                        uint32_t originalRow = draggedTalent->row();
                        uint32_t originalColumn = draggedTalent->column();
                        
                        if (targetTalent) {
                            // Swap positions with the target talent
                            targetTalent->set_row(originalRow);
                            targetTalent->set_column(originalColumn);
                            
                            draggedTalent->set_row(targetRow);
                            draggedTalent->set_column(targetCol);
                            
                            dropped = true;
                        } else {
                            // No talent at target position, just move the dragged talent
                            draggedTalent->set_row(targetRow);
                            draggedTalent->set_column(targetCol);
                            dropped = true;
                        }
                    }
                }
            }

        	// Reset drag state
            m_isDragging = false;
            m_draggedTalentId = 0;
        }
        
        // Draw drop target highlight if we're dragging and hovering over a valid cell
        if (m_isDragging && hoverRow != -1 && hoverCol != -1) {
            float x = canvasPos.x + (nodeSize + nodeSpacingX) * hoverCol + nodeSpacingX;
            float y = canvasPos.y + (nodeSize + nodeSpacingY) * hoverRow + nodeSpacingY;
            
            // Use a glowing effect for the drop target
            ImU32 highlightColor = IM_COL32(180, 220, 255, 100); // Light blue semi-transparent
            ImU32 highlightBorderColor = IM_COL32(100, 200, 255, 200); // Brighter blue for border
            
            // Fill with subtle highlight
            drawList->AddRectFilled(
                ImVec2(x - 2, y - 2),
                ImVec2(x + nodeSize + 2, y + nodeSize + 2),
                highlightColor,
                6.0f
            );
            
            // Draw a pulsing border (thicker)
            static float pulsingValue = 0.0f;
            pulsingValue += ImGui::GetIO().DeltaTime * 3.0f;  // Adjust speed as needed
            
            float pulseAlpha = 0.5f + 0.5f * sinf(pulsingValue); // Oscillate between 0.5 and 1.0
            ImU32 pulsingBorderColor = ImColor(0.4f, 0.8f, 1.0f, pulseAlpha);
            
            drawList->AddRect(
                ImVec2(x - 3, y - 3),
                ImVec2(x + nodeSize + 3, y + nodeSize + 3),
                pulsingBorderColor,
                6.0f,
                0,
                3.0f  // Thicker border
            );
        }
          
        // End the grid child window - talent details are now displayed in the right column
        ImGui::EndChild();
    }

	void TalentEditorWindow::DrawTalentNodeEditor(proto::TalentEntry& talent)
    {
        ImGui::BeginChild("TalentNodeEditor", ImVec2(0, 0), true);
        
        // Basic properties
        ImGui::Text("Position: Row %u, Column %u", talent.row(), talent.column());
        
        // Spell ranks editor
        ImGui::Text("Spell Ranks:");
        
        static char spellSearchBuffer[128] = "";
        static bool showSpellSelector = false;
        static int currentRankEditing = -1;
        
        // List all current ranks
        for (int i = 0; i < talent.ranks_size(); ++i) {
            uint32_t spellId = talent.ranks(i);
            ImGui::PushID(i);
            
            auto spellEntry = m_project.spells.getById(spellId);
            std::string spellName;
            std::string rankText;
            
            if (spellEntry) {
                spellName = spellEntry->name();
                if (spellEntry->has_rank())
                    rankText = " (Rank " + std::to_string(spellEntry->rank()) + ")";
            }
            else {
                spellName = "Unknown Spell";
                rankText = "";
            }
            
            // Prepare the label for the combo box
            std::string comboLabel = "[" + std::to_string(spellId) + "] " + spellName + rankText;
            
            ImGui::Text("Rank %d:", i+1);
            ImGui::SameLine();
            
            // Create a combo box for spell selection
            if (ImGui::BeginCombo(("##SpellSelect" + std::to_string(i)).c_str(), comboLabel.c_str()))
            {
                // First display the search box
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##SpellSearch", "Search spells...", spellSearchBuffer, IM_ARRAYSIZE(spellSearchBuffer));
                
                std::string searchText = spellSearchBuffer;
                std::transform(searchText.begin(), searchText.end(), searchText.begin(),
                    [](unsigned char c) { return std::tolower(c); });
                
                // Add a dummy item for clearing the selection
                if (ImGui::Selectable("Clear Selection", false))
                {
                    talent.set_ranks(i, 0);
                }
                
                // Display filtered spell list
                int matchCount = 0;
                const int MAX_DISPLAYED_ITEMS = 200; // Limit displayed items for performance
                
                for (int spellIdx = 0; spellIdx < m_project.spells.count(); ++spellIdx)
                {
                    const auto& spell = m_project.spells.getTemplates().entry(spellIdx);
                    
                    // Skip if doesn't match search
                    if (!searchText.empty())
                    {
                        std::string spellLower = spell.name();
                        std::transform(spellLower.begin(), spellLower.end(), spellLower.begin(),
                            [](unsigned char c) { return std::tolower(c); });
                        
                        // Check if spell name or ID contains search text
                        std::string idStr = std::to_string(spell.id());
                        if (spellLower.find(searchText) == std::string::npos &&
                            idStr.find(searchText) == std::string::npos)
                        {
                            continue;
                        }
                    }
                    
                    // Format the spell display with ID, name and rank
                    std::string spellDisplayName = "[" + std::to_string(spell.id()) + "] " + spell.name();
                    if (spell.has_rank())
                        spellDisplayName += " (Rank " + std::to_string(spell.rank()) + ")";
                    
                    // Try to get spell icon
                    bool hasIcon = false;
                    if (spell.has_icon()) {
                        std::string iconPath = spell.icon();

                    	if (m_iconCache.find(iconPath) == m_iconCache.end()) {
                            try {
                                m_iconCache[iconPath] = TextureManager::Get().CreateOrRetrieve(iconPath);
                            }
                            catch (...) {
                                // Icon loading failed, continue without icon
                            }
                        }
                        
                        // Display icon if available
                        if (m_iconCache.contains(iconPath) && m_iconCache[iconPath]) {
                            ImGui::Image(m_iconCache[iconPath]->GetTextureObject(), ImVec2(20, 20));
                            ImGui::SameLine();
                            hasIcon = true;
                        }
                    }
                    
                    if (!hasIcon) {
                        // Add a placeholder space for better alignment when some spells have icons
                        ImGui::Dummy(ImVec2(20, 20));
                        ImGui::SameLine();
                    }
                    
                    // Display the selectable with the spell information
                    bool isSelected = (spell.id() == spellId);
                    if (ImGui::Selectable(spellDisplayName.c_str(), isSelected)) {
                        talent.set_ranks(i, spell.id());
                    }
                    
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                    
                    // Limit the number of displayed spells for performance
                    matchCount++;
                    if (matchCount >= MAX_DISPLAYED_ITEMS) {
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), 
                            "Too many matches, showing first %d. Please refine your search.", 
                            MAX_DISPLAYED_ITEMS);
                        break;
                    }
                }
                
                ImGui::EndCombo();
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
            talent.add_ranks(0); // Add a default spell ID
        }
        
        ImGui::Separator();
        
        // Delete talent button
        if (ImGui::Button("Delete Talent", ImVec2(150, 0))) {
            m_project.talents.remove(talent.id());
            m_selectedTalentId = -1;
        }
        
        ImGui::EndChild();
    }

	void TalentEditorWindow::CreateNewTalent(uint32_t tabId, uint32_t row, uint32_t column)
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
