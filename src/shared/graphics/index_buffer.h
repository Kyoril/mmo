// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "buffer_base.h"

#include <memory>


namespace mmo
{
	/// Size of an index element in the buffer.
	enum class IndexBufferSize
	{
		/// 16 bit index buffer.
		Index_16,
		/// 32 bit index buffer.
		Index_32,
	};

	/// This class represents the base class of a index buffer.
	class IndexBuffer
		: public mmo::BufferBase
	{
	public:
		IndexBuffer(size_t InIndexCount, IndexBufferSize InIndexSize);
		/// Virtual default destructor because of inheritance.
		virtual ~IndexBuffer() = default;

	protected:
		size_t IndexCount;
		IndexBufferSize IndexSize;
	};

	typedef std::shared_ptr<IndexBuffer> IndexBufferPtr;
}
