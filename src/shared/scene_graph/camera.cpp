// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "camera.h"


namespace mmo
{
	Camera::Camera(Scene& scene, String name)
		: MovableObject(scene)
		, m_name(std::move(name))
	{
	}
}
