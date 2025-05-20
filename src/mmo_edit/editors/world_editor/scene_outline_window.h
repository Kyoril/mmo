#pragma once

#include "base/non_copyable.h"
#include "selection.h"
#include "scene_graph/scene.h"
#include <functional>
#include <string>
#include <vector>
#include <chrono>

#include "preview_providers/static_texture_preview_provider.h"

namespace mmo
{
    class MapEntity;  // Forward declaration of MapEntity

    class SceneOutlineWindow final : public NonCopyable
    {
    public:
        SceneOutlineWindow(Selection& selection, Scene& scene);
        ~SceneOutlineWindow() = default;

        // Draw the scene outline window
        void Draw(const std::string& title);

        // Update the internal list of scene objects
        void Update();

        // Clears the selection when the scene outline is cleared
        void ClearSelection();        // Set the callback to be called when an object is deleted
        void SetDeleteCallback(std::function<void(uint64)> callback);

        // Set the callback to be called when an object is renamed
        void SetRenameCallback(std::function<void(uint64, const std::string&)> callback);

        // Set the callback to be called when an object's category is changed
        void SetCategoryChangeCallback(std::function<void(uint64, const std::string&)> callback);    private:
        struct SceneOutlineEntry 
        {
            uint64 id;
            std::string name;        // Original entity name
            std::string displayName; // Custom name if set, otherwise original name
            std::string category;    // Hierarchical category path like "Haven/Buildings"
            bool selected;
            MapEntity* entityPtr;    // Direct pointer to the MapEntity for easier access
        };

        void BuildEntryList();
        void DisplayEntry(const SceneOutlineEntry& entry);   
        
        // Get ImGui texture ID for folder icon
        ImTextureID GetFolderIconTexture() const;

    private:        
        Selection& m_selection;
        Scene& m_scene;
        std::vector<SceneOutlineEntry> m_entries;
        bool m_needsUpdate;
        std::function<void(uint64)> m_deleteCallback;
        std::function<void(uint64, const std::string&)> m_renameCallback;
        std::function<void(uint64, const std::string&)> m_categoryChangeCallback;
        std::chrono::steady_clock::time_point m_lastRebuildTime;
        TexturePtr m_folderTexture;  // Folder icon texture
        
        // UI state tracking variables
        uint64 m_editingId = 0; // Entity being renamed
        char m_nameBuffer[256] = {0};
        uint64 m_categoryChangeEntityId = 0; // Entity having its category changed
        char m_categoryBuffer[256] = {0};
        bool m_openCategoryChangePopup = false; // Flag to open category change popup
        
        // Drag and drop state tracking
        bool m_isDragging = false;
        uint64 m_draggedEntityId = 0;
        std::string m_draggedEntityName;
        
        // Map of categories to their entries for hierarchical display
        std::map<std::string, std::vector<size_t>> m_categoryToEntriesMap;
    };
}
