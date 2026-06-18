#include "debug_path_visualizer.h"
#include "scene_graph/manual_render_object.h"
#include "scene_graph/material_manager.h"

namespace mmo
{
    DebugPathVisualizer::DebugPathVisualizer(Scene& scene)
        : m_scene(scene)
        , m_remainingTime(0.0f)
    {
        // Create a scene node for the path visualization
        m_pathNode = m_scene.GetRootSceneNode().CreateChildSceneNode("DebugPathNode");

        // Create a manual object to render the path
        m_pathObject = m_scene.CreateManualRenderObject("DebugPathObject");
        m_pathObject->SetRenderQueueGroup(Overlay);
        m_pathNode->AttachObject(*m_pathObject);
        m_pathObject->SetCastShadows(false);

        // Initially hidden
    	m_pathNode->SetVisible(false);
    }

    DebugPathVisualizer::~DebugPathVisualizer()
    {
        m_scene.DestroyManualRenderObject(*m_pathObject);
        m_scene.DestroySceneNode(*m_pathNode);
    }

    void DebugPathVisualizer::ShowPath(const std::vector<Vector3>& path, float duration)
    {
        // Clear any existing path
        ClearPath();

        if (path.size() < 2)
            return;

        // Begin defining the path object
        {
	        ManualRenderOperationRef<ManualLineListOperation> lineOperation = m_pathObject->AddLineListOperation(
		        MaterialManager::Get().Load("Models/Engine/Axis.hmat"));

            // Draw lines between points
            for (size_t i = 0; i < path.size() - 1; ++i)
            {
                // Main path line
                lineOperation->AddLine(
                    path[i], path[i + 1]
                ).SetColor(0xffff0000);
            }

            // Add point markers
            for (const auto& point : path)
            {
                // Small vertical line at each point for visibility
                lineOperation->AddLine(
                    point, point + Vector3::UnitY * 0.5f
                ).SetColor(0xff00ff00);
            }

            // Especially mark start and end points
            if (!path.empty())
            {
                // Start point - Blue
                const float markerSize = 0.5f;
                const Vector3& start = path.front();

                lineOperation->AddLine(
                    start + Vector3(-markerSize, 0.0f, 0.0f), start + Vector3(markerSize, 0.0f, 0.0f)
                ).SetColor(0xff0000ff);

                lineOperation->AddLine(
                    start + Vector3(0.0f, 0.0f, -markerSize), start + Vector3(0.0f, 0.0f, markerSize)
                ).SetColor(0xff0000ff);

                // End point - Yellow
                const Vector3& end = path.back();
                lineOperation->AddLine(
                    end + Vector3(-markerSize, 0.0f, 0.0f), end + Vector3(markerSize, 0.0f, 0.0f)
                ).SetColor(0xffffff00);

                lineOperation->AddLine(
                    end + Vector3(0.0f, 0.0f, -markerSize), end + Vector3(0.0f, 0.0f, markerSize)
                ).SetColor(0xffffff00);
            }
        }
        
        // Show the path and set duration
        m_pathNode->SetVisible(true);
        m_pathNode->UpdateBounds();
        m_remainingTime = duration;
    }

    void DebugPathVisualizer::ShowLineOfSight(const Vector3& from, const Vector3& to, const Vector3& hitPoint, bool hasLos, float duration)
    {
        ClearPath();

        // Begin defining the path object
        {
            ManualRenderOperationRef<ManualLineListOperation> lineOp = m_pathObject->AddLineListOperation(
                MaterialManager::Get().Load("Models/Engine/Axis.hmat"));

            if (hasLos)
            {
                // Green line: full ray is clear.
                lineOp->AddLine(from, to).SetColor(0xff00ff00);
            }
            else
            {
                // Red line from origin to hit point.
                lineOp->AddLine(from, hitPoint).SetColor(0xffff0000);

                // Dim yellow line from hit point to target (blocked portion).
                lineOp->AddLine(hitPoint, to).SetColor(0xff888800);

                // X marker at hit point to make it obvious.
                constexpr float s = 0.4f;
                lineOp->AddLine(hitPoint + Vector3(-s, 0.f, 0.f), hitPoint + Vector3(s, 0.f, 0.f)).SetColor(0xffff4400);
                lineOp->AddLine(hitPoint + Vector3(0.f, 0.f, -s), hitPoint + Vector3(0.f, 0.f, s)).SetColor(0xffff4400);
                lineOp->AddLine(hitPoint + Vector3(-s, s, 0.f), hitPoint + Vector3(s, -s, 0.f)).SetColor(0xffff4400);
                lineOp->AddLine(hitPoint + Vector3(s, s, 0.f), hitPoint + Vector3(-s, -s, 0.f)).SetColor(0xffff4400);
            }

            // Small cross at origin (blue) and at target (white).
            constexpr float m = 0.3f;
            lineOp->AddLine(from + Vector3(-m, 0.f, 0.f), from + Vector3(m, 0.f, 0.f)).SetColor(0xff0088ff);
            lineOp->AddLine(from + Vector3(0.f, 0.f, -m), from + Vector3(0.f, 0.f, m)).SetColor(0xff0088ff);
            lineOp->AddLine(to + Vector3(-m, 0.f, 0.f), to + Vector3(m, 0.f, 0.f)).SetColor(0xffffffff);
            lineOp->AddLine(to + Vector3(0.f, 0.f, -m), to + Vector3(0.f, 0.f, m)).SetColor(0xffffffff);
        }

        m_pathNode->SetVisible(true);
        m_pathNode->UpdateBounds();
        m_remainingTime = duration;
    }

    void DebugPathVisualizer::ClearPath()
    {
        m_pathObject->Clear();
        m_pathNode->SetVisible(false);
        m_remainingTime = 0.0f;
    }

    void DebugPathVisualizer::Update(float deltaSeconds)
    {
        if (m_remainingTime > 0.0f)
        {
            m_remainingTime -= deltaSeconds;

            if (m_remainingTime <= 0.0f)
            {
                ClearPath();
            }
        }
    }
}
