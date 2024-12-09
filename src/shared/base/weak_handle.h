#pragma once

#include "non_copyable.h"
#include "base/signal.h"

namespace mmo
{
	/// Base class for weak script handles.
	template<class T>
	class WeakHandle : public NonCopyable
	{
	public:
		template<class Signal>
		explicit WeakHandle(T& object, Signal& invalidation)
			: m_object(&object)
			, m_invalidated(invalidation.connect([this] { Reset(); }))
		{
		}

		WeakHandle()
			: m_object(nullptr)
		{
		}

		virtual ~WeakHandle() override = default;

		[[nodiscard]] bool Empty() const
		{
			return (Get() == nullptr);
		}

		T* Get() const
		{
			return m_object;
		}

		T& operator*() const
		{
			ASSERT(Get());
			return *Get();
		}

		T* operator->() const
		{
			ASSERT(Get());
			return Get();
		}

	private:
		T* m_object;
		scoped_connection m_invalidated;

		void Reset()
		{
			m_object = nullptr;
			m_invalidated.disconnect();
		}
	};
}
