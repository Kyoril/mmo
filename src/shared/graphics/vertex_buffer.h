// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "buffer_base.h"

#include <memory>


namespace mmo
{
	/// This class represents the base class of a vertex buffer.
	class VertexBuffer : public BufferBase
	{
	public:
		/// 
		VertexBuffer(size_t InVertexCount, size_t InVertexSize, bool dynamic);
		/// Virtual default destructor because of inheritance.
		virtual ~VertexBuffer() = default;

	protected:
		size_t VertexCount;
		size_t VertexSize;
		bool m_dynamic;
	};

	typedef std::unique_ptr<VertexBuffer> VertexBufferPtr;
}
