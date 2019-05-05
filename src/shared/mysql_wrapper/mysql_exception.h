// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include <stdexcept>
#include <typeinfo>

namespace mmo
{
	namespace mysql
	{
		struct Exception : std::runtime_error
		{
			explicit Exception(const std::string &message);
		};


		struct OutOfRangeException : Exception
		{
			OutOfRangeException();
		};


		struct InvalidTypeException : Exception
		{
			template <class Expected>
			explicit InvalidTypeException(const Expected &)
				: Exception(std::string("Type expected: ") + typeid(Expected).name())
			{
			}
		};


		struct QueryFailException : Exception
		{
			explicit QueryFailException(const std::string &message);
		};


		struct StatementException : Exception
		{
			explicit StatementException(const std::string &message);
		};
	}
}
