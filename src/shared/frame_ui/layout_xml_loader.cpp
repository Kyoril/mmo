// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "layout_xml_loader.h"
#include "frame_mgr.h"
#include "frame.h"

#include "xml_handler/xml_attributes.h"
#include "log/default_log_levels.h"

namespace mmo
{
	static const std::string UILayoutElement("UILayout");
	static const std::string FrameElement("Frame");
	static const std::string FrameNameAttribute("name");
	static const std::string FrameTypeAttribute("type");
	static const std::string FrameStyleAttribute("style");
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
		const std::string type(attributes.GetValueAsString(FrameTypeAttribute, FrameElement));
		const std::string parent(attributes.GetValueAsString(FrameParentAttribute));
		const std::string text(attributes.GetValueAsString(FrameTextAttribute));
		const std::string style(attributes.GetValueAsString(FrameStyleAttribute));
		const std::string renderer(attributes.GetValueAsString(FrameRendererAttribute));
		const bool hidden = attributes.GetValueAsBool(FrameHiddenAttribute, false);
		const bool enabled = attributes.GetValueAsBool(FrameEnabledAttribute, true);

		// Attempt to create the frame
		FramePtr frame = FrameManager::Get().Create(type, name);
		if (!frame)
		{
			throw std::runtime_error("Could not create frame named!");
		}

		// First, duplicate template frame if there is any. We do this to allow
		// overriding other frame properties by this frame definition.
		if (attributes.Exists(FrameInheritsAttribute))
		{
			const std::string inherits(attributes.GetValueAsString(FrameInheritsAttribute));

			// Find template frame
			FramePtr templateFrame = FrameManager::Get().Find(inherits);
			if (templateFrame == nullptr)
			{
				throw std::runtime_error("Unable to find template frame '" + inherits + "'");
			}

			// Copy properties
			templateFrame->Copy(*frame);
		}

		// Setup frame properties
		frame->SetVisible(!hidden);
		frame->SetEnabled(enabled);

		// Setup style and renderer
		if (!style.empty()) frame->SetStyle(style);
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
}
