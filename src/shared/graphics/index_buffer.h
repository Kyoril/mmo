// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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

		/// @brief Gets the size of each index element.
		[[nodiscard]] IndexBufferSize GetIndexSize() const { return IndexSize; }

		/// @brief Gets the number of indices in the buffer.
		[[nodiscard]] size_t GetIndexCount() const { return IndexCount; }

	protected:
		size_t IndexCount;
		IndexBufferSize IndexSize;
	};

	typedef std::shared_ptr<IndexBuffer> IndexBufferPtr;
}
