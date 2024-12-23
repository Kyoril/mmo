// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "chained_xml_handler.h"
#include "xml_attributes.h"


namespace mmo
{
	ChainedXmlHandler::ChainedXmlHandler()
		: m_chainedHandler(nullptr)
		, m_completed(false)
	{
	}

	ChainedXmlHandler::~ChainedXmlHandler()
	{
	}

	void ChainedXmlHandler::ElementStart(const std::string & element, const XmlAttributes & attributes)
	{
		// chained handler gets first crack at this element
		if (m_chainedHandler)
		{
			m_chainedHandler->ElementStart(element, attributes);

			// clean up if completed
			if (m_chainedHandler->Completed())
			{
				CleanupChainedHandler();
			}
		}
		else
		{
			ElementStartLocal(element, attributes);
		}
	}

	void ChainedXmlHandler::ElementEnd(const std::string & element)
	{
		// chained handler gets first crack at this element
		if (m_chainedHandler)
		{
			m_chainedHandler->ElementEnd(element);

			// clean up if completed
			if (m_chainedHandler->Completed())
			{
				CleanupChainedHandler();
			}
		}
		else
		{
			ElementEndLocal(element);
		}
	}

	bool ChainedXmlHandler::Completed() const
	{
		return m_completed;
	}

	void ChainedXmlHandler::CleanupChainedHandler()
	{
		m_chainedHandler.reset();
	}

}