// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/signal.h"

#include <string>


namespace mmo
{
	/// This class represents a custom property whose value can be set by xml.
	class Property
	{
	public:
		/// Fired when the property value was changed.
		signal<void(const Property&)> Changed;

	public:
		/// Initializes this property instance with a name and a default value.
		/// @param name The name of this property.
		/// @param defaultValue The default value of this property.
		explicit Property(std::string defaultValue = "");

	public:
		/// Sets the value of this property.
		void Set(std::string value);
		void Set(bool value);
		bool GetBoolValue() const;

	public:
		inline const std::string &GetDefaultValue() const { return m_defaultValue; }
		inline const std::string &GetValue() const { return m_value; }

	private:
		/// The default value of this property.
		std::string m_defaultValue;
		/// The current value of this property.
		std::string m_value;
	};
}
