// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "asset_file_name.h"

namespace mmo
{
	String AssetFileNameError(const String& name)
	{
		if (name.empty())
		{
			return "The name is empty.";
		}
		if (name.find("..") != String::npos)
		{
			return "The name must not contain \"..\".";
		}
		for (const char c : name)
		{
			if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|'
				|| static_cast<unsigned char>(c) < 32)
			{
				return "The name must not contain path separators or any of : * ? \" < > |";
			}
		}
		if (name.back() == '.' || name.back() == ' ')
		{
			return "The name must not end with a dot or a space.";
		}
		return {};
	}
}
