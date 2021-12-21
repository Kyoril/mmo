#pragma once

#include "graphics/graphics_device.h"

namespace mmo
{
	class RenderOperation
	{
	public:
		TopologyType topology { TopologyType::TriangleList };

		bool useIndexes { true };

		VertexBufferPtr vertexBuffer;

		IndexBufferPtr indexBuffer;
	};
}