// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "light.h"
#include "scene_node.h"
#include "math/radian.h"
#include "math/degree.h"

namespace mmo
{
	const String Light::LIGHT_TYPE_NAME = "Light";
	Light::Light(LightType type)
		: m_type(type)
	{
	}

	Light::Light(const String& name, LightType type)
		: MovableObject(name)
		, m_type(type)
	{
	}

	Vector3 Light::GetDirection() const
	{
		// For directional and spot lights, the direction is the negative Z axis of the node's orientation
		if (m_type == LightType::Directional || m_type == LightType::Spot)
		{
			if (auto node = GetParentSceneNode())
			{
				// Get the node's orientation and transform the negative Z axis
				return node->GetDerivedOrientation() * Vector3(0.0f, 0.0f, -1.0f);
			}
		}

		// Default direction (for point lights or if no parent node)
		return Vector3(0.0f, 0.0f, -1.0f);
	}

	void Light::SetDirection(const Vector3& direction)
	{
		// For directional and spot lights, set the node's orientation to point in the given direction
		if (m_type == LightType::Directional || m_type == LightType::Spot)
		{
			if (auto node = GetParentSceneNode())
			{
				// Create a rotation that aligns the negative Z axis with the given direction
				Vector3 normalizedDir = direction;
				normalizedDir.Normalize();
				Vector3 zAxis = Vector3(0.0f, 0.0f, -1.0f);
				
				// If the direction is almost parallel to the Z axis, we need a special case
				float dot = normalizedDir.Dot(zAxis);
				if (dot > 0.999f || dot < -0.999f)
				{
					if (dot < 0.0f)
					{
						// Direction is already aligned with negative Z, no rotation needed
						node->SetOrientation(Quaternion::Identity);
					}
					else
					{
						// Direction is opposite to negative Z, rotate 180 degrees around X axis
						node->SetOrientation(Quaternion(Radian(Degree(180.0f)), Vector3(1.0f, 0.0f, 0.0f)));
					}
				}
				else
				{
					// General case: find the rotation from negative Z to the direction
					Vector3 rotationAxis = zAxis.Cross(normalizedDir);
					rotationAxis.Normalize();
					float rotationAngle = std::acos(dot);
					node->SetOrientation(Quaternion(Radian(rotationAngle), rotationAxis));
				}
			}
		}
	}

	Vector3 Light::GetDerivedPosition() const
	{
		// Get the position from the parent scene node
		if (auto node = GetParentSceneNode())
		{
			return node->GetDerivedPosition();
		}

		// Default position if no parent node
		return Vector3::Zero;
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
		else // Directional light
		{
			// Directional lights don't have a specific position, so use a small radius
			return 0.1f;
		}
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
}
