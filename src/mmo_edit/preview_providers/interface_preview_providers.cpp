// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "interface_preview_providers.h"

namespace mmo
{
	LuaPreviewProvider::LuaPreviewProvider()
		: StaticTexturePreviewProvider("Editor/Lua.htex")
	{
		// The base class constructor will handle loading the texture
	}

	const std::set<String>& LuaPreviewProvider::GetSupportedExtensions() const
	{
		static std::set<String> extensions { ".lua" };
		return extensions;
	}

	XmlPreviewProvider::XmlPreviewProvider()
		: StaticTexturePreviewProvider("Editor/Xml.htex")
	{
		// The base class constructor will handle loading the texture
	}

	const std::set<String>& XmlPreviewProvider::GetSupportedExtensions() const
	{
		static std::set<String> extensions{ ".xml" };
		return extensions;
	}
}