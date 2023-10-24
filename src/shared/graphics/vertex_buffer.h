// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

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
		VertexBuffer(uint32 vertexCount, uint32 vertexSize, bool dynamic);
		/// Virtual default destructor because of inheritance.
		virtual ~VertexBuffer() = default;

	public:

		[[nodiscard]] uint32 GetVertexCount() const { return m_vertexCount; }
		[[nodiscard]] uint32 GetVertexSize() const { return m_vertexSize; }
		[[nodiscard]] bool IsDynamic() const { return m_dynamic; }
		virtual std::unique_ptr<VertexBuffer> Clone() = 0;

	protected:
		uint32 m_vertexCount;
		uint32 m_vertexSize;
		bool m_dynamic;
	};

	typedef std::unique_ptr<VertexBuffer> VertexBufferPtr;
}
