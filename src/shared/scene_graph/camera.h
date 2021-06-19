// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "movable_object.h"

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "math/matrix4.h"
#include "math/quaternion.h"
#include "math/radian.h"

namespace mmo
{
	class Scene;

	/// This class represents a camera inside of a scene. A camera is a special type
	/// of movable object, which allows to collect all renderable objects inside of it's
	/// view frustum.
	class Camera final
		: public MovableObject
		, public NonCopyable
	{
	public:
		Camera(Scene& scene, String name);

	public:
		const String& GetName() const { return m_name; }
	
	private:
		String m_name;
		Radian m_fovY;
		float m_fatDist;
		float m_nearDist;
		float m_aspect;
		float m_orthoHeight;
		Quaternion m_lastParentOrientation;
		Vector3 m_lastParentPosition;
		Matrix4 m_projMatrix;
		Matrix4 m_viewMatrix;
		bool m_viewInvalid;
		float m_left, m_right, m_top, m_bottom;
	};
}
