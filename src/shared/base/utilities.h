// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include <string>
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <vector>
#include <ctime>
#include <cctype>

#include "random.h"
#ifndef _MSC_VER
#	include <strings.h>
#endif

#ifndef _MSC_VER
#	define _stricmp(l,r) strcasecmp(l, r)
#endif

namespace mmo
{
	static RandomnessGenerator randomGenerator(time(nullptr));

	/// A custom compare operator used to make string keys in a hash container case insensitive.
	struct StrCaseIComp
	{
		bool operator() (const std::string& lhs, const std::string& rhs) const {
			return _stricmp(lhs.c_str(), rhs.c_str()) < 0;
		}
	};

	// Function to trim whitespace from both ends of a string
	static inline std::string Trim(const std::string& str)
	{
		// Find the first non-whitespace character
		auto start = std::find_if_not(str.begin(), str.end(), [](unsigned char ch) {
			return std::isspace(ch);
			});

		// Find the last non-whitespace character
		auto end = std::find_if_not(str.rbegin(), str.rend(), [](unsigned char ch) {
			return std::isspace(ch);
			}).base();

		// If there's no content, return an empty string
		if (start >= end) {
			return "";
		}

		return std::string(start, end);
	}

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

	template< typename T, size_t N >
	size_t countof(const T(&)[N]) { return N; }

	static void TokenizeString(std::string str, std::vector<std::string>& out_tokens)
	{
		std::string token;

		for (size_t i = 0; i < str.length(); i++) 
		{
			char c = str[i];

			if (c == ' ' || c == '\t') 
			{
				out_tokens.emplace_back(std::move(token));
			}
			else if (c == '\"') 
			{
				i++;

				while (str[i] != '\"') { token.push_back(str[i]); i++; }
			}
			else 
			{
				token.push_back(str[i]);
			}
		}

		if (!token.empty())
		{
			out_tokens.emplace_back(std::move(token));
		}
	}
	
	namespace detail
	{
		template<class T>
		struct WritableHexDigit
		{
			const T& digit;

			explicit WritableHexDigit(const T& in_digit)
				: digit(in_digit)
			{
			}
		};
		
		template<class T>
		std::ostream &operator <<(std::ostream &stream, const WritableHexDigit<T> &digit)
		{
			return stream
				<< "0x" 
				<< std::hex << std::setfill('0') << std::setw(sizeof(digit.digit) * 2)
				<< digit.digit;
		}
	}
	
	template<typename T>
	static detail::WritableHexDigit<T> log_hex_digit(const T& digit)
	{
		return detail::WritableHexDigit<T>(digit);
	}
}