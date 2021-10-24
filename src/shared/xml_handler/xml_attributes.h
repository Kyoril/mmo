// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include <string>
#include <map>


namespace mmo
{
	/// 
	class XmlAttributes
	{
	public:

		void Add(const std::string& attrName, const std::string& attrValue);

		void Remove(const std::string& attrName);

		bool Exists(const std::string& attrName) const;

		size_t GetCount() const;

		const std::string& GetName(size_t index) const;

		const std::string& GetValue(size_t index) const;

		const std::string& GetValue(const std::string& attrName) const;

		const std::string& GetValueAsString(const std::string& attrName, const std::string& def = "") const;

		bool GetValueAsBool(const std::string& attrName, bool def = false) const;

		int GetValueAsInt(const std::string& attrName, int def = 0) const;

		float GetValueAsFloat(const std::string& attrName, float def = 0.0f) const;

	protected:

		typedef std::map<std::string, std::string> AttributeMap;
		AttributeMap m_attrs;
	};
}
