// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "camera.h"

#include "scene_node.h"
#include "graphics/graphics_device.h"
#include "log/default_log_levels.h"

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

	void Camera::InvalidateView()
	{
		m_recalcView = true;
		m_recalcFrustum = true;
		m_recalcWorldSpaceCorners = true;
	}

	void Camera::InvalidateFrustum()
	{
		m_recalcFrustum = true;
		m_recalcFrustumPlanes = true;
		m_recalcWorldSpaceCorners = true;
	}

	void Camera::GetNormalizedScreenPosition(const Vector3& worldPosition, float& x, float& y) const
	{
		const Matrix4& viewMatrix = GetViewMatrix();
		const Matrix4& projMatrix = GetProjectionMatrix();

		// Project world position to normalized screen space position
		const Vector4 positionCameraSpace = viewMatrix * Vector4(worldPosition, 1.0f);
		const Vector4 clipSpace = projMatrix * positionCameraSpace;

		const auto screenPos = Vector3(clipSpace.x / clipSpace.w, clipSpace.y / clipSpace.w, clipSpace.z / clipSpace.w);
		
		x = (screenPos.x + 1.0f) * 0.5f;
		y = 1.0f - (screenPos.y + 1.0f) * 0.5f;
	}

	AABBVisibility Camera::GetVisibility(const AABB& bound) const
	{
		if (bound.IsNull())
		{
			return aabb_visibility::None;
		}

		// Get center of the box
		const Vector3 center = bound.GetCenter();

		// Get the half-size of the box
		const Vector3 halfSize = bound.GetExtents();

		bool allInside = true;

		for (const auto& plane : m_frustumPlanes)
		{
			// This updates frustum planes and deals with cull frustum
			const Plane::Side side = plane.GetSide(center, halfSize);
			if (side == Plane::NegativeSide) return aabb_visibility::None;

			// We can't return now as the box could be later on the negative side of a plane.
			if (side == Plane::BothSides)
			{
				allInside = false;
			}
		}

		if (allInside)
		{
			return aabb_visibility::Full;
		}

		return aabb_visibility::Partial;
	}

	void Camera::SetFarClipDistance(const float distance)
	{
		m_farDist = distance;
		InvalidateFrustum();
	}

	void Camera::SetNearClipDistance(const float distance)
	{
		m_nearDist = distance;
		InvalidateFrustum();
	}

	void Camera::SetProjectionType(const ProjectionType type)
	{
		m_projectionType = type;
		InvalidateFrustum();
	}

	void Camera::SetOrthoWindow(float w, float h)
	{
		m_orthoHeight = h;
		m_aspect = w / h;
		InvalidateFrustum();
	}

	void Camera::SetOrthoWindowHeight(float h)
	{
		m_orthoHeight = h;
		InvalidateFrustum();
	}

	void Camera::SetOrthoWindowWidth(float w)
	{
		m_orthoHeight = w / m_aspect;
		InvalidateFrustum();
	}

	void Camera::SetFrustumExtents(float left, float right, float top, float bottom)
	{
		m_frustumExtentsManuallySet = true;
		m_left = left;
		m_right = right;
		m_top = top;
		m_bottom = bottom;

		InvalidateFrustum();
	}

	void Camera::ResetFrustumExtents()
	{
		m_frustumExtentsManuallySet = false;
		InvalidateFrustum();
	}

	void Camera::GetFrustumExtents(float& outLeft, float& outRight, float& outTop, float& outBottom) const
	{
		UpdateFrustum();
		outLeft = m_left;
		outRight = m_right;
		outTop = m_top;
		outBottom = m_bottom;
	}

	void Camera::SetFOVy(const Radian& fovY)
	{
		m_fovY = fovY;
		InvalidateFrustum();
	}

	const Vector3* Camera::GetWorldSpaceCorners() const
	{
		UpdateWorldSpaceCorners();
		return m_worldSpaceCorners;
	}

	void Camera::SetCustomProjMatrix(bool enable, const Matrix4& projMatrix)
	{
		m_customProjMatrix = enable;
		if (enable)
		{
			m_projMatrix = projMatrix;
		}
		InvalidateFrustum();
	}

	void Camera::UpdateFrustum() const
	{
		if (!IsFrustumOutOfDate())
		{
			return;
		}

		if (!m_customProjMatrix)
		{
			// Create the appropriate projection matrix based on projection type
			if (m_projectionType == ProjectionType::Perspective)
			{
				m_projMatrix = GraphicsDevice::Get().MakeProjectionMatrix(m_fovY, m_aspect, m_nearDist, m_farDist);
			}
			else // ProjectionType::Orthographic
			{
				// Use orthographic projection
				const float halfWidth = GetOrthoWindowWidth() * 0.5f;
				const float halfHeight = GetOrthoWindowHeight() * 0.5f;
				m_projMatrix =					
					GraphicsDevice::Get().MakeOrthographicMatrix(-halfWidth, halfHeight, halfWidth, -halfHeight, m_nearDist, m_farDist);
			}
		}
		
		if (!m_customViewMatrix)
		{
			m_viewMatrix = MakeViewMatrix(GetDerivedPosition(), GetDerivedOrientation());
		}
		
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
		if (m_frustumExtentsManuallySet)
		{
			left = m_left;
			right = m_right;
			bottom = m_bottom;
			top = m_top;
		}
		else if (m_projectionType == ProjectionType::Perspective)
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
		else
		{
			float half_w = GetOrthoWindowWidth() * 0.5f;
			float half_h = GetOrthoWindowHeight() * 0.5f;

			left = -half_w;
			right = +half_w;
			bottom = -half_h;
			top = +half_h;

			m_left = left;
			m_right = right;
			m_top = top;
			m_bottom = bottom;
		}
		
	}

	void Camera::UpdateWorldSpaceCorners() const
	{
		UpdateView();

		if (m_recalcWorldSpaceCorners)
		{
			const Matrix4 eyeToWorld = (GetProjectionMatrix() * GetViewMatrix()).Inverse();

			// NDC cube corners in clip space
			const Vector4 ndcCorners[8] = {
				{-1, -1, 0, 1},
				{ 1, -1, 0, 1},
				{-1,  1, 0, 1},
				{ 1,  1, 0, 1},
				{-1, -1,  1, 1},
				{ 1, -1,  1, 1},
				{-1,  1,  1, 1},
				{ 1,  1,  1, 1},
			};

			for (int i = 0; i < 8; ++i)
			{
				Vector4 corner = eyeToWorld * ndcCorners[i];
				corner /= corner.w;
				m_worldSpaceCorners[i] = Vector3(corner.x, corner.y, corner.z);
			}

			m_recalcWorldSpaceCorners = false;
		}
	}

	void Camera::NotifyMoved()
	{
		MovableObject::NotifyMoved();

		InvalidateView();
		InvalidateFrustum();
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
		InvalidateFrustum();
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

	Vector3 Camera::GetDerivedDirection() const
	{
		// Direction points down -Z
		UpdateView();

		return GetDerivedOrientation() * Vector3::NegativeUnitZ;
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

	void Camera::SetupShadowCamera(Camera& shadowCamera, const Vector3& lightDirection)
	{
	    // Ensure frustum corners are up to date
	    UpdateFrustumPlanes();
	    UpdateFrustum();

		float clipNear = GetNearClipDistance(); // 0.3f
		float clipFar = GetFarClipDistance();  // 1000.0f

		float sliceNear = 0.3f;
		float sliceFar = 50.0f; // units in world space you want shadows for

		float z0 = (sliceNear - clipNear) / (clipFar - clipNear); // convert world z to ndc z
		float z1 = (sliceFar - clipNear) / (clipFar - clipNear);

	    // 1. Transform frustum corners to world space
	    Vector3 worldCorners[8];
	    const Matrix4 invViewProj = (GetProjectionMatrix() * GetViewMatrix()).Inverse();
	    for (int i = 0; i < 8; ++i)
	    {
			static const Vector4 clipCorners[8] = {
				{-1, -1, z0, 1}, {1, -1, z0, 1}, {-1, 1, z0, 1}, {1, 1, z0, 1},
				{-1, -1, z1, 1}, {1, -1, z1, 1}, {-1, 1, z1, 1}, {1, 1, z1, 1}
			};

			Vector4 corner = invViewProj * clipCorners[i];
			worldCorners[i] = Vector3(corner.x / corner.w, corner.y / corner.w, corner.z / corner.w);
	    }

	    // 2. Compute bounding box of the frustum
	    AABB frustumBounds;
	    for (int i = 0; i < 8; ++i)
	    {
	        frustumBounds.Combine(worldCorners[i]);
	    }

	    // 3. Build light-space view matrix
	    Vector3 center = frustumBounds.GetCenter();
	    
	    // Use a normalized light direction
	    Vector3 normalizedLightDir = lightDirection.NormalizedCopy();
	    
	    // Pull back from center in light direction - use a larger value for directional lights
	    Vector3 eye = center - normalizedLightDir * (frustumBounds.GetSize().GetLength());
	    
	    // Choose an appropriate up vector that's not parallel to the light direction
	    Vector3 up = Vector3::UnitY;
	    if (::fabsf(up.Dot(normalizedLightDir)) > 0.9f)
	        up = Vector3::UnitZ;

	    // Create the view matrix for the light's perspective
		Matrix4 lightView = Matrix4::LookAt(eye, center, up);

	    // 4. Project frustum bounds into light space and build orthographic projection
		Vector3 lightSpaceCorners[8];
	    AABB lightSpaceBounds;
		float minZ = FLT_MAX, maxZ = -FLT_MAX;
	    for (int i = 0; i < 8; ++i)
	    {
			lightSpaceCorners[i] = lightView.TransformAffine(worldCorners[i]);
			lightSpaceBounds.Combine(lightSpaceCorners[i]);

			minZ = std::min(minZ, lightSpaceCorners[i].z);
			maxZ = std::max(maxZ, lightSpaceCorners[i].z);
	    }

		// 5. Shift the light view forward so that near plane is at 0 in light space
		lightView = Matrix4::GetTrans(0.0f, 0.0f, -minZ) * lightView;

		AABB shiftedLightBounds;
		for (int i = 0; i < 8; ++i)
		{
			Vector3 shifted = lightView.TransformAffine(worldCorners[i]);
			shiftedLightBounds.Combine(shifted);
		}

	    // 5. Create orthographic projection matrix
		float padding = 5.0f;
		float nearZ = std::max(0.0f, minZ - padding);
		float farZ = maxZ + padding;

	    // Set up the shadow camera with the calculated values
	    shadowCamera.SetProjectionType(ProjectionType::Orthographic);
	    shadowCamera.SetOrthoWindow(shiftedLightBounds.GetSize().x, shiftedLightBounds.GetSize().y);
		shadowCamera.SetNearClipDistance(nearZ);
		shadowCamera.SetFarClipDistance(farZ);
	    shadowCamera.SetCustomViewMatrix(true, lightView);
	}

	void Camera::SetCustomViewMatrix(bool enable, const Matrix4& viewMatrix)
	{
		m_customViewMatrix = enable;
		if (enable)
		{
			ASSERT(viewMatrix.IsAffine());
			m_viewMatrix = viewMatrix;
		}
		InvalidateView();
	}
}
