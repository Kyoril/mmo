// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "graphics/index_buffer.h"

#include <vector>

namespace mmo
{
	class GraphicsDeviceMetal;

	/// Null implementation of an index buffer.
	class IndexBufferMetal : public IndexBuffer
	{
	public:
        IndexBufferMetal(GraphicsDeviceMetal& device, size_t indexCount, IndexBufferSize indexSize, const void* initialData);

	public:
		//~Begin CGxBufferBase
		virtual void* Map(LockOptions lock) override;
		virtual void Unmap() override;
		virtual void Set(uint16 slot) override;
		//~End CGxBufferBase

	private:
		GraphicsDeviceMetal& m_device;
		std::vector<uint32> m_indices;
	};
}

