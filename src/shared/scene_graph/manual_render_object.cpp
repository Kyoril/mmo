// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "manual_render_object.h"

#include "mesh_manager.h"
#include "scene_graph/render_operation.h"
#include "scene_graph/render_queue.h"

namespace mmo
{
	void ManualRenderOperation::Finish()
	{
		m_parent.NotifyOperationUpdated();
	}

	void ManualRenderOperation::PrepareRenderOperation(RenderOperation& operation)
	{
		operation.topology = GetTopologyType();
		operation.vertexFormat = GetFormat();
		operation.vertexData = m_vertexData.get();
		operation.indexData = m_indexData.get();
		operation.material = m_material;
	}

	const Matrix4& ManualRenderOperation::GetWorldTransform() const
	{
		return m_parent.GetParentNodeFullTransform();
	}

	float ManualRenderOperation::GetSquaredViewDepth(const Camera& camera) const
	{
		// TODO
		return 0.0f;
	}

	void ManualLineListOperation::ConvertToSubmesh(SubMesh& subMesh)
	{
		subMesh.useSharedVertices = false;

		TODO("Implement");
		/*subMesh.m_vertexBuffer = std::move(m_vertexBuffer);
		subMesh.m_indexBuffer = std::move(m_indexBuffer);
		subMesh.m_indexStart = 0;
		subMesh.m_indexEnd = subMesh.m_vertexBuffer->GetVertexCount();*/
	}

	void ManualTriangleListOperation::ConvertToSubmesh(SubMesh& subMesh)
	{
		subMesh.useSharedVertices = false;

		subMesh.vertexData = std::make_unique<VertexData>(*m_vertexData->vertexDeclaration, *m_vertexData->vertexBufferBinding);
		subMesh.vertexData->vertexCount = m_vertexData->vertexCount;
		subMesh.vertexData->vertexStart = m_vertexData->vertexStart;
		subMesh.vertexData->m_hardwareAnimationDataList = m_vertexData->m_hardwareAnimationDataList;

		if (m_indexData)
		{
			subMesh.indexData = std::make_unique<IndexData>();
			subMesh.indexData->indexCount = m_indexData->indexCount;
			subMesh.indexData->indexStart = m_indexData->indexStart;
			subMesh.indexData->indexBuffer = std::move(m_indexData->indexBuffer);
		}

		subMesh.SetMaterial(m_material);
	}

	ManualRenderObject::ManualRenderObject(GraphicsDevice& device, const String& name)
		: MovableObject(name)
		, m_device(device)
	{
	}

	ManualRenderOperationRef<ManualLineListOperation> ManualRenderObject::AddLineListOperation(MaterialPtr material)
	{
		auto operation = std::make_unique<ManualLineListOperation>(m_device, *this, material);
		const auto result = operation.get();

		m_operations.emplace_back(std::move(operation));

		return *result;
	}

	ManualRenderOperationRef<ManualTriangleListOperation> ManualRenderObject::AddTriangleListOperation(MaterialPtr material)
	{
		auto operation = std::make_unique<ManualTriangleListOperation>(m_device, *this, material);
		const auto result = operation.get();

		m_operations.emplace_back(std::move(operation));

		return *result;
	}

	void ManualRenderObject::Clear() noexcept
	{
		m_operations.clear();

		m_worldAABB.SetNull();
		m_boundingRadius = 0.0f;
	}

	MeshPtr ManualRenderObject::ConvertToMesh(const String& meshName) const
	{
		assert(!m_operations.empty() && "Can not convert empty render object into a mesh!");

		MeshManager& meshMgr = MeshManager::Get();
		MeshPtr m = meshMgr.CreateManual(meshName);

		for (const auto& operation : m_operations)
		{
			SubMesh& subMesh = m->CreateSubMesh();
			operation->ConvertToSubmesh(subMesh);
		}

		m->SetBounds(m_worldAABB);

		return m;
	}

	const String& ManualRenderObject::GetMovableType() const
	{
		static String manualRenderObjectType = "ManualRenderObject";
		return manualRenderObjectType;
	}

	void ManualRenderObject::VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables)
	{
		for(auto& operation : m_operations)
		{
			visitor.Visit(*operation, 0, false);
		}
	}

	void ManualRenderObject::PopulateRenderQueue(RenderQueue& queue)
	{
		for(auto& operation : m_operations)
		{
			queue.AddRenderable(*operation, m_renderQueueId, m_renderQueuePriority);
		}
	}

	void ManualRenderObject::NotifyOperationUpdated()
	{
		m_worldAABB.SetNull();
		
		for(const auto& operation : m_operations)
		{
			m_worldAABB.Combine(operation->GetBoundingBox());
		}
	}
}
