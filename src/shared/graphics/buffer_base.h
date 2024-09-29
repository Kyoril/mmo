// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include <cstddef>
#include <vector>

#include "base/typedefs.h"

namespace mmo
{
	enum class BufferUsage
	{
		Static = 1,
		Dynamic = 2,
		WriteOnly = 4,
		Discardable = 8,
		StaticWriteOnly = Static | WriteOnly,
		DynamicWriteOnly = Dynamic | WriteOnly,
		DynamicWriteOnlyDiscardable = Dynamic | WriteOnly | Discardable
	};

	typedef std::vector<BufferUsage> BufferUsageList;

	enum class LockOptions
	{
		Normal,
		Discard,
		ReadOnly,
		NoOverwrite,
		WriteOnly
	};

	/// This is the base class of a hardware buffer, for example used for vertex buffers.
	/// It supports map and unmap.
	class BufferBase 
		: public NonCopyable
	{
	public:
		/// Virtual default destructor because of inheritance.
		virtual ~BufferBase() override = default;

	public:
		/// This method supports mapping the vertex buffer to access it's data if possible.
		virtual void* Map(LockOptions lock) = 0;
		/// 
		virtual void Unmap() = 0;
		/// 
		virtual void Set(uint16 slot) = 0;
	};

	/// Scoped buffer lock which will call map in the beginning and unmap at the destructor.
	/// This ensures that the Unmap call always happens.
	template<class T>
	class CScopedGxBufferLock
	{
	public:
		CScopedGxBufferLock(BufferBase& buffer, const LockOptions options)
			: m_buffer(buffer)
		{
			m_memory = static_cast<T*>(m_buffer.Map(options));
		}
		~CScopedGxBufferLock()
		{
			m_buffer.Unmap();
		}

	public:
		T* Get()
		{
			return m_memory;
		}
		T* operator[](std::size_t index)
		{
			return m_memory + index;
		}

		T* operator->() const { return Get(); }

	private:
		BufferBase& m_buffer;
		T* m_memory;
	};
}
