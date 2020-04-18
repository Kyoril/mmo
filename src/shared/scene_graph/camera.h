// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "movable_object.h"

#include "base/non_copyable.h"

namespace mmo
{
	namespace scene_graph
	{
		/// This class represents a camera inside of a scene. A camera is a special type
		/// of movable object, which allows to collect all renderable objects inside of it's
		/// view frustum.
		class Camera final
			: public IMovableObject
			, public NonCopyable
		{
		public:

		};
	}
}
