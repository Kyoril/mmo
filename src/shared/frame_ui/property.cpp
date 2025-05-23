// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "property.h"

#include "base/utilities.h"


namespace mmo
{
	Property::Property(std::string defaultValue)
		: m_defaultValue(std::move(defaultValue))
	{
		m_value = m_defaultValue;
	}

	void Property::Set(std::string value)
	{
		if (m_value != value)
		{
			m_value = std::move(value);
			Changed(*this);
		}
	}

	void Property::Set(bool value)
	{
		Set(std::string(value ? "true" : "false"));
	}

	bool Property::GetBoolValue() const
	{
        if (_stricmp(m_value.c_str(), "true") == 0)
		{
			return true;
		}

		return false;
	}
}

