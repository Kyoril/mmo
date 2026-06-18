// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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
        explicit DebugPathVisualizer(Scene& scene);
        ~DebugPathVisualizer();

        /// Show a movement path as a series of connected red lines.
        void ShowPath(const std::vector<Vector3>& path, float duration = 10.0f);

        /// Show a line-of-sight debug ray.
        /// When hasLos is true a single green line from->to is drawn.
        /// When hasLos is false a red line from->hitPoint and a dim yellow line
        /// hitPoint->to are drawn, with an X marker at hitPoint.
        void ShowLineOfSight(const Vector3& from, const Vector3& to, const Vector3& hitPoint, bool hasLos, float duration = 10.0f);

        void ClearPath();

        void Update(float deltaSeconds);

    private:
        Scene& m_scene;
        SceneNode* m_pathNode;
        ManualRenderObject* m_pathObject;
        float m_remainingTime;
    };
}