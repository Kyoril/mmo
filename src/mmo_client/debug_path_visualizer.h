#pragma once

#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/manual_render_object.h"
#include "math/vector3.h"

#include <vector>
#include <memory>

namespace mmo
{
    class DebugPathVisualizer
    {
    public:
        DebugPathVisualizer(Scene& scene);
        ~DebugPathVisualizer();

        // Show path visualization
        void ShowPath(const std::vector<Vector3>& path, float duration = 10.0f);

        // Clear current path visualization
        void ClearPath();

        // Update method to handle automatic expiration
        void Update(float deltaSeconds);

    private:
        Scene& m_scene;
        SceneNode* m_pathNode;
        ManualRenderObject* m_pathObject;
        float m_remainingTime;
    };
}