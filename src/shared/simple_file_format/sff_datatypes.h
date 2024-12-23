// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>
#include <array>

namespace sff
{
	enum DataType
	{
		TypeInteger,
		TypeString,
		TypeArray,
		TypeTable
	};

	enum
	{
		CountOfTypes = 4
	};


	enum FileEncoding
	{
		UTF8,
		UTF8_BOM,
		UTF8_guess
	};


	static const std::string &getFileEncodingName(FileEncoding encoding)
	{
		static const std::array<std::string, 3> Names =
		{{
				"UTF-8",
				"UTF-8 with BOM",
				"UTF-8 with or without BOM"
			}
		};
		return Names[encoding];
	}
}
