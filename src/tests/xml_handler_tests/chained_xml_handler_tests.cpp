// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "xml_handler/chained_xml_handler.h"
#include "xml_handler/xml_attributes.h"

#include <string>
#include <vector>

using namespace mmo;

namespace
{
	/// Records every element it handles locally, and reports itself completed once the
	/// element it was given as its terminator ends.
	class RecordingHandler : public ChainedXmlHandler
	{
	public:
		explicit RecordingHandler(std::string terminator = std::string())
			: m_terminator(std::move(terminator))
		{
		}

		std::vector<std::string> log;

		/// Installs a chained handler that gets first crack at incoming elements.
		void Chain(std::unique_ptr<ChainedXmlHandler> handler)
		{
			m_chainedHandler = std::move(handler);
		}

		bool HasChainedHandler() const
		{
			return m_chainedHandler != nullptr;
		}

	protected:
		void ElementStartLocal(const std::string& element, const XmlAttributes&) override
		{
			log.push_back("start:" + element);
		}

		void ElementEndLocal(const std::string& element) override
		{
			log.push_back("end:" + element);

			if (!m_terminator.empty() && element == m_terminator)
			{
				m_completed = true;
			}
		}

	private:
		std::string m_terminator;
	};

	const XmlAttributes emptyAttributes;
}

TEST_CASE("ChainedXmlHandler starts out incomplete and unchained", "[xml_handler][chained]")
{
	RecordingHandler handler;

	CHECK_FALSE(handler.Completed());
	CHECK_FALSE(handler.HasChainedHandler());
}

TEST_CASE("ChainedXmlHandler handles elements locally when nothing is chained",
	"[xml_handler][chained]")
{
	RecordingHandler handler;

	handler.ElementStart("Frame", emptyAttributes);
	handler.ElementEnd("Frame");

	CHECK(handler.log == std::vector<std::string>{ "start:Frame", "end:Frame" });
}

TEST_CASE("ChainedXmlHandler gives a chained handler first crack at every element",
	"[xml_handler][chained]")
{
	RecordingHandler parent;

	auto child = std::make_unique<RecordingHandler>();
	RecordingHandler& childRef = *child;
	parent.Chain(std::move(child));

	parent.ElementStart("Button", emptyAttributes);
	parent.ElementEnd("Button");

	// Everything went to the child; the parent handled nothing itself.
	CHECK(childRef.log == std::vector<std::string>{ "start:Button", "end:Button" });
	CHECK(parent.log.empty());
}

TEST_CASE("ChainedXmlHandler releases a chained handler as soon as it completes",
	"[xml_handler][chained]")
{
	RecordingHandler parent;
	parent.Chain(std::make_unique<RecordingHandler>("Button"));
	REQUIRE(parent.HasChainedHandler());

	// The child marks itself completed on </Button>, and the parent drops it in the same
	// call -- so the very next element is the parent's again.
	parent.ElementStart("Button", emptyAttributes);
	CHECK(parent.HasChainedHandler());

	parent.ElementEnd("Button");
	CHECK_FALSE(parent.HasChainedHandler());

	parent.ElementStart("Frame", emptyAttributes);
	CHECK(parent.log == std::vector<std::string>{ "start:Frame" });
}

TEST_CASE("ChainedXmlHandler completing a child does not complete the parent",
	"[xml_handler][chained]")
{
	RecordingHandler parent;
	parent.Chain(std::make_unique<RecordingHandler>("Button"));

	parent.ElementStart("Button", emptyAttributes);
	parent.ElementEnd("Button");

	CHECK_FALSE(parent.Completed());
}

TEST_CASE("ChainedXmlHandler forwards through a chain more than one deep",
	"[xml_handler][chained]")
{
	RecordingHandler parent;

	auto middle = std::make_unique<RecordingHandler>();
	RecordingHandler& middleRef = *middle;

	auto leaf = std::make_unique<RecordingHandler>();
	RecordingHandler& leafRef = *leaf;

	middleRef.Chain(std::move(leaf));
	parent.Chain(std::move(middle));

	parent.ElementStart("Deep", emptyAttributes);

	// Only the innermost handler sees the element.
	CHECK(leafRef.log == std::vector<std::string>{ "start:Deep" });
	CHECK(middleRef.log.empty());
	CHECK(parent.log.empty());
}
