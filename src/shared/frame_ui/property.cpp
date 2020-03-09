// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "property.h"


namespace mmo
{
	Property::Property(std::string name, std::string defaultValue)
		: m_name(std::move(name))
		, m_defaultValue(std::move(defaultValue))
	{
		m_value = m_defaultValue;
	}

	void Property::Set(std::string value)
	{
		if (m_value != value)
		{
			m_value = std::move(value);
			Changed();
		}
	}
}

