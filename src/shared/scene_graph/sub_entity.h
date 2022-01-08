#pragma once

#include "scene_graph/renderable.h"

namespace mmo
{
	class Entity;
	class SubMesh;

	class SubEntity : public Renderable
	{
	public:
		SubEntity(Entity& parent, SubMesh& subMesh);

	public:
		[[nodiscard]] Entity& GetParent() const noexcept { return m_parent; }

		[[nodiscard]] SubMesh& GetSubMesh() const noexcept { return m_subMesh; }

		//virtual void SetVisible(bool visible);

		//virtual bool IsVisible() const;

		/*virtual void SetRenderQueueGroup(uint8 queueId);

		virtual void SetRenderQueueGroupAndPriority(uint8 queueId, uint16 priority);

		virtual uint8 GetRenderQueueGroup() const;

		virtual uint16 GetRenderQueuePriority() const;

		virtual bool IsRenderQueueGroupSet() const;

		virtual bool IsRenderQueuePrioritySet() const;*/

		virtual void PrepareRenderOperation(RenderOperation& operation) override;
		
		void SetStartIndex(const uint32 startIndex) { m_indexStart = startIndex; }

		[[nodiscard]] uint32 GetStartIndex() const noexcept { return m_indexStart; }

		void SetEndIndex(const uint32 endIndex) { m_indexEnd = endIndex; }

		[[nodiscard]] uint32 GetEndIndex() const noexcept { return m_indexEnd; }

		[[nodiscard]] float GetSquaredViewDepth(const Camera& camera) const override;

		const Matrix4& GetWorldTransform() const override;


	private:
		Entity& m_parent;
		SubMesh& m_subMesh;
		uint32 m_indexStart { 0 };
		uint32 m_indexEnd { 0 };
		bool m_visible { false };

		uint8 m_renderQueueId { 0 };
		bool m_renderQueueIdSet { false };
		uint16 m_renderQueuePriority { 0 };
		bool m_renderQueuePrioritySet { false };

		mutable float m_cachedCameraDist { 0.0f };
		mutable const Camera* m_cachedCamera { nullptr };
	};
}
