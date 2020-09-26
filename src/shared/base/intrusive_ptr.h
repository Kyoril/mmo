/////////////////////////////////////////////////////////////////////////////////////
// simple - lightweight & fast signal/slots & utility library                      //
//                                                                                 //
//   v1.2 - public domain                                                          //
//   no warranty is offered or implied; use this code at your own risk             //
//                                                                                 //
// AUTHORS                                                                         //
//                                                                                 //
//   Written by Michael Bleis                                                      //
//                                                                                 //
//                                                                                 //
// LICENSE                                                                         //
//                                                                                 //
//   This software is dual-licensed to the public domain and under the following   //
//   license: you are granted a perpetual, irrevocable license to copy, modify,    //
//   publish, and distribute this file as you see fit.                             //
/////////////////////////////////////////////////////////////////////////////////////

// Note: Moved intrusive_ptr<T> class out of signal.h to make it more lightweight to
// access without including the whole signal.h file.

#pragma once

#include <atomic>			// std::atomic
#include <type_traits>		// std::nullptr_t


namespace mmo
{

	template <class T>
	struct intrusive_ptr
	{
		typedef T value_type;
		typedef T* pointer;
		typedef T& reference;

		intrusive_ptr()
			: ptr{ nullptr }
		{
		}

		explicit intrusive_ptr(std::nullptr_t)
			: ptr{ nullptr }
		{
		}

		explicit intrusive_ptr(pointer p)
			: ptr{ p }
		{
			if (ptr) {
				ptr->addref();
			}
		}

		intrusive_ptr(intrusive_ptr const& p)
			: ptr{ p.ptr }
		{
			if (ptr) {
				ptr->addref();
			}
		}

		intrusive_ptr(intrusive_ptr&& p) noexcept
			: ptr{ p.ptr }
		{
			p.ptr = nullptr;
		}

		template <class U>
		explicit intrusive_ptr(intrusive_ptr<U> const& p)
			: ptr{ p.ptr }
		{
			if (ptr) {
				ptr->addref();
			}
		}

		template <class U>
		explicit intrusive_ptr(intrusive_ptr<U>&& p)
			: ptr{ p.ptr }
		{
			p.ptr = nullptr;
		}

		~intrusive_ptr()
		{
			if (ptr) {
				ptr->release();
			}
		}

		pointer get() const
		{
			return ptr;
		}

		operator pointer() const
		{
			return ptr;
		}

		pointer operator -> () const
		{
			assert(ptr != nullptr);
			return ptr;
		}

		reference operator * () const
		{
			assert(ptr != nullptr);
			return *ptr;
		}

		pointer* operator & ()
		{
			assert(ptr == nullptr);
			return &ptr;
		}

		pointer const* operator & () const
		{
			return &ptr;
		}

		intrusive_ptr& operator = (pointer p)
		{
			if (p) {
				p->addref();
			}
			pointer o = ptr;
			ptr = p;
			if (o) {
				o->release();
			}
			return *this;
		}

		intrusive_ptr& operator = (std::nullptr_t)
		{
			if (ptr) {
				ptr->release();
				ptr = nullptr;
			}
			return *this;
		}

		intrusive_ptr& operator = (intrusive_ptr const& p)
		{
			return (*this = p.ptr);
		}

		intrusive_ptr& operator = (intrusive_ptr&& p) noexcept
		{
			if (ptr) {
				ptr->release();
			}
			ptr = p.ptr;
			p.ptr = nullptr;
			return *this;
		}

		template <class U>
		intrusive_ptr& operator = (intrusive_ptr<U> const& p)
		{
			return (*this = p.ptr);
		}

		template <class U>
		intrusive_ptr& operator = (intrusive_ptr<U>&& p)
		{
			if (ptr) {
				ptr->release();
			}
			ptr = p.ptr;
			p.ptr = nullptr;
			return *this;
		}

		void swap(pointer* pp)
		{
			pointer p = ptr;
			ptr = *pp;
			*pp = p;
		}

		void swap(intrusive_ptr& p)
		{
			swap(&p.ptr);
		}

	private:
		pointer ptr;
	};

	template <class T, class U>
	bool operator == (intrusive_ptr<T> const& a, intrusive_ptr<U> const& b)
	{
		return a.get() == b.get();
	}

	template <class T, class U>
	bool operator != (intrusive_ptr<T> const& a, intrusive_ptr<U> const& b)
	{
		return a.get() != b.get();
	}

	template <class T, class U>
	bool operator < (intrusive_ptr<T> const& a, intrusive_ptr<U> const& b)
	{
		return a.get() < b.get();
	}

	template <class T, class U>
	bool operator <= (intrusive_ptr<T> const& a, intrusive_ptr<U> const& b)
	{
		return a.get() <= b.get();
	}

	template <class T, class U>
	bool operator > (intrusive_ptr<T> const& a, intrusive_ptr<U> const& b)
	{
		return a.get() > b.get();
	}

	template <class T, class U>
	bool operator >= (intrusive_ptr<T> const& a, intrusive_ptr<U> const& b)
	{
		return a.get() >= b.get();
	}

	template <class T>
	bool operator == (intrusive_ptr<T> const& a, std::nullptr_t)
	{
		return a.get() == nullptr;
	}

	template <class T>
	bool operator == (std::nullptr_t, intrusive_ptr<T> const& b)
	{
		return nullptr == b.get();
	}

	template <class T>
	bool operator != (intrusive_ptr<T> const& a, std::nullptr_t)
	{
		return a.get() != nullptr;
	}

	template <class T>
	bool operator != (std::nullptr_t, intrusive_ptr<T> const& b)
	{
		return nullptr != b.get();
	}

	struct ref_count
	{
		unsigned long addref()
		{
			return ++count;
		}

		unsigned long release()
		{
			return --count;
		}

		unsigned long get() const
		{
			return count;
		}

	private:
		unsigned long count{ 0 };
	};

	struct ref_count_atomic
	{
		unsigned long addref()
		{
			return ++count;
		}

		unsigned long release()
		{
			return --count;
		}

		unsigned long get() const
		{
			return count.load();
		}

	private:
		std::atomic<unsigned long> count{ 0 };
	};

	template <class Class, class RefCount = ref_count>
	struct ref_counted
	{
		ref_counted() = default;

		ref_counted(ref_counted const&)
		{
		}

		ref_counted& operator = (ref_counted const&)
		{
			return *this;
		}

		void addref()
		{
			count.addref();
		}

		void release()
		{
			if (count.release() == 0) {
				delete static_cast<Class*>(this);
			}
		}

	protected:
		~ref_counted() = default;

	private:
		RefCount count{};
	};

}
