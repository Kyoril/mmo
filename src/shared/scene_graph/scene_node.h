// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"


namespace mmo
{
	/// This class is a node inside of a scene. Nodes can be used to group
	/// movable objects together and move them around in a scene.
	/// Each scene has exactly one root node, which can have one or multiple
	/// child nodes and/or attached movable objects to it.
	class SceneNode final
		: public NonCopyable
	{
	public:

	}
}
