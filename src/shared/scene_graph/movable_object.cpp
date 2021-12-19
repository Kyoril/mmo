// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "movable_object.h"
#include "movable_object_factory.h"
#include "scene_node.h"
#include "render_queue.h"


namespace mmo
{
	
	uint32 MovableObject::m_defaultQueryFlags = 0xFFFFFFFF;
	uint32 MovableObject::m_defaultVisibilityFlags = 0xFFFFFFFF;

	MovableObject::MovableObject()
		: m_creator(nullptr)
        , m_scene(nullptr)
        , m_parentNode(nullptr)
        , m_parentIsTagPoint(false)
        , m_visible(true)
		, m_debugDisplay(false)
        , m_upperDistance(0)
        , m_squaredUpperDistance(0)
		, m_minPixelSize(0)
        , m_beyondFarDistance(false)
        , m_renderQueueId(Main)
        , m_renderQueueIdSet(false)
		, m_renderQueuePriority(100)
		, m_renderQueuePrioritySet(false)
		, m_queryFlags(m_defaultQueryFlags)
        , m_visibilityFlags(m_defaultVisibilityFlags)
        , m_renderingDisabled(false)
	{
	}

	MovableObject::MovableObject(const String& name)
		: m_name(std::move(name))
		, m_creator(nullptr)
        , m_scene(nullptr)
        , m_parentNode(nullptr)
        , m_parentIsTagPoint(false)
        , m_visible(true)
		, m_debugDisplay(false)
        , m_upperDistance(0)
        , m_squaredUpperDistance(0)
		, m_minPixelSize(0)
        , m_beyondFarDistance(false)
        , m_renderQueueId(Main)
        , m_renderQueueIdSet(false)
		, m_renderQueuePriority(100)
		, m_renderQueuePrioritySet(false)
		, m_queryFlags(m_defaultQueryFlags)
        , m_visibilityFlags(m_defaultVisibilityFlags)
        , m_renderingDisabled(false)
	{
	}

	MovableObject::~MovableObject()
	{
		objectDestroyed(*this);

		if (m_parentNode)
		{
			if (m_parentIsTagPoint)
			{
				// TODO
			}
			else
			{
				m_parentNode->DetachObject(*this);
			}
		}
	}

	SceneNode* MovableObject::GetParentSceneNode() const
	{
		return m_parentNode;
	}

	void MovableObject::NotifyAttached(SceneNode& parent, const bool isTagPoint)
	{
        const bool different = &parent != m_parentNode;

        m_parentNode = &parent;
        m_parentIsTagPoint = isTagPoint;
		
        // Call listener (note, only called if there's something to do)
        if (different)
        {
            if (m_parentNode)
            {
	            objectAttached(*this);
            }
			else
			{
				objectDetached(*this);
			}
        }
	}

	bool MovableObject::IsAttached() const
	{
		return m_parentNode != nullptr;
	}
	
	void MovableObject::DetachFromParent()
	{
		if (IsAttached())
		{
			if (m_parentIsTagPoint)
			{
				// TODO
			}
			else
			{
				m_parentNode->DetachObject(*this);
			}
		}
	}

	bool MovableObject::IsInScene() const
	{
		if (m_parentNode != 0)
		{
			if (m_parentIsTagPoint)
			{
				// TODO
			}
			else
			{
				return m_parentNode->IsInSceneGraph();
			}
		}

		return false;
	}

	void MovableObject::NotifyMoved()
	{
		objectMoved(*this);
	}

	void MovableObject::SetCurrentCamera(Camera& cam)
	{
		if (m_parentNode)
		{
			m_beyondFarDistance = false;

			// TODO: Calculate visibility
			
		}

        m_renderingDisabled = !objectRendering(*this, cam);
	}

	const AABB& MovableObject::GetWorldBoundingBox(bool derive) const
	{
		if (derive)
        {
            m_worldAABB = GetBoundingBox();
            m_worldAABB.Transform(GetParentNodeFullTransform());
        }

        return m_worldAABB;
	}

	void MovableObject::SetRenderQueueGroup(const uint8 queueId)
    {
		ASSERT(queueId <= Max && "Render queue out of range!");

        m_renderQueueId = queueId;
        m_renderQueueIdSet = true;
    }
	
	void MovableObject::SetRenderQueueGroupAndPriority(const uint8 queueId, const uint16 priority)
	{
		SetRenderQueueGroup(queueId);

		m_renderQueuePriority = priority;
		m_renderQueuePrioritySet = true;
	}
	
	const Matrix4& MovableObject::GetParentNodeFullTransform() const
	{
		if(m_parentNode)
		{
			return m_parentNode->GetFullTransform();
		}
		
        return Matrix4::Identity;
	}

	const Sphere& MovableObject::GetWorldBoundingSphere(const bool derive) const
	{
		if (derive)
		{
			ASSERT(m_parentNode);

			const Vector3& scl = m_parentNode->GetDerivedScale();
			const float factor = std::max(std::max(scl.x, scl.y), scl.z);
			m_worldBoundingSphere.SetRadius(GetBoundingRadius() * factor);
			m_worldBoundingSphere.SetCenter(m_parentNode->GetDerivedPosition());
		}

		return m_worldBoundingSphere;
	}

	void MovableObject::SetVisible(const bool visible)
	{
        m_visible = visible;
	}

	bool MovableObject::ShouldBeVisible() const
	{
        return m_visible;
	}

	bool MovableObject::IsVisible() const
	{
		 if (!m_visible || m_beyondFarDistance || m_renderingDisabled)
		 {
			return false; 
		 }

		 // TODO: Visibility Flags for scene

		 return true;
	}

	uint32 MovableObject::GetTypeFlags() const
	{
		if (m_creator)
		{
			return m_creator->GetTypeFlags();
		}
		else
		{
			return 0xFFFFFFFF;
		}
	}
}
