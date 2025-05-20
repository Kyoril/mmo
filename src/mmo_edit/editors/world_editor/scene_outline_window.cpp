#include "scene_outline_window.h"
#include "selected_map_entity.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include "scene_graph/entity.h"
#include "scene_graph/scene_node.h"
#include "world_editor_instance.h" // Include this for MapEntity definition
#include "graphics/texture_mgr.h" // For the folder icon

namespace mmo
{    SceneOutlineWindow::SceneOutlineWindow(Selection& selection, Scene& scene)
        : m_selection(selection)
        , m_scene(scene)
        , m_needsUpdate(true)
        , m_lastRebuildTime()
        , m_editingId(0)
        , m_categoryChangeEntityId(0)
        , m_openCategoryChangePopup(false)
        , m_isDragging(false)
        , m_draggedEntityId(0)
    {
        // Initialize buffers
        m_nameBuffer[0] = '\0';
        m_categoryBuffer[0] = '\0';
        
        // Load the folder icon texture
        m_folderTexture = TextureManager::Get().CreateOrRetrieve("Editor/Folder_BaseHi_256x.htex");
    }
    
    ImTextureID SceneOutlineWindow::GetFolderIconTexture() const
    {
        return m_folderTexture ? m_folderTexture->GetTextureObject() : nullptr;
    }
    
    void SceneOutlineWindow::Draw(const std::string& title)
    {
        if (ImGui::Begin(title.c_str()))
        {
            // Handle the category change popup
            if (m_openCategoryChangePopup)
            {
                ImGui::OpenPopup("Change Category");
                m_openCategoryChangePopup = false;
            }
            // Make sure the current frame knows about any active rename operations
            // This helps with proper focus handling between frames
            if (m_editingId != 0)
            {
                ImGui::PushID("RenameOperation");
                ImGui::PopID();
            }
            
            // Add a refresh button at the top
            if (ImGui::Button("Refresh"))
            {
                m_needsUpdate = true;
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Clear Selection"))
            {
                ClearSelection();
            }
            
            // Update the list of entities if needed
            if (m_needsUpdate)
            {
                // Use a timer to limit rebuilds for performance
                auto currentTime = std::chrono::steady_clock::now();
                if (m_lastRebuildTime.time_since_epoch().count() == 0 || 
                    std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - m_lastRebuildTime).count() > 250)
                {
                    BuildEntryList();
                    m_needsUpdate = false;
                    m_lastRebuildTime = currentTime;
                }
            }
            
            // Add a search filter
            static char searchBuffer[128] = "";
            ImGui::Text("Search: ");
            ImGui::SameLine();
            if (ImGui::InputText("##SearchFilter", searchBuffer, IM_ARRAYSIZE(searchBuffer)))
            {
                // No need to rebuild the list, we'll filter during display
            }
            
            // Create a list box with filtered entities
            ImGui::Separator();

            // Reserve space at the bottom for the status text (entity count)
            const float statusBarHeight = ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y * 2;
            
            // Use child window to get scrolling, but leave room at the bottom for the status text
            ImGui::BeginChild("SceneObjectsList", ImVec2(0, -statusBarHeight), true);
            
            // Get the search string as lowercase for case-insensitive comparison
            std::string searchString = searchBuffer;
            std::transform(searchString.begin(), searchString.end(), searchString.begin(),
                [](unsigned char c){ return std::tolower(c); });
            
            // Display categories and their entries
            int visibleEntities = 0;
            
            // First, determine which categories to display based on the search filter
            std::map<std::string, bool> shouldDisplayCategory;
            if (!searchString.empty())
            {
                // If we have a search string, check which categories have matching entries
                for (const auto& [category, indices] : m_categoryToEntriesMap)
                {
                    shouldDisplayCategory[category] = false;
                    for (size_t index : indices)
                    {
                        const auto& entry = m_entries[index];
                        std::string lowerName = entry.displayName;
                        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                            [](unsigned char c){ return std::tolower(c); });
                        
                        if (lowerName.find(searchString) != std::string::npos)
                        {
                            shouldDisplayCategory[category] = true;
                            
                            // Also mark parent categories as visible
                            std::string parentPath;
                            std::istringstream categoryStream(category);
                            std::string segment;
                            while (std::getline(categoryStream, segment, '/'))
                            {
                                if (!parentPath.empty())
                                {
                                    parentPath += "/";
                                    shouldDisplayCategory[parentPath] = true;
                                }
                                parentPath += segment;
                                shouldDisplayCategory[parentPath] = true;
                            }
                        }
                    }
                }
            }
            else
            {
                // If no search, all categories should be visible
                for (const auto& [category, _] : m_categoryToEntriesMap)
                {
                    shouldDisplayCategory[category] = true;
                }
            }
            
            // Helper function to get immediate subcategories
            auto getDirectSubcategories = [](const std::string& parentCategory, const std::map<std::string, std::vector<size_t>>& categories) {
                std::vector<std::string> result;
                
                // If parent is empty, return all root categories
                if (parentCategory.empty()) 
                {
                    for (const auto& [category, _] : categories) 
                    {
                        if (category.find('/') == std::string::npos) 
                        {
                            result.push_back(category);
                        }
                    }
                    return result;
                }
                
                const std::string prefix = parentCategory + "/";
                for (const auto& [category, _] : categories) 
                {
                    if (category.starts_with(prefix)) 
                    {
                        // Check if this is a direct child (no additional slashes)
                        std::string relativePath = category.substr(prefix.length());
                        if (relativePath.find('/') == std::string::npos) 
                        {
                            result.push_back(category);
                        }
                    }
                }
                return result;
            };
            
            // Recursive function to display the category hierarchy
            std::function<void(const std::string&, int&)> displayCategory = [&](const std::string& category, int& visibleCount) {
                if (!shouldDisplayCategory[category]) {
                    return;
                }
                
                // Get the category name without path
                std::string displayName = category;
                size_t lastSlash = category.find_last_of('/');
                if (lastSlash != std::string::npos) {
                    displayName = category.substr(lastSlash + 1);
                }
                
                // Get direct subcategories
                auto subcategories = getDirectSubcategories(category, m_categoryToEntriesMap);
                
                // Determine if this category has entries or only subcategories
                bool hasEntries = !m_categoryToEntriesMap[category].empty();
                bool hasSubcategories = !subcategories.empty();
                
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
                if (!hasSubcategories) {
                    flags |= ImGuiTreeNodeFlags_Leaf;
                }
                  // Special handling for root categories to be initially open
                bool isRootCategory = category.find('/') == std::string::npos;
                if (isRootCategory) {
                    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                }
                
                // Apply highlighting style if an entity is being dragged and we're hovering over this category
                bool isDragHovered = m_isDragging && ImGui::IsMouseHoveringRect(ImGui::GetCursorScreenPos(), 
                    ImVec2(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x, 
                           ImGui::GetCursorScreenPos().y + ImGui::GetFrameHeight()));
                if (isDragHovered) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetColorU32(ImGuiCol_ButtonHovered));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetColorU32(ImGuiCol_ButtonActive));
                }
                
                // First get folder icon texture
                ImTextureID folderIcon = GetFolderIconTexture();
                
                // Variable to track if node is open
                bool nodeOpen = false;
                
                // Display folder icon + tree node
                if (folderIcon)
                {
                    // Use a simpler approach - use PushID to create a unique ID context
                    ImGui::PushID(category.c_str());  // Use category as unique identifier
                    
                    // Get sizing information
                    const float TEXT_BASE_HEIGHT = ImGui::GetTextLineHeight();
                    const float ICON_SIZE = TEXT_BASE_HEIGHT;
                    const float SPACING = ImGui::GetStyle().ItemInnerSpacing.x;
                    
                    // Start with the folder icon
                    ImGui::Image(folderIcon, ImVec2(ICON_SIZE, ICON_SIZE));
                    ImGui::SameLine(0, SPACING);
                    
                    // Then display the tree node with just the text
                    nodeOpen = ImGui::TreeNode(displayName.c_str());
                    
                    ImGui::PopID();
                }
                else
                {
                    // Fallback if no icon is available
                    nodeOpen = ImGui::TreeNodeEx(displayName.c_str(), flags);
                }
                
                // Pop temporary style colors if we applied them
                if (isDragHovered)
                {
                    ImGui::PopStyleColor(2);
                }
                
                // Add drop target functionality for the category
                if (ImGui::BeginDragDropTarget())
                {
                    // Accept drop of an entity onto this category
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_ENTITY_ITEM"))
                    {
                        // Extract the entity ID from the payload
                        uint64 entityId = *(const uint64*)payload->Data;
                        
                        // Find the entity in our entries to get current details
                        for (const auto& entry : m_entries)
                        {
                            if (entry.id == entityId)
                            {
                                // Skip if the entity is already in this category to avoid unnecessary updates
                                if (entry.category != category)
                                {
                                    // Call the category change callback
                                    if (m_categoryChangeCallback)
                                    {
                                        m_categoryChangeCallback(entityId, category);
                                        m_needsUpdate = true;
                                    }
                                }

                                break;
                            }
                        }
                        
                        // Reset drag state immediately after successful drop
                        m_isDragging = false;
                        m_draggedEntityId = 0;
                        m_draggedEntityName.clear();
                    }
                    ImGui::EndDragDropTarget();
                }
                
                // Reset drag state when the mouse is released
                if (ImGui::IsMouseReleased(0) && m_isDragging)
                {
                    m_isDragging = false;
                    m_draggedEntityId = 0;
                    m_draggedEntityName.clear();
                }
                
                if (nodeOpen) 
                {
                    // Display direct entries in this category
                    if (hasEntries) 
                    {
                        for (size_t index : m_categoryToEntriesMap[category]) 
                        {
                            const auto& entry = m_entries[index];
                            
                            // Check if this entry passes the search filter
                            bool passesSearchFilter = true;
                            if (!searchString.empty()) 
                            {
                                std::string lowerName = entry.displayName;
                                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                                    [](unsigned char c){ return std::tolower(c); });
                                
                                passesSearchFilter = lowerName.find(searchString) != std::string::npos;
                            }
                            
                            if (passesSearchFilter) 
                            {
                                DisplayEntry(entry);
                                visibleCount++;
                            }
                        }
                    }
                    
                    // Display subcategories
                    for (const auto& subcategory : subcategories) 
                    {
                        displayCategory(subcategory, visibleCount);
                    }
                    
                    ImGui::TreePop();
                }
            };            // Display root categories
            auto rootCategories = getDirectSubcategories("", m_categoryToEntriesMap);
            for (const auto& category : rootCategories) 
            {
                displayCategory(category, visibleEntities);
            }
            ImGui::EndChild();
            
            // Display entity count in the reserved space below the scroll view
            // Add subtle visual separation
            ImGui::Separator();
            
            // Center the status text
            float windowWidth = ImGui::GetWindowWidth();
            std::string statusText = "Displaying " + std::to_string(visibleEntities) + " entities";
            float textWidth = ImGui::CalcTextSize(statusText.c_str()).x;
            ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
            
            // Display with slightly different styling to make it stand out
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "%s", statusText.c_str());
            
            // Handle the category change modal outside of any entity processing loop
            // This is important because ImGui modals need to be processed at a consistent place in the UI hierarchy
            
            // If the popup flag is set, explicitly open the popup
            if (m_openCategoryChangePopup)
            {
                ImGui::OpenPopup("Change Category");
                m_openCategoryChangePopup = false;
            }
                
            // Modal dialog with auto resize
            bool isOpen = true;
            if (ImGui::BeginPopupModal("Change Category", &isOpen, ImGuiWindowFlags_AlwaysAutoResize)) 
            {
                // Set keyboard focus to input field when modal opens
                static bool modalFirstFrame = true;
                if (modalFirstFrame)
                {
                    ImGui::SetKeyboardFocusHere();
                    modalFirstFrame = false;
                }
                
                // If the popup was closed without using the buttons, reset state
                if (!isOpen)
                {
                    m_categoryChangeEntityId = 0;
                    modalFirstFrame = true;
                    ImGui::CloseCurrentPopup();
                }
                
                ImGui::Text("Enter category path (e.g. Haven/Buildings):");
                
                // Highlight the input field
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetColorU32(ImGuiCol_FrameBgHovered));
                
                // Add AutoSelectAll flag to select all text when focused
                bool inputActivated = ImGui::InputText("##category", m_categoryBuffer, IM_ARRAYSIZE(m_categoryBuffer), 
                                   ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                
                // Remove highlighting after input field
                ImGui::PopStyleColor();
                
                // Explicitly handle keyboard focus 
                if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Tab)) && ImGui::IsItemActive())
                {
                    ImGui::SetKeyboardFocusHere(1); // Move focus to next widget (OK button)
                }
                
                // Handle Escape to cancel
                if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)))
                {
                    m_categoryChangeEntityId = 0;
                    modalFirstFrame = true;
                    ImGui::CloseCurrentPopup();
                }
                
                if (inputActivated) 
                {
                    // Handle Enter key as OK button press
                    if (m_categoryChangeCallback && m_categoryChangeEntityId != 0) 
                    {
                        m_categoryChangeCallback(m_categoryChangeEntityId, m_categoryBuffer);
                        m_needsUpdate = true;
                    }

                    m_categoryChangeEntityId = 0;
                    modalFirstFrame = true;
                    ImGui::CloseCurrentPopup();
                }
                
                ImGui::Separator();
                  // Make the buttons more noticeable
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonHovered));
                
                if (ImGui::Button("OK", ImVec2(120, 0))) 
                {
                    if (m_categoryChangeCallback && m_categoryChangeEntityId != 0) 
                    {
                        // Use the stored entity ID instead of the current entry
                        m_categoryChangeCallback(m_categoryChangeEntityId, m_categoryBuffer);
                        
                        // Flag for update to reflect the changes immediately
                        m_needsUpdate = true;
                    }
                    
                    // Reset the tracking ID
                    m_categoryChangeEntityId = 0; 
                    modalFirstFrame = true;
                    ImGui::CloseCurrentPopup();
                }
                
                ImGui::PopStyleColor();
                ImGui::SameLine();
                
                if (ImGui::Button("Cancel", ImVec2(120, 0))) 
                {
                    // Reset the tracking ID
                    m_categoryChangeEntityId = 0;
                    modalFirstFrame = true;
                    ImGui::CloseCurrentPopup();
                }
                
                ImGui::EndPopup();
            }
            
            // Global check for mouse release to properly reset drag state
            // This handles the case when user drops outside of a valid target
            if (ImGui::IsMouseReleased(0) && m_isDragging)
            {
                m_isDragging = false;
                m_draggedEntityId = 0;
                m_draggedEntityName.clear();
            }
        }
        
        ImGui::End();
    }

    void SceneOutlineWindow::Update()
	{
	    // Set the flag for update, but the actual update will happen in Draw
	    // if enough time has passed since the last update
	    m_needsUpdate = true;
	}

    void SceneOutlineWindow::ClearSelection()
    {
        m_selection.Clear();
        Update();
    }

    void SceneOutlineWindow::SetDeleteCallback(std::function<void(uint64)> callback)
    {
        m_deleteCallback = std::move(callback);
    }

    void SceneOutlineWindow::SetRenameCallback(std::function<void(uint64, const std::string&)> callback)
    {
        m_renameCallback = std::move(callback);
    }

    void SceneOutlineWindow::SetCategoryChangeCallback(std::function<void(uint64, const std::string&)> callback)
    {
        m_categoryChangeCallback = std::move(callback);
    }    void SceneOutlineWindow::BuildEntryList()
    {
        m_entries.clear();
        m_categoryToEntriesMap.clear();
        
        // First, add a special "Uncategorized" category
        m_categoryToEntriesMap["Uncategorized"] = {};
        
        // Get all entities from the scene
        const auto allEntities = m_scene.GetAllEntities();
        
        // Add each entity to the outline, focusing on MapEntity objects
        for (const auto* entity : allEntities)
        {
            const std::string entityName = entity->GetName();
            
            // Skip utility objects like grids, sky, debug helpers, etc.
            if (entityName.find("Grid") != std::string::npos ||
                entityName.find("Sky") != std::string::npos ||
                entityName.find("Debug") != std::string::npos ||
                entityName.find("Terrain") != std::string::npos ||
                entityName.find("Axis") != std::string::npos ||
                entityName.find("Transform") != std::string::npos ||
                entityName.find("Circle") != std::string::npos ||
                entityName.find("Plane") != std::string::npos ||
                entityName.find("Camera") != std::string::npos)
            {
                continue;
            }
            
            // Process MapEntity objects - these are the world objects we want to show
            MapEntity* mapEntity = nullptr;
            try
            {
                mapEntity = entity->GetUserObject<MapEntity>();
                if (!mapEntity)
                {
                    continue;
                }
            }
            catch(...)
            {
                continue;
            }
            
            // Create and populate entry
            SceneOutlineEntry entry;
            entry.name = entityName;
            entry.selected = false;
            entry.entityPtr = mapEntity;
            entry.id = 0;
            
            if (mapEntity)
            {
                // Use the unique entity ID
                entry.id = mapEntity->GetUniqueId();
                
                // Use custom name if available, otherwise use entity name
                entry.displayName = mapEntity->GetDisplayName();
                if (entry.displayName.empty())
                {
                    entry.displayName = mapEntity->GetEntity().GetName();
                }
                
                // Use category if available
                entry.category = mapEntity->GetCategory();
                if (entry.category.empty())
                {
                    entry.category = "Uncategorized";
                }
                
                // Check if category exists in map, if not create it
                if (m_categoryToEntriesMap.find(entry.category) == m_categoryToEntriesMap.end())
                {
                    m_categoryToEntriesMap[entry.category] = {};
                }
                
                // Also create parent categories if needed
                std::string categoryPath;
                std::istringstream categoryStream(entry.category);
                std::string segment;
                while (std::getline(categoryStream, segment, '/'))
                {
                    if (!categoryPath.empty())
                    {
                        categoryPath += "/";
                    }
                    categoryPath += segment;
                    
                    if (m_categoryToEntriesMap.find(categoryPath) == m_categoryToEntriesMap.end())
                    {
                        m_categoryToEntriesMap[categoryPath] = {};
                    }
                }
            }
            
            // Check if this entity is selected
            const auto& selectedObjects = m_selection.GetSelectedObjects();
            for (const auto& selectedObject : selectedObjects)
			{
                if (auto* mapEntitySelectable = dynamic_cast<SelectedMapEntity*>(selectedObject.get()))
                {
                    if (mapEntitySelectable->GetEntity().GetUniqueId() == entry.id)
                    {
                        entry.selected = true;
                        break;
                    }
                }
            }
            
            // Add entry to the list
            m_entries.push_back(std::move(entry));
        }
        
        // Update category mappings with indices of entries
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            const auto& entry = m_entries[i];
            m_categoryToEntriesMap[entry.category].push_back(i);
        }
        
        // Sort entries in each category alphabetically by display name
        for (auto& [category, indices] : m_categoryToEntriesMap)
        {
            std::sort(indices.begin(), indices.end(), [this](size_t a, size_t b) {
                return m_entries[a].displayName < m_entries[b].displayName;
            });
        }
    }    void SceneOutlineWindow::DisplayEntry(const SceneOutlineEntry& entry)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        
        if (entry.selected)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        // Create display text with icon and name
        std::string displayText = entry.displayName;
            
        // Handle renaming - Check if this entry is currently being renamed
        if (m_editingId == entry.id) 
        {
            // Editing mode - highlight and visually indicate that we're in edit mode
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetColorU32(ImGuiCol_FrameBgActive));
            ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetColorU32(ImGuiCol_HeaderActive));
            
            // Make the input field fill the available width
            ImGui::SetNextItemWidth(-1);
            bool finishedEdit = false;
            
            // Set focus to the input field - using a static flag that's scoped to this entity ID
            static uint64 lastEditId = 0;
            if (lastEditId != entry.id)
            {
                // This is a new edit for a different entity, force focus
                ImGui::SetKeyboardFocusHere();
                lastEditId = entry.id;
            }
            
            // Handle the input field
            if (ImGui::InputText(("##rename" + std::to_string(entry.id)).c_str(), m_nameBuffer, 
                               IM_ARRAYSIZE(m_nameBuffer), 
                               ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) 
            {
                finishedEdit = true;
                lastEditId = 0; // Reset for next time
            }
            
            // Handle ESC key to cancel
            if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)))
            {
                m_editingId = 0;
                lastEditId = 0; // Reset the focus tracker
                finishedEdit = false;
                ImGui::SetWindowFocus(); // Return focus to the window
            }
            
            // Handle focus loss as commit after a short delay to avoid accidental commits
            if (!ImGui::IsItemActive() && m_editingId != 0 && lastEditId == entry.id) 
            {
                finishedEdit = true;
                lastEditId = 0; // Reset the focus tracker
            }
            
            // Apply the name change if we've finished editing
            if (finishedEdit) 
            {
                if (m_renameCallback && entry.entityPtr) 
                {
                    m_renameCallback(entry.id, m_nameBuffer);
                    
                    // Flag for update to reflect the changes immediately
                    m_needsUpdate = true;
                }
                m_editingId = 0;
            }
            
            // Clean up the style colors
            ImGui::PopStyleColor(2);
        }
        else 
        {
        	// Normal display mode
            bool nodeOpen = ImGui::TreeNodeEx(displayText.c_str(), flags);
            
            // Double-click to rename
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) 
            {
                if (entry.entityPtr) 
                {
                    m_editingId = entry.id;
                    strncpy(m_nameBuffer, entry.displayName.c_str(), sizeof(m_nameBuffer) - 1);
                    m_nameBuffer[sizeof(m_nameBuffer) - 1] = '\0';
                }
            }
            
            // Begin drag source for category changes
            if (entry.entityPtr && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) 
            {
                // Set the drag data - we'll use the entity ID and name as the payload
                ImGui::SetDragDropPayload("SCENE_ENTITY_ITEM", &entry.id, sizeof(uint64));
                
                // Display what's being dragged
                ImGui::Text("Moving: %s", entry.displayName.c_str());
                
                // Store drag state for reference elsewhere
                m_isDragging = true;
                m_draggedEntityId = entry.id;
                m_draggedEntityName = entry.displayName;
                
                ImGui::EndDragDropSource();
            }
              
            // Handle selection on click
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) 
            {
                Entity* entity = m_scene.GetEntity(entry.name);
                if (entity) 
                {
                    if (!ImGui::GetIO().KeyCtrl) 
                    {
                        m_selection.Clear();
                    }
                    
                    // If it's a map entity, find it and select it
                    if (entry.entityPtr) 
                    {
                        m_selection.AddSelectable(std::make_unique<SelectedMapEntity>(*entry.entityPtr, [](Selectable&) {}));
                    }
                    // Other entity types can be handled here
                }
            }
            
            // Context menu
            if (ImGui::BeginPopupContextItem()) 
            {
                // Only show these options for map entities
                if (entry.entityPtr) 
                {
                	if (ImGui::MenuItem("Rename")) 
                    {
                        m_editingId = entry.id;
                        strncpy(m_nameBuffer, entry.displayName.c_str(), sizeof(m_nameBuffer) - 1);
                        m_nameBuffer[sizeof(m_nameBuffer) - 1] = '\0';
                        
                        // This forces ImGui to set focus to the rename field in the next frame
                        ImGui::SetNextWindowFocus();
                        
                        // Close the context menu immediately after selecting rename
                        ImGui::CloseCurrentPopup();
                    }
                	if (ImGui::MenuItem("Change Category")) 
                    {
                        // Store which entity we're changing
                        m_categoryChangeEntityId = entry.id;
                        
                        // Copy category to buffer
                        strncpy(m_categoryBuffer, entry.category.c_str(), sizeof(m_categoryBuffer) - 1);
                        m_categoryBuffer[sizeof(m_categoryBuffer) - 1] = '\0';
                        
                        // Set flag to open popup and close the current context menu
                        m_openCategoryChangePopup = true;
                        ImGui::CloseCurrentPopup(); // Close context menu to prevent overlap
                    }
                    
                    ImGui::Separator();
                }
                
                if (ImGui::MenuItem("Delete")) 
                {
                    if (m_deleteCallback && entry.id != 0) 
                    {
                        m_deleteCallback(entry.id);
                        
                        // Flag for update to reflect the changes immediately
                        m_needsUpdate = true;
                    }
                }
                
                if (ImGui::MenuItem("Focus")) 
                {
                    // Could implement focus functionality here
                }
                
                ImGui::EndPopup();
            }
        }
    }
}
