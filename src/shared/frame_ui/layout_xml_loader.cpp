// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "layout_xml_loader.h"
#include "frame_mgr.h"
#include "frame.h"
#include "frame_event.h"
#include "state_imagery.h"
#include "imagery_section.h"
#include "image_component.h"
#include "border_component.h"
#include "button.h"
#include "text_component.h"

#include "xml_handler/xml_attributes.h"
#include "log/default_log_levels.h"
#include "base/filesystem.h"
#include "textfield.h"



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
	static const std::string FrameIdAttribute("id");
	static const std::string FontElement("Font");
	static const std::string FontNameAttribute("name");
	static const std::string FontFileAttribute("file");
	static const std::string FontSizeAttribute("size");
	static const std::string FontOutlineAttribute("outline");
	static const std::string FontShadowXAttribute("shadowX");
	static const std::string FontShadowYAttribute("shadowY");
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

	static const std::string ScriptsElement("Scripts");
	static const std::string OnClickElement("OnClick");
	static const std::string OnLoadElement("OnLoad");
	static const std::string OnUpdateElement("OnUpdate");
	static const std::string OnTabPressedElement("OnTabPressed");
	static const std::string OnEnterPressedElement("OnEnterPressed");
	static const std::string OnShowElement("OnShow");
	static const std::string OnHideElement("OnHide");
	static const std::string OnEnterElement("OnEnter");
	static const std::string OnLeaveElement("OnLeave");

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
	static const std::string PropertyValueElement("PropertyValue");
	static const std::string PropertyValuePropertyAttribute("property");
	static const std::string BorderComponentElement("BorderComponent");
	static const std::string BorderComponentBorderSizeAttribute("borderSize");
	static const std::string BorderComponentTopSizeAttribute("topSize");
	static const std::string BorderComponentLeftSizeAttribute("leftSize");
	static const std::string BorderComponentRightSizeAttribute("rightSize");
	static const std::string BorderComponentBottomSizeAttribute("bottomSize");
	static const std::string BorderComponentTintAttribute("tint");


	void LoadUIFile(const std::string& filename);


	void mmo::LayoutXmlLoader::SetFilename(std::string filename)
	{
		m_filename = std::move(filename);
	}

	void LayoutXmlLoader::LoadScriptFiles()
	{
		for (const auto& file : m_scriptsToLoad)
		{
			LoadUIFile(file);
		}

		m_scriptsToLoad.clear();

		for(auto& func : m_scriptFunctions)
		{
			func();
		}

		m_scriptFunctions.clear();
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
			else if (element == PropertyValueElement)
			{
				ElementPropertyValueStart(attributes);
			}
			else if (element == ScriptsElement)
			{
				ElementScriptsStart(attributes);
			}
			else if (element == OnClickElement)
			{
				ElementOnClickStart(attributes);
			}
			else if (element == OnLoadElement)
			{
				ElementOnLoadStart(attributes);
			}
			else if (element == OnUpdateElement)
			{
				ElementOnUpdateStart(attributes);
			}
			else if (element == OnShowElement)
			{
				ElementOnShowStart(attributes);
			}
			else if (element == OnHideElement)
			{
				ElementOnHideStart(attributes);
			}
			else if (element == OnTabPressedElement)
			{
				ElementOnTabPressedStart(attributes);
			}
			else if (element == OnEnterPressedElement)
			{
				ElementOnEnterPressedStart(attributes);
			}
			else
			{
				// We didn't find a valid frame event now a supported tag - output a warning for
				// the developers.
				WLOG("Unknown element found while parsing the ui-layout file '" << m_filename << "': '" << element << "'");
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
			else if (element == PropertyValueElement)
			{
				ElementPropertyValueEnd();
			}
			else if (element == ScriptsElement)
			{
				ElementScriptsEnd();
			}
			else if (element == OnClickElement)
			{
				ElementOnClickEnd();
			}
			else if (element == OnLoadElement)
			{
				ElementOnLoadEnd();
			}
			else if (element == OnUpdateElement)
			{
				ElementOnUpdateEnd();
			}
			else if (element == OnTabPressedElement)
			{
				ElementOnTabPressedEnd();
			}
			else if (element == OnEnterPressedElement)
			{
				ElementOnEnterPressedEnd();
			}
			else if (element == OnShowElement)
			{
				ElementOnShowEnd();
			}
			else if (element == OnHideElement)
			{
				ElementOnHideEnd();
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
		if (m_hasAreaTag || m_scriptTag)
		{
			ELOG("Unexpected Frame element in file " << m_filename);
			return;
		}

		// Get the name of the frame to create
		const std::string name(attributes.GetValueAsString(FrameNameAttribute));
		const std::string parent(attributes.GetValueAsString(FrameParentAttribute));
		const std::string renderer(attributes.GetValueAsString(FrameRendererAttribute));
		const bool setAllPoints = attributes.GetValueAsBool(FrameSetAllPointsAttribute, false);
		const int32 id(attributes.GetValueAsInt(FrameIdAttribute, 0));

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
				ELOG("Unable to find template frame '" << inherits << "' when trying to create new frame named " << name << " in file " << m_filename);
			}
			else
			{
				// Override frame type to be used
				type = templateFrame->GetType();
			}
		}

		// Attempt to create the frame
		const FramePtr frame = FrameManager::Get().Create(type, name);
		if (!frame)
		{
			ELOG("Failed to create Frame named '" << name << "' of type '" << type << "' in file " << m_filename);
			return;
		}

		// Copy properties over
		if (templateFrame)
		{
			templateFrame->Copy(*frame);
		}

		frame->SetId(id);

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
				ELOG("Unable to find parant frame by name " << parent << " - failed to set parent of frame " << name << " in file " << m_filename);
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
			ELOG("Unexpected Area element in file " << m_filename);
			return;
		}

		m_hasAreaTag = true;
	}

	void LayoutXmlLoader::ElementAreaEnd()
	{
		m_hasAreaTag = false;
	}

	void LayoutXmlLoader::ElementSizeStart(const XmlAttributes & attributes)
	{
		if (m_hasSizeTag || m_hasPositionTag || !m_hasAreaTag || m_scriptTag)
		{
			ELOG("Unexpected Size element in file " << m_filename);
			return;
		}

		m_hasSizeTag = true;
	}

	void LayoutXmlLoader::ElementSizeEnd()
	{
		m_hasSizeTag = false;
	}

	void LayoutXmlLoader::ElementPositionStart(const XmlAttributes & attributes)
	{
		if (m_hasPositionTag || m_hasSizeTag || !m_hasAreaTag || m_scriptTag)
		{
			ELOG("Unexpected Position element in file " << m_filename);
			return;
		}

		m_hasPositionTag = true;
	}

	void LayoutXmlLoader::ElementPositionEnd()
	{
		m_hasPositionTag = false;
	}

	void LayoutXmlLoader::ElementAbsDimensionStart(const XmlAttributes & attributes)
	{
		if (!m_hasSizeTag && !m_hasPositionTag || m_scriptTag)
		{
			ELOG("Unexpected AbsDimension element in file " << m_filename);
			return;
		}

		// Load attributes
		const float x = attributes.GetValueAsFloat(AbsDimensionXAttribute);
		const float y = attributes.GetValueAsFloat(AbsDimensionYAttribute);

		// Ensure we have a frame on the stack to work with
		ASSERT(!m_frames.empty());

		if (m_imageComponent)
		{
			m_imageComponent->SetSize(static_cast<uint16>(x), static_cast<uint16>(y));
		}
		else if (!m_component)
		{
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
	}

	void LayoutXmlLoader::ElementAbsDimensionEnd()
	{
		// Nothing to do here yet
	}

	void LayoutXmlLoader::ElementAnchorStart(const XmlAttributes & attributes)
	{
		if (!m_hasAreaTag || m_scriptTag)
		{
			ELOG("Unexpected Anchor element in file " << m_filename);
			return;
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
			ELOG("Anchor point '" << pointAttr << "' is an invalid value (in file " << m_filename << ")");
			return;
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
				ELOG("Anchor specified relative target frame '" + relativeToAttr + "' which doesn't exist (in file " << m_filename << ")");
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
		if (m_scriptTag)
		{
			ELOG("Unexpected " << ScriptElement << " tag in file " << m_filename);
			return;
		}

		const std::string file(attributes.GetValueAsString(ScriptFileAttribute));
		if (file.empty())
		{
			ELOG("Script element requires a valid file attribute, but has none or was empty in file " << m_filename);
			return;
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
			ELOG("Script file names have to have the *.lua extension, but file name was '" << f << "' in file " << m_filename);
			return;
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
		if (m_frames.empty() || m_hasAreaTag || m_hasSizeTag || m_hasVisualTag || m_scriptTag)
		{
			ELOG("Unexpected Visual element in file " << m_filename);
			return;
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
		if (!m_hasVisualTag || m_section != nullptr || m_stateImagery != nullptr || m_scriptTag)
		{
			ELOG("Unexpected ImagerySection element in file " << m_filename);
			return;
		}

		// Parse attributes
		const std::string name(attributes.GetValueAsString(ImagerySectionNameAttribute));
		if (name.empty())
		{
			ELOG("ImagerySection element has to have a valid name in file " << m_filename);
			return;
		}

		// Ensure that such a section doesn't already exist
		if (m_frames.top()->GetImagerySectionByName(name) != nullptr)
		{
			ELOG("ImagerySection with the name '" + name + "' already exists in frame '" + m_frames.top()->GetName() + "' in file " << m_filename);
			return;
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
		if (!m_hasVisualTag || m_section != nullptr || m_stateImagery != nullptr || m_scriptTag)
		{
			ELOG("Unexpected StateImagery element in file " << m_filename);
			return;
		}

		// Parse attributes
		const std::string name(attributes.GetValueAsString(StateImageryNameAttribute));
		if (name.empty())
		{
			ELOG("StateImagery element has to have a valid name but none was provided in file " << m_filename);
			return;
		}

		// Ensure that such a state imagery doesn't already exist
		if (m_frames.top()->GetStateImageryByName(name) != nullptr)
		{
			ELOG("StateImagery with the name '" << name << "' already exists in frame '" << m_frames.top()->GetName() << "'!");
			return;
		}

		// Add new imagery section
		m_stateImagery = std::make_unique<StateImagery>(name);
	}

	void LayoutXmlLoader::ElementImageryEnd()
	{
		m_frames.top()->AddStateImagery(*m_stateImagery);
		m_stateImagery.reset();
	}

	void LayoutXmlLoader::ElementLayerStart(const XmlAttributes & attributes)
	{
		if (m_layer != nullptr || m_stateImagery == nullptr || m_scriptTag)
		{
			ELOG("Unexpected " << LayerElement << " element in file " << m_filename);
			return;
		}

		// Add a new layer to the state imagery
		m_layer = std::make_unique<FrameLayer>();
	}

	void LayoutXmlLoader::ElementLayerEnd()
	{
		if (!m_layer)
		{
			return;
		}

		m_stateImagery->AddLayer(*m_layer);
		m_layer.reset();
	}

	void LayoutXmlLoader::ElementSectionStart(const XmlAttributes & attributes)
	{
		// Ensure that the element may appear at this location
		if (m_layer == nullptr || m_scriptTag)
		{
			ELOG("Unexpected " << SectionElement << " element in file " << m_filename);
			return;
		}

		// Get the section name attribute
		const std::string section(attributes.GetValueAsString(SectionSectionAttribute));
		if (section.empty())
		{
			ELOG("Section element needs to have a section name specified in file " << m_filename);
			return;
		}

		// Find section by name
		const ImagerySection* sectionEntry = m_frames.top()->GetImagerySectionByName(section);
		if (sectionEntry == nullptr)
		{
			ELOG("Unable to find section named '" << section << "' in frame '" << m_frames.top()->GetName() << "'!");
			return;
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
		if (m_component != nullptr || m_section == nullptr || m_scriptTag)
		{
			ELOG("Unexpected " << TextComponentElement << " element in file " << m_filename);
			return;
		}

		const std::string color(attributes.GetValueAsString(TextComponentColorAttribute));
		const std::string horzAlignAttr(attributes.GetValueAsString(TextComponentHorzAlignAttribute));
		const std::string vertAlignAttr(attributes.GetValueAsString(TextComponentVertAlignAttribute));

		// Setup component and add it to the current section
		auto component = std::make_unique<TextComponent>(*m_frames.top());

		if (horzAlignAttr.starts_with('$'))
		{
			component->SetHorzAlignmentPropertyName(horzAlignAttr.substr(1));
		}
		else
		{
			component->SetHorizontalAlignment(HorizontalAlignmentByName(horzAlignAttr));
		}

		if (vertAlignAttr.starts_with('$'))
		{
			component->SetVertAlignmentPropertyName(vertAlignAttr.substr(1));
		}
		else
		{
			component->SetVerticalAlignment(VerticalAlignmentByName(vertAlignAttr));
		}

		if (color.starts_with('$'))
		{
			component->SetColorPropertyName(color.substr(1));
		}
		else if(attributes.Exists(TextComponentColorAttribute))
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
		if (m_component != nullptr || m_section == nullptr || m_scriptTag)
		{
			ELOG("Unexpected " << ImageComponentElement << " element in file " << m_filename);
			return;
		}

		const std::string texture(attributes.GetValueAsString(ImageComponentTextureAttribute));
		const std::string tilingAttr(attributes.GetValueAsString(ImageComponentTilingAttribute));
		const std::string tint(attributes.GetValueAsString(ImageComponentTintAttribute));
		
		// Setup component and add it to the current section
		auto component = std::make_unique<ImageComponent>(*m_frames.top(), texture);
		m_imageComponent = component.get();

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
		m_imageComponent = nullptr;
	}

	void LayoutXmlLoader::ElementBorderComponentStart(const XmlAttributes & attributes)
	{
		if (m_component != nullptr || m_section == nullptr || m_scriptTag)
		{
			ELOG("Unexpected " << BorderComponentElement << " element in file " << m_filename);
			return;
		}

		const std::string texture(attributes.GetValueAsString(ImageComponentTextureAttribute));
		const float borderSize = attributes.GetValueAsFloat(BorderComponentBorderSizeAttribute);
		const std::string tint(attributes.GetValueAsString(BorderComponentTintAttribute));

		// Check for texture name existance
		if (texture.empty())
		{
			ELOG("BorderComponent needs a texture filename but none was given in file " << m_filename);
			return;
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

		// Apply tint
		if (attributes.Exists(BorderComponentTintAttribute))
		{
			argb_t argb;

			std::stringstream colorStream;
			colorStream.str(tint);
			colorStream.clear();

			colorStream >> std::hex >> argb;
			borderComponent->SetTint(argb);
		}

		m_component = std::move(borderComponent);
	}

	void LayoutXmlLoader::ElementBorderComponentEnd()
	{
		m_section->AddComponent(std::move(m_component));
	}

	void LayoutXmlLoader::ElementPropertyStart(const XmlAttributes & attributes)
	{
		if (m_frames.empty() || m_hasAreaTag || m_hasVisualTag || m_scriptTag)
		{
			ELOG("Unexpected " << PropertyElement << " element in file " << m_filename);
			return;
		}

		// Grab attributes
		const std::string name(attributes.GetValueAsString(PropertyNameAttribute));
		std::string value(attributes.GetValueAsString(PropertyValueAttribute));

		// Verify attributes
		if (name.empty())
		{
			ELOG("Property needs to have a name but none was given in file " << m_filename);
			return;
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
		if (m_hasEventsTag || m_frames.empty() || m_hasAreaTag || m_hasVisualTag || m_scriptTag)
		{
			ELOG("Unexpected " << EventsElement << " element in file " << m_filename);
			return;
		}

		m_hasEventsTag = true;
	}

	void LayoutXmlLoader::ElementEventsEnd()
	{
		m_hasEventsTag = false;
	}

	void LayoutXmlLoader::ElementInsetStart(const XmlAttributes & attributes)
	{
		if (!m_hasAreaTag || !m_component || m_scriptTag)
		{
			ELOG("Unexpected " + InsetElement + " element in file " << m_filename);
			return;
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
			ELOG("Unexpected " << FontElement << " element in file " << m_filename);
			return;
		}

		const std::string name(attributes.GetValueAsString(FontNameAttribute));
		const std::string file(attributes.GetValueAsString(FontFileAttribute));
		const float size = attributes.GetValueAsFloat(FontSizeAttribute);
		const float outline = attributes.GetValueAsFloat(FontOutlineAttribute);

		const float shadowX = attributes.GetValueAsFloat(FontShadowXAttribute, 0.0f);
		const float shadowY = attributes.GetValueAsFloat(FontShadowYAttribute, 0.0f);

		// Check parameters
		if (size <= 0.0f || file.empty() || name.empty())
		{
			ELOG("Font needs to have a valid name, file and size defined!");
			return;
		}

		// Setup a font map
		FrameManager::FontMap map;
		map.FontFile = file;
		map.Size = size;
		map.Outline = outline;
		map.ShadowX = shadowX;
		map.ShadowY = shadowY;
		FrameManager::Get().AddFontMap(name, map);
	}

	void LayoutXmlLoader::ElementFontEnd()
	{
	}

	void LayoutXmlLoader::ElementPropertyValueStart(const XmlAttributes& attributes)
	{
		if (!m_component || m_hasAreaTag || !m_hasVisualTag)
		{
			ELOG(PropertyValueElement << " tag needs to be placed in a frame component!");
			return;
		}
		
		const std::string propertyName(attributes.GetValueAsString(PropertyValuePropertyAttribute));
		if (propertyName.empty())
		{
			WLOG("Did not find a valid property name attribute in " << PropertyValueElement << " tag");
		}

		if (m_imageComponent)
		{
			m_imageComponent->SetImagePropertyName(propertyName);
		}
	}

	void LayoutXmlLoader::ElementPropertyValueEnd()
	{

	}

	void LayoutXmlLoader::ElementScriptsStart(const XmlAttributes& attributes)
	{
		if (m_frames.empty() || m_hasAreaTag || m_hasVisualTag || m_scriptTag)
		{
			ELOG("Unexpected " << ScriptsElement << " element!");
			return;
		}

		m_scriptTag = true;
	}

	void LayoutXmlLoader::ElementScriptsEnd()
	{
		m_scriptTag = false;
	}

	void LayoutXmlLoader::ElementOnClickStart(const XmlAttributes& attributes)
	{
		if (!m_scriptTag)
		{
			ELOG("Unexpected " << OnClickElement << " element!");
			return;
		}
	}

	void LayoutXmlLoader::ElementOnClickEnd()
	{
		FramePtr frame = m_frames.top();
		if (frame)
		{
			String script = m_text;
			m_scriptFunctions.push_back([frame, script]()
			{
				const luabind::object onClick = FrameManager::Get().CompileFunction(frame->GetName() + ":OnClick", script);
				frame->SetOnClick(onClick);
			});
		}
		else
		{
			ELOG("OnClick element found outside of frame!");
		}
	}

	void LayoutXmlLoader::ElementOnLoadStart(const XmlAttributes& attributes)
	{
		if (!m_scriptTag)
		{
			ELOG("Unexpected " << OnLoadElement << " element!");
			return;
		}
	}

	void LayoutXmlLoader::ElementOnLoadEnd()
	{
		String script = m_text;
		FramePtr frame = m_frames.top();
		m_scriptFunctions.push_back([frame, script]()
		{
			const luabind::object onUpdate = FrameManager::Get().CompileFunction(frame->GetName() + ":OnLoad", script);
			frame->SetOnLoad(onUpdate);
		});
	}

	void LayoutXmlLoader::ElementOnUpdateStart(const XmlAttributes& attributes)
	{
		if (!m_scriptTag)
		{
			ELOG("Unexpected " << OnUpdateElement << " element!");
			return;
		}
	}

	void LayoutXmlLoader::ElementOnUpdateEnd()
	{
		String script = m_text;
		FramePtr frame = m_frames.top();
		m_scriptFunctions.push_back([frame, script]()
		{
			const luabind::object onUpdate = FrameManager::Get().CompileFunction(frame->GetName() + ":OnUpdate", script);
			frame->SetOnUpdate(onUpdate);
		});

	}

	void LayoutXmlLoader::ElementOnTabPressedStart(const XmlAttributes& attributes)
	{
		if (!m_scriptTag)
		{
			ELOG("Unexpected " << OnTabPressedElement << " element!");
			return;
		}
	}

	void LayoutXmlLoader::ElementOnTabPressedEnd()
	{
		String script = m_text;
		FramePtr frame = m_frames.top();
		m_scriptFunctions.push_back([frame, script]()
			{
				const luabind::object onTabPressed = FrameManager::Get().CompileFunction(frame->GetName() + ":OnTabPressed", script);
				frame->SetOnTabPressed(onTabPressed);
			});
	}

	void LayoutXmlLoader::ElementOnEnterPressedStart(const XmlAttributes& attributes)
	{
		if (!m_scriptTag)
		{
			ELOG("Unexpected " << OnEnterPressedElement << " element!");
			return;
		}
	}

	void LayoutXmlLoader::ElementOnEnterPressedEnd()
	{
		String script = m_text;
		FramePtr frame = m_frames.top();
		m_scriptFunctions.push_back([frame, script]()
			{
				const luabind::object onEnterPressed = FrameManager::Get().CompileFunction(frame->GetName() + ":OnEnterPressed", script);
				frame->SetOnEnterPressed(onEnterPressed);
			});
	}

	void LayoutXmlLoader::ElementOnShowStart(const XmlAttributes& attributes)
	{
		if (!m_scriptTag)
		{
			ELOG("Unexpected " << OnShowElement << " element!");
			return;
		}
	}

	void LayoutXmlLoader::ElementOnShowEnd()
	{
		String script = m_text;
		FramePtr frame = m_frames.top();
		m_scriptFunctions.push_back([frame, script]()
			{
				const luabind::object onShow = FrameManager::Get().CompileFunction(frame->GetName() + ":OnShow", script);
				frame->SetOnShow(onShow);
			});
	}

	void LayoutXmlLoader::ElementOnHideStart(const XmlAttributes& attributes)
	{
		if (!m_scriptTag)
		{
			ELOG("Unexpected " << OnHideElement << " element!");
			return;
		}
	}

	void LayoutXmlLoader::ElementOnHideEnd()
	{
		String script = m_text;
		FramePtr frame = m_frames.top();
		m_scriptFunctions.push_back([frame, script]()
			{
				const luabind::object onHide = FrameManager::Get().CompileFunction(frame->GetName() + ":OnHide", script);
				frame->SetOnHide(onHide);
			});
	}
}
