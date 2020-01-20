// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "xml_handler/xml_handler.h"

namespace mmo
{
	/// Provides a frame layout xml handler using the basic xml interface.
	class LayoutXmlHandler final : public XmlHandler
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
		void ElementWindowEnd();

		void operator=(const LayoutXmlHandler&) {}
	};
}
