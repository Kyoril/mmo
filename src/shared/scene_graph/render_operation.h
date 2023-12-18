// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "graphics/graphics_device.h"
#include "graphics/vertex_index_data.h"

namespace mmo
{
	class RenderOperation
	{
	public:
		TopologyType topology { TopologyType::TriangleList };

		VertexFormat vertexFormat { VertexFormat::PosColor };

		VertexData* vertexData{ nullptr };

		IndexData* indexData{ nullptr };

		MaterialPtr material{nullptr};

		std::vector<ConstantBuffer*> vertexConstantBuffers{};

		std::vector<ConstantBuffer*> pixelConstantBuffers{};
	};
}
