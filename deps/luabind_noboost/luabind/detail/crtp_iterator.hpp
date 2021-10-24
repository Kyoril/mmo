﻿// Changes by Robin Klimonow (Kyoril):
//  No longer inherits from std::iterator as this behavior is deprecated in C++17.
//	Instead, those classes should typedef the 5 types iterator_category, value_type,
//	difference_Type, pointer and reference manually.

#ifndef LUABIND_CRTP_ITERATOR_HPP_INCLUDED
#define LUABIND_CRTP_ITERATOR_HPP_INCLUDED

#include <iterator>

namespace luabind {
	namespace detail {

		template< typename CRTP, typename Category, typename ValueType, typename ReferenceType = ValueType&, typename DifferenceType = ptrdiff_t >
		class crtp_iterator 
		{
		public:
			using iterator_category = Category;
			using value_type = ValueType;
			using difference_type = DifferenceType;
			using pointer = ValueType*;
			using reference = ReferenceType;

		public:


			CRTP& operator++()
			{
				upcast().increment();
				return upcast();
			}

			CRTP operator++(int)
			{
				CRTP tmp(upcast());
				upcast().increment();
				return tmp;
			}

			bool operator==(const CRTP& rhs)
			{
				return upcast().equal(rhs);
			}

			bool operator!=(const CRTP& rhs)
			{
				return !upcast().equal(rhs);
			}

			reference operator*()
			{
				return upcast().dereference();
			}

			reference operator->()
			{
				return upcast().dereference();
			}

		private:
			CRTP& upcast() { return static_cast<CRTP&>(*this); }
			const CRTP& upcast() const { return static_cast<const CRTP&>(*this); }
		};

	}
}

#endif
