// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/graph/graph_document.h"
#include "pcg/graph/graph_io.h"
#include "pcg/graph/node_registry.h"

#include <algorithm>

using namespace mmo;
using namespace mmo::pcg::graph;

namespace
{
	NodeOutput NoOp(const EvalContext&, uint64, const AttributeSet&, const std::map<String, Collection>&)
	{
		return NodeOutput();
	}

	/// A small registry: an area source, a points source, a single-input points sink, a multi-input
	/// points merge and a pass-through points node.
	NodeRegistry MakeRegistry()
	{
		NodeRegistry r;
		{
			NodeTypeDef def;
			def.type = "AreaSource";
			def.category = "Input";
			def.title = "Area Source";
			def.outputs.push_back(PinDef{ "Out", { ItemKind::Area } });
			def.execute = NoOp;
			r.Register(def);
		}
		{
			NodeTypeDef def;
			def.type = "PointSource";
			def.category = "Input";
			def.title = "Point Source";
			def.outputs.push_back(PinDef{ "Out", { ItemKind::Points } });
			def.settings.push_back(SettingDef{ "count", int64(12), 0, 100, {}, "How many" });
			def.settings.push_back(SettingDef{ "radius", 2.5f, 0, 0, {}, "Radius" });
			def.settings.push_back(SettingDef{ "mode", String("Grid"), 0, 0, { "Grid", "Random" }, "Mode" });
			def.execute = NoOp;
			r.Register(def);
		}
		{
			NodeTypeDef def;
			def.type = "Sink";
			def.category = "Output";
			def.title = "Sink";
			def.inputs.push_back(PinDef{ "In", { ItemKind::Points }, false, true });
			def.execute = NoOp;
			r.Register(def);
		}
		{
			NodeTypeDef def;
			def.type = "Merge";
			def.category = "Point";
			def.title = "Merge";
			def.inputs.push_back(PinDef{ "In", { ItemKind::Points }, true, false });
			def.outputs.push_back(PinDef{ "Out", { ItemKind::Points } });
			def.execute = NoOp;
			r.Register(def);
		}
		return r;
	}

	/// A graph with a known chain and an unknown node type (settings, links to its pins).
	Graph MakeGraphWithUnknownNode()
	{
		Graph g;
		g.name = "Doc";

		NodeInstance a;
		a.id = 1;
		a.type = "PointSource";
		a.settings["count"] = int64(5);
		a.editorX = 10.0f;
		a.editorY = 20.0f;

		NodeInstance u;
		u.id = 2;
		u.type = "FutureNode";
		u.settings["strength"] = 0.75f;
		u.settings["label"] = String("keep me");
		u.comment = "from a newer build";

		NodeInstance m;
		m.id = 3;
		m.type = "Merge";

		NodeInstance s;
		s.id = 4;
		s.type = "Sink";

		g.nodes = { a, u, m, s };
		g.links = { { 1, "Out", 2, "Source" }, { 2, "Result", 3, "In" }, { 2, "Extra", 3, "In" }, { 3, "Out", 4, "In" } };
		g.nextNodeId = 5;

		ParameterDef p;
		p.name = "Count";
		p.defaultValue = int64(7);
		g.parameters.push_back(p);
		return g;
	}

	GraphDocument LoadDoc(const NodeRegistry& r, const Graph& g)
	{
		GraphDocument doc;
		String error;
		REQUIRE(doc.Load(WriteGraph(g), r, error));
		return doc;
	}

	uint64 Pin(const GraphDocument& doc, const uint32 node, const bool isOutput, const String& name)
	{
		const uint64 id = doc.FindPinId(node, isOutput, name);
		REQUIRE(id != 0);
		return id;
	}

	size_t CountLinksInto(const Graph& g, const uint32 node, const String& pin)
	{
		return static_cast<size_t>(std::count_if(g.links.begin(), g.links.end(), [&](const Link& l)
		{
			return l.toNode == node && l.toPin == pin;
		}));
	}
}

TEST_CASE("GraphDocument round trips unknown node types text-identically", "[graph_document]")
{
	const NodeRegistry r = MakeRegistry();
	const String text = WriteGraph(MakeGraphWithUnknownNode());

	GraphDocument doc;
	String error;
	REQUIRE(doc.Load(text, r, error));
	CHECK(doc.Save() == text);

	CHECK(doc.IsUnknownNode(2));
	CHECK_FALSE(doc.IsUnknownNode(1));
	CHECK_FALSE(doc.IsUnknownNode(99));

	// The unknown node's pins are taken from its links, verbatim and sorted.
	CHECK(doc.GetPinNames(2, false) == std::vector<String>{ "Source" });
	CHECK(doc.GetPinNames(2, true) == std::vector<String>{ "Extra", "Result" });
	CHECK(doc.GetGraph().nodes[1].settings.at("label") == AttributeValue(String("keep me")));
	CHECK(doc.ResolveLinks().size() == 4);
}

TEST_CASE("GraphDocument Load rejects malformed text and keeps the previous graph", "[graph_document]")
{
	const NodeRegistry r = MakeRegistry();
	GraphDocument doc = LoadDoc(r, MakeGraphWithUnknownNode());
	const uint64 revision = doc.Revision();

	String error;
	CHECK_FALSE(doc.Load("{ not json", r, error));
	CHECK_FALSE(error.empty());
	CHECK(doc.GetGraph().nodes.size() == 4);
	CHECK(doc.Revision() == revision);
}

TEST_CASE("GraphDocument AddNode uses the setting defaults", "[graph_document]")
{
	const NodeRegistry r = MakeRegistry();
	GraphDocument doc;
	String error;
	REQUIRE(doc.Load(WriteGraph(Graph()), r, error));

	const uint32 id = doc.AddNode("PointSource", 32.0f, -8.0f);
	REQUIRE(id == 1);
	const NodeInstance* node = doc.FindNode(id);
	REQUIRE(node != nullptr);
	CHECK(node->type == "PointSource");
	CHECK(node->editorX == 32.0f);
	CHECK(node->editorY == -8.0f);
	CHECK(node->settings.size() == 3);
	CHECK(node->settings.at("count") == AttributeValue(int64(12)));
	CHECK(node->settings.at("radius") == AttributeValue(2.5f));
	CHECK(node->settings.at("mode") == AttributeValue(String("Grid")));

	CHECK(doc.AddNode("Sink", 0, 0) == 2);
	CHECK(doc.GetGraph().nextNodeId == 3);

	// Unknown types cannot be added.
	CHECK(doc.AddNode("NoSuchType", 0, 0) == 0);
	CHECK(doc.GetGraph().nodes.size() == 2);
}

TEST_CASE("GraphDocument CanLink refuses mismatched kinds, self-links and cycles", "[graph_document]")
{
	const NodeRegistry r = MakeRegistry();
	GraphDocument doc;
	String error;
	REQUIRE(doc.Load(WriteGraph(Graph()), r, error));

	const uint32 area = doc.AddNode("AreaSource", 0, 0);
	const uint32 points = doc.AddNode("PointSource", 0, 0);
	const uint32 m1 = doc.AddNode("Merge", 0, 0);
	const uint32 m2 = doc.AddNode("Merge", 0, 0);
	const uint32 sink = doc.AddNode("Sink", 0, 0);

	String reason;
	// Kind mismatch: Area into a Points input.
	CHECK_FALSE(doc.CanLink(Pin(doc, area, true, "Out"), Pin(doc, sink, false, "In"), reason));
	CHECK_FALSE(reason.empty());

	// Direction: input to output.
	reason.clear();
	CHECK_FALSE(doc.CanLink(Pin(doc, sink, false, "In"), Pin(doc, points, true, "Out"), reason));
	CHECK_FALSE(reason.empty());

	// Self-link.
	reason.clear();
	CHECK_FALSE(doc.CanLink(Pin(doc, m1, true, "Out"), Pin(doc, m1, false, "In"), reason));
	CHECK_FALSE(reason.empty());

	// Valid, then a cycle m1 -> m2 -> m1.
	reason.clear();
	CHECK(doc.CanLink(Pin(doc, points, true, "Out"), Pin(doc, m1, false, "In"), reason));
	REQUIRE(doc.AddLink(Pin(doc, m1, true, "Out"), Pin(doc, m2, false, "In")));
	CHECK_FALSE(doc.CanLink(Pin(doc, m2, true, "Out"), Pin(doc, m1, false, "In"), reason));
	CHECK_FALSE(reason.empty());
	CHECK_FALSE(doc.AddLink(Pin(doc, m2, true, "Out"), Pin(doc, m1, false, "In")));

	// The same link twice.
	CHECK_FALSE(doc.CanLink(Pin(doc, m1, true, "Out"), Pin(doc, m2, false, "In"), reason));

	// Unknown pin ids.
	CHECK_FALSE(doc.CanLink(GraphDocument::MakePinId(99, true, 0), Pin(doc, sink, false, "In"), reason));
	CHECK_FALSE(doc.CanLink(GraphDocument::MakePinId(points, true, 5), Pin(doc, sink, false, "In"), reason));
	CHECK(doc.GetGraph().links.size() == 1);
}

TEST_CASE("GraphDocument linking into a single-input pin replaces the old link", "[graph_document]")
{
	const NodeRegistry r = MakeRegistry();
	GraphDocument doc;
	String error;
	REQUIRE(doc.Load(WriteGraph(Graph()), r, error));

	const uint32 a = doc.AddNode("PointSource", 0, 0);
	const uint32 b = doc.AddNode("PointSource", 0, 0);
	const uint32 merge = doc.AddNode("Merge", 0, 0);
	const uint32 sink = doc.AddNode("Sink", 0, 0);

	REQUIRE(doc.AddLink(Pin(doc, a, true, "Out"), Pin(doc, sink, false, "In")));
	REQUIRE(doc.AddLink(Pin(doc, b, true, "Out"), Pin(doc, sink, false, "In")));
	REQUIRE(CountLinksInto(doc.GetGraph(), sink, "In") == 1);
	CHECK(doc.GetGraph().links[0].fromNode == b);

	// A multi-input pin keeps every link.
	REQUIRE(doc.AddLink(Pin(doc, a, true, "Out"), Pin(doc, merge, false, "In")));
	REQUIRE(doc.AddLink(Pin(doc, b, true, "Out"), Pin(doc, merge, false, "In")));
	CHECK(CountLinksInto(doc.GetGraph(), merge, "In") == 2);
}

TEST_CASE("GraphDocument RemoveNode drops its links", "[graph_document]")
{
	const NodeRegistry r = MakeRegistry();
	GraphDocument doc = LoadDoc(r, MakeGraphWithUnknownNode());

	REQUIRE(doc.RemoveNode(2));
	CHECK(doc.FindNode(2) == nullptr);
	CHECK(doc.GetGraph().nodes.size() == 3);
	REQUIRE(doc.GetGraph().links.size() == 1);
	CHECK(doc.GetGraph().links[0].fromNode == 3);
	CHECK(doc.GetGraph().links[0].toNode == 4);
	CHECK(doc.GetPinNames(2, true).empty());

	CHECK_FALSE(doc.RemoveNode(2));
}

TEST_CASE("GraphDocument RemoveLink removes the link by id", "[graph_document]")
{
	const NodeRegistry r = MakeRegistry();
	GraphDocument doc = LoadDoc(r, MakeGraphWithUnknownNode());

	const uint64 from = Pin(doc, 3, true, "Out");
	const uint64 to = Pin(doc, 4, false, "In");
	REQUIRE(doc.RemoveLink(GraphDocument::MakeLinkId(from, to)));
	CHECK(doc.GetGraph().links.size() == 3);
	CHECK_FALSE(doc.RemoveLink(GraphDocument::MakeLinkId(from, to)));
}

TEST_CASE("GraphDocument pin and link ids are stable across Save and Load", "[graph_document]")
{
	const NodeRegistry r = MakeRegistry();
	GraphDocument doc = LoadDoc(r, MakeGraphWithUnknownNode());

	// The pin id layout.
	const uint64 pin = GraphDocument::MakePinId(3, true, 0);
	CHECK(pin == ((uint64(3) << 16) | (uint64(1) << 15)));
	const GraphDocument::PinRef ref = GraphDocument::DecodePinId(pin);
	CHECK(ref.node == 3);
	CHECK(ref.isOutput);
	CHECK(ref.index == 0);

	const std::vector<GraphDocument::ResolvedLink> before = doc.ResolveLinks();
	REQUIRE(before.size() == 4);

	GraphDocument again;
	String error;
	REQUIRE(again.Load(doc.Save(), r, error));
	const std::vector<GraphDocument::ResolvedLink> after = again.ResolveLinks();
	REQUIRE(after.size() == before.size());
	for (size_t i = 0; i < before.size(); ++i)
	{
		CHECK(after[i].id == before[i].id);
		CHECK(after[i].fromPin == before[i].fromPin);
		CHECK(after[i].toPin == before[i].toPin);
		CHECK(after[i].id == GraphDocument::MakeLinkId(after[i].fromPin, after[i].toPin));
		CHECK(after[i].id != 0);
	}
	CHECK(again.FindPinId(2, true, "Result") == doc.FindPinId(2, true, "Result"));
	CHECK(again.FindPinId(2, true, "Extra") == doc.FindPinId(2, true, "Extra"));
}

TEST_CASE("GraphDocument SetSetting checks the setting type", "[graph_document]")
{
	const NodeRegistry r = MakeRegistry();
	GraphDocument doc = LoadDoc(r, MakeGraphWithUnknownNode());

	CHECK(doc.SetSetting(1, "count", int64(40)));
	CHECK(doc.FindNode(1)->settings.at("count") == AttributeValue(int64(40)));

	// Wrong type for a declared setting.
	CHECK_FALSE(doc.SetSetting(1, "count", 1.0f));
	// A parameter binding is allowed for any declared setting.
	CHECK(doc.SetSetting(1, "count", String("$Count")));
	CHECK(doc.FindNode(1)->settings.at("count") == AttributeValue(String("$Count")));
	// Unknown nodes keep whatever they get.
	CHECK(doc.SetSetting(2, "strength", int64(3)));
	// No such node.
	CHECK_FALSE(doc.SetSetting(42, "count", int64(1)));
}

TEST_CASE("GraphDocument Revision increases on every mutation", "[graph_document]")
{
	const NodeRegistry r = MakeRegistry();
	GraphDocument doc;
	uint64 last = doc.Revision();
	const auto bumped = [&]()
	{
		const bool result = doc.Revision() > last;
		last = doc.Revision();
		return result;
	};

	String error;
	REQUIRE(doc.Load(WriteGraph(Graph()), r, error));
	CHECK(bumped());

	const uint32 a = doc.AddNode("PointSource", 0, 0);
	CHECK(bumped());
	const uint32 sink = doc.AddNode("Sink", 0, 0);
	CHECK(bumped());

	const uint64 from = Pin(doc, a, true, "Out");
	const uint64 to = Pin(doc, sink, false, "In");
	REQUIRE(doc.AddLink(from, to));
	CHECK(bumped());

	REQUIRE(doc.SetSetting(a, "radius", 4.0f));
	CHECK(bumped());

	doc.EditGraph().name = "Renamed";
	CHECK(bumped());

	REQUIRE(doc.SetNodePosition(a, 5.0f, 6.0f));
	CHECK(bumped());
	// An unchanged position is not a mutation.
	CHECK_FALSE(doc.SetNodePosition(a, 5.0f, 6.0f));
	CHECK_FALSE(bumped());

	REQUIRE(doc.RemoveLink(GraphDocument::MakeLinkId(from, to)));
	CHECK(bumped());

	REQUIRE(doc.RemoveNode(a));
	CHECK(bumped());

	// Failed mutations leave the revision alone.
	CHECK_FALSE(doc.RemoveNode(a));
	CHECK(doc.AddNode("NoSuchType", 0, 0) == 0);
	CHECK_FALSE(doc.SetSetting(a, "radius", 1.0f));
	CHECK_FALSE(bumped());
}

TEST_CASE("GraphDocument builds, binds and reloads a built-in chain", "[graph_document]")
{
	// The editor's smoke flow on the real registry: Get Controls -> Sample Area -> Spawn Meshes, settings,
	// one setting bound to a new parameter, then save and reload.
	const NodeRegistry& r = NodeRegistry::Default();
	GraphDocument doc;
	String error;
	REQUIRE(doc.Load(WriteGraph(Graph()), r, error));

	const uint32 controls = doc.AddNode("GetControls", 0, 0);
	const uint32 sampler = doc.AddNode("SampleArea", 250, 0);
	const uint32 spawn = doc.AddNode("SpawnMeshes", 500, 0);
	REQUIRE(controls != 0);
	REQUIRE(sampler != 0);
	REQUIRE(spawn != 0);

	REQUIRE(doc.AddLink(Pin(doc, controls, true, "Out"), Pin(doc, sampler, false, "In")));
	REQUIRE(doc.AddLink(Pin(doc, sampler, true, "Out"), Pin(doc, spawn, false, "In")));
	REQUIRE(doc.SetSetting(controls, "tags", String("forest")));
	REQUIRE(doc.SetSetting(spawn, "meshes", String("Models/Tree.hmsh|2;Models/Bush.hmsh|1")));

	ParameterDef spacing;
	spacing.name = "minSpacing";
	spacing.defaultValue = 3.0f;
	doc.EditGraph().parameters.push_back(spacing);
	REQUIRE(doc.SetSetting(sampler, "minSpacing", String("$minSpacing")));
	REQUIRE(doc.SetNodePosition(sampler, 260.0f, 40.0f));

	CHECK(ValidateGraph(doc.GetGraph(), r).empty());

	GraphDocument reloaded;
	REQUIRE(reloaded.Load(doc.Save(), r, error));
	CHECK(reloaded.Save() == doc.Save());
	CHECK(ValidateGraph(reloaded.GetGraph(), r).empty());
	CHECK(reloaded.FindNode(sampler)->editorX == 260.0f);
	CHECK(reloaded.FindNode(sampler)->editorY == 40.0f);
	CHECK(reloaded.FindNode(sampler)->settings.at("minSpacing") == AttributeValue(String("$minSpacing")));
	CHECK(reloaded.FindNode(spawn)->settings.at("meshes") == AttributeValue(String("Models/Tree.hmsh|2;Models/Bush.hmsh|1")));
	CHECK(reloaded.ResolveLinks().size() == 2);
	REQUIRE(reloaded.GetGraph().parameters.size() == 1);
	CHECK(reloaded.GetGraph().parameters[0].defaultValue == AttributeValue(3.0f));
}

TEST_CASE("GraphDocument EvalRevision ignores positions and comments", "[graph_document]")
{
	const NodeRegistry r = MakeRegistry();
	GraphDocument doc;
	String error;
	REQUIRE(doc.Load(WriteGraph(Graph()), r, error));
	const uint32 a = doc.AddNode("PointSource", 0, 0);
	const uint32 sink = doc.AddNode("Sink", 0, 0);

	uint64 revision = doc.Revision();
	uint64 evalRevision = doc.EvalRevision();

	// Positions and comments: Revision only.
	REQUIRE(doc.SetNodePosition(a, 12.0f, 3.0f));
	CHECK(doc.Revision() > revision);
	CHECK(doc.EvalRevision() == evalRevision);
	revision = doc.Revision();

	REQUIRE(doc.SetNodeComment(a, "note"));
	CHECK(doc.FindNode(a)->comment == "note");
	CHECK(doc.Revision() > revision);
	CHECK(doc.EvalRevision() == evalRevision);
	revision = doc.Revision();
	CHECK_FALSE(doc.SetNodeComment(a, "note"));
	CHECK(doc.Revision() == revision);

	// Everything else: both.
	REQUIRE(doc.AddLink(Pin(doc, a, true, "Out"), Pin(doc, sink, false, "In")));
	CHECK(doc.Revision() > revision);
	CHECK(doc.EvalRevision() > evalRevision);
	revision = doc.Revision();
	evalRevision = doc.EvalRevision();

	REQUIRE(doc.SetSetting(a, "radius", 1.0f));
	CHECK(doc.Revision() > revision);
	CHECK(doc.EvalRevision() > evalRevision);
	revision = doc.Revision();
	evalRevision = doc.EvalRevision();

	doc.EditGraph();
	CHECK(doc.Revision() > revision);
	CHECK(doc.EvalRevision() > evalRevision);
}

TEST_CASE("GraphDocument SetNodeDebug bumps Revision only", "[graph_document]")
{
	const NodeRegistry r = MakeRegistry();
	GraphDocument doc;
	String error;
	REQUIRE(doc.Load(WriteGraph(Graph()), r, error));
	const uint32 a = doc.AddNode("PointSource", 0, 0);

	uint64 revision = doc.Revision();
	const uint64 evalRevision = doc.EvalRevision();

	doc.SetNodeDebug(a, true);
	CHECK(doc.FindNode(a)->debug);
	CHECK(doc.Revision() > revision);
	CHECK(doc.EvalRevision() == evalRevision);
	revision = doc.Revision();

	// No change, no bump.
	doc.SetNodeDebug(a, true);
	CHECK(doc.Revision() == revision);

	// Missing nodes are ignored.
	doc.SetNodeDebug(a + 100, true);
	CHECK(doc.Revision() == revision);

	doc.SetNodeDebug(a, false);
	CHECK_FALSE(doc.FindNode(a)->debug);
	CHECK(doc.Revision() > revision);
	CHECK(doc.EvalRevision() == evalRevision);
}
