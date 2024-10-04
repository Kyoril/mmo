// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "buffer_base.h"

#include "base/typedefs.h"

#include <memory>


namespace mmo
{
	/// This class represents the base class of a vertex buffer.
	class VertexBuffer : public BufferBase
	{
	public:
		/// 
		VertexBuffer(uint32 vertexCount, uint32 vertexSize, BufferUsage usage);
		/// Virtual default destructor because of inheritance.
		virtual ~VertexBuffer() = default;

	public:

		[[nodiscard]] uint32 GetVertexCount() const { return m_vertexCount; }
		[[nodiscard]] uint32 GetVertexSize() const { return m_vertexSize; }
		[[nodiscard]] BufferUsage GetUsage() const { return m_usage; }
		virtual std::shared_ptr<VertexBuffer> Clone() = 0;

	protected:
		uint32 m_vertexCount;
		uint32 m_vertexSize;
		BufferUsage m_usage;
	};

	typedef std::shared_ptr<VertexBuffer> VertexBufferPtr;
}
