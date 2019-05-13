
#pragma once

#include <string>
#include <cstdio>

namespace mmo
{
	/// A custom compare operator used to make string keys in a hash container case insensitive.
	struct StrCaseIComp
	{
		bool operator() (const std::string& lhs, const std::string& rhs) const {
			return _stricmp(lhs.c_str(), rhs.c_str()) < 0;
		}
	};
}