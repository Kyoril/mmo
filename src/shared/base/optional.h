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

// Note: Moved optional class out of signal.h so that it can be included without
// including the whole signal library stuff. Also moved it into the mmo namespace
// and changed assert references to custom ASSERT macro of this project.

#pragma once

#include "macros.h"
#include "error.h"

#include <utility>
#include <type_traits>


namespace mmo
{
#ifndef SIMPLE_NO_EXCEPTIONS
	struct bad_optional_access : error
	{
		const char* what() const throw() override
		{
			return "simplesig: Bad optional access. Return value of this signal is probably empty.";
		}
	};
#endif

	template <class T>
	struct optional
	{
		typedef T value_type;

		optional() = default;

		~optional()
		{
			if (engaged()) {
				disengage();
			}
		}

		template <class U>
		explicit optional(U&& val)
		{
			engage(std::forward<U>(val));
		}

		optional(optional const& opt)
		{
			if (opt.engaged()) {
				engage(*opt.object());
			}
		}

		optional(optional&& opt)
		{
			if (opt.engaged()) {
				engage(std::move(*opt.object()));
				opt.disengage();
			}
		}

		template <class U>
		explicit optional(optional<U> const& opt)
		{
			if (opt.engaged()) {
				engage(*opt.object());
			}
		}

		template <class U>
		explicit optional(optional<U>&& opt)
		{
			if (opt.engaged()) {
				engage(std::move(*opt.object()));
				opt.disengage();
			}
		}

		template <class U>
		optional& operator = (U&& rhs)
		{
			if (engaged()) {
				disengage();
			}
			engage(std::forward<U>(rhs));
			return *this;
		}

		optional& operator = (optional const& rhs)
		{
			if (this != &rhs) {
				if (engaged()) {
					disengage();
				}
				if (rhs.engaged()) {
					engage(*rhs.object());
				}
			}
			return *this;
		}

		template <class U>
		optional& operator = (optional<U> const& rhs)
		{
			if (this != &rhs) {
				if (engaged()) {
					disengage();
				}
				if (rhs.engaged()) {
					engage(*rhs.object());
				}
			}
			return *this;
		}

		optional& operator = (optional&& rhs)
		{
			if (engaged()) {
				disengage();
			}
			if (rhs.engaged()) {
				engage(std::move(*rhs.object()));
				rhs.disengage();
			}
			return *this;
		}

		template <class U>
		optional& operator = (optional<U>&& rhs)
		{
			if (engaged()) {
				disengage();
			}
			if (rhs.engaged()) {
				engage(std::move(*rhs.object()));
				rhs.disengage();
			}
			return *this;
		}

		bool engaged() const
		{
			return initialized;
		}

		explicit operator bool() const
		{
			return engaged();
		}

		value_type& operator * ()
		{
			return value();
		}

		value_type const& operator * () const
		{
			return value();
		}

		value_type* operator -> ()
		{
			return object();
		}

		value_type const* operator -> () const
		{
			return object();
		}

		value_type& value()
		{
	#ifndef SIMPLE_NO_EXCEPTIONS
			if (!engaged()) {
				throw bad_optional_access{};
			}
	#endif
			return *object();
		}

		value_type const& value() const
		{
	#ifndef SIMPLE_NO_EXCEPTIONS
			if (!engaged()) {
				throw bad_optional_access{};
			}
	#endif
			return *object();
		}

		template <class U>
		value_type value_or(U&& val) const
		{
			return engaged() ? *object() : value_type{ std::forward<U>(val) };
		}

		void swap(optional& other)
		{
			if (this != &other) {
				auto t{ std::move(*this) };
				*this = std::move(other);
				other = std::move(t);
			}
		}

	private:
		void* storage()
		{
			return static_cast<void*>(&buffer);
		}

		void const* storage() const
		{
			return static_cast<void const*>(&buffer);
		}

		value_type* object()
		{
			ASSERT(initialized == true);
			return static_cast<value_type*>(storage());
		}

		value_type const* object() const
		{
			ASSERT(initialized == true);
			return static_cast<value_type const*>(storage());
		}

		template <class U>
		void engage(U&& val)
		{
			ASSERT(initialized == false);
			new (storage()) value_type{ std::forward<U>(val) };
			initialized = true;
		}

		void disengage()
		{
			ASSERT(initialized == true);
			object()->~value_type();
			initialized = false;
		}

		bool initialized = false;
		std::aligned_storage_t<sizeof(value_type), std::alignment_of<value_type>::value> buffer;
	};
}
