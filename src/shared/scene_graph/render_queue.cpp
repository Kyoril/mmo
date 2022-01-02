// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "render_queue.h"

#include "movable_object.h"
#include "camera.h"
#include "pass.h"
#include "queued_renderable_visitor.h"

namespace mmo
{
	void QueuedRenderableCollection::AddRenderable(Pass* pass, Renderable* rend)
	{
		if (m_organisationMode & SortDescending)
		{

		}

		if (m_organisationMode & PassGroup)
		{

		}
	}

	void RenderPriorityGroup::AddRenderable(const Renderable& renderable)
	{
		// TODO: Determine renderable type
		AddSolidRenderable(renderable);
	}

	void RenderPriorityGroup::AddTransparentRenderable(const Renderable& renderable)
	{

	}

	void RenderPriorityGroup::AddUnsortedTransparentRenderable(const Renderable& renderable)
	{

	}

	void RenderPriorityGroup::AddSolidRenderable(const Renderable& renderable)
	{
		
	}

	VisibleObjectsBoundsInfo::VisibleObjectsBoundsInfo()
	{
		Reset();
	}

	void VisibleObjectsBoundsInfo::Reset()
	{
		aabb.SetNull();
		minDistance = minDistanceInFrustum = std::numeric_limits<float>::infinity();
		maxDistance = maxDistanceInFrustum = 0;
	}

	void VisibleObjectsBoundsInfo::Merge(const AABB& boxBounds, const Sphere& sphereBounds, const Camera& cam)
	{
		aabb.Combine(boxBounds);
		
		const Vector3 vsSpherePos = cam.GetViewMatrix() * sphereBounds.GetCenter();
		const float camDistToCenter = vsSpherePos.GetLength();
		minDistance = std::min(minDistance, std::max(0.0f, camDistToCenter - sphereBounds.GetRadius()));
		maxDistance = std::max(maxDistance, camDistToCenter + sphereBounds.GetRadius());
		minDistanceInFrustum = std::min(minDistanceInFrustum, std::max(0.0f, camDistToCenter - sphereBounds.GetRadius()));
		maxDistanceInFrustum = std::max(maxDistanceInFrustum, camDistToCenter + sphereBounds.GetRadius());
	}

	void VisibleObjectsBoundsInfo::MergeNonRenderedButInFrustum(const AABB& boxBounds, const Sphere& sphereBounds, const Camera& cam)
	{
		const Vector3 vsSpherePos = cam.GetViewMatrix() * sphereBounds.GetCenter();
		const float camDistToCenter = vsSpherePos.GetLength();
		minDistanceInFrustum = std::min(minDistanceInFrustum, std::max(0.0f, camDistToCenter - sphereBounds.GetRadius()));
		maxDistanceInFrustum = std::max(maxDistanceInFrustum, camDistToCenter + sphereBounds.GetRadius());
	}

	RenderQueueGroup::RenderQueueGroup(RenderQueue& queue)
		: m_queue(queue)
	{
	}

	void RenderQueueGroup::Clear()
	{

	}

	void RenderQueueGroup::AddRenderable(Renderable& renderable, uint8 groupId, uint16 priority)
	{
		auto it = m_priorityGroups.find(priority);
		if (it == m_priorityGroups.end())
		{
			it = m_priorityGroups.insert(std::make_pair(priority, std::make_unique<RenderPriorityGroup>())).first;
		}

		it->second->AddRenderable(renderable);
	}

	RenderQueue::RenderQueue()
		: m_defaultGroup(Main)
		, m_defaultRenderablePriority(100)
	{
		m_groups[Main] = std::make_unique<RenderQueueGroup>(*this);
	}

	RenderQueue::~RenderQueue()
	{

	}

	void RenderQueue::Clear()
	{
		for (const auto& group : m_groups)
		{
			group.second->Clear();
		}
	}

	void RenderQueue::AddRenderable(Renderable& renderable, uint8 groupId, uint16 priority)
	{
		// TODO
	}

	void RenderQueue::AddRenderable(Renderable& renderable, uint8 groupId)
	{
		AddRenderable(renderable, groupId, m_defaultRenderablePriority);
	}

	void RenderQueue::AddRenderable(Renderable& renderable)
	{
		AddRenderable(renderable, m_defaultGroup, m_defaultRenderablePriority);
	}

	RenderQueueGroup* RenderQueue::GetQueueGroup(const uint8 groupId)
	{
		const auto groupIt = m_groups.find(groupId);
		if (groupIt == m_groups.end())
		{
			m_groups[groupId] = std::make_unique<RenderQueueGroup>(*this);
			return m_groups[groupId].get();
		}

		return groupIt->second.get();

	}

	void RenderQueue::Combine(const RenderQueue& other)
	{
		// TODO
	}

	void RenderQueue::ProcessVisibleObject(MovableObject& movableObject, Camera& camera, VisibleObjectsBoundsInfo& visibleBounds)
	{
		movableObject.SetCurrentCamera(camera);

		if (!movableObject.IsVisible())
		{
			return;
		}

		// TODO
	}
}
