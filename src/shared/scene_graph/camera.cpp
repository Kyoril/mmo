// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

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
		m_lastParentOrientation = quaternion;
		m_lastParentOrientation.Normalize();
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

		const Vector3 rayOrigin = inverseVP * nearPoint;
		const Vector3 rayTarget = inverseVP * midPoint;

		Vector3 rayDirection = rayTarget - rayOrigin;
		rayDirection.Normalize();

		return Ray(rayOrigin, rayDirection, maxDistance);
	}

	void Camera::GetNormalizedScreenPosition(const Vector3& worldPosition, float& x, float& y) const
	{
		const Matrix4& viewMatrix = GetViewMatrix();
		const Matrix4& projMatrix = GetProjectionMatrix();

		// Project world position to normalized screen space position
		const Vector4 positionCameraSpace = viewMatrix * Vector4(worldPosition, 1.0f);
		const Vector4 clipSpace = projMatrix * positionCameraSpace;

		const Vector3 screenPos = Vector3(clipSpace.x / clipSpace.w, clipSpace.y / clipSpace.w, clipSpace.z / clipSpace.w);
		
		x = (screenPos.x + 1.0f) * 0.5f;
		y = (screenPos.y + 1.0f) * 0.5f;
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

	void Camera::UpdateFrustumPlanes() const
	{
		UpdateView();
		UpdateFrustum();

		if (m_recalcFrustumPlanes)
		{
			UpdateFrustumPlanesImpl();
		}
	}

	void Camera::UpdateFrustumPlanesImpl() const
	{
		Matrix4 combo = m_projMatrix * m_viewMatrix;

		m_frustumPlanes[FrustumPlaneLeft].normal.x = combo[3][0] + combo[0][0];
		m_frustumPlanes[FrustumPlaneLeft].normal.y = combo[3][1] + combo[0][1];
		m_frustumPlanes[FrustumPlaneLeft].normal.z = combo[3][2] + combo[0][2];
		m_frustumPlanes[FrustumPlaneLeft].d = combo[3][3] + combo[0][3];

		m_frustumPlanes[FrustumPlaneRight].normal.x = combo[3][0] - combo[0][0];
		m_frustumPlanes[FrustumPlaneRight].normal.y = combo[3][1] - combo[0][1];
		m_frustumPlanes[FrustumPlaneRight].normal.z = combo[3][2] - combo[0][2];
		m_frustumPlanes[FrustumPlaneRight].d = combo[3][3] - combo[0][3];

		m_frustumPlanes[FrustumPlaneTop].normal.x = combo[3][0] - combo[1][0];
		m_frustumPlanes[FrustumPlaneTop].normal.y = combo[3][1] - combo[1][1];
		m_frustumPlanes[FrustumPlaneTop].normal.z = combo[3][2] - combo[1][2];
		m_frustumPlanes[FrustumPlaneTop].d = combo[3][3] - combo[1][3];

		m_frustumPlanes[FrustumPlaneBottom].normal.x = combo[3][0] + combo[1][0];
		m_frustumPlanes[FrustumPlaneBottom].normal.y = combo[3][1] + combo[1][1];
		m_frustumPlanes[FrustumPlaneBottom].normal.z = combo[3][2] + combo[1][2];
		m_frustumPlanes[FrustumPlaneBottom].d = combo[3][3] + combo[1][3];

		m_frustumPlanes[FrustumPlaneNear].normal.x = combo[3][0] + combo[2][0];
		m_frustumPlanes[FrustumPlaneNear].normal.y = combo[3][1] + combo[2][1];
		m_frustumPlanes[FrustumPlaneNear].normal.z = combo[3][2] + combo[2][2];
		m_frustumPlanes[FrustumPlaneNear].d = combo[3][3] + combo[2][3];

		m_frustumPlanes[FrustumPlaneFar].normal.x = combo[3][0] - combo[2][0];
		m_frustumPlanes[FrustumPlaneFar].normal.y = combo[3][1] - combo[2][1];
		m_frustumPlanes[FrustumPlaneFar].normal.z = combo[3][2] - combo[2][2];
		m_frustumPlanes[FrustumPlaneFar].d = combo[3][3] - combo[2][3];

		for (auto& p : m_frustumPlanes)
		{
			const float length = p.normal.Normalize();
			p.d /= length;
		}

		m_recalcFrustumPlanes = false;
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
		const Radian thetaY(m_fovY * 0.5f);
		const float tanThetaY = ::tanf(thetaY.GetValueRadians());
		const float tanThetaX = tanThetaY * m_aspect;

		const float nearFocal = m_nearDist;
		const float nearOffsetX = 0.0f * nearFocal;
		const float nearOffsetY = 0.0f * nearFocal;
		const float half_w = tanThetaX * m_nearDist;
		const float half_h = tanThetaY * m_nearDist;

		m_left = -half_w + nearOffsetX;
		m_top = +half_h + nearOffsetY;
		m_right = +half_w + nearOffsetX;
		m_bottom = -half_h + nearOffsetY;

		left = m_left;
		right = m_right;
		bottom = m_bottom;
		top = m_top;
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

	bool Camera::IsVisible(const Sphere& sphere) const
	{
		UpdateFrustumPlanes();

		// For each plane, see if sphere is on negative side
		// If so, object is not visible
		for (int plane = 0; plane < 6; ++plane)
		{
			// Skip far plane if infinite view frustum
			if (plane == FrustumPlaneFar && m_farDist == 0)
				continue;

			// If the distance from sphere center to plane is negative, and 'more negative' 
			// than the radius of the sphere, sphere is outside frustum
			if (m_frustumPlanes[plane].GetDistance(sphere.GetCenter()) < -sphere.GetRadius())
			{
				return false;
			}
		}

		return true;
	}

	bool Camera::IsVisible(const AABB& bound) const
	{
		// Null boxes always invisible
		if (bound.IsNull()) return false;

		// Make any pending updates to the calculated frustum planes
		UpdateFrustumPlanes();

		const Vector3 center = bound.GetCenter();
		const Vector3 halfSize = bound.GetExtents();

		// For each plane, see if all points are on the negative side
		// If so, object is not visible
		for (int plane = 0; plane < 6; ++plane)
		{
			// Skip far plane if infinite view frustum
			if (plane == FrustumPlaneFar && m_farDist == 0)
				continue;

			Plane::Side side = m_frustumPlanes[plane].GetSide(center, halfSize);
			if (side == Plane::NegativeSide)
			{
				return false;
			}

		}

		return true;
	}
}
