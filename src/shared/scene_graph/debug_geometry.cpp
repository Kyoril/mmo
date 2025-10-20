#include "debug_geometry.h"
#include "scene_graph/manual_render_object.h"
#include "scene_graph/material_manager.h"

namespace mmo
{
    DebugGeometry::DebugGeometry(Scene& scene, const String& name)
        : m_scene(scene)
        , m_debugNode(nullptr)
        , m_debugObject(nullptr)
        , m_hasLines(false)
        , m_hasTriangles(false)
        , m_isVisible(false)
    {
        // Create a scene node for the debug geometry visualization
        m_debugNode = m_scene.GetRootSceneNode().CreateChildSceneNode(name + "_Node");

        // Create a manual object to render the debug geometry
        m_debugObject = m_scene.CreateManualRenderObject(name + "_Object");
        m_debugObject->SetRenderQueueGroup(Overlay);
        m_debugNode->AttachObject(*m_debugObject);
        m_debugObject->SetCastShadows(false);

        // Initially hidden
        m_debugNode->SetVisible(false);

        m_renderConnection = m_debugObject->objectRendering.connect(this, &DebugGeometry::OnRendering);
    }

    DebugGeometry::~DebugGeometry()
    {
        // Clean up render operations first
        m_lineOperation.reset();
        m_triangleOperation.reset();
        
        // Clean up scene objects
        if (m_debugObject)
        {
            m_scene.DestroyManualRenderObject(*m_debugObject);
            m_debugObject = nullptr;
        }
        
        if (m_debugNode)
        {
            m_scene.DestroySceneNode(*m_debugNode);
            m_debugNode = nullptr;
        }
    }

    void DebugGeometry::AddLine(const Vector3& start, const Vector3& end, uint32 color)
    {
        EnsureLineOperations();
        
        // Add the line to the operation
        (*m_lineOperation)->AddLine(start, end).SetColor(color);
        m_hasLines = true;
        
        // Make sure the debug geometry is visible
        if (!IsVisible())
        {
            SetVisible(true);
        }
    }

    void DebugGeometry::AddTriangle(const Vector3& v1, const Vector3& v2, const Vector3& v3, uint32 color)
    {
        EnsureTriangleOperations();
        
        // Add the triangle to the operation
        (*m_triangleOperation)->AddTriangle(v1, v2, v3).SetColor(color);
        m_hasTriangles = true;
        
        // Make sure the debug geometry is visible
        if (!IsVisible())
        {
            SetVisible(true);
        }
    }

    void DebugGeometry::Clear()
    {
        // Clear the manual render object, which removes all operations
        m_debugObject->Clear();
        
        // Reset operation pointers
        m_lineOperation.reset();
        m_triangleOperation.reset();
        
        // Reset state flags
        m_hasLines = false;
        m_hasTriangles = false;
        
        // Hide the debug geometry since there's nothing to show
        SetVisible(false);
    }

    void DebugGeometry::SetVisible(bool visible)
    {
        m_isVisible = visible;
        if (m_debugNode)
        {
            m_debugNode->SetVisible(visible);
            
            if (visible)
            {
                // Update bounds when making visible
                m_debugNode->UpdateBounds();
            }
        }
    }

    bool DebugGeometry::IsVisible() const
    {
        return m_isVisible;
    }

    void DebugGeometry::Finish()
    {
        if (m_triangleOperation) m_triangleOperation.reset();
		if (m_lineOperation) m_lineOperation.reset();
    }

    void DebugGeometry::EnsureLineOperations()
    {
        if (!m_lineOperation)
        {
            // Create line operation with debug material
            m_lineOperation = std::make_unique<ManualRenderOperationRef<ManualLineListOperation>>(
                m_debugObject->AddLineListOperation(
                    MaterialManager::Get().Load("Models/Engine/Axis.hmat")
                )
            );
        }
    }

    void DebugGeometry::EnsureTriangleOperations()
    {
        if (!m_triangleOperation)
        {
            // Create triangle operation with debug material
            m_triangleOperation = std::make_unique<ManualRenderOperationRef<ManualTriangleListOperation>>(
                m_debugObject->AddTriangleListOperation(
                    MaterialManager::Get().Load("Models/Engine/Axis.hmat")
                )
            );
        }
    }

    bool DebugGeometry::OnRendering(const MovableObject&, const Camera&)
    {


        return true;
    }
}
