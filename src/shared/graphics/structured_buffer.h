// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <memory>

#include "buffer_base.h"
#include "base/macros.h"

namespace mmo
{
	/// @brief Abstract base class for structured buffers (StructuredBuffer/RWStructuredBuffer in HLSL).
	/// 
	/// Structured buffers allow for dynamic-sized arrays of structured data to be passed to shaders.
	/// Unlike constant buffers which have a fixed size limit (usually 64KB), structured buffers can
	/// hold much larger amounts of data, making them ideal for scenarios with many elements like lights.
	class StructuredBuffer
	{
	protected:
		/// @brief Constructs a structured buffer.
		/// @param elementSize The size of a single element in bytes.
		/// @param elementCount The maximum number of elements the buffer can hold.
		StructuredBuffer(size_t elementSize, size_t elementCount)
			: m_elementSize(elementSize)
			, m_elementCount(elementCount)
		{
			ASSERT(elementSize > 0);
			ASSERT(elementCount > 0);
		}

	public:
		/// @brief Virtual destructor.
		virtual ~StructuredBuffer() = default;

	public:
		/// @brief Binds the structured buffer to a shader stage at the specified slot.
		/// @param type The shader stage to bind to.
		/// @param slot The slot index to bind to.
		virtual void BindToStage(ShaderType type, uint32 slot) = 0;

		/// @brief Updates the buffer with new data.
		/// @param data Pointer to the data to upload.
		/// @param elementCount Number of elements to upload (must be <= max element count).
		virtual void Update(const void* data, size_t elementCount) = 0;

		/// @brief Gets the size of a single element in bytes.
		/// @return The element size in bytes.
		[[nodiscard]] size_t GetElementSize() const
		{
			return m_elementSize;
		}

		/// @brief Gets the maximum number of elements this buffer can hold.
		/// @return The maximum element count.
		[[nodiscard]] size_t GetElementCount() const
		{
			return m_elementCount;
		}

		/// @brief Gets the total buffer size in bytes.
		/// @return The total size in bytes.
		[[nodiscard]] size_t GetTotalSize() const
		{
			return m_elementSize * m_elementCount;
		}

	protected:
		/// @brief Size of a single element in bytes.
		size_t m_elementSize;
		
		/// @brief Maximum number of elements the buffer can hold.
		size_t m_elementCount;
	};

	typedef std::shared_ptr<StructuredBuffer> StructuredBufferPtr;
}
