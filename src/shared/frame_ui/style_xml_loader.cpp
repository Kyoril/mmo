// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "style_xml_loader.h"
#include "style_mgr.h"
#include "text_component.h"
#include "image_component.h"
#include "border_component.h"
#include "state_imagery.h"
#include "imagery_section.h"

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
	static const std::string TextComponentElement("TextComponent");
	static const std::string TextComponentColorAttribute("color");
	static const std::string TextComponentFontAttribute("font");
	static const std::string TextComponentSizeAttribute("size");
	static const std::string TextComponentHorzAlignAttribute("horzAlign");
	static const std::string TextComponentVertAlignAttribute("vertAlign");
	static const std::string TextComponentOutlineAttribute("outline");
	static const std::string ImageComponentElement("ImageComponent");
	static const std::string ImageComponentTextureAttribute("texture");
	static const std::string BorderComponentElement("BorderComponent");
	static const std::string AreaElement("Area");


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
		else if (element == TextComponentElement)
		{
			ElementTextComponentStart(attributes);
		}
		else if (element == ImageComponentElement)
		{
			ElementImageComponentStart(attributes);
		}
		else if (element == BorderComponentElement)
		{
			ElementBorderComponentStart(attributes);
		}
		else if (element == AreaElement)
		{
			ElementAreaStart(attributes);
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
		else if (element == TextComponentElement)
		{
			ElementTextComponentEnd();
		}
		else if (element == ImageComponentElement)
		{
			ElementImageComponentEnd();
		}
		else if (element == BorderComponentElement)
		{
			ElementBorderComponentEnd();
		}
		else if (element == AreaElement)
		{
			ElementAreaEnd();
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
		m_section = std::make_shared<ImagerySection>(name);
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
		// Ensure that the element may appear at this location
		if (m_layer != nullptr || m_stateImagery == nullptr)
		{
			throw std::runtime_error("Unexpected Layer element!");
		}

		// Add a new layer to the state imagery
		m_layer = std::make_shared<FrameLayer>();
		m_stateImagery->AddLayer(m_layer);
	}

	void StyleXmlLoader::ElementLayerEnd()
	{
		// Reset the current layer element
		m_layer.reset();
	}

	void StyleXmlLoader::ElementSectionStart(const XmlAttributes & attributes)
	{
		// Ensure that the element may appear at this location
		if (m_layer == nullptr)
		{
			throw std::runtime_error("Unexpected Section element!");
		}

		// Get the section name attribute
		const std::string section(attributes.GetValueAsString(SectionSectionAttribute));
		if (section.empty())
		{
			throw std::runtime_error("Section element needs to have a section name specified!");
		}

		// Find section by name
		const ImagerySection* sectionEntry = m_style->GetImagerySectionByName(section);
		if (sectionEntry == nullptr)
		{
			throw std::runtime_error("Unable to find section named '" + section + "' in style '" + m_style->GetName() + "'!");
		}

		// Add section entry to layer
		m_layer->AddSection(*sectionEntry);
	}

	void StyleXmlLoader::ElementSectionEnd()
	{
		// Nothing to do here yet
	}

	void StyleXmlLoader::ElementTextComponentStart(const XmlAttributes & attributes)
	{
		if (m_component != nullptr || m_section == nullptr)
		{
			throw std::runtime_error("Unexpected TextComponent element!");
		}

		const std::string font(attributes.GetValueAsString(TextComponentFontAttribute));
		const int size = attributes.GetValueAsInt(TextComponentSizeAttribute);
		const float outline = attributes.GetValueAsFloat(TextComponentOutlineAttribute);
		const std::string horzAlignAttr(attributes.GetValueAsString(TextComponentHorzAlignAttribute));
		const std::string vertAlignAttr(attributes.GetValueAsString(TextComponentVertAlignAttribute));

		// Check for font name existance
		if (font.empty())
		{
			throw std::runtime_error("TextComponent needs a font name!");
		}

		// Setup component and add it to the current section
		auto component = std::make_shared<TextComponent>(font, size, outline);
		component->SetHorizontalAlignment(HorizontalAlignmentByName(horzAlignAttr));
		component->SetVerticalAlignment(VerticalAlignmentByName(vertAlignAttr));
		m_component = component;
		m_section->AddComponent(m_component);
	}

	void StyleXmlLoader::ElementTextComponentEnd()
	{
		m_component.reset();
	}

	void StyleXmlLoader::ElementImageComponentStart(const XmlAttributes & attributes)
	{
		if (m_component != nullptr || m_section == nullptr)
		{
			throw std::runtime_error("Unexpected ImageComponent element!");
		}

		const std::string texture(attributes.GetValueAsString(ImageComponentTextureAttribute));

		// Check for texture name existance
		if (texture.empty())
		{
			throw std::runtime_error("ImageComponent needs a texture filename!");
		}

		// Setup component and add it to the current section
		m_component = std::make_shared<ImageComponent>(texture);
		m_section->AddComponent(m_component);
	}

	void StyleXmlLoader::ElementImageComponentEnd()
	{
		m_component.reset();
	}

	void StyleXmlLoader::ElementBorderComponentStart(const XmlAttributes & attributes)
	{
		if (m_component != nullptr || m_section == nullptr)
		{
			throw std::runtime_error("Unexpected BorderComponent element!");
		}

		const std::string texture(attributes.GetValueAsString(ImageComponentTextureAttribute));

		// Check for texture name existance
		if (texture.empty())
		{
			throw std::runtime_error("BorderComponent needs a texture filename!");
		}

		// Setup component and add it to the current section
		m_component = std::make_shared<BorderComponent>(texture, 22.0f);
		m_section->AddComponent(m_component);
	}

	void StyleXmlLoader::ElementBorderComponentEnd()
	{
		m_component.reset();
	}

	void StyleXmlLoader::ElementAreaStart(const XmlAttributes & attributes)
	{
		if (m_component == nullptr)
		{
			throw std::runtime_error("Unexpected Area element!");
		}

		// TODO: Handle method
	}

	void StyleXmlLoader::ElementAreaEnd()
	{
		// TODO: Handle method
	}
}
