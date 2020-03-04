// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "frame.h"

#include "xml_handler/xml_handler.h"

#include <stack>


namespace mmo
{
	/// Provides a frame layout xml handler using the basic xml interface.
	class LayoutXmlLoader final 
		: public XmlHandler
	{
	public:
		// ~ Begin XmlHandler
		virtual void ElementStart(const std::string& element, const XmlAttributes& attributes) override;
		virtual void ElementEnd(const std::string& element) override;
		virtual void Text(const std::string& text) override;
		// ~ End XmlHandler


	private:
		void ElementFrameStart(const XmlAttributes& attributes);
		void ElementFrameEnd();
		void ElementAreaStart(const XmlAttributes& attributes);
		void ElementAreaEnd();
		void ElementScriptStart(const XmlAttributes& attributes);
		void ElementScriptEnd();

		void operator=(const LayoutXmlLoader&) {}

	private:
		/// Stack of added frames.
		std::stack<FramePtr> m_frames;
	};
}
