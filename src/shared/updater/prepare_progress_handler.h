#pragma once

#include <string>

namespace mmo
{
	namespace updating
	{
		struct IPrepareProgressHandler
		{
			virtual ~IPrepareProgressHandler();
			virtual void beginCheckLocalCopy(const std::string &name) = 0;
		};
	}
}
