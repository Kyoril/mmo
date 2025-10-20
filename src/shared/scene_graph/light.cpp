// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "light.h"

#include "camera.h"
#include "scene.h"
#include "scene_node.h"
#include "math/radian.h"
#include "math/degree.h"

namespace mmo
{
	const String Light::LIGHT_TYPE_NAME = "Light";

	Light::Light(LightType type)
		: m_type(type)
	{
		SetCastShadows(false);
		SetQueryFlags(0);
	}

	Light::Light(const String& name, LightType type)
		: MovableObject(name)
		, m_type(type)
	{
		SetCastShadows(false);
		SetQueryFlags(0);
	}

	void Light::Update() const
	{
		if (m_derivedTransformDirty)
		{
			if (m_parentNode)
			{
				// Ok, update with SceneNode we're attached to
				const Quaternion& parentOrientation = m_parentNode->GetDerivedOrientation();
				const Vector3& parentPosition = m_parentNode->GetDerivedPosition();
				m_derivedDirection = parentOrientation * m_direction;
				m_derivedPosition = (parentOrientation * m_position) + parentPosition;
			}
			else
			{
				m_derivedPosition = m_position;
				m_derivedDirection = m_direction;
			}

			m_derivedTransformDirty = false;
		}
	}

	void Light::NotifyMoved()
	{
		m_derivedTransformDirty = true;

		MovableObject::NotifyMoved();
	}

	Vector3 Light::GetDirection() const
	{
		return m_direction;
	}

	void Light::SetDirection(const Vector3& direction)
	{
		m_direction = direction;
		m_derivedTransformDirty = true;
	}

	Vector3 Light::GetDerivedPosition() const
	{
		Update();
		return m_derivedPosition;
	}

	const String& Light::GetMovableType() const
	{
		return LIGHT_TYPE_NAME;
	}

	const AABB& Light::GetBoundingBox() const
	{
		// For point and spot lights, create a bounding box based on the light range
		if (m_type == LightType::Point || m_type == LightType::Spot)
		{
			// Create a bounding box centered at the origin with dimensions based on the light range
			m_boundingBox = AABB(
				Vector3(-m_range, -m_range, -m_range),
				Vector3(m_range, m_range, m_range)
			);
		}
		else // Directional light
		{
			// Directional lights don't have a specific position, so use a small bounding box
			m_boundingBox = AABB(
				Vector3(-0.1f, -0.1f, -0.1f),
				Vector3(0.1f, 0.1f, 0.1f)
			);
		}

		return m_boundingBox;
	}

	float Light::GetBoundingRadius() const
	{
		// For point and spot lights, the bounding radius is the light range
		if (m_type == LightType::Point || m_type == LightType::Spot)
		{
			return m_range;
		}
		
		// Directional lights don't have a specific position, so use a small radius
		return 0.1f;
	}

	void Light::VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables)
	{
		// Lights don't have renderables by themselves
		// This method is called when the scene wants to render the objects
		// For lights, we don't need to do anything here
	}

	void Light::PopulateRenderQueue(RenderQueue& queue)
	{
		// Lights don't add themselves to the render queue
		// They are processed separately during the lighting pass
	}

	void Light::NotifyAttachmentChanged(Node* parent, bool isTagPoint)
	{
		MovableObject::NotifyAttachmentChanged(parent, isTagPoint);

		m_derivedTransformDirty = true;

		if (parent)
		{
			// Should NOT attach light to root node!!!
			ASSERT(parent->GetParent() != nullptr);
		}
	}

	float Light::DeriveShadowNearClipDistance(const Camera& camera)
	{
		if (m_shadowNearClipDist > 0)
		{
			return m_shadowNearClipDist;
		}

		return camera.GetNearClipDistance();
	}

	float Light::DeriveShadowFarClipDistance(const Camera& camera)
	{
		if (m_shadowFarClipDist >= 0)
		{
			return m_shadowFarClipDist;
		}

		if (m_type == LightType::Directional)
		{
			return 100.0f;
		}

		return m_range;
	}

	const Vector3& Light::GetDerivedDirection() const
	{
		Update();
		return m_derivedDirection;
	}

	void Light::SetShadowFarDistance(float distance)
	{
		m_ownShadowFarDist = true;
		m_shadowFarDist = distance;
		m_shadowFarDistSquared = distance * distance;
	}

	void Light::ResetShadowFarDistance()
	{
		m_ownShadowFarDist = false;
	}

	float Light::GetShadowFarDistance() const
	{
		if (m_ownShadowFarDist)
		{
			return m_shadowFarDist;
		}

		return m_scene->GetShadowFarDistance();
	}
}
