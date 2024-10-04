// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "renderable.h"
#include "render_queue.h"
#include "base/signal.h"
#include "base/typedefs.h"
#include "math/aabb.h"
#include "math/sphere.h"

namespace mmo
{
	class RenderQueue;
	class MovableObjectFactory;
	class SceneNode;
	class Scene;
	class Camera;

	/// Base class of an object in a scene which is movable, so it has a node which it is attached to.
	class MovableObject
	{
	public:
		signal<void(MovableObject&)> objectDestroyed;
		signal<void(MovableObject&)> objectAttached;
		signal<void(MovableObject&)> objectDetached;
		signal<void(MovableObject&)> objectMoved;
		signal<bool(const MovableObject&, const Camera&), any_or<bool, true>> objectRendering;

	protected:
		String m_name;
		MovableObjectFactory* m_creator;
		Scene* m_scene;
		SceneNode* m_parentNode;
		bool m_parentIsTagPoint;
		bool m_visible;
		bool m_debugDisplay;
		float m_upperDistance;
		float m_squaredUpperDistance;
		float m_minPixelSize;
		bool m_beyondFarDistance;
		uint8 m_renderQueueId { Main };
		bool m_renderQueueIdSet { false };
		uint16 m_renderQueuePriority { 100 };
		bool m_renderQueuePrioritySet { false };
		uint32 m_queryFlags { 0xffffffff };
		uint32 m_visibilityFlags { 0xffffffff };
		mutable AABB m_worldAABB;
		mutable Sphere m_worldBoundingSphere;
		bool m_renderingDisabled;

		static uint32 m_defaultQueryFlags;
		static uint32 m_defaultVisibilityFlags;

	public:
		explicit MovableObject();
		explicit MovableObject(const String& name);
		virtual ~MovableObject();

	public:
		virtual void SetCreator(MovableObjectFactory* creator) noexcept { m_creator = creator; }

		[[nodiscard]] virtual MovableObjectFactory* GetCreator() const noexcept { return m_creator; }

		virtual void SetScene(Scene* scene) noexcept { m_scene = scene; }
		
		[[nodiscard]] Scene* GetScene() const noexcept { return m_scene; }

		[[nodiscard]] virtual const String& GetName() const noexcept { return m_name; }

		[[nodiscard]] virtual const String& GetMovableType() const = 0;

		[[nodiscard]] virtual SceneNode* GetParentSceneNode() const;

		[[nodiscard]] virtual bool ParentIsTagPoint() const noexcept { return m_parentIsTagPoint; }

		virtual void NotifyAttachmentChanged(SceneNode* parent, bool isTagPoint = false);

		virtual bool IsAttached() const;

		virtual void DetachFromParent();

		virtual bool IsInScene() const;

		virtual void NotifyMoved();

		virtual void SetCurrentCamera(Camera& cam);
		
		[[nodiscard]] virtual const AABB& GetBoundingBox() const = 0;

		[[nodiscard]] virtual float GetBoundingRadius() const = 0;

		[[nodiscard]] virtual const AABB& GetWorldBoundingBox(bool derive = false) const;

		void SetRenderQueueGroup(uint8 queueId);

		void SetRenderQueueGroupAndPriority(uint8 queueId, uint16 priority);

		uint8 GetRenderQueueGroup() const noexcept;

		const Matrix4& GetParentNodeFullTransform() const;

		[[nodiscard]] virtual const Sphere& GetWorldBoundingSphere(bool derive = false) const;

		virtual void SetVisible(bool visible);

		[[nodiscard]] virtual bool ShouldBeVisible() const;

		[[nodiscard]] virtual bool IsVisible() const;

		virtual void SetRenderingDistance(const float dist) noexcept
		{
			m_upperDistance = dist;
			m_squaredUpperDistance = dist * dist;
		}

		[[nodiscard]] virtual float GetRenderingDistance() const { return m_upperDistance; }

		virtual void SetRenderingMinPixelSize(const float pixelSize) noexcept
		{
			m_minPixelSize = pixelSize;
		}

		[[nodiscard]] virtual float GetRenderingMinPixelSize() const noexcept { return m_minPixelSize; }

		virtual void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables = false) = 0;

		uint32 GetTypeFlags() const;

		uint32 GetQueryFlags() const { return m_queryFlags; }

		void SetQueryFlags(const uint32 mask) { m_queryFlags = mask; }
		
		virtual void SetDebugDisplayEnabled(const bool enabled) noexcept { m_debugDisplay = enabled; }

		[[nodiscard]] virtual bool IsDebugDisplayEnabled(void) const noexcept { return m_debugDisplay; }

		/// @brief Method by which the movable must add Renderable instances to the rendering queue.
		virtual void PopulateRenderQueue(RenderQueue& queue) = 0;
	};
}
