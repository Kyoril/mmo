// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "movable_object.h"
#include "movable_object_factory.h"
#include "scene_node.h"
#include "render_queue.h"
#include "scene.h"
#include "tag_point.h"


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
				static_cast<TagPoint*>(m_parentNode)->GetParentEntity()->DetachObjectFromBone(*this);
			}
			else
			{
				static_cast<SceneNode*>(m_parentNode)->DetachObject(*this);
			}
		}
	}

	Node* MovableObject::GetParentNode() const
	{
		return m_parentNode;
	}

	SceneNode* MovableObject::GetParentSceneNode() const
	{
		if (m_parentIsTagPoint)
		{
			TagPoint* tagPoint = static_cast<TagPoint*>(m_parentNode);
			return tagPoint->GetParentEntity()->GetParentSceneNode();
		}
		else
		{
			return static_cast<SceneNode*>(m_parentNode);
		}
	}

	void MovableObject::NotifyAttachmentChanged(Node* parent, const bool isTagPoint)
	{
        const bool different = parent != m_parentNode;

        m_parentNode = parent;
        m_parentIsTagPoint = isTagPoint;
		m_worldAABBDirty = true;
		
        // Call listener (note, only called if there's something to do)
        if (different)
        {
			m_worldAABBDirty = true;

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
				TagPoint* tp = static_cast<TagPoint*>(m_parentNode);
				tp->GetParentEntity()->DetachObjectFromBone(*this);
			}
			else
			{
				static_cast<SceneNode*>(m_parentNode)->DetachObject(*this);
			}
		}
	}

	bool MovableObject::IsInScene() const
	{
		if (m_parentNode != nullptr)
		{
			if (m_parentIsTagPoint)
			{
				TagPoint* tp = static_cast<TagPoint*>(m_parentNode);
				return tp->GetParentEntity()->IsInScene();
			}

			SceneNode* sn = static_cast<SceneNode*>(m_parentNode);
			return sn->IsInSceneGraph();
		}

		return false;
	}

	void MovableObject::NotifyMoved()
	{
		objectMoved(*this);
		m_worldAABBDirty = true;
	}

	void MovableObject::SetCurrentCamera(Camera& cam)
	{
		m_beyondFarDistance = false;

		if (m_parentNode)
		{
			const float squaredDist = m_parentNode->GetSquaredViewDepth(cam);
			if (m_upperDistance > 0)
			{
				m_beyondFarDistance = squaredDist > m_squaredUpperDistance;
			}
		}

        m_renderingDisabled = !objectRendering(*this, cam);
	}

	const AABB& MovableObject::GetWorldBoundingBox(bool derive) const
	{
		if (derive && m_worldAABBDirty)
        {
            m_worldAABB = GetBoundingBox();
            m_worldAABB.Transform(GetParentNodeFullTransform());
			m_worldAABBDirty = false;
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

	uint8 MovableObject::GetRenderQueueGroup() const
	{
		return m_renderQueueId;
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
