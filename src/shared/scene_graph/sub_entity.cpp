
#include "sub_entity.h"

#include "camera.h"
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
		m_visible = m_subMesh.IsVisibleByDefault();
	}

	void SubEntity::PrepareRenderOperation(RenderOperation& operation)
	{
		if (m_parent.HasSkeleton())
		{
			operation.vertexConstantBuffers.push_back(m_parent.m_boneMatrixBuffer.get());
		}

		m_subMesh.PrepareRenderOperation(operation);
		operation.material = GetMaterial();
	}

	float SubEntity::GetSquaredViewDepth(const Camera& camera) const
	{
        if (m_cachedCamera == &camera)
        {
			return m_cachedCameraDist;
        }

		const auto* parent = m_parent.GetParentSceneNode();
		ASSERT(parent);

		// Calculate the squared distance from the camera to the parent node's world position
		m_cachedCameraDist = (parent->GetDerivedPosition() - camera.GetDerivedPosition()).GetSquaredLength();
		m_cachedCamera = &camera;

		return m_cachedCameraDist;
	}

	const Matrix4& SubEntity::GetWorldTransform() const
	{
		return m_parent.GetParentNodeFullTransform();
	}
}
