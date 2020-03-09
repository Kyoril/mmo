// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "layout_xml_loader.h"
#include "frame_mgr.h"
#include "frame.h"
#include "state_imagery.h"
#include "imagery_section.h"
#include "image_component.h"
#include "border_component.h"
#include "text_component.h"

#include "xml_handler/xml_attributes.h"
#include "log/default_log_levels.h"

namespace mmo
{
	static const std::string UILayoutElement("UILayout");
	static const std::string FrameElement("Frame");
	static const std::string FrameNameAttribute("name");
	static const std::string FrameTypeAttribute("type");
	static const std::string FrameRendererAttribute("renderer");
	static const std::string FrameTextAttribute("text");
	static const std::string FrameParentAttribute("parent");
	static const std::string FrameHiddenAttribute("hidden");
	static const std::string FrameEnabledAttribute("enabled");
	static const std::string FrameInheritsAttribute("inherits");
	static const std::string AreaElement("Area");
	static const std::string SizeElement("Size");
	static const std::string PositionElement("Position");
	static const std::string AbsDimensionElement("AbsDimension");
	static const std::string AbsDimensionXAttribute("x");
	static const std::string AbsDimensionYAttribute("y");
	static const std::string AnchorElement("Anchor");
	static const std::string AnchorPointAttribute("point");
	static const std::string AnchorRelativePointAttribute("relativePoint");
	static const std::string AnchorRelativeToAttribute("relativeTo");
	static const std::string ScriptElement("Script");
	static const std::string ScriptFileAttribute("file");
	static const std::string EventNameAttribute("name");
	static const std::string EventFunctionAttribute("function");

	static const std::string PropertyElement("Property");
	static const std::string PropertyNameAttribute("name");
	static const std::string PropertyDefaultValueAttribute("defaultValue");


	static const std::string VisualElement("Visual");
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
	static const std::string ImageComponentTilingAttribute("tiling");
	static const std::string BorderComponentElement("BorderComponent");
	static const std::string BorderComponentBorderSizeAttribute("borderSize");


	void LayoutXmlLoader::ElementStart(const std::string & element, const XmlAttributes & attributes)
	{
		if (element == FrameElement)
		{
			ElementFrameStart(attributes);
		}
		else if (element == AreaElement)
		{
			ElementAreaStart(attributes);
		}
		else if (element == SizeElement)
		{
			ElementSizeStart(attributes);
		}
		else if (element == PositionElement)
		{
			ElementPositionStart(attributes);
		}
		else if (element == AbsDimensionElement)
		{
			ElementAbsDimensionStart(attributes);
		}
		else if (element == AnchorElement)
		{
			ElementAnchorStart(attributes);
		}
		else if (element == ScriptElement)
		{
			ElementScriptStart(attributes);
		}
		if (element == VisualElement)
		{
			ElementVisualStart(attributes);
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
		else if (element == PropertyElement)
		{
			ElementPropertyStart(attributes);
		}
		else
		{
			WLOG("Unknown element found while parsing the ui-layout file: '" << element << "'");
		}
	}

	void LayoutXmlLoader::ElementEnd(const std::string & element) 
	{
		if (element == FrameElement)
		{
			ElementFrameEnd();
		}
		else if (element == AreaElement)
		{
			ElementAreaEnd();
		}
		else if (element == SizeElement)
		{
			ElementSizeEnd();
		}
		else if (element == PositionElement)
		{
			ElementPositionEnd();
		}
		else if (element == AbsDimensionElement)
		{
			ElementAbsDimensionEnd();
		}
		else if (element == AnchorElement)
		{
			ElementAnchorEnd();
		}
		else if (element == ScriptElement)
		{
			ElementScriptEnd();
		}
		if (element == VisualElement)
		{
			ElementVisualEnd();
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
		else if (element == PropertyElement)
		{
			ElementPropertyEnd();
		}
	}

	void LayoutXmlLoader::Text(const std::string & text) 
	{
	}

	void LayoutXmlLoader::ElementFrameStart(const XmlAttributes & attributes)
	{
		if (m_hasAreaTag)
		{
			throw std::runtime_error("Unexpected Frame element!");
		}

		// Get the name of the frame to create
		const std::string name(attributes.GetValueAsString(FrameNameAttribute));
		const std::string parent(attributes.GetValueAsString(FrameParentAttribute));
		const std::string text(attributes.GetValueAsString(FrameTextAttribute));
		const std::string renderer(attributes.GetValueAsString(FrameRendererAttribute));
		const bool hidden = attributes.GetValueAsBool(FrameHiddenAttribute, false);
		const bool enabled = attributes.GetValueAsBool(FrameEnabledAttribute, true);

		// Frame type might be overridden
		std::string type(attributes.GetValueAsString(FrameTypeAttribute, "Frame"));

		// Contains the inherited frame (if any provided)
		FramePtr templateFrame;

		// First, duplicate template frame if there is any. We do this to allow
		// overriding other frame properties by this frame definition.
		if (attributes.Exists(FrameInheritsAttribute))
		{
			const std::string inherits(attributes.GetValueAsString(FrameInheritsAttribute));

			// Find template frame
			templateFrame = FrameManager::Get().Find(inherits);
			if (templateFrame == nullptr)
			{
				throw std::runtime_error("Unable to find template frame '" + inherits + "'");
			}

			// Override frame type to be used
			type = templateFrame->GetType();
		}

		// Attempt to create the frame
		FramePtr frame = FrameManager::Get().Create(type, name);
		if (!frame)
		{
			throw std::runtime_error("Could not create frame named!");
		}

		// Copy properties over
		if (templateFrame)
		{
			templateFrame->Copy(*frame);
		}

		// Setup frame properties
		frame->SetVisible(!hidden);
		frame->SetEnabled(enabled);

		// Setup style and renderer
		if (!renderer.empty()) frame->SetRenderer(renderer);

		// Set frame text if there is any
		if (!text.empty())
		{
			frame->SetText(text);
		}

		// Check if a parent attribute has been set
		if (!parent.empty())
		{
			FramePtr parentFrame = FrameManager::Get().Find(parent);
			if (parentFrame == nullptr)
			{
				throw std::runtime_error("Parent frame named " + parent + " doesn't exist!");
			}

			// Add this frame to the parent frame
			parentFrame->AddChild(frame);
		}
		else
		{
			// No parent property set - check if we already have a frame on the stack, and if so, add
			// the current frame as a child frame to the frame on top of the stack
			if (!m_frames.empty())
			{
				m_frames.top()->AddChild(frame);
			}
		}

		// Push it to the stack of frames
		m_frames.push(frame);
	}

	void LayoutXmlLoader::ElementFrameEnd()
	{
		// Remove the last processes frame from the stack and ensure that
		// there is one
		ASSERT(!m_frames.empty());
		m_frames.pop();
	}

	void LayoutXmlLoader::ElementAreaStart(const XmlAttributes & attributes)
	{
		if (m_hasAreaTag || m_frames.empty())
		{
			throw std::runtime_error("Unexpected Area element!");
		}

		m_hasAreaTag = true;
	}

	void LayoutXmlLoader::ElementAreaEnd()
	{
		m_hasAreaTag = false;
	}

	void LayoutXmlLoader::ElementSizeStart(const XmlAttributes & attributes)
	{
		if (m_hasSizeTag || m_hasPositionTag || !m_hasAreaTag)
		{
			throw std::runtime_error("Unexpected Size element!");
		}

		m_hasSizeTag = true;
	}

	void LayoutXmlLoader::ElementSizeEnd()
	{
		m_hasSizeTag = false;
	}

	void LayoutXmlLoader::ElementPositionStart(const XmlAttributes & attributes)
	{
		if (m_hasPositionTag || m_hasSizeTag || !m_hasAreaTag)
		{
			throw std::runtime_error("Unexpected Position element!");
		}

		m_hasPositionTag = true;
	}

	void LayoutXmlLoader::ElementPositionEnd()
	{
		m_hasPositionTag = false;
	}

	void LayoutXmlLoader::ElementAbsDimensionStart(const XmlAttributes & attributes)
	{
		if (!m_hasSizeTag && !m_hasPositionTag)
		{
			throw std::runtime_error("Unexpected AbsDimension element!");
		}

		// Load attributes
		const float x = attributes.GetValueAsFloat(AbsDimensionXAttribute);
		const float y = attributes.GetValueAsFloat(AbsDimensionYAttribute);

		// Ensure we have a frame on the stack to work with
		ASSERT(!m_frames.empty());

		// Set the size or position property of the top frame on the stack
		if (m_hasSizeTag)
		{
			m_frames.top()->SetPixelSize(Size(x, y));
		}
		else
		{
			m_frames.top()->SetPosition(Point(x, y));
		}
	}

	void LayoutXmlLoader::ElementAbsDimensionEnd()
	{
		// Nothing to do here yet
	}

	void LayoutXmlLoader::ElementAnchorStart(const XmlAttributes & attributes)
	{
		if (!m_hasAreaTag)
		{
			throw std::runtime_error("Unexpected Anchor element!");
		}

		// Ensure we have a frame on the stack
		ASSERT(!m_frames.empty());
		auto& topFrame = m_frames.top();

		// Parse attributes
		const std::string pointAttr(attributes.GetValueAsString(AnchorPointAttribute));
		const std::string relativePointAttr(attributes.GetValueAsString(AnchorRelativePointAttribute));
		const std::string relativeToAttr(attributes.GetValueAsString(AnchorRelativeToAttribute));

		// Evaluate point attribute
		AnchorPoint point = AnchorPointByName(pointAttr);
		if (point == anchor_point::None)
		{
			throw std::runtime_error("Anchor has no valid point specified!");
		}

		// Evaluate relative point. If invalid, use point as relative point
		AnchorPoint relativePoint = AnchorPointByName(relativePointAttr);
		if (relativePoint == anchor_point::None) relativePoint = point;

		// Evaluate relative to frame name
		FramePtr relativeTo;
		if (!relativeToAttr.empty())
		{
			relativeTo = FrameManager::Get().Find(relativeToAttr);
			if (relativeTo == nullptr)
			{
				throw std::runtime_error("Anchor specified relative target frame '" + relativeToAttr + "' which doesn't exist!");
			}
		}

		// Set anchor for current frame
		topFrame->SetAnchor(point, relativePoint, relativeTo);
	}

	void LayoutXmlLoader::ElementAnchorEnd()
	{
	}

	void LayoutXmlLoader::ElementScriptStart(const XmlAttributes & attributes)
	{
	}

	void LayoutXmlLoader::ElementScriptEnd()
	{
	}

	void LayoutXmlLoader::ElementVisualStart(const XmlAttributes & attributes)
	{
		// Style has to be a top-level element
		if (m_frames.empty() || m_hasAreaTag || m_hasSizeTag || m_hasVisualTag)
		{
			throw std::runtime_error("Unexpected Visual element!");
		}

		m_hasVisualTag = true;
	}

	void LayoutXmlLoader::ElementVisualEnd()
	{
		m_hasVisualTag = false;
	}

	void LayoutXmlLoader::ElementImagerySectionStart(const XmlAttributes & attributes)
	{
		// Ensure that the element may appear at this location
		if (!m_hasVisualTag || m_section != nullptr || m_stateImagery != nullptr)
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
		if (m_frames.top()->GetImagerySectionByName(name) != nullptr)
		{
			throw std::runtime_error("ImagerySection with the name '" + name + "' already exists in frame '" + m_frames.top()->GetName() + "'!");
		}

		// Add new imagery section
		m_section = std::make_shared<ImagerySection>(name);
		m_frames.top()->AddImagerySection(m_section);
	}

	void LayoutXmlLoader::ElementImagerySectionEnd()
	{
		// Reset ImagerySection element
		m_section.reset();
	}

	void LayoutXmlLoader::ElementImageryStart(const XmlAttributes & attributes)
	{
		// Ensure that the element may appear at this location
		if (!m_hasVisualTag || m_section != nullptr || m_stateImagery != nullptr)
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
		if (m_frames.top()->GetStateImageryByName(name) != nullptr)
		{
			throw std::runtime_error("StateImagery with the name '" + name + "' already exists in frame '" + m_frames.top()->GetName() + "'!");
		}

		// Add new imagery section
		m_stateImagery = std::make_shared<StateImagery>(name);
		m_frames.top()->AddStateImagery(m_stateImagery);
	}

	void LayoutXmlLoader::ElementImageryEnd()
	{
		// Reset current state imagery object
		m_stateImagery.reset();
	}

	void LayoutXmlLoader::ElementLayerStart(const XmlAttributes & attributes)
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

	void LayoutXmlLoader::ElementLayerEnd()
	{
		// Reset the current layer element
		m_layer.reset();
	}

	void LayoutXmlLoader::ElementSectionStart(const XmlAttributes & attributes)
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
		const ImagerySection* sectionEntry = m_frames.top()->GetImagerySectionByName(section);
		if (sectionEntry == nullptr)
		{
			throw std::runtime_error("Unable to find section named '" + section + "' in frame '" + m_frames.top()->GetName() + "'!");
		}

		// Add section entry to layer
		m_layer->AddSection(*sectionEntry);
	}

	void LayoutXmlLoader::ElementSectionEnd()
	{
		// Nothing to do here yet
	}

	void LayoutXmlLoader::ElementTextComponentStart(const XmlAttributes & attributes)
	{
		if (m_component != nullptr || m_section == nullptr)
		{
			throw std::runtime_error("Unexpected TextComponent element!");
		}

		const std::string font(attributes.GetValueAsString(TextComponentFontAttribute));
		const int size = attributes.GetValueAsInt(TextComponentSizeAttribute);
		const float outline = attributes.GetValueAsFloat(TextComponentOutlineAttribute);
		const std::string color(attributes.GetValueAsString(TextComponentColorAttribute));
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

		if (attributes.Exists(TextComponentColorAttribute))
		{
			argb_t argb;

			std::stringstream colorStream;
			colorStream.str(color);
			colorStream.clear();

			colorStream >> std::hex >> argb;
			component->SetColor(argb);
		}

		m_component = component;
		m_section->AddComponent(m_component);
	}

	void LayoutXmlLoader::ElementTextComponentEnd()
	{
		m_component.reset();
	}

	void LayoutXmlLoader::ElementImageComponentStart(const XmlAttributes & attributes)
	{
		if (m_component != nullptr || m_section == nullptr)
		{
			throw std::runtime_error("Unexpected ImageComponent element!");
		}

		const std::string texture(attributes.GetValueAsString(ImageComponentTextureAttribute));
		const std::string tilingAttr(attributes.GetValueAsString(ImageComponentTilingAttribute));

		// Check for texture name existance
		if (texture.empty())
		{
			throw std::runtime_error("ImageComponent needs a texture filename!");
		}

		// Setup component and add it to the current section
		auto component = std::make_shared<ImageComponent>(texture);

		// Apply tiling mode if set
		if (attributes.Exists(ImageComponentTilingAttribute))
		{
			component->SetTilingMode(ImageTilingModeByName(tilingAttr));
		}

		m_component = component;
		m_section->AddComponent(m_component);
	}

	void LayoutXmlLoader::ElementImageComponentEnd()
	{
		m_component.reset();
	}

	void LayoutXmlLoader::ElementBorderComponentStart(const XmlAttributes & attributes)
	{
		if (m_component != nullptr || m_section == nullptr)
		{
			throw std::runtime_error("Unexpected BorderComponent element!");
		}

		const std::string texture(attributes.GetValueAsString(ImageComponentTextureAttribute));
		const int borderSize = attributes.GetValueAsInt(BorderComponentBorderSizeAttribute);

		// Check for texture name existance
		if (texture.empty())
		{
			throw std::runtime_error("BorderComponent needs a texture filename!");
		}

		// Check for border size
		if (!attributes.Exists(BorderComponentBorderSizeAttribute) ||
			borderSize <= 0)
		{
			throw std::runtime_error("BorderComponent needs a valid border size value!");
		}

		// Setup component and add it to the current section
		m_component = std::make_shared<BorderComponent>(texture, borderSize);
		m_section->AddComponent(m_component);
	}

	void LayoutXmlLoader::ElementBorderComponentEnd()
	{
		m_component.reset();
	}

	void LayoutXmlLoader::ElementPropertyStart(const XmlAttributes & attributes)
	{

	}

	void LayoutXmlLoader::ElementPropertyEnd()
	{
		// Nothing to do here yet
	}
}
