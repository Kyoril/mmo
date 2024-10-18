// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "frame.h"
#include "frame_layer.h"

#include "xml_handler/xml_handler.h"

#include <stack>


namespace mmo
{
	class ImageComponent;
	class FrameEvent;
	class StateImagery;
	class ImagerySection;

	/// Provides a frame layout xml handler using the basic xml interface.
	class LayoutXmlLoader final 
		: public XmlHandler
	{
	public:
		/// Sets the name of the file that is currently being processed.
		void SetFilename(std::string filename);
		/// Loads all script files that have been found while parsing the layout xml file. Afterwards,
		/// it clears the list.
		void LoadScriptFiles();

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
		void ElementInsetStart(const XmlAttributes& attributes);
		void ElementInsetEnd();
		void ElementFontStart(const XmlAttributes& attributes);
		void ElementFontEnd();
		void ElementPropertyValueStart(const XmlAttributes& attributes);
		void ElementPropertyValueEnd();

		void ElementScriptsStart(const XmlAttributes& attributes);
		void ElementScriptsEnd();
		void ElementOnClickStart(const XmlAttributes& attributes);
		void ElementOnClickEnd();
		void ElementOnLoadStart(const XmlAttributes& attributes);
		void ElementOnLoadEnd();
		void ElementOnUpdateStart(const XmlAttributes& attributes);
		void ElementOnUpdateEnd();
		void ElementOnTabPressedStart(const XmlAttributes& attributes);
		void ElementOnTabPressedEnd();
		void ElementOnEnterPressedStart(const XmlAttributes& attributes);
		void ElementOnEnterPressedEnd();
		void ElementOnShowStart(const XmlAttributes& attributes);
		void ElementOnShowEnd();
		void ElementOnHideStart(const XmlAttributes& attributes);
		void ElementOnHideEnd();

	private:
		/// Stack of added frames.
		std::stack<FramePtr> m_frames;

		std::vector<std::function<void()>> m_scriptFunctions;

		/// Whether an area tag has been opened.
		bool m_hasAreaTag = false;
		
		bool m_hasSizeTag = false;

		bool m_hasPositionTag = false;

		bool m_hasVisualTag = false;

		bool m_scriptTag = false;

		std::unique_ptr<ImagerySection> m_section;
		std::unique_ptr<StateImagery> m_stateImagery;
		std::unique_ptr<FrameLayer> m_layer;
		std::unique_ptr<FrameComponent> m_component;
		ImageComponent* m_imageComponent { nullptr };

		std::string m_text;

		bool m_hasEventsTag = false;

		FrameEvent* m_frameEvent = nullptr;

		std::string m_filename;

		/// Stores the file names of script file to load after the layout file has been processed.
		std::vector<std::string> m_scriptsToLoad;

		Rect m_insetRect;
	};
}
