#include "scene_outline_window.h"
#include "selected_map_entity.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include "scene_graph/entity.h"
#include "scene_graph/scene_node.h"
#include "world_editor_instance.h" // Include this for MapEntity definition

namespace mmo
{
    SceneOutlineWindow::SceneOutlineWindow(Selection& selection, Scene& scene)
        : m_selection(selection)
        , m_scene(scene)
        , m_needsUpdate(true)
        , m_lastRebuildTime()
    {
    }

    void SceneOutlineWindow::Draw(const std::string& title)
    {
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
            }            // Add a filter checkbox to show only entities and spawns
            static bool showOnlyEntitiesAndSpawns = true;
            if (ImGui::Checkbox("Show only Entities & Spawns", &showOnlyEntitiesAndSpawns))
            {
                m_needsUpdate = true;
            }
            
            // Add a search filter
            static char searchBuffer[128] = "";
            ImGui::Text("Search: ");
            ImGui::SameLine();
            if (ImGui::InputText("##SearchFilter", searchBuffer, IM_ARRAYSIZE(searchBuffer)))
            {
                // No need to rebuild the list, we'll filter during display
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

            // Create a list box with filtered entities
            ImGui::Separator();

            // Use child window to get scrolling
            ImGui::BeginChild("SceneObjectsList", ImVec2(0, 0), true);            // Get the search string as lowercase for case-insensitive comparison
            std::string searchString = searchBuffer;
            std::transform(searchString.begin(), searchString.end(), searchString.begin(),
                [](unsigned char c){ return std::tolower(c); });
                
            // Display entities based on filters
            int visibleEntities = 0;
            for (const auto& entry : m_entries)
            {
                bool passesTypeFilter = !showOnlyEntitiesAndSpawns || 
                                      entry.isMapEntity || 
                                      entry.isUnitSpawn || 
                                      entry.isObjectSpawn;
                                      
                // Check search filter - only if search string isn't empty
                bool passesSearchFilter = true;
                if (!searchString.empty())
                {
                    std::string lowerName = entry.name;
                    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                        [](unsigned char c){ return std::tolower(c); });
                    
                    passesSearchFilter = lowerName.find(searchString) != std::string::npos;
                }
                
                // Display entry if it passes both filters
                if (passesTypeFilter && passesSearchFilter)
                {
                    DisplayEntry(entry);
                    visibleEntities++;
                }
            }
            
            // Show count of displayed entities
            ImGui::Separator();
            ImGui::Text("Displaying %d of %d entities", visibleEntities, static_cast<int>(m_entries.size()));

            ImGui::EndChild();
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

    void SceneOutlineWindow::BuildEntryList()
    {
        m_entries.clear();

        // Get all entities from the scene
        const auto allEntities = m_scene.GetAllEntities();
        
        // Add each entity to the outline, filtering utility objects
        for (const auto* entity : allEntities)
        {
            // Skip utility objects like grids, sky, debug helpers, etc.
            const std::string entityName = entity->GetName().data();
            
            // Filter out utility objects by name patterns
            if (entityName.find("Grid") != std::string::npos ||
                entityName.find("Sky") != std::string::npos ||
                entityName.find("Debug") != std::string::npos ||
                entityName.find("Terrain") != std::string::npos ||
                entityName.find("Axis") != std::string::npos ||
                entityName.find("Transform") != std::string::npos ||
                entityName.find("Circle") != std::string::npos ||
                entityName.find("Plane") != std::string::npos)
            {
                continue;
            }
                
            SceneOutlineEntry entry;
            entry.name = entityName;
            entry.selected = false;
            entry.isUnitSpawn = entityName.find("Spawn_") != std::string::npos;
            entry.isObjectSpawn = entityName.find("ObjectSpawn_") != std::string::npos;
            entry.isMapEntity = !entry.isUnitSpawn && !entry.isObjectSpawn; // Default to map entity if not spawn
            entry.id = 0;            // Check if this entity is selected - using a more efficient approach
            bool isSelected = false;
            const auto& selectedObjects = m_selection.GetSelectedObjects();
            
            // First try to get the MapEntity from user object to avoid expensive checks for each selection
            try
            {
                if (auto* mapEntity = entity->GetUserObject<MapEntity>())
                {
                    entry.isMapEntity = true;
                    entry.id = mapEntity->GetUniqueId();
                    
                    // Now check if this entity is selected by comparing IDs rather than pointers
                    for (const auto& selectedObject : selectedObjects)
                    {
                        if (auto* mapEntitySelectable = dynamic_cast<SelectedMapEntity*>(selectedObject.get()))
                        {
                            if (mapEntitySelectable->GetEntity().GetUniqueId() == mapEntity->GetUniqueId())
                            {
                                entry.selected = true;
                                break;
                            }
                        }
                    }
                }
            }
            catch(...)
            {
                // Silently ignore if the user object doesn't exist or isn't a MapEntity
            }

            m_entries.push_back(std::move(entry));
        }

        // Sort entries alphabetically by name
        std::sort(m_entries.begin(), m_entries.end(), 
            [](const SceneOutlineEntry& a, const SceneOutlineEntry& b) {
                return a.name < b.name;
            }
        );
    }

    void SceneOutlineWindow::DisplayEntry(const SceneOutlineEntry& entry)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        
        if (entry.selected)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        
        // Use a cached display name to avoid string concatenation in every frame
        // Custom display based on entity type, with minimal string operations
        std::string iconPrefix;
        if (entry.isMapEntity)
        {
            iconPrefix = "[Entity] ";
        }
        else if (entry.isUnitSpawn)
        {
            iconPrefix = "[Unit] ";
        }
        else if (entry.isObjectSpawn)
        {
            iconPrefix = "[Object] ";
        }
        
        // Create display text with icon and name
        std::string displayName = iconPrefix + entry.name;

        // Render the tree node
        bool nodeOpen = ImGui::TreeNodeEx(displayName.c_str(), flags);

        // Handle selection on click
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            // Performance optimization: Get entity only when needed
            Entity* entity = m_scene.GetEntity(entry.name);
            if (entity)
            {
                if (!ImGui::GetIO().KeyCtrl)
                {
                    m_selection.Clear();
                }

                // If it's a map entity, find it and select it
                if (entry.isMapEntity)
                {
                    try
                    {
                        MapEntity* mapEntity = entity->GetUserObject<MapEntity>();
                        if (mapEntity)
                        {
                            m_selection.AddSelectable(std::make_unique<SelectedMapEntity>(*mapEntity, [](Selectable&) {}));
                        }
                    }
                    catch(...)
                    {
                        // Silently ignore if the user object doesn't exist or isn't a MapEntity
                    }
                }
                // Other entity types can be added here
            }
        }

        // Context menu for each item - only process when actually right-clicked
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Delete"))
            {
                if (m_deleteCallback && entry.id != 0)
                {
                    m_deleteCallback(entry.id);
                }
            }
            
            // Add more context menu options
            if (ImGui::MenuItem("Focus"))
            {
                // Could implement focus on item functionality here
                // This would require adding a focus callback to the class
            }
            
            ImGui::EndPopup();
        }
    }
}
