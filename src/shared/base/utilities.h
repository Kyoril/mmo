
#pragma once

#include <string>
#include <cstdio>
#include <algorithm>

namespace mmo
{
	/// A custom compare operator used to make string keys in a hash container case insensitive.
	struct StrCaseIComp
	{
		bool operator() (const std::string& lhs, const std::string& rhs) const {
			return _stricmp(lhs.c_str(), rhs.c_str()) < 0;
		}
	};

	static inline size_t FindCaseInsensitive(std::string data, std::string toSearch, size_t pos = 0)
	{
		// Convert complete given String to lower case
		std::transform(data.begin(), data.end(), data.begin(), ::tolower);
		// Convert complete given Sub String to lower case
		std::transform(toSearch.begin(), toSearch.end(), toSearch.begin(), ::tolower);
		// Find sub string in given string
		return data.find(toSearch, pos);
	}

	static inline size_t RFindCaseInsensitive(std::string data, std::string toSearch, size_t pos = std::string::npos)
	{
		// Convert complete given String to lower case
		std::transform(data.begin(), data.end(), data.begin(), ::tolower);
		// Convert complete given Sub String to lower case
		std::transform(toSearch.begin(), toSearch.end(), toSearch.begin(), ::tolower);
		// Find sub string in given string
		return data.rfind(toSearch, pos);
	}

	static inline bool StringEndsWith(const std::string& data, const std::string& compare)
	{
		return data.rfind(compare, std::string::npos) == data.size() - compare.size();
	}

	static inline bool StringEndsWithCaseInsensitive(const std::string& data, const std::string& compare)
	{
		return RFindCaseInsensitive(data, compare) == data.size() - compare.size();
	}

	static inline std::string GetFileExtension(const std::string& data)
	{
		size_t point = data.find_last_of('.');
		if (point != data.npos)
		{
			return data.substr(point);
		}

		return std::string();
	}
}