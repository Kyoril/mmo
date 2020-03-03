// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "style_xml_loader.h"
#include "style_mgr.h"

#include "xml_handler/xml_attributes.h"
#include "log/default_log_levels.h"

namespace mmo
{
	static const std::string StyleElement("Style");
	static const std::string StyleNameAttribute("name");
	static const std::string ImagerySectionElement("ImagerySection");
	static const std::string ImagerySectionNameAttribute("name");
	static const std::string StateImageryElement("StateImagery");
	static const std::string StateImageryNameAttribute("name");
	static const std::string LayerElement("Layer");
	static const std::string SectionElement("Section");
	static const std::string SectionSectionAttribute("section");
	static const std::string SectionColorAttribute("color");


	void StyleXmlLoader::ElementStart(const std::string & element, const XmlAttributes & attributes)
	{
		if (element == StyleElement)
		{
			ElementStyleStart(attributes);
		}
		else if (element == ImagerySectionElement)
		{
			ElementImagerySectionStart(attributes);
		}
		else if (element == StateImageryElement)
		{
			ElementImageryStart(attributes);
		}
		else if (element == LayerElement)
		{
			ElementLayerStart(attributes);
		}
		else if (element == SectionElement)
		{
			ElementSectionStart(attributes);
		}
		else
		{
			ELOG("Unknown element found while parsing the ui-style file: '" << element << "'");
		}
	}

	void StyleXmlLoader::ElementEnd(const std::string & element)
	{
		if (element == StyleElement)
		{
			ElementStyleEnd();
		}
		else if (element == ImagerySectionElement)
		{
			ElementImagerySectionEnd();
		}
		else if (element == StateImageryElement)
		{
			ElementImageryEnd();
		}
		else if (element == LayerElement)
		{
			ElementLayerEnd();
		}
		else if (element == SectionElement)
		{
			ElementSectionEnd();
		}
	}

	void StyleXmlLoader::Text(const std::string & text)
	{
	}

	void StyleXmlLoader::ElementStyleStart(const XmlAttributes & attributes)
	{
		// Style has to be a top-level element
		if (m_style != nullptr)
		{
			throw std::runtime_error("Nested styles are not supported!");
		}

		// Obtain style name
		std::string name(attributes.GetValueAsString(StyleNameAttribute));
		if (name.empty())
		{
			throw std::runtime_error("Style need to have a valid name!");
		}

		// Try to create the given style by name
		StylePtr style = StyleManager::Get().Create(name);
		if (!style)
		{
			// The only reason for the Create method to fail is that the style already exists
			throw std::runtime_error("A style named '" + name + "' already exists!");
		}

		// Use this style element now
		m_style = std::move(style);
	}

	void StyleXmlLoader::ElementStyleEnd()
	{
		// No longer use a style element
		ASSERT(m_style != nullptr);
		m_style.reset();
	}

	void StyleXmlLoader::ElementImagerySectionStart(const XmlAttributes & attributes)
	{
		// Ensure that the element may appear at this location
		if (m_style == nullptr || m_section != nullptr || m_stateImagery != nullptr)
		{
			throw std::runtime_error("Unexpected ImagerySection element!");
		}

		// Parse attributes
		const std::string name(attributes.GetValueAsString(ImagerySectionNameAttribute));
		if (name.empty())
		{
			throw std::runtime_error("ImagerySection element has to have a valid name!");
		}

		// Ensure that such a section doesn't already exist
		if (m_style->GetImagerySectionByName(name) != nullptr)
		{
			throw std::runtime_error("ImagerySection with the name '" + name + "' already exists in style '" + m_style->GetName() + "'!");
		}

		// Add new imagery section
		m_section = std::make_shared<StateImagerySection>(name);
		m_style->AddImagerySection(m_section);
	}

	void StyleXmlLoader::ElementImagerySectionEnd()
	{
		// Reset ImagerySection element
		m_section.reset();
	}

	void StyleXmlLoader::ElementImageryStart(const XmlAttributes & attributes)
	{
		// Ensure that the element may appear at this location
		if (m_style == nullptr || m_section != nullptr || m_stateImagery != nullptr)
		{
			throw std::runtime_error("Unexpected StateImagery element!");
		}

		// Parse attributes
		const std::string name(attributes.GetValueAsString(StateImageryNameAttribute));
		if (name.empty())
		{
			throw std::runtime_error("StateImagery element has to have a valid name!");
		}

		// Ensure that such a state imagery doesn't already exist
		if (m_style->GetStateImageryByName(name) != nullptr)
		{
			throw std::runtime_error("StateImagery with the name '" + name + "' already exists in style '" + m_style->GetName() + "'!");
		}

		// Add new imagery section
		m_stateImagery = std::make_shared<StateImagery>(name);
		m_style->AddStateImagery(m_stateImagery);
	}

	void StyleXmlLoader::ElementImageryEnd()
	{
		// Reset current state imagery object
		m_stateImagery.reset();
	}

	void StyleXmlLoader::ElementLayerStart(const XmlAttributes & attributes)
	{
		// TODO: Handle Layer element start
	}

	void StyleXmlLoader::ElementLayerEnd()
	{
		// TODO: Handle Layer element end
	}

	void StyleXmlLoader::ElementSectionStart(const XmlAttributes & attributes)
	{
		// TODO: Handle section element start
	}

	void StyleXmlLoader::ElementSectionEnd()
	{
		// TODO: Handle section element end
	}
}
