// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "graphics/graphics_device.h"

namespace mmo
{
	class RenderOperation
	{
	public:
		TopologyType topology { TopologyType::TriangleList };

		VertexFormat vertexFormat { VertexFormat::PosColor };

		bool useIndexes { true };

		VertexBuffer* vertexBuffer;

		IndexBuffer* indexBuffer;

		uint32 startIndex { 0 };

		uint32 endIndex { 0 };

		std::vector<ConstantBuffer*> vertexConstantBuffers;

		std::vector<ConstantBuffer*> pixelConstantBuffers;
	};
}