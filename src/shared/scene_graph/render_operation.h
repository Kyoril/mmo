// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "graphics/graphics_device.h"

namespace mmo
{
	class RenderOperation
	{
	public:
		TopologyType topology { TopologyType::TriangleList };

		bool useIndexes { true };

		VertexBuffer* vertexBuffer;

		IndexBuffer* indexBuffer;
	};
}