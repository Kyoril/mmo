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
	static const std::string FrameStyleAttribute("style");
	static const std::string FrameRendererAttribute("renderer");
	static const std::string FrameTextAttribute("text");
	static const std::string FrameParentAttribute("parent");
	static const std::string AreaElement("Area");
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
		// Get the name of the frame to create
		std::string name(attributes.GetValueAsString(FrameNameAttribute));
		std::string parent(attributes.GetValueAsString(FrameParentAttribute));
		std::string text(attributes.GetValueAsString(FrameTextAttribute));
		std::string style(attributes.GetValueAsString(FrameStyleAttribute));
		std::string renderer(attributes.GetValueAsString(FrameRendererAttribute));

		// Attempt to create the frame
		FramePtr frame = FrameManager::Get().Create("Frame", name);
		if (!frame)
		{
			throw std::runtime_error("Could not create frame named!");
		}

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
	}

	void LayoutXmlLoader::ElementAreaEnd()
	{
	}

	void LayoutXmlLoader::ElementScriptStart(const XmlAttributes & attributes)
	{
	}

	void LayoutXmlLoader::ElementScriptEnd()
	{
	}
}
