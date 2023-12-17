// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "graphics/index_buffer.h"

#include <vector>

namespace mmo
{
	class GraphicsDeviceNull;

	/// Null implementation of an index buffer.
	class IndexBufferNull : public IndexBuffer
	{
	public:
		IndexBufferNull(GraphicsDeviceNull& device, size_t indexCount, IndexBufferSize indexSize, BufferUsage usage, const void* initialData);

	public:
		//~Begin CGxBufferBase
		virtual void* Map(LockOptions options) override;
		virtual void Unmap() override;
		virtual void Set(uint16 slot) override;
		//~End CGxBufferBase

	private:
		GraphicsDeviceNull& m_device;
		std::vector<uint32> m_indices;
	};
}

