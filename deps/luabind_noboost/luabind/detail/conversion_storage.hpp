// Copyright Daniel Wallin 2008. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef LUABIND_CONVERSION_STORAGE_080930_HPP
# define LUABIND_CONVERSION_STORAGE_080930_HPP

# include <luabind/config.hpp>
# include <type_traits>
#include <cstddef>

namespace luabind {
	namespace detail {

		using destruction_function = void(*)(void*);

		// This is used by the converters in policy.hpp, and
		// class_rep::convert_to as temporary storage when constructing
		// holders.

		struct conversion_storage
		{
			conversion_storage()
				: destructor(0)
			{}

			~conversion_storage()
			{
				if(destructor)
					destructor(&data);
			}

			// Unfortunately the converters currently doesn't have access to
			// the actual type being converted when this is instantiated, so
			// we have to guess a max size.
			alignas(alignof(std::max_align_t)) std::byte data[128];
			destruction_function destructor;
		};

	}
} // namespace luabind::detail

#endif // LUABIND_CONVERSION_STORAGE_080930_HPP

