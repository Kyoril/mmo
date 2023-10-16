// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "manual_render_object.h"

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
		operation.vertexBuffer = m_vertexBuffer.get();
		operation.indexBuffer = m_indexBuffer.get();
		operation.useIndexes = (m_indexBuffer != nullptr);
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
	
	ManualRenderObject::ManualRenderObject(GraphicsDevice& device, const String& name)
		: m_device(device)
		, MovableObject(name)
	{
	}

	ManualRenderOperationRef<ManualLineListOperation> ManualRenderObject::AddLineListOperation()
	{
		auto operation = std::make_unique<ManualLineListOperation>(m_device, *this);
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

	const String& ManualRenderObject::GetMovableType() const
	{
		static String ManualRenderObjectType = "ManualRenderObject";
		return ManualRenderObjectType;
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
