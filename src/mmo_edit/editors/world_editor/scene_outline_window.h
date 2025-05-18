#pragma once

#include "base/non_copyable.h"
#include "selection.h"
#include "scene_graph/scene.h"
#include <functional>
#include <string>
#include <vector>
#include <chrono>

namespace mmo
{
    class SceneOutlineWindow : public NonCopyable
    {
    public:
        SceneOutlineWindow(Selection& selection, Scene& scene);
        ~SceneOutlineWindow() = default;

        // Draw the scene outline window
        void Draw(const std::string& title);

        // Update the internal list of scene objects
        void Update();

        // Clears the selection when the scene outline is cleared
        void ClearSelection();

        // Set the callback to be called when an object is deleted
        void SetDeleteCallback(std::function<void(uint64)> callback);

    private:
        struct SceneOutlineEntry 
        {
            uint64 id;
            std::string name;
            bool selected;
            bool isUnitSpawn;
            bool isObjectSpawn;
            bool isMapEntity;
        };

        void BuildEntryList();
        void DisplayEntry(const SceneOutlineEntry& entry);    private:
        Selection& m_selection;
        Scene& m_scene;
        std::vector<SceneOutlineEntry> m_entries;
        bool m_needsUpdate;
        std::function<void(uint64)> m_deleteCallback;
        std::chrono::steady_clock::time_point m_lastRebuildTime;
    };
}
