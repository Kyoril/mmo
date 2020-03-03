// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "layout_xmlhandler.h"
#include "frame_mgr.h"
#include "frame.h"

#include "xml_handler/xml_attributes.h"
#include "log/default_log_levels.h"

namespace mmo
{
	static const std::string UILayoutElement("UILayout");
	static const std::string FrameElement("Frame");
	static const std::string FrameTypeAttribute("type");
	static const std::string FrameNameAttribute("name");
	static const std::string FrameTextAttribute("text");
	static const std::string FrameParentAttribute("parent");
	static const std::string EventNameAttribute("name");
	static const std::string EventFunctionAttribute("function");
	static const std::string LayersElement("Layers");
	static const std::string LayerElement("Layer");


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
			WLOG("Unknown element found while parsing the ui-layout file: '" << element << "'");
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
			ElementFrameEnd();
		}
	}

	void LayoutXmlHandler::Text(const std::string & text) 
	{
	}

	void LayoutXmlHandler::ElementUILayoutStart(const XmlAttributes & attributes)
	{
		// Ui is the first tag and may only appear once.
		if (m_uiStarted)
		{
			throw std::runtime_error("Nested Ui tag is not supported!");
		}

		m_uiStarted = true;
	}

	void LayoutXmlHandler::ElementFrameStart(const XmlAttributes & attributes)
	{
		// Get the name of the frame to create
		std::string name(attributes.GetValueAsString(FrameNameAttribute));
		std::string parent(attributes.GetValueAsString(FrameParentAttribute));
		std::string text(attributes.GetValueAsString(FrameTextAttribute));

		// Attempt to create the frame
		FramePtr frame = FrameManager::Get().Create("Frame", name);
		if (!frame)
		{
			throw std::runtime_error("Could not create frame named!");
		}

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

		// Push it to the stack of frames
		m_frames.push(frame);
	}

	void LayoutXmlHandler::ElementAutoFrameStart(const XmlAttributes & attributes)
	{

	}

	void LayoutXmlHandler::ElementUILayoutEnd()
	{
		// We don't need to check this here, as the xml parser will already ensure that
		// check for valid end elements is performed.
		m_uiStarted = false;
	}

	void LayoutXmlHandler::ElementFrameEnd()
	{
		// Remove the last processes frame from the stack and ensure that
		// there is one
		ASSERT(!m_frames.empty());
		m_frames.pop();
	}
}
