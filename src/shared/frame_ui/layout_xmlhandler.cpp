// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "layout_xmlhandler.h"

#include "log/default_log_levels.h"

namespace mmo
{
	static const std::string UILayoutElement("UILayout");
	static const std::string FrameElement("Frame");
	static const std::string FrameTypeAttribute("Type");
	static const std::string FrameNameAttribute("Name");
	static const std::string AutoFrameNameSuffixAttribute("NameSuffix");
	static const std::string LayoutParentAttribute("Parent");
	static const std::string LayoutImportFilenameAttribute("Filename");
	static const std::string LayoutImportPrefixAttribute("Prefix");
	static const std::string EventNameAttribute("Name");
	static const std::string EventFunctionAttribute("Function");

	void LayoutXmlHandler::ElementStart(const std::string & element, const XmlAttributes & attributes)
	{
		if (element == UILayoutElement)
		{
			ElementUILayoutStart(attributes);
		}
		else if (element == FrameElement)
		{
			ElementFrameStart(attributes);
		}
		else
		{
			WLOG("Unexpected data was found while parsing the ui-layout file: '" << element << "' is unknown.");
		}
	}

	void LayoutXmlHandler::ElementEnd(const std::string & element) 
	{
		if (element == UILayoutElement)
		{
			ElementUILayoutEnd();
		}
		else if (element == FrameElement)
		{
			ElementWindowEnd();
		}
	}

	void LayoutXmlHandler::Text(const std::string & text) 
	{
	}

	void LayoutXmlHandler::ElementUILayoutStart(const XmlAttributes & attributes)
	{
	}

	void LayoutXmlHandler::ElementFrameStart(const XmlAttributes & attributes)
	{
	}

	void LayoutXmlHandler::ElementAutoFrameStart(const XmlAttributes & attributes)
	{
	}

	void LayoutXmlHandler::ElementUILayoutEnd()
	{
	}

	void LayoutXmlHandler::ElementWindowEnd()
	{
	}
}
