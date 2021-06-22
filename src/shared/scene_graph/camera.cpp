// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "camera.h"

#include "graphics/graphics_device.h"

namespace mmo
{
	Camera::Camera(Scene& scene, String name)
		: MovableObject(scene)
		, m_name(std::move(name))
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
}
