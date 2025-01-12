
#include "detour_debug_drawer.h"

#include "log/default_log_levels.h"

namespace mmo
{
	DetourDebugDraw::DetourDebugDraw(Scene& scene, MaterialPtr material)
		: m_scene(scene)
		, m_type(DU_DRAW_TRIS)
		, m_material(material)
	{
		ASSERT(material);

		m_object = m_scene.CreateManualRenderObject("DetourDebugVis");
		m_scene.GetRootSceneNode().AttachObject(*m_object);
	}

	void DetourDebugDraw::begin(duDebugDrawPrimitives prim, float size)
	{
		m_type = prim;

		switch(m_type)
		{
		case DU_DRAW_TRIS:
			m_triangleOp = std::make_unique<ManualRenderOperationRef<ManualTriangleListOperation>>(m_object->AddTriangleListOperation(m_material));
			break;
		}
	}

	void DetourDebugDraw::vertex(const float* pos, unsigned int color)
	{
		this->vertex(pos[0], pos[1], pos[2], color);
	}

	void DetourDebugDraw::vertex(const float x, const float y, const float z, unsigned int color)
	{
		if (m_type != DU_DRAW_TRIS)
		{
			return;
		}

		m_vertices.emplace_back(x, y, z);
		m_colors.push_back(color);

		if (m_vertices.size() == 3)
		{
			ASSERT(m_triangleOp);

			const auto& op = *m_triangleOp;

			auto& triangle = op->AddTriangle(m_vertices[0], m_vertices[1], m_vertices[2]);
			triangle.SetStartColor(0, m_colors[0]);
			triangle.SetStartColor(1, m_colors[1]);
			triangle.SetStartColor(2, m_colors[2]);

			m_vertices.clear();
			m_colors.clear();
		}
	}

	void DetourDebugDraw::vertex(const float* pos, unsigned int color, const float* uv)
	{
		this->vertex(pos[0], pos[1], pos[2], color);
	}

	void DetourDebugDraw::vertex(const float x, const float y, const float z, unsigned int color, const float u, const float v)
	{
		this->vertex(x, y, z, color);
	}

	void DetourDebugDraw::end()
	{
		if (m_type != DU_DRAW_TRIS)
		{
			return;
		}

		ASSERT(m_triangleOp);
		const auto& op = *m_triangleOp;
		op->Finish();

		m_triangleOp.reset();
	}
}
