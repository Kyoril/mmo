#pragma once

#include "scene_graph/debug_geometry_interface.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/manual_render_object.h"
#include "math/vector3.h"
#include "base/typedefs.h"

#include <memory>

namespace mmo
{
    /**
     * @brief Implementation of IDebugGeometry for drawing debug primitives.
     * 
     * This class provides functionality to draw colored lines and triangles in absolute
     * scene coordinates using ManualRenderObject internally. All geometry is rendered
     * with no shadows and in the overlay render queue for debugging purposes.
     */
    class DebugGeometry final : public IDebugGeometry
    {
    public:
        /**
         * @brief Constructs a new DebugGeometry instance.
         * 
         * @param scene The scene to render the debug geometry in.
         * @param name Optional name for the debug geometry node (default: "DebugGeometry").
         */
        explicit DebugGeometry(Scene& scene, const String& name = "DebugGeometry");

        /**
         * @brief Destructor. Cleans up scene nodes and render objects.
         */
        ~DebugGeometry() override;

        // Disable copy and move
        DebugGeometry(const DebugGeometry&) = delete;
        DebugGeometry& operator=(const DebugGeometry&) = delete;
        DebugGeometry(DebugGeometry&&) = delete;
        DebugGeometry& operator=(DebugGeometry&&) = delete;

    public:
        /// @copydoc IDebugGeometry::AddLine
        void AddLine(const Vector3& start, const Vector3& end, uint32 color = 0xffffffff) override;

        /// @copydoc IDebugGeometry::AddTriangle
        void AddTriangle(const Vector3& v1, const Vector3& v2, const Vector3& v3, uint32 color = 0xffffffff) override;

        /// @copydoc IDebugGeometry::Clear
        void Clear() override;

        /// @copydoc IDebugGeometry::SetVisible
        void SetVisible(bool visible) override;

        /// @copydoc IDebugGeometry::IsVisible
        bool IsVisible() const override;

        void Finish() override;

    private:
        /**
         * @brief Ensures that line operations are initialized and ready to use.
         */
        void EnsureLineOperations();

        /**
         * @brief Ensures that triangle operations are initialized and ready to use.
         */
        void EnsureTriangleOperations();

        bool OnRendering(const MovableObject&, const Camera&);

    private:
        Scene& m_scene;
        SceneNode* m_debugNode;
        ManualRenderObject* m_debugObject;

        scoped_connection m_renderConnection;
        
        std::unique_ptr<ManualRenderOperationRef<ManualLineListOperation>> m_lineOperation;
        std::unique_ptr<ManualRenderOperationRef<ManualTriangleListOperation>> m_triangleOperation;
        
        bool m_hasLines;
        bool m_hasTriangles;
        bool m_isVisible;
    };
}
