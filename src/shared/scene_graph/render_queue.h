#pragma once
#include <map>
#include <memory>

#include "math/aabb.h"
#include "renderable.h"
#include "base/signal.h"
#include "base/typedefs.h"

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
		Queue1 = 10,
		Queue2 = 20,
		WorldGeometry1 = 25,
		Queue3 = 30,
		Queue4 = 40,
		
		/// The default render queue
		Main = 50,
		Queue6 = 60,
		Queue7 = 70,
		WorldGeometry2 = 75,
		Queue8 = 80,
		Queue9 = 90,
		
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
		
		void AddRenderable(Pass* pass, Renderable* rend);
		
	protected:
		
		uint8 m_organisationMode { 0 };
	};

	class RenderPriorityGroup
	{
	public:
		void AddRenderable(const Renderable& renderable);

	private:
		void AddTransparentRenderable(const Renderable& renderable);

		void AddUnsortedTransparentRenderable(const Renderable& renderable);

		void AddSolidRenderable(const Renderable& renderable);

	private:
		QueuedRenderableCollection m_solidCollection;
	};

	struct VisibleObjectsBoundsInfo
	{
		AABB aabb;
		float minDistance;
		float maxDistance;
		float minDistanceInFrustum;
		float maxDistanceInFrustum;

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

		void AddRenderable(Renderable& renderable, uint8 groupId, uint16 priority);

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
	};
}
