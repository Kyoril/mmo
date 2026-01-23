// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics/graphics_device.h"
#include "graphics/vertex_index_data.h"

namespace mmo
{
	class RenderOperation
	{
	public:
		explicit RenderOperation(const uint32 renderGroupId)
			: m_renderGroupId(renderGroupId)
		{
		}

	public:
		TopologyType topology { TopologyType::TriangleList };

		VertexFormat vertexFormat { VertexFormat::PosColor };

		VertexData* vertexData{ nullptr };

		IndexData* indexData{ nullptr };

		MaterialPtr material{nullptr};

		PixelShaderType pixelShaderType{ PixelShaderType::Forward };

		std::vector<ConstantBuffer*> vertexConstantBuffers{};

		std::vector<ConstantBuffer*> pixelConstantBuffers{};

		/// @brief Instance buffer for GPU instancing. If non-null, enables instanced rendering.
		VertexBuffer* instanceBuffer{ nullptr };

		/// @brief Number of instances to render when instanceBuffer is set.
		uint32 instanceCount{ 0 };

		uint32 GetRenderGroupId() const { return m_renderGroupId; }

	private:
		uint32 m_renderGroupId;
	};
}
