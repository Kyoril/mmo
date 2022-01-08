
#include "sub_entity.h"

#include "entity.h"
#include "sub_mesh.h"
#include "scene_graph/render_operation.h"
#include "scene_graph/scene_node.h"

namespace mmo
{
	SubEntity::SubEntity(Entity& parent, SubMesh& subMesh)
		: m_parent(parent)
		, m_subMesh(subMesh)
	{
	}

	void SubEntity::PrepareRenderOperation(RenderOperation& operation)
	{
		m_subMesh.PrepareRenderOperation(operation);
	}

	float SubEntity::GetSquaredViewDepth(const Camera& camera) const
	{
        if (m_cachedCamera == &camera)
        {
			return m_cachedCameraDist;
        }

		const auto* parent = m_parent.GetParentSceneNode();
		ASSERT(parent);

		TODO("Implement calculation");

		m_cachedCameraDist = 0.0f;
		m_cachedCamera = &camera;

		return m_cachedCameraDist;
	}

	const Matrix4& SubEntity::GetWorldTransform() const
	{
		return m_parent.GetParentNodeFullTransform();
	}
}
