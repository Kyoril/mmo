// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "frame.h"
#include "frame_layer.h"

#include "xml_handler/xml_handler.h"

#include <stack>


namespace mmo
{
	class StateImagery;
	class ImagerySection;

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
		void ElementSizeStart(const XmlAttributes& attributes);
		void ElementSizeEnd();
		void ElementPositionStart(const XmlAttributes& attributes);
		void ElementPositionEnd();
		void ElementAbsDimensionStart(const XmlAttributes& attributes);
		void ElementAbsDimensionEnd();
		void ElementAnchorStart(const XmlAttributes& attributes);
		void ElementAnchorEnd();
		void ElementScriptStart(const XmlAttributes& attributes);
		void ElementScriptEnd();
		void ElementVisualStart(const XmlAttributes& attributes);
		void ElementVisualEnd();
		void ElementImagerySectionStart(const XmlAttributes& attributes);
		void ElementImagerySectionEnd();
		void ElementImageryStart(const XmlAttributes& attributes);
		void ElementImageryEnd();
		void ElementLayerStart(const XmlAttributes& attributes);
		void ElementLayerEnd();
		void ElementSectionStart(const XmlAttributes& attributes);
		void ElementSectionEnd();
		void ElementTextComponentStart(const XmlAttributes& attributes);
		void ElementTextComponentEnd();
		void ElementImageComponentStart(const XmlAttributes& attributes);
		void ElementImageComponentEnd();
		void ElementBorderComponentStart(const XmlAttributes& attributes);
		void ElementBorderComponentEnd();
		void ElementPropertyStart(const XmlAttributes& attributes);
		void ElementPropertyEnd();
		void ElementEventsStart(const XmlAttributes& attributes);
		void ElementEventsEnd();

	private:
		/// Stack of added frames.
		std::stack<FramePtr> m_frames;
		/// Whether an area tag has been opened.
		bool m_hasAreaTag = false;
		
		bool m_hasSizeTag = false;

		bool m_hasPositionTag = false;

		bool m_hasVisualTag = false;

		std::shared_ptr<ImagerySection> m_section;
		std::shared_ptr<StateImagery> m_stateImagery;
		std::shared_ptr<FrameLayer> m_layer;
		std::shared_ptr<FrameComponent> m_component;

		std::string m_text;

		bool m_hasEventsTag = false;

		FrameEvent* m_frameEvent = nullptr;
	};
}
