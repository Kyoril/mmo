#include "scene_outline_window.h"
#include "selected_map_entity.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include "scene_graph/entity.h"
#include "scene_graph/scene_node.h"
#include "world_editor_instance.h" // Include this for MapEntity definition

namespace mmo
{    SceneOutlineWindow::SceneOutlineWindow(Selection& selection, Scene& scene)
        : m_selection(selection)
        , m_scene(scene)
        , m_needsUpdate(true)
        , m_lastRebuildTime()
        , m_editingId(0)
        , m_categoryChangeEntityId(0)
        , m_openCategoryChangePopup(false)
    {
        // Initialize buffers
        m_nameBuffer[0] = '\0';
        m_categoryBuffer[0] = '\0';
    }    void SceneOutlineWindow::Draw(const std::string& title)
    {
        // We'll handle the popup opening in the main body now
        
        if (ImGui::Begin(title.c_str()))
        {
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

            // Use child window to get scrolling
            ImGui::BeginChild("SceneObjectsList", ImVec2(0, 0), true);
            
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
                
                // Display the category
                bool nodeOpen = ImGui::TreeNode(displayName.c_str());
                
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
            };
            
            // Display root categories
            auto rootCategories = getDirectSubcategories("", m_categoryToEntriesMap);
            for (const auto& category : rootCategories) 
            {
                displayCategory(category, visibleEntities);
            }
              // Show count of displayed entities
            ImGui::Separator();
            ImGui::Text("Displaying %d entities", visibleEntities);

            ImGui::EndChild();            // Handle the category change modal outside of any entity processing loop
            // This is important because ImGui modals need to be processed at a consistent place in the UI hierarchy
            
            // If the popup flag is set, explicitly open the popup
            if (m_openCategoryChangePopup) {
                ImGui::OpenPopup("Change Category");
                m_openCategoryChangePopup = false;
            }
                
            // Modal dialog with auto resize
            bool isOpen = true;
            if (ImGui::BeginPopupModal("Change Category", &isOpen, ImGuiWindowFlags_AlwaysAutoResize)) 
            {
                // Set keyboard focus to input field when modal opens
                static bool setFocus = true;
                if (setFocus) {
                    ImGui::SetKeyboardFocusHere();
                    setFocus = false;
                }
                  ImGui::Text("Enter category path (e.g. Haven/Buildings):");
                
                // Add AutoSelectAll flag to select all text when focused
                bool inputActivated = ImGui::InputText("##category", m_categoryBuffer, IM_ARRAYSIZE(m_categoryBuffer), 
                                   ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                
                // Explicitly handle keyboard focus 
                if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Tab)) && ImGui::IsItemActive())
                {
                    ImGui::SetKeyboardFocusHere(1); // Move focus to next widget (OK button)
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
                    setFocus = true;
                    ImGui::CloseCurrentPopup();
                }
                
                ImGui::Separator();
                
                if (ImGui::Button("OK", ImVec2(120, 0)) || (!isOpen)) 
                {
                    if (m_categoryChangeCallback && m_categoryChangeEntityId != 0) 
                    {
                        // Use the stored entity ID instead of the current entry
                        m_categoryChangeCallback(m_categoryChangeEntityId, m_categoryBuffer);
                        
                        // Flag for update to reflect the changes immediately
                        m_needsUpdate = true;
                    }
                    m_categoryChangeEntityId = 0; // Reset the tracking ID
                    setFocus = true;
                    ImGui::CloseCurrentPopup();
                }
                
                ImGui::SameLine();
                
                if (ImGui::Button("Cancel", ImVec2(120, 0))) 
                {
                    m_categoryChangeEntityId = 0; // Reset the tracking ID
                    setFocus = true;
                    ImGui::CloseCurrentPopup();
                }
                
                ImGui::EndPopup();
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
        std::string displayText = entry.displayName;            // Handle renaming
        if (m_editingId == entry.id) 
        {
            // Editing mode - help the user know we're in edit mode
            ImGui::PushStyleColor(ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetColorU32(ImGuiCol_FrameBgActive));
            ImGui::SetNextItemWidth(-1);
            bool finishedEdit = false;
            
            // Set focus to the input field on the first frame
            static bool setFocus = true;
            if (setFocus)
            {
                ImGui::SetKeyboardFocusHere();
                setFocus = false;
            }
            
            if (ImGui::InputText("##rename", m_nameBuffer, IM_ARRAYSIZE(m_nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) 
            {
                finishedEdit = true;
                setFocus = true; // Reset for next time
            }
            
            // Handle focus loss as commit
            if (!ImGui::IsItemActive() && m_editingId != 0) 
            {
                finishedEdit = true;
                setFocus = true; // Reset for next time
            }
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
            
            // Clean up the style color
            ImGui::PopStyleColor();
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
