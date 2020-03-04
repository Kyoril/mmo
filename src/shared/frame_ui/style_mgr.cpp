// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "style_mgr.h"

#include "base/macros.h"


namespace mmo
{
	static StyleManager &s_styleMgr = StyleManager::Get();


	StyleManager& StyleManager::Get()
	{
		static StyleManager instance;
		return instance;
	}

	StylePtr StyleManager::Create(const std::string & name)
	{
		if (m_stylesByName.find(name) != m_stylesByName.end())
		{
			return nullptr;
		}
		
		// Create the style instance
		auto style = std::make_shared<Style>(name);
		m_stylesByName[name] = style;

		return style;
	}

	StylePtr StyleManager::Find(const std::string & name)
	{
		const auto it = m_stylesByName.find(name);
		if (it == m_stylesByName.end())
			return nullptr;

		return it->second;
	}

	void StyleManager::Destroy(const std::string & name)
	{
		const auto it = m_stylesByName.find(name);
		ASSERT(it != m_stylesByName.end());

		if (it != m_stylesByName.end())
		{
			m_stylesByName.erase(it);
		}
	}
}
