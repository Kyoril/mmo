// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "render_queue.h"

#include "movable_object.h"
#include "camera.h"

namespace mmo
{
	void QueuedRenderableCollection::AddRenderable(Renderable& rend)
	{
		m_renderables.push_back(&rend);
	}

	void QueuedRenderableCollection::Clear()
	{
		m_renderables.clear();
	}

	void QueuedRenderableCollection::AcceptVisitor(QueuedRenderableVisitor& visitor) const
	{
		for(auto& renderable : m_renderables)
		{
			visitor.Visit(*renderable);
		}
	}
	void RenderPriorityGroup::AddRenderable(Renderable& renderable)
	{
		// TODO: Implement non-solid renderables based on material settings

		AddSolidRenderable(renderable);
	}

	void RenderPriorityGroup::Clear()
	{
		m_solidCollection.Clear();
	}

	void RenderPriorityGroup::AddSolidRenderable(Renderable& renderable)
	{
		m_solidCollection.AddRenderable(renderable);
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
		for (const auto& [priority, group] : m_priorityGroups)
		{
			group->Clear();
		}
	}

	void RenderQueueGroup::AddRenderable(Renderable& renderable, uint16 priority)
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
		auto* group = GetQueueGroup(groupId);
		ASSERT(group);
		
		if (!renderableQueued(renderable, groupId, priority, *this))
		{
			// TODO: Skip this renderable if the collector works as intended
			//return;
		}

		group->AddRenderable(renderable, priority);
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

		const AABB& worldBoundingBox = movableObject.GetWorldBoundingBox(true);
		if (!camera.IsVisible(worldBoundingBox))
		{
			return;
		}

		movableObject.PopulateRenderQueue(*this);
		visibleBounds.Merge(worldBoundingBox, movableObject.GetWorldBoundingSphere(true), camera);
	}
}
