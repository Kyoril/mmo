#pragma once

#include <memory>

#include "buffer_base.h"
#include "base/macros.h"

namespace mmo
{
	//// This is the base class of a constant buffer.
	class ConstantBuffer
	{
	protected:
		ConstantBuffer(size_t size)
			: m_size(size)
		{
			ASSERT(size > 0);
			ASSERT(size % 4 == 0);
		}

	public:
		virtual ~ConstantBuffer() = default;

	public:
		virtual void BindToStage(ShaderType type, uint32 slot) = 0;

		/// A fast way to update the data of the constant buffer. Data has to have the same size as the buffer!
		virtual void Update(void* data) = 0;

		/// Gets the size of the constant buffer in bytes.
		size_t GetSize() const { return m_size; }

	protected:
		size_t m_size;
	};

	typedef std::shared_ptr<ConstantBuffer> ConstantBufferPtr;
}
