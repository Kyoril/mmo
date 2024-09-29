// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include <map>
#include <memory>

#include "math/aabb.h"
#include "renderable.h"
#include "base/signal.h"
#include "base/typedefs.h"
#include "queued_renderable_visitor.h"

namespace mmo
{
	class Pass;
	class MovableObject;
	class Sphere;
	class Camera;


	enum RenderQueueGroupId
	{
        /// Use this queue for objects which must be rendered first e.g. backgrounds
		Background = 0,
		
        /// First queue (after backgrounds), used for skies if rendered first
		SkiesEarly = 5,
		WorldGeometry1 = 25,
		
		/// The default render queue
		Main = 50,
		
        /// Penultimate queue (before overlays), used for skies if rendered last
		SkiesLate = 95,
		
        /// Use this queue for objects which must be rendered last e.g. overlays
		Overlay = 100,
		
		/// Final possible render queue, don't exceed this
		Max = 105
	};

	class QueuedRenderableCollection
	{
	public:
		enum OrganizationMode
		{
			/// Group by pass
			PassGroup = 1,
			/// Sort descending camera distance
			SortDescending = 2,
			/// Sort ascending camera distance
			///	overlaps with descending since both use same sort
			SortAscending = 6
		};
		
		void AddRenderable(Renderable& rend);

		void Clear();

		void AcceptVisitor(QueuedRenderableVisitor& visitor) const;

	protected:
		std::vector<Renderable*> m_renderables;
	};

	class RenderPriorityGroup
	{
	public:
		void AddRenderable(Renderable& renderable);
		void Clear();

		[[nodiscard]] const QueuedRenderableCollection& GetSolids() const noexcept { return m_solidCollection; }

	private:
		void AddSolidRenderable(Renderable& renderable);

	private:
		QueuedRenderableCollection m_solidCollection;
	};

	struct VisibleObjectsBoundsInfo
	{
		AABB aabb;
		float minDistance{0.0f};
		float maxDistance{0.0f};
		float minDistanceInFrustum{0.0f};
		float maxDistanceInFrustum{0.0f};

	public:
		VisibleObjectsBoundsInfo();

		void Reset();

		void Merge(const AABB& boxBounds, const Sphere& sphereBounds, const Camera& cam);

		void MergeNonRenderedButInFrustum(const AABB& boxBounds, const Sphere& sphereBounds, const Camera& cam);
	};

	class RenderQueue;

	class RenderQueueGroup
	{
    public:
        typedef std::map<uint16, std::unique_ptr<RenderPriorityGroup>, std::less<> > PriorityMap;

	public:
		explicit RenderQueueGroup(RenderQueue& queue);

	public:
        void Clear();

		void AddRenderable(Renderable& renderable, uint16 priority);

		PriorityMap::iterator begin() { return m_priorityGroups.begin(); }
		PriorityMap::iterator end() { return m_priorityGroups.end(); }

	private:
		RenderQueue& m_queue;
		PriorityMap m_priorityGroups;
	};

	class RenderQueue
	{
	public:
		signal<bool(Renderable& renderable, uint8 groupID, uint16 priority, RenderQueue& queue)> renderableQueued;

	public:
        typedef std::map<uint8, std::unique_ptr<RenderQueueGroup>> RenderQueueGroupMap;

	protected:
		RenderQueueGroupMap m_groups;
		uint8 m_defaultGroup;
		uint16 m_defaultRenderablePriority;
		
	public:
		RenderQueue();
		virtual ~RenderQueue();

		void Clear();

		void AddRenderable(Renderable& renderable, uint8 groupId, uint16 priority);

		void AddRenderable(Renderable& renderable, uint8 groupId);

		void AddRenderable(Renderable& renderable);
		
		[[nodiscard]] RenderQueueGroup* GetQueueGroup(uint8 groupId);

		[[nodiscard]] uint8 GetDefaultQueueGroup() const { return m_defaultGroup; }

		void SetDefaultRenderablePriority(const uint16 priority) { m_defaultRenderablePriority = priority; }

		[[nodiscard]] uint16 GetDefaultRenderablePriority() const { return m_defaultRenderablePriority; }

		void SetDefaultQueueGroup(const uint8 group) { m_defaultGroup = group; }

		void Combine(const RenderQueue& other);

		void ProcessVisibleObject(MovableObject& movableObject, Camera& camera, VisibleObjectsBoundsInfo& visibleBounds);


		RenderQueueGroupMap::iterator begin() { return m_groups.begin(); }
		RenderQueueGroupMap::iterator end() { return m_groups.end(); }
	};
}
