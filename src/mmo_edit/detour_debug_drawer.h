#pragma once

#include <recastnavigation/DebugUtils/Include/DebugDraw.h>

#include "base/typedefs.h"
#include "math/vector3.h"
#include "scene_graph/scene.h"

namespace mmo
{
    class DetourDebugDraw final : public duDebugDraw, public NonCopyable
    {
    private:
        static constexpr float IgnoreLineSize = 1.5f;
        duDebugDrawPrimitives m_type;
        float m_size;
        bool m_steep;

        std::vector<Vector3> m_vertices;
        std::vector<uint32> m_colors;

        Scene& m_scene;
        MaterialPtr m_material;

        ManualRenderObject* m_object;

    public:
        explicit DetourDebugDraw(Scene& scene, MaterialPtr material);

        void Steep(bool steep) { m_steep = steep; }

        void Clear();

    public:
        void depthMask(bool /*state*/) override {}
        void texture(bool /*state*/) override {}

        void begin(duDebugDrawPrimitives prim, float size = 1.f) override;
        void vertex(const float* pos, unsigned int color) override;
        void vertex(const float x, const float y, const float z, unsigned int color) override;
        void vertex(const float* pos, unsigned int color, const float* uv) override;
        void vertex(const float x, const float y, const float z, unsigned int color, const float u, const float v) override;
        void end() override;

    private:
        std::unique_ptr<ManualRenderOperationRef<ManualTriangleListOperation>> m_triangleOp;
        std::unique_ptr<ManualRenderOperationRef<ManualLineListOperation>> m_lineOp;
    };
}
