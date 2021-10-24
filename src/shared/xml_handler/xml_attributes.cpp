// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "xml_attributes.h"

#include "base/macros.h"

#include <sstream>
#include <iterator>
#include <stdexcept>


namespace mmo
{
	void XmlAttributes::Add(const std::string & attrName, const std::string & attrValue)
	{
		m_attrs[attrName] = attrValue;
	}

	void XmlAttributes::Remove(const std::string & attrName)
	{
		auto pos = m_attrs.find(attrName);
		if (pos != m_attrs.end())
		{
			m_attrs.erase(pos);
		}
	}

	bool XmlAttributes::Exists(const std::string & attrName) const
	{
		return m_attrs.find(attrName) != m_attrs.end();
	}

	size_t XmlAttributes::GetCount() const
	{
		return m_attrs.size();
	}

	const std::string & XmlAttributes::GetName(size_t index) const
	{
		auto iter = m_attrs.cbegin();
		std::advance(iter, index);

		return (*iter).first;
	}

	const std::string & XmlAttributes::GetValue(size_t index) const
	{
		auto iter = m_attrs.cbegin();
		std::advance(iter, index);

		return (*iter).second;
	}

	const std::string & XmlAttributes::GetValue(const std::string & attrName) const
	{
		auto pos = m_attrs.find(attrName);
		if (pos != m_attrs.end())
		{
			return (*pos).second;
		}

		throw std::runtime_error("No value exists for an attribute named " + attrName + ".");
	}

	const std::string & XmlAttributes::GetValueAsString(const std::string & attrName, const std::string & def) const
	{
		return (Exists(attrName)) ? GetValue(attrName) : def;
	}

	bool XmlAttributes::GetValueAsBool(const std::string & attrName, bool def) const
	{
		if (!Exists(attrName))
		{
			return def;
		}

		const std::string& val = GetValue(attrName);
		if (val == "false" || val == "0")
		{
			return false;
		}
		else if (val == "true" || val == "1")
		{
			return true;
		}

		return def;
	}

	int XmlAttributes::GetValueAsInt(const std::string & attrName, int def) const
	{
		if (!Exists(attrName))
		{
			return def;
		}

		int val;
		std::istringstream strm(GetValue(attrName).c_str());
		strm >> val;

		// success?
		ASSERT(!strm.fail());

		return val;
	}

	float XmlAttributes::GetValueAsFloat(const std::string & attrName, float def) const
	{
		if (!Exists(attrName))
		{
			return def;
		}

		float val;
		std::istringstream strm(GetValue(attrName).c_str());

		strm >> val;

		// success?
		ASSERT(!strm.fail());

		return val;
	}
}
