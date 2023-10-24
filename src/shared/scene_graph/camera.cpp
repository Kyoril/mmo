// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "camera.h"

#include "scene_node.h"
#include "graphics/graphics_device.h"

#include "math/sphere.h"

namespace mmo
{
	Camera::Camera(const String& name)
		: MovableObject(name)
		, m_fovY(Radian(Pi / 4.0f))
		, m_aspect(16.0f / 9.0f)
		, m_orthoHeight(1000)
		, m_viewInvalid(true)
	{
	}

	const Matrix4& Camera::GetProjectionMatrix() const
	{
		UpdateFrustum();
		
		return m_projMatrix;
	}

	const Matrix4& Camera::GetViewMatrix() const
	{
		UpdateFrustum();
		
		return m_viewMatrix;
	}

	void Camera::SetOrientation(const Quaternion& quaternion)
	{
		m_viewInvalid = true;
	}

	Ray Camera::GetCameraToViewportRay(float viewportX, float viewportY, float maxDistance) const
	{
		const Matrix4 inverseVP = (GetProjectionMatrix() * GetViewMatrix()).Inverse();

		const float nx = (2.0f * viewportX) - 1.0f;
		const float ny = 1.0f - (2.0f * viewportY);

		const Vector3 nearPoint(nx, ny, -1.f);
		const Vector3 midPoint(nx, ny, 0.0f);

		// Get ray origin and ray target on near plane in world space
		Vector3 rayOrigin, rayTarget;

		rayOrigin = inverseVP * nearPoint;
		rayTarget = inverseVP * midPoint;

		Vector3 rayDirection = rayTarget - rayOrigin;
		rayDirection.Normalize();

		return Ray(rayOrigin, rayDirection, maxDistance);
	}

	void Camera::UpdateFrustum() const
	{
		if (!IsFrustumOutOfDate())
		{
			return;
		}

		float left, right, bottom, top;
		CalcProjectionParameters(left, right, bottom, top);

		m_projMatrix = GraphicsDevice::Get().MakeProjectionMatrix(m_fovY, m_aspect, m_nearDist, m_farDist);
		m_viewMatrix = MakeViewMatrix(GetDerivedPosition(), GetDerivedOrientation());
		
		m_viewInvalid = false;
	}

	void Camera::UpdateView() const
	{
		if (!IsViewOutOfDate())
		{
			return;
		}
		
		const Quaternion& orientation = GetOrientationForViewUpdate();
		const Vector3& position = GetPositionForViewUpdate();
		m_viewMatrix = MakeViewMatrix(position, orientation);
		m_recalcView = false;

		m_recalcFrustumPlanes = true;
		m_recalcWorldSpaceCorners = true;

		if (m_obliqueDepthProjection)
		{
			m_recalcFrustum = true;
		}
	}

	bool Camera::IsViewOutOfDate() const
	{
		if (m_parentNode)
		{
			if (m_recalcView ||
				m_parentNode->GetDerivedOrientation() != m_lastParentOrientation ||
				m_parentNode->GetDerivedPosition() != m_lastParentPosition)
			{
				m_lastParentOrientation = m_parentNode->GetDerivedOrientation();
				m_lastParentPosition = m_parentNode->GetDerivedPosition();
				m_recalcView = true;
			}
		}

		return m_recalcView;
	}

	bool Camera::IsFrustumOutOfDate() const
	{
		if (m_obliqueDepthProjection)
		{
			if (IsViewOutOfDate())
			{
				m_recalcFrustum = true;
			}

			// TODO
		}

		return m_recalcFrustum;
	}

	void Camera::CalcProjectionParameters(float& left, float& right, float& bottom, float& top) const
	{
	}

	const String& Camera::GetMovableType() const
	{
		static String cameraMovableType = "Camera";
		return cameraMovableType;
	}

	const AABB& Camera::GetBoundingBox() const
	{
		static AABB replaceMe;
		return replaceMe;
	}

	float Camera::GetBoundingRadius() const
	{
		// TODO
		return 0.0f;
	}

	void Camera::VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables)
	{
		// TODO
	}

	void Camera::SetAspectRatio(const float aspect)
	{
		m_aspect = aspect;
		m_viewInvalid = true;
	}

	const Quaternion& Camera::GetDerivedOrientation() const
	{
		if (m_parentNode)
		{
			m_lastParentOrientation = m_parentNode->GetDerivedOrientation();
		}

		return m_lastParentOrientation;
	}

	const Vector3& Camera::GetDerivedPosition() const
	{
		if (m_parentNode)
		{
			m_lastParentPosition = m_parentNode->GetDerivedPosition();
		}

		return m_lastParentPosition;
	}

	bool Camera::IsVisible(const Sphere& bound) const
	{
		UpdateFrustum();

		return true;
	}
}
