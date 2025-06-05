#pragma once

#include "deps/lua/lua.hpp"
#include "luabind/luabind.hpp"
#include "luabind/iterator_policy.hpp"
#include "luabind/out_value_policy.hpp"

#include <functional>
#include <type_traits>

namespace luabind
{
	template<typename Fn>
	scope def_lambda(const char* name, Fn&& fn)
	{
		// Deduce the function signature and forward to luabind::def
		// Wrap in std::function so luabind sees the signature

		using FnType = typename std::remove_reference<Fn>::type;
		using StdFuncType = decltype(std::function(FnType(fn)));
		return luabind::def<StdFuncType>(name, std::forward<Fn>(fn));
	}
}
