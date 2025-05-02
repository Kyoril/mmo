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
