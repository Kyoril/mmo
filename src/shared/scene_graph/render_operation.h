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

		/// @brief Resets the operation to defaults for reuse without releasing the heap storage of its
		///        constant-buffer vectors. Reusing a single RenderOperation across draw calls (clearing
		///        instead of reconstructing) avoids a per-draw-call heap allocation for these vectors,
		///        which matters because RenderSingleObject runs once per visible object per pass.
		void Reset(const uint32 renderGroupId)
		{
			m_renderGroupId = renderGroupId;
			topology = TopologyType::TriangleList;
			vertexFormat = VertexFormat::PosColor;
			vertexData = nullptr;
			indexData = nullptr;
			material.reset();
			pixelShaderType = PixelShaderType::Forward;
			vertexConstantBuffers.clear();
			pixelConstantBuffers.clear();
			instanceBuffer = nullptr;
			instanceCount = 0;
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
