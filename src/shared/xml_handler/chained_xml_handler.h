// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "xml_handler.h"

#include <memory>


namespace mmo
{
	/// This class is an abstract xml handler class which allows chained handlers,
	/// so this handler can be part of a chain.
	class ChainedXmlHandler : public XmlHandler
	{
	public:
		ChainedXmlHandler();
		virtual ~ChainedXmlHandler();

	public:
		// XMLHandler overrides
		void ElementStart(const std::string& element, const XmlAttributes& attributes) override;
		void ElementEnd(const std::string& element) override;

	public:
		/// Returns whether this chained handler has completed.
		bool Completed() const;

	protected:
		/// Function that handles elements locally (used at end of handler chain)
		virtual void ElementStartLocal(const std::string& element, const XmlAttributes& attributes) = 0;
		/// Function that handles elements locally (used at end of handler chain)
		virtual void ElementEndLocal(const std::string& element) = 0;
		/// clean up any chained handler.
		void CleanupChainedHandler();

	protected:
		/// Chained xml handler object.
		std::unique_ptr<ChainedXmlHandler> m_chainedHandler;
		/// is the chaind handler completed.
		bool m_completed;
	};
}
