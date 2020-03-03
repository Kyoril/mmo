// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "frame.h"

#include "xml_handler/xml_handler.h"

#include <stack>


namespace mmo
{
	/// Provides a frame layout xml handler using the basic xml interface.
	class LayoutXmlHandler final 
		: public XmlHandler
	{
	public:
		// ~ Begin XmlHandler
		virtual void ElementStart(const std::string& element, const XmlAttributes& attributes) override;
		virtual void ElementEnd(const std::string& element) override;
		virtual void Text(const std::string& text) override;
		// ~ End XmlHandler


	private:
		void ElementUILayoutStart(const XmlAttributes& attributes);
		void ElementFrameStart(const XmlAttributes& attributes);
		void ElementAutoFrameStart(const XmlAttributes& attributes);
		void ElementUILayoutEnd();
		void ElementFrameEnd();

		void operator=(const LayoutXmlHandler&) {}

	private:
		/// Whether the UI tag has been started.
		bool m_uiStarted;
		/// Stack of added frames.
		std::stack<FramePtr> m_frames;
	};
}
