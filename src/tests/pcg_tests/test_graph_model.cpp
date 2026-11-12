// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/graph/graph_io.h"
#include "pcg/graph/node_registry.h"

#include <limits>

using namespace mmo;
using namespace mmo::pcg::graph;

namespace
{
	Graph MakeGraph()
	{
		Graph g;
		g.name = "Test Graph";
		g.nextNodeId = 9;

		NodeInstance a;
		a.id = 4;
		a.type = "Sampler";
		a.settings["f"] = 0.1f;
		a.settings["i"] = int64(-42);
		a.settings["b"] = true;
		a.settings["v"] = Vector3(1.5f, -2.0f, 0.3f);
		a.settings["s"] = String("hello \"world\"");
		a.editorX = 12.5f;
		a.editorY = -3.0f;
		a.debug = true;
		a.comment = "first";

		NodeInstance b;
		b.id = 2;
		b.type = "Filter";

		NodeInstance c;
		c.id = 7;
		c.type = "Merge";

		NodeInstance d;
		d.id = 3;
		d.type = "Output";

		g.nodes = { a, b, c, d };
		g.links = { { 7, "Out", 3, "In" }, { 2, "Out", 7, "A" }, { 4, "Out", 7, "B" } };

		ParameterDef p;
		p.name = "Density";
		p.defaultValue = 0.25f;
		p.description = "How dense";
		g.parameters.push_back(p);
		ParameterDef q;
		q.name = "Label";
		q.defaultValue = String("x");
		g.parameters.push_back(q);
		return g;
	}
}

TEST_CASE("Graph files round trip text-identically", "[graph_model]")
{
	const Graph original = MakeGraph();
	const String text = WriteGraph(original);

	Graph parsed;
	String error;
	REQUIRE(ParseGraph(text, parsed, error));
	CHECK(error.empty());
	CHECK(parsed.name == "Test Graph");
	CHECK(parsed.nodes.size() == 4);
	CHECK(parsed.links.size() == 3);
	CHECK(parsed.parameters.size() == 2);
	CHECK(parsed.nextNodeId == 9);

	const String again = WriteGraph(parsed);
	CHECK(again == text);

	// Nodes come out sorted by id.
	CHECK(parsed.nodes.front().id == 2);
	CHECK(parsed.nodes.back().id == 7);

	// Settings of every type survive.
	const NodeInstance* sampler = nullptr;
	for (const NodeInstance& n : parsed.nodes)
	{
		if (n.id == 4)
		{
			sampler = &n;
		}
	}
	REQUIRE(sampler != nullptr);
	CHECK(std::get<float>(sampler->settings.at("f")) == 0.1f);
	CHECK(std::get<int64>(sampler->settings.at("i")) == -42);
	CHECK(std::get<bool>(sampler->settings.at("b")));
	CHECK(std::get<Vector3>(sampler->settings.at("v")) == Vector3(1.5f, -2.0f, 0.3f));
	CHECK(std::get<String>(sampler->settings.at("s")) == "hello \"world\"");
	CHECK(sampler->debug);
	CHECK(sampler->comment == "first");
}

TEST_CASE("Bad graph files are rejected with a message", "[graph_model]")
{
	Graph out;
	out.name = "untouched";
	String error;

	CHECK_FALSE(ParseGraph("{ not json", out, error));
	CHECK_FALSE(error.empty());

	error.clear();
	CHECK_FALSE(ParseGraph("[]", out, error));
	CHECK_FALSE(error.empty());

	error.clear();
	CHECK_FALSE(ParseGraph("{\"version\": 2, \"nodes\": []}", out, error));
	CHECK_FALSE(error.empty());

	error.clear();
	CHECK_FALSE(ParseGraph("{\"nodes\": []}", out, error));
	CHECK_FALSE(error.empty());

	error.clear();
	CHECK_FALSE(ParseGraph("{\"version\": 1, \"nodes\": [{\"id\": 1, \"type\": \"A\"}, {\"id\": 1, \"type\": \"B\"}]}", out, error));
	CHECK(error.find("duplicate") != String::npos);

	error.clear();
	CHECK_FALSE(ParseGraph("{\"version\": 1, \"nodes\": [{\"id\": 1}]}", out, error));
	CHECK_FALSE(error.empty());

	error.clear();
	CHECK_FALSE(ParseGraph("{\"version\": 1, \"nodes\": [{\"id\": 1, \"type\": \"A\", \"settings\": {\"x\": {\"type\": \"nope\", \"value\": 1}}}]}", out, error));
	CHECK_FALSE(error.empty());

	CHECK(out.name == "untouched");
}

TEST_CASE("Unknown keys are ignored and missing optional parts default", "[graph_model]")
{
	Graph out;
	String error;
	const String text =
		"{\"version\": 1, \"future\": {\"a\": [1,2,3]}, \"nodes\": ["
		"{\"id\": 5, \"type\": \"A\", \"extra\": true, \"settings\": {\"x\": {\"type\": \"int\", \"value\": 3, \"note\": \"hi\"}}}"
		"], \"links\": [{\"fromNode\": 5, \"fromPin\": \"o\", \"toNode\": 5, \"toPin\": \"i\", \"color\": \"red\"}]}";
	REQUIRE(ParseGraph(text, out, error));
	CHECK(out.nodes.size() == 1);
	CHECK(out.links.size() == 1);
	CHECK(out.nextNodeId == 6);
	CHECK(out.name.empty());
	CHECK(std::get<int64>(out.nodes[0].settings.at("x")) == 3);

	Graph empty;
	REQUIRE(ParseGraph(WriteGraph(empty), out, error));
	CHECK(out.nodes.empty());
}

TEST_CASE("Non-finite floats and invalid UTF-8 are written safely", "[graph_model]")
{
	Graph g = MakeGraph();
	g.nodes[0].settings["f"] = std::numeric_limits<float>::quiet_NaN();
	g.nodes[0].settings["v"] = Vector3(std::numeric_limits<float>::infinity(), 1.0f, -std::numeric_limits<float>::infinity());
	g.nodes[0].editorX = std::numeric_limits<float>::infinity();
	g.nodes[0].comment = String("bad \xFF byte");

	const String text = WriteGraph(g);
	Graph parsed;
	String error;
	REQUIRE(ParseGraph(text, parsed, error));
	for (const NodeInstance& n : parsed.nodes)
	{
		if (n.id == 4)
		{
			CHECK(std::get<float>(n.settings.at("f")) == 0.0f);
			CHECK(std::get<Vector3>(n.settings.at("v")) == Vector3(0.0f, 1.0f, 0.0f));
			CHECK(n.editorX == 0.0f);
		}
	}
}

TEST_CASE("Out-of-range numbers are refused", "[graph_model]")
{
	Graph out;
	String error;
	auto node = [](const String& settings) { return "{\"version\": 1, \"nodes\": [{\"id\": 1, \"type\": \"A\", \"settings\": {\"x\": " + settings + "}}]}"; };

	CHECK_FALSE(ParseGraph(node("{\"type\": \"float\", \"value\": 1e400}"), out, error));
	CHECK_FALSE(error.empty());
	error.clear();
	CHECK_FALSE(ParseGraph(node("{\"type\": \"float\", \"value\": 1e300}"), out, error));
	CHECK_FALSE(error.empty());
	error.clear();
	CHECK_FALSE(ParseGraph(node("{\"type\": \"vector3\", \"value\": [0, 1e300, 0]}"), out, error));
	CHECK_FALSE(error.empty());
	error.clear();
	CHECK_FALSE(ParseGraph(node("{\"type\": \"int\", \"value\": 18446744073709551615}"), out, error));
	CHECK_FALSE(error.empty());
	error.clear();
	CHECK_FALSE(ParseGraph("{\"version\": 1, \"nodes\": [{\"id\": 4294967295, \"type\": \"A\"}]}", out, error));
	CHECK_FALSE(error.empty());
	error.clear();
	CHECK_FALSE(ParseGraph("{\"version\": 1, \"nodes\": [{\"id\": 1, \"type\": \"A\", \"editorX\": 1e300}]}", out, error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("Node ids stop at MaxNodeId so nextNodeId cannot wrap", "[graph_model]")
{
	Graph out;
	String error;
	REQUIRE(ParseGraph("{\"version\": 1, \"nodes\": [{\"id\": 4294967294, \"type\": \"A\"}]}", out, error));
	CHECK(out.nextNodeId == 0xFFFFFFFFu);
	CHECK(out.nextNodeId != 0);

	error.clear();
	CHECK_FALSE(ParseGraph("{\"version\": 1, \"nodes\": [{\"id\": 1, \"type\": \"A\"}], \"links\": [{\"fromNode\": 1, \"fromPin\": \"Out\", \"toNode\": 4294967295, \"toPin\": \"In\"}]}", out, error));
	CHECK_FALSE(error.empty());

	error.clear();
	CHECK_FALSE(ParseGraph("{\"version\": 1, \"nodes\": [{\"id\": 1, \"type\": \"A\"}], \"links\": [{\"fromNode\": 0, \"fromPin\": \"Out\", \"toNode\": 1, \"toPin\": \"In\"}]}", out, error));
	CHECK_FALSE(error.empty());

	Graph g;
	NodeInstance node;
	node.id = 0xFFFFFFFFu;
	node.type = "GetRegion";
	g.nodes.push_back(node);
	const std::vector<GraphIssue> issues = ValidateGraph(g, NodeRegistry::Default());
	REQUIRE(issues.size() == 1);
	CHECK(issues[0].message.find("invalid id") != String::npos);
}
