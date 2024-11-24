
#include "light.h"

#include "camera.h"
#include "scene.h"
#include "scene_node.h"
#include "math/intersects.h"

namespace mmo
{
	Light::Light()
	{
		m_minPixelSize = 0.0f;
	}

	Light::Light(const String& name)
		: MovableObject(name)
	{
		m_minPixelSize = 0.0f;
	}

	Light::~Light() = default;

	void Light::SetColor(const float red, const float green, const float blue)
	{
		SetColor(Color(red, green, blue));
	}

	void Light::SetColor(const Color& color)
	{
		m_color = color;
	}

	void Light::SetAttenuation(const float range, const float constant, const float linear, const float quadratic)
	{
		m_range = range;
		m_attenuationConst = constant;
		m_attenuationLinear = linear;
		m_attenuationQuad = quadratic;
	}

	void Light::SetPosition(const float x, const float y, const float z)
	{
		SetPosition(Vector3(x, y, z));
	}

	void Light::SetPosition(const Vector3& position)
	{
		m_position = position;
		m_derivedTransformDirty = true;
	}

	void Light::SetDirection(float x, float y, float z)
	{
		SetDirection(Vector3(x, y, z));
	}

	void Light::SetDirection(const Vector3& direction)
	{
		m_direction = direction;
		m_derivedTransformDirty = true;
	}

	void Light::SetSpotlightRange(const Radian& innerAngle, const Radian& outerAngle, const float falloff)
	{
		m_spotInner = innerAngle;
		m_spotOuter = outerAngle;
		m_spotFalloff = falloff;
	}

	const Vector3& Light::GetDerivedPosition(bool cameraRelativeIfSet) const
	{
		Update();

		if (cameraRelativeIfSet && m_cameraToBeRelativeTo)
		{
			return m_derivedCamRelativePosition;
		}

		return m_derivedPosition;
	}

	const Vector3& Light::GetDerivedDirection() const
	{
		Update();

		return m_derivedDirection;
	}

	void Light::SetShadowFarDistance(const float distance)
	{
		m_ownShadowFarDist = true;
		m_shadowFarDist = distance;
		m_shadowFarDistSquared = distance * distance;
	}

	float Light::GetShadowFarDistance() const
	{
		ASSERT(m_scene);
		return m_ownShadowFarDist ? m_shadowFarDist : m_scene->GetShadowFarDistance();
	}

	float Light::GetShadowFarDistanceSquared() const
	{
		ASSERT(m_scene);
		return m_ownShadowFarDist ? m_shadowFarDistSquared : m_scene->GetShadowFarDistanceSquared();
	}

	float Light::DeriveShadowNearClipDistance(const Camera& camera) const
	{
		if (m_shadowNearClip > 0.0f)
		{
			return m_shadowNearClip;
		}

		return camera.GetNearClipDistance();
	}

	float Light::DeriveShadowFarClipDistance(const Camera& camera) const
	{
		
		if (m_shadowFarClip > 0.0f)
		{
			return m_shadowFarClip;
		}

		if (m_lightType == LightType::Directional)
		{
			return 0.0f;
		}

		return m_range;
	}

	void Light::SetCameraRelative(Camera* camera)
	{
		m_cameraToBeRelativeTo = camera;
		m_derivedCamRelativeDirty = true;
	}

	const float pi = static_cast<float>(4.0 * atan(1.0 ));
    const float two_pi = static_cast<float>( 2.0 * pi );
    const float half_pi = static_cast<float>( 0.5 * pi );

	// TODO: Move to custom location
	Radian ASin (const float value)
    {
        if ( -1.0f < value )
        {
            if ( value < 1.0f )
            {
				return Radian(asin(value));   
            }

        	return Radian(half_pi);
        }

		return Radian(-half_pi);
    }

	bool Light::IsInLightRange(const Sphere& sphere) const
	{
		//directional light always intersects (check only spotlight and point)
		if (m_lightType == LightType::Directional)
		{
			return true;
		}

		// Check that the sphere is within the sphere of the light
		bool intersects = sphere.Intersects(Sphere(m_derivedPosition, m_range));

		// If this is a spotlight, check that the sphere is within the cone of the spot light
		if (intersects && m_lightType == LightType::Spot)
		{
			// check first check of the sphere surrounds the position of the light
			// (this covers the case where the center of the sphere is behind the position of the light
			// something which is not covered in the next test).
			intersects = sphere.Intersects(m_derivedPosition);

			// if not test cones
			if (!intersects)
			{
				// Calculate the cone that exists between the sphere and the center position of the light
				const Vector3 lightSphereConeDirection = sphere.GetCenter() - m_derivedPosition;
				const Radian halfLightSphereConeAngle = ASin(sphere.GetRadius() / lightSphereConeDirection.GetLength());

				//Check that the light cone and the light-position-to-sphere cone intersect)
				const Radian angleBetweenConeDirections = lightSphereConeDirection.AngleBetween(m_derivedDirection);
				intersects = angleBetweenConeDirections <=  halfLightSphereConeAngle + m_spotOuter * 0.5f;
			}
		}

		return intersects;
	}

	bool Light::IsInLightRange(const AABB& aabb) const
	{
		if (m_lightType == LightType::Directional)
		{
			return true;
		}

		if (aabb.Intersects(m_derivedPosition))
		{
			return true;
		}
		
		// Check that the container is within the sphere of the light
		bool intersects = Intersects(Sphere(m_derivedPosition, m_range), aabb);

		// If this is a spotlight, do a more specific check
		if (intersects && (m_lightType == LightType::Spot) && m_spotOuter.GetValueRadians() <= Pi)
		{
			// Create a rough bounding box around the light and check if
			const Quaternion localToWorld = Vector3::UnitZ.GetRotationTo(m_derivedDirection);

			const float boxOffset = Sin(m_spotOuter * 0.5f) * m_range;
			AABB lightBoxBound;
			lightBoxBound.Combine(Vector3::Zero);
			lightBoxBound.Combine(localToWorld * Vector3(boxOffset, boxOffset, -m_range));
			lightBoxBound.Combine(localToWorld * Vector3(-boxOffset, boxOffset, -m_range));
			lightBoxBound.Combine(localToWorld * Vector3(-boxOffset, -boxOffset, -m_range));
			lightBoxBound.Combine(localToWorld * Vector3(boxOffset, -boxOffset, -m_range));
			lightBoxBound.max += m_derivedPosition;
			lightBoxBound.min += m_derivedPosition;
			intersects = lightBoxBound.Intersects(aabb);
			
			// If the bounding box check succeeded do one more test
			if (intersects)
			{
				// Check intersection again with the bounding sphere of the container
				// Helpful for when the light is at an angle near one of the vertexes of the bounding box
				intersects = IsInLightRange(Sphere(aabb.GetCenter(), aabb.GetExtents().GetLength()));
			}
		}
		return intersects;
	}

	void Light::NotifyAttachmentChanged(Node* parent, const bool isTagPoint)
	{
		m_derivedTransformDirty = true;

		MovableObject::NotifyAttachmentChanged(parent, isTagPoint);
	}

	void Light::NotifyMoved()
	{
		m_derivedTransformDirty = true;

		MovableObject::NotifyMoved();
	}

	const AABB& Light::GetBoundingBox() const
	{
		// Lights don't have a bounding box
		static AABB box;
		return box;
	}

	void Light::PopulateRenderQueue(RenderQueue& queue)
	{
		// Nothing to do, lights don't render anything
	}

	const String& Light::GetMovableType() const
	{
		static String movableType = "Light";
		return movableType;
	}

	float Light::GetBoundingRadius() const
	{
		// Lights don't have a bounding radius
		return 0.0f;
	}

	void Light::VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables)
	{
		// Nothing to render
	}

	void Light::Update() const
	{
		if (m_derivedTransformDirty)
		{
			if (m_parentNode)
			{
				const auto& parentOrientation = m_parentNode->GetDerivedOrientation();
				const auto& parentPosition = m_parentNode->GetDerivedPosition();
				m_derivedDirection = parentOrientation * m_direction;
				m_derivedPosition = parentOrientation * m_position;
				m_derivedPosition += parentPosition;
			}
			else
			{
				m_derivedPosition = m_position;
				m_derivedDirection = m_direction;
			}

			m_derivedTransformDirty = false;
			m_derivedCamRelativeDirty = true;
		}

		if (m_cameraToBeRelativeTo && m_derivedCamRelativeDirty)
		{
			m_derivedCamRelativePosition = m_derivedPosition - m_cameraToBeRelativeTo->GetDerivedPosition();
			m_derivedCamRelativeDirty = false;
		}
	}
}
