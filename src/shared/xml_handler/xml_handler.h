// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <string>


namespace mmo
{
	class XmlAttributes;

	/// This is the base class for an xml handler.
	class XmlHandler
	{
	public:
		/// Virtual default destructor because of inheritance.
		virtual ~XmlHandler() = default;

	public:
		/// Method called to notify the handler at the start of each xml element encountered.
		virtual void ElementStart(const std::string& element, const XmlAttributes& attributes) {}
		/// Method called to notify the handler at the end of each xml element encountered.
		virtual void ElementEnd(const std::string& element) {}
		/// Method called to notify text node, several successive text nodes are aggregated.
		virtual void Text(const std::string& text) {}
	};
}