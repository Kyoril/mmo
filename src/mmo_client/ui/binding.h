#pragma once

#include "base/typedefs.h"

#include "luabind/luabind.hpp"

namespace mmo
{
	struct Binding
	{
		String name;
		String description;
		String category;
		luabind::object script;
	};

	class Bindings
	{
	public:
		void Load(const String& bindingsFile);

	private:
		std::vector<Binding> m_bindings;
	};
}