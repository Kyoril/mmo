// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

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
#include "base/filesystem.h"
#include "base/utilities.h"


namespace mmo
{
	static const std::string UILayoutElement("UILayout");
	static const std::string FrameElement("Frame");
	static const std::string FrameNameAttribute("name");
	static const std::string FrameTypeAttribute("type");
	static const std::string FrameRendererAttribute("renderer");
	static const std::string FrameParentAttribute("parent");
	static const std::string FrameInheritsAttribute("inherits");
	static const std::string FrameSetAllPointsAttribute("setAllPoints");
	static const std::string FontElement("Font");
	static const std::string FontNameAttribute("name");
	static const std::string FontFileAttribute("file");
	static const std::string FontSizeAttribute("size");
	static const std::string FontOutlineAttribute("outline");
	static const std::string AreaElement("Area");
	static const std::string InsetElement("Inset");
	static const std::string InsetLeftAttribute("left");
	static const std::string InsetRightAttribute("right");
	static const std::string InsetBottomAttribute("bottom");
	static const std::string InsetTopAttribute("top");
	static const std::string InsetAllAttribute("all");
	static const std::string SizeElement("Size");
	static const std::string PositionElement("Position");
	static const std::string AbsDimensionElement("AbsDimension");
	static const std::string AbsDimensionXAttribute("x");
	static const std::string AbsDimensionYAttribute("y");
	static const std::string AnchorElement("Anchor");
	static const std::string AnchorPointAttribute("point");
	static const std::string AnchorRelativePointAttribute("relativePoint");
	static const std::string AnchorRelativeToAttribute("relativeTo");
	static const std::string AnchorOffsetAttribute("offset");
	static const std::string ScriptElement("Script");
	static const std::string ScriptFileAttribute("file");
	static const std::string EventsElement("Events");

	static const std::string PropertyElement("Property");
	static const std::string PropertyNameAttribute("name");
	static const std::string PropertyValueAttribute("value");

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
	static const std::string TextComponentHorzAlignAttribute("horzAlign");
	static const std::string TextComponentVertAlignAttribute("vertAlign");
	static const std::string ImageComponentElement("ImageComponent");
	static const std::string ImageComponentTextureAttribute("texture");
	static const std::string ImageComponentTilingAttribute("tiling");
	static const std::string ImageComponentTintAttribute("tint");
	static const std::string BorderComponentElement("BorderComponent");
	static const std::string BorderComponentBorderSizeAttribute("borderSize");
	static const std::string BorderComponentTopSizeAttribute("topSize");
	static const std::string BorderComponentLeftSizeAttribute("leftSize");
	static const std::string BorderComponentRightSizeAttribute("rightSize");
	static const std::string BorderComponentBottomSizeAttribute("bottomSize");


	void LayoutXmlLoader::SetFilename(std::string filename)
	{
		m_filename = std::move(filename);
	}

	void LayoutXmlLoader::LoadScriptFiles()
	{
		for (const auto& file : m_scriptsToLoad)
		{
			FrameManager::Get().LoadUIFile(file);
		}

		m_scriptsToLoad.clear();
	}

	void LayoutXmlLoader::ElementStart(const std::string & element, const XmlAttributes & attributes)
	{
		// Clear text buffer before handling a new tag
		m_text.clear();

		if (m_hasEventsTag)
		{
			// We are in an Events tag - try to find the frame event by name.
			/*m_frameEvent = m_frames.top()->FindEvent(element);
			if (m_frameEvent != nullptr)
			{
				return;
			}*/

			WLOG("Event '" << element << "' is not supported!");
		}
		else
		{
			// Now handle tags by name
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
			else if (element == VisualElement)
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
			else if (element == EventsElement)
			{
				ElementEventsStart(attributes);
			}
			else if (element == InsetElement)
			{
				ElementInsetStart(attributes);
			}
			else if (element == FontElement)
			{
				ElementFontStart(attributes);
			}
			else
			{
				// We didn't find a valid frame event now a supported tag - output a warning for
				// the developers.
				WLOG("Unknown element found while parsing the ui-layout file: '" << element << "'");
			}
		}
		
	}

	void LayoutXmlLoader::ElementEnd(const std::string & element) 
	{
		// If we have a valid frame event, assign the xml text as event code
		if (m_frameEvent != nullptr)
		{
			m_frameEvent->Set(m_text);
			m_frameEvent = nullptr;
		}
		else
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
			else if (element == VisualElement)
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
			else if (element == EventsElement)
			{
				ElementEventsEnd();
			}
			else if (element == EventsElement)
			{
				ElementInsetEnd();
			}
			else if (element == FontElement)
			{
				ElementFontEnd();
			}
		}
	}

	void LayoutXmlLoader::Text(const std::string & text) 
	{
		// Append text to the buffer. Since xml text may be processed in multiple blocks,
		// this callback may actually be called multiple times for a single text block.

		// m_text will automatically be cleared before a new tag is processed to ensure
		// that multiple text blocks are not concatenated together by accident.
		m_text.append(text);
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
		const std::string renderer(attributes.GetValueAsString(FrameRendererAttribute));
		const bool setAllPoints = attributes.GetValueAsBool(FrameSetAllPointsAttribute, false);

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

		// Setup style and renderer
		if (!renderer.empty()) frame->SetRenderer(renderer);

		// Used to find the parent frame
		FramePtr parentFrame = nullptr;

		// Check if a parent attribute has been set
		if (!parent.empty())
		{
			parentFrame = FrameManager::Get().Find(parent);
			if (parentFrame == nullptr)
			{
				throw std::runtime_error("Parent frame named " + parent + " doesn't exist!");
			}
		}
		else
		{
			// No parent property set - check if we already have a frame on the stack, and if so, add
			// the current frame as a child frame to the frame on top of the stack
			if (!m_frames.empty())
			{
				parentFrame = m_frames.top();
			}
		}

		// Set all anchor points to match the parent frame's anchor points
		if (setAllPoints)
		{
			frame->SetAnchor(anchor_point::Left, anchor_point::Left, nullptr);
			frame->SetAnchor(anchor_point::Top, anchor_point::Top, nullptr);
			frame->SetAnchor(anchor_point::Right, anchor_point::Right, nullptr);
			frame->SetAnchor(anchor_point::Bottom, anchor_point::Bottom, nullptr);
		}

		// Add this frame to the parent frame if we found one
		if (parentFrame)
		{
			parentFrame->AddChild(frame);
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
		const float offset = attributes.GetValueAsFloat(AnchorOffsetAttribute);

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
		topFrame->SetAnchor(point, relativePoint, relativeTo, offset);
	}

	void LayoutXmlLoader::ElementAnchorEnd()
	{
	}

	void LayoutXmlLoader::ElementScriptStart(const XmlAttributes & attributes)
	{
		const std::string file(attributes.GetValueAsString(ScriptFileAttribute));
		if (file.empty())
		{
			throw std::runtime_error("Script element requires a valid file attribute!");
		}

		// Retrieve the full file name and add it to the list of scripts to load later.
		const std::filesystem::path p = std::filesystem::path(m_filename).parent_path();
		const std::filesystem::path f{ file };

		// Ensure that the file has the .lua extension. This way, we ensure that the only lua
		// scripts are loaded, since the FrameManager::LoadUIFile method is later used to load
		// these files and this would theoretically allow to load layouts (which is bad with
		// the current design where only one instance of the xml loader is present).
		if (!f.has_extension() || _stricmp(f.extension().generic_string().c_str(), ".lua") != 0)
		{
			throw std::runtime_error("Script file names have to have the *.lua extension!");
		}

		// Add the file to the list of files to load later.
		m_scriptsToLoad.push_back((p / file).generic_string());
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
		m_section = std::make_unique<ImagerySection>(name);
	}

	void LayoutXmlLoader::ElementImagerySectionEnd()
	{
		// Reset ImagerySection element
		m_frames.top()->AddImagerySection(*m_section);
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
		m_stateImagery = std::make_unique<StateImagery>(name);
	}

	void LayoutXmlLoader::ElementImageryEnd()
	{
		// Reset current state imagery object
		m_frames.top()->AddStateImagery(*m_stateImagery);
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
		m_layer = std::make_unique<FrameLayer>();
	}

	void LayoutXmlLoader::ElementLayerEnd()
	{
		// Reset the current layer element
		m_stateImagery->AddLayer(*m_layer);
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

		const std::string color(attributes.GetValueAsString(TextComponentColorAttribute));
		const std::string horzAlignAttr(attributes.GetValueAsString(TextComponentHorzAlignAttribute));
		const std::string vertAlignAttr(attributes.GetValueAsString(TextComponentVertAlignAttribute));

		// Setup component and add it to the current section
		auto component = std::make_unique<TextComponent>(*m_frames.top());
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

		m_component = std::move(component);
	}

	void LayoutXmlLoader::ElementTextComponentEnd()
	{
		m_section->AddComponent(std::move(m_component));
	}

	void LayoutXmlLoader::ElementImageComponentStart(const XmlAttributes & attributes)
	{
		if (m_component != nullptr || m_section == nullptr)
		{
			throw std::runtime_error("Unexpected ImageComponent element!");
		}

		const std::string texture(attributes.GetValueAsString(ImageComponentTextureAttribute));
		const std::string tilingAttr(attributes.GetValueAsString(ImageComponentTilingAttribute));
		const std::string tint(attributes.GetValueAsString(ImageComponentTintAttribute));

		// Check for texture name existance
		if (texture.empty())
		{
			throw std::runtime_error("ImageComponent needs a texture filename!");
		}

		// Setup component and add it to the current section
		auto component = std::make_unique<ImageComponent>(*m_frames.top(), texture);

		// Apply tiling mode if set
		if (attributes.Exists(ImageComponentTilingAttribute))
		{
			component->SetTilingMode(ImageTilingModeByName(tilingAttr));
		}
		// Apply tint
		if (attributes.Exists(ImageComponentTintAttribute))
		{
			argb_t argb;

			std::stringstream colorStream;
			colorStream.str(tint);
			colorStream.clear();

			colorStream >> std::hex >> argb;
			component->SetTint(argb);
		}

		m_component = std::move(component);
	}

	void LayoutXmlLoader::ElementImageComponentEnd()
	{
		m_section->AddComponent(std::move(m_component));
	}

	void LayoutXmlLoader::ElementBorderComponentStart(const XmlAttributes & attributes)
	{
		if (m_component != nullptr || m_section == nullptr)
		{
			throw std::runtime_error("Unexpected BorderComponent element!");
		}

		const std::string texture(attributes.GetValueAsString(ImageComponentTextureAttribute));
		const float borderSize = attributes.GetValueAsFloat(BorderComponentBorderSizeAttribute);

		// Check for texture name existance
		if (texture.empty())
		{
			throw std::runtime_error("BorderComponent needs a texture filename!");
		}

		// Setup component and add it to the current section
		auto borderComponent = std::make_unique<BorderComponent>(*m_frames.top(), texture, borderSize);

		// Check for border size
		if (!attributes.Exists(BorderComponentBorderSizeAttribute))
		{
			const float top = attributes.GetValueAsInt(BorderComponentTopSizeAttribute);
			const float left = attributes.GetValueAsInt(BorderComponentLeftSizeAttribute);
			const float right = attributes.GetValueAsInt(BorderComponentRightSizeAttribute);
			const float bottom = attributes.GetValueAsInt(BorderComponentBottomSizeAttribute);
			borderComponent->SetBorderSize(Rect(left, top, right, bottom));
		}

		m_component = std::move(borderComponent);
	}

	void LayoutXmlLoader::ElementBorderComponentEnd()
	{
		m_section->AddComponent(std::move(m_component));
	}

	void LayoutXmlLoader::ElementPropertyStart(const XmlAttributes & attributes)
	{
		if (m_frames.empty() || m_hasAreaTag || m_hasVisualTag)
		{
			throw std::runtime_error("Unexpected " + PropertyElement + " element!");
		}

		// Grab attributes
		const std::string name(attributes.GetValueAsString(PropertyNameAttribute));
		std::string value(attributes.GetValueAsString(PropertyValueAttribute));

		// Verify attributes
		if (name.empty())
		{
			throw std::runtime_error("Property needs to have a name!");
		}

		// HACK: Add translation in here. We don't want to set it in the frame's SetText, because
		// input boxes for example may change their text frequently and may not be localized when the
		// user enters a localization string id there
		if (name == "Text")
		{
			// Apply localization
			value = Localize(FrameManager::Get().GetLocalization(), value);
		}

		// Add the property to the frame or (if it already exists) just set it's value
		m_frames.top()->AddProperty(name, value);
	}

	void LayoutXmlLoader::ElementPropertyEnd()
	{
		// Nothing to do here yet
	}

	void LayoutXmlLoader::ElementEventsStart(const XmlAttributes & attributes)
	{
		if (m_hasEventsTag || m_frames.empty() || m_hasAreaTag || m_hasVisualTag)
		{
			throw std::runtime_error("Unexpected " + EventsElement + " element!");
		}

		m_hasEventsTag = true;
	}

	void LayoutXmlLoader::ElementEventsEnd()
	{
		m_hasEventsTag = false;
	}

	void LayoutXmlLoader::ElementInsetStart(const XmlAttributes & attributes)
	{
		if (!m_hasAreaTag || !m_component)
		{
			throw std::runtime_error("Unexpected " + InsetElement + " element!");
		}

		if (attributes.Exists(InsetAllAttribute))
		{
			const float allInset = attributes.GetValueAsFloat(InsetAllAttribute);
			m_insetRect.left = m_insetRect.top = m_insetRect.bottom = m_insetRect.right = allInset;
		}
		else
		{
			m_insetRect.left = attributes.GetValueAsFloat(InsetLeftAttribute);
			m_insetRect.top = attributes.GetValueAsFloat(InsetTopAttribute);
			m_insetRect.right = attributes.GetValueAsFloat(InsetRightAttribute);
			m_insetRect.bottom = attributes.GetValueAsFloat(InsetBottomAttribute);
		}

		m_component->SetInset(m_insetRect);
	}

	void LayoutXmlLoader::ElementInsetEnd()
	{
	}

	void LayoutXmlLoader::ElementFontStart(const XmlAttributes & attributes)
	{
		// Font may not be defined inside frames
		if (!m_frames.empty())
		{
			throw std::runtime_error("Unexpected " + FontElement + " element!");
		}

		const std::string name(attributes.GetValueAsString(FontNameAttribute));
		const std::string file(attributes.GetValueAsString(FontFileAttribute));
		const float size = attributes.GetValueAsFloat(FontSizeAttribute);
		const float outline = attributes.GetValueAsFloat(FontOutlineAttribute);

		// Check parameters
		if (size <= 0.0f || file.empty() || name.empty())
		{
			throw std::runtime_error("Font needs to have a valid name, file and size defined!");
		}

		// Setup a font map
		FrameManager::FontMap map;
		map.FontFile = file;
		map.Size = size;
		map.Outline = outline;
		FrameManager::Get().AddFontMap(name, map);
	}

	void LayoutXmlLoader::ElementFontEnd()
	{
	}
}
