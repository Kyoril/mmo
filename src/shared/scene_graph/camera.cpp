// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "camera.h"

#include "graphics/graphics_device.h"

namespace mmo
{
	Camera::Camera(const String& name)
		: MovableObject(name)
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
		if (!m_viewInvalid)
		{
			return;
		}

		m_projMatrix = GraphicsDevice::Get().MakeProjectionMatrix(m_fovY, m_aspect, m_nearDist, m_farDist);
		m_viewMatrix = Matrix4::Identity;
		
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
}
