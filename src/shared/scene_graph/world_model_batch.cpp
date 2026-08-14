// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_batch.h"

#include "camera.h"
#include "render_operation.h"
#include "render_queue.h"
#include "sub_mesh.h"
#include "graphics/graphics_device.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace mmo
{
	String WorldModelBatch::s_movableType = "WorldModelBatch";

	WorldModelBatch::WorldModelBatch(const String& name, MeshPtr mesh, const uint16 submeshIndex, MaterialPtr material)
		: MovableObject(name)
		, m_mesh(std::move(mesh))
		, m_submeshIndex(submeshIndex)
		, m_material(std::move(material))
	{
		m_bounds.SetNull();
	}

	void WorldModelBatch::ClearInstances()
	{
		m_instances.clear();
	}

	void WorldModelBatch::AddInstance(const MeshInstanceData& instance)
	{
		m_instances.push_back(instance);
	}

	VertexData* WorldModelBatch::GetVertexData() const
	{
		if (!m_mesh || m_mesh->GetSubMeshCount() <= m_submeshIndex)
		{
			return nullptr;
		}

		const SubMesh& subMesh = m_mesh->GetSubMesh(m_submeshIndex);
		return subMesh.useSharedVertices ? m_mesh->sharedVertexData.get() : subMesh.vertexData.get();
	}

	IndexData* WorldModelBatch::GetIndexData() const
	{
		if (!m_mesh || m_mesh->GetSubMeshCount() <= m_submeshIndex)
		{
			return nullptr;
		}

		return m_mesh->GetSubMesh(m_submeshIndex).indexData.get();
	}

	void WorldModelBatch::UploadInstances(GraphicsDevice& device)
	{
		if (m_instances.empty())
		{
			m_instanceBuffer = nullptr;
			m_bounds.SetNull();
			m_boundingRadius = 0.0f;
			NotifyMoved();
			return;
		}

		// Recreated rather than mapped: a batch is rebuilt only when the owning world model's
		// transform changes, which for placed world models is never after load.
		m_instanceBuffer = device.CreateVertexBuffer(
			m_instances.size(),
			sizeof(MeshInstanceData),
			BufferUsage::Dynamic,
			m_instances.data());

		// Bounds are the union of the mesh bounds under every instance transform, in world space.
		// All eight corners are transformed because an instance may be rotated or non-uniformly
		// scaled, in which case transforming just min/max would not bound the result.
		const AABB& meshBounds = m_mesh ? m_mesh->GetBounds() : AABB();

		Vector3 minBounds(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
		Vector3 maxBounds(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

		for (const auto& instance : m_instances)
		{
			Vector3 corners[8];
			meshBounds.GetCorners(corners);

			for (const auto& corner : corners)
			{
				const Vector3 transformed = instance.worldMatrix.TransformAffine(corner);
				minBounds.x = std::min(minBounds.x, transformed.x);
				minBounds.y = std::min(minBounds.y, transformed.y);
				minBounds.z = std::min(minBounds.z, transformed.z);
				maxBounds.x = std::max(maxBounds.x, transformed.x);
				maxBounds.y = std::max(maxBounds.y, transformed.y);
				maxBounds.z = std::max(maxBounds.z, transformed.z);
			}
		}

		m_bounds = AABB(minBounds, maxBounds);
		m_boundingRadius = (maxBounds - minBounds).GetLength() * 0.5f;

		// The local bounds just changed, so any cached world bounding box derived from them is stale.
		// This matters on the very first upload: the batch is attached to its node before it has any
		// instances, and anything that derived a (degenerate) world box in between would otherwise
		// keep it and the batch would be frustum-culled forever.
		NotifyMoved();
	}

	const String& WorldModelBatch::GetMovableType() const
	{
		return s_movableType;
	}

	void WorldModelBatch::VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables)
	{
		if (!m_instances.empty() && m_instanceBuffer)
		{
			visitor.Visit(*this, 0, false);
		}
	}

	void WorldModelBatch::PopulateRenderQueue(RenderQueue& queue)
	{
		if (m_instances.empty() || !m_instanceBuffer || !GetVertexData() || !GetIndexData())
		{
			return;
		}

		// Mirrors Entity::PopulateRenderQueue: translucent materials render in the group ten above
		// the object's own, so batched geometry sorts against entities exactly as it did before.
		uint8 renderQueueGroup = GetRenderQueueGroup();
		if (m_material && m_material->IsTranslucent())
		{
			renderQueueGroup += 10;
		}

		queue.AddRenderable(*this, renderQueueGroup);
	}

	void WorldModelBatch::PrepareRenderOperation(RenderOperation& operation)
	{
		VertexData* vertexData = GetVertexData();
		IndexData* indexData = GetIndexData();
		if (!vertexData || !indexData || !m_instanceBuffer)
		{
			return;
		}

		operation.topology = TopologyType::TriangleList;
		operation.vertexData = vertexData;
		operation.indexData = indexData;
		operation.material = m_material;

		operation.instanceBuffer = m_instanceBuffer.get();
		operation.instanceCount = static_cast<uint32>(m_instances.size());
	}

	const Matrix4& WorldModelBatch::GetWorldTransform() const
	{
		// Instance matrices are already world-space and the instanced vertex shader never reads
		// matWorld, so there is no per-object transform to report.
		return Matrix4::Identity;
	}

	float WorldModelBatch::GetSquaredViewDepth(const Camera& camera) const
	{
		if (m_bounds.IsNull())
		{
			return 0.0f;
		}

		return (m_bounds.GetCenter() - camera.GetDerivedPosition()).GetSquaredLength();
	}
}
