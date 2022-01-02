// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "camera.h"

#include "scene_node.h"
#include "graphics/graphics_device.h"

namespace mmo
{
	Camera::Camera(const String& name)
		: MovableObject(name)
		, m_fovY(Radian(Pi / 4.0f))
		, m_farDist(1000.0f)
		, m_nearDist(0.001f)
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

	void Camera::UpdateFrustum() const
	{
		//if (!m_viewInvalid)
		//{
		//	return;
		//}

		m_projMatrix = GraphicsDevice::Get().MakeProjectionMatrix(m_fovY, m_aspect, m_nearDist, m_farDist);
		m_viewMatrix = MakeViewMatrix(GetParentSceneNode()->GetDerivedPosition(), GetParentSceneNode()->GetDerivedOrientation());
		
		m_viewInvalid = false;
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
}
