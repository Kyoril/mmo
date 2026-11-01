// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/graph/evaluator.h"
#include "pcg/graph/node_registry.h"
#include "pcg/pcg_random.h"

#include <map>
#include <memory>

using namespace mmo;
using namespace mmo::pcg::graph;

namespace
{
	/// Execution counter per node id, shared by the test node types.
	using ExecLog = std::map<uint32, int>;

	/// Test node types. Every executor records the node it ran for (the "id" setting carries it).
	NodeRegistry MakeRegistry(std::shared_ptr<ExecLog> log, std::shared_ptr<std::vector<uint32>> order)
	{
		NodeRegistry r;
		const auto record = [log, order](const AttributeSet& settings)
		{
			const auto it = settings.find("id");
			const uint32 id = it != settings.end() ? static_cast<uint32>(std::get<int64>(it->second)) : 0;
			(*log)[id]++;
			order->push_back(id);
		};

		// Source: no input, emits `count` points at x = 0..count-1 plus a seed-derived attribute.
		{
			NodeTypeDef def;
			def.type = "Test.Source";
			def.category = "Test";
			def.title = "Source";
			def.outputs.push_back(PinDef{ "Out", { ItemKind::Points } });
			def.settings.push_back(SettingDef{ "count", int64(2) });
			def.settings.push_back(SettingDef{ "id", int64(0) });
			def.execute = [record](const EvalContext&, const uint64 seed, const AttributeSet& settings, const std::map<String, Collection>&)
			{
				record(settings);
				NodeOutput out;
				DataItem item;
				item.kind = ItemKind::Points;
				const int64 count = std::get<int64>(settings.at("count"));
				for (int64 i = 0; i < count; ++i)
				{
					Point p;
					p.position = Vector3(static_cast<float>(i), 0, 0);
					p.seed = seed;
					item.points.points.push_back(p);
				}
				out.pins["Out"].push_back(item);
				return out;
			};
			r.Register(def);
		}

		// Offset: moves every point of its (required) input by `dx`.
		{
			NodeTypeDef def;
			def.type = "Test.Offset";
			def.category = "Test";
			def.title = "Offset";
			def.inputs.push_back(PinDef{ "In", { ItemKind::Points }, true, true });
			def.outputs.push_back(PinDef{ "Out", { ItemKind::Points } });
			def.settings.push_back(SettingDef{ "dx", 1.0f });
			def.settings.push_back(SettingDef{ "id", int64(0) });
			def.execute = [record](const EvalContext&, uint64, const AttributeSet& settings, const std::map<String, Collection>& inputs)
			{
				record(settings);
				NodeOutput out;
				Collection c = inputs.at("In");
				for (DataItem& item : c)
				{
					for (Point& p : item.points.points)
					{
						p.position.x += std::get<float>(settings.at("dx"));
					}
				}
				out.pins["Out"] = c;
				return out;
			};
			r.Register(def);
		}

		// Params: reads ctx.parameters (readsContext) and emits them as a Params item.
		{
			NodeTypeDef def;
			def.type = "Test.Params";
			def.category = "Test";
			def.title = "Params";
			def.outputs.push_back(PinDef{ "Out", { ItemKind::Params } });
			def.settings.push_back(SettingDef{ "id", int64(0) });
			def.readsContext = true;
			def.execute = [record](const EvalContext& ctx, uint64, const AttributeSet& settings, const std::map<String, Collection>&)
			{
				record(settings);
				NodeOutput out;
				DataItem item;
				item.kind = ItemKind::Params;
				item.attributes = ctx.parameters;
				out.pins["Out"].push_back(item);
				return out;
			};
			r.Register(def);
		}

		// Combine: accepts anything on an optional multi-input and passes it through.
		{
			NodeTypeDef def;
			def.type = "Test.Combine";
			def.category = "Test";
			def.title = "Combine";
			def.inputs.push_back(PinDef{ "In", {} });
			def.outputs.push_back(PinDef{ "Out", {} });
			def.settings.push_back(SettingDef{ "id", int64(0) });
			def.execute = [record](const EvalContext&, uint64, const AttributeSet& settings, const std::map<String, Collection>& inputs)
			{
				record(settings);
				NodeOutput out;
				const auto it = inputs.find("In");
				out.pins["Out"] = it != inputs.end() ? it->second : Collection();
				return out;
			};
			r.Register(def);
		}

		// Spline: emits a spline (for kind mismatch tests).
		{
			NodeTypeDef def;
			def.type = "Test.Spline";
			def.category = "Test";
			def.title = "Spline";
			def.outputs.push_back(PinDef{ "Out", { ItemKind::Spline } });
			def.settings.push_back(SettingDef{ "id", int64(0) });
			def.execute = [record](const EvalContext&, uint64, const AttributeSet& settings, const std::map<String, Collection>&)
			{
				record(settings);
				NodeOutput out;
				DataItem item;
				item.kind = ItemKind::Spline;
				out.pins["Out"].push_back(item);
				return out;
			};
			r.Register(def);
		}

		// Fail: always reports an error.
		{
			NodeTypeDef def;
			def.type = "Test.Fail";
			def.category = "Test";
			def.title = "Fail";
			def.inputs.push_back(PinDef{ "In", {} });
			def.outputs.push_back(PinDef{ "Out", { ItemKind::Points } });
			def.settings.push_back(SettingDef{ "id", int64(0) });
			def.execute = [record](const EvalContext&, uint64, const AttributeSet& settings, const std::map<String, Collection>&)
			{
				record(settings);
				NodeOutput out;
				out.error = "boom";
				return out;
			};
			r.Register(def);
		}

		return r;
	}

	NodeInstance Node(const uint32 id, const String& type)
	{
		NodeInstance n;
		n.id = id;
		n.type = type;
		n.settings["id"] = int64(id);
		return n;
	}

	void Connect(Graph& g, const uint32 from, const uint32 to, const String& toPin = "In")
	{
		g.links.push_back(Link{ from, "Out", to, toPin });
	}

	struct Fixture
	{
		std::shared_ptr<ExecLog> log = std::make_shared<ExecLog>();
		std::shared_ptr<std::vector<uint32>> order = std::make_shared<std::vector<uint32>>();
		NodeRegistry registry = MakeRegistry(log, order);

		void Reset()
		{
			log->clear();
			order->clear();
		}

		int Runs(const uint32 id) const
		{
			const auto it = log->find(id);
			return it != log->end() ? it->second : 0;
		}
	};

	struct FlatTerrain final : ITerrainSampler
	{
		uint64 revision = 1;

		bool Height(float, float, float& h) const override
		{
			h = 0;
			return true;
		}

		Vector3 Normal(float, float) const override
		{
			return Vector3(0, 1, 0);
		}

		float SplatWeight(const String&, float, float) const override
		{
			return 0;
		}

		bool WaterLevel(float, float, float&) const override
		{
			return false;
		}

		uint64 Revision() const override
		{
			return revision;
		}
	};

	bool HasIssueFor(const std::vector<GraphIssue>& issues, const uint32 node)
	{
		for (const GraphIssue& i : issues)
		{
			if (i.node == node)
			{
				return true;
			}
		}
		return false;
	}

	/// Two branches: 1 Source -> 2 Offset -> 5 Combine; 3 Params -> 4 Combine -> 5 Combine; 6 Source -> 7 Offset (sibling).
	Graph MakeBranchyGraph()
	{
		Graph g;
		g.nodes.push_back(Node(1, "Test.Source"));
		g.nodes.push_back(Node(2, "Test.Offset"));
		g.nodes.push_back(Node(3, "Test.Params"));
		g.nodes.push_back(Node(4, "Test.Combine"));
		g.nodes.push_back(Node(5, "Test.Combine"));
		g.nodes.push_back(Node(6, "Test.Source"));
		g.nodes.push_back(Node(7, "Test.Offset"));
		g.nextNodeId = 8;
		Connect(g, 1, 2);
		Connect(g, 2, 5);
		Connect(g, 3, 4);
		Connect(g, 4, 5);
		Connect(g, 6, 7);
		return g;
	}
}

TEST_CASE("Registry finds, lists sorted and default exists", "[graph_eval]")
{
	Fixture f;
	REQUIRE(f.registry.Find("Test.Source") != nullptr);
	CHECK(f.registry.Find("Nope") == nullptr);

	const auto all = f.registry.All();
	REQUIRE(all.size() == 6);
	CHECK(all[0]->title == "Combine");
	CHECK(all[5]->title == "Spline");

	// Re-registering a type replaces it.
	NodeTypeDef def;
	def.type = "Test.Source";
	def.category = "Test";
	def.title = "Zeta";
	f.registry.Register(def);
	CHECK(f.registry.All().size() == 6);
	CHECK(f.registry.Find("Test.Source")->title == "Zeta");

	// The default registry is a singleton.
	CHECK(&NodeRegistry::Default() == &NodeRegistry::Default());
}

TEST_CASE("Chain A->B->C evaluates in order with inputs and defaults", "[graph_eval]")
{
	Fixture f;
	Graph g;
	g.nodes.push_back(Node(3, "Test.Offset"));
	g.nodes.push_back(Node(1, "Test.Source"));
	g.nodes.push_back(Node(2, "Test.Offset"));
	g.nodes[0].settings["dx"] = 10.0f;
	Connect(g, 1, 2);
	Connect(g, 2, 3);

	EvalContext ctx;
	const EvalResult result = Evaluate(g, f.registry, ctx, nullptr);
	REQUIRE(result.ok);
	CHECK(result.issues.empty());
	REQUIRE(*f.order == std::vector<uint32>{ 1, 2, 3 });

	const Collection& out = result.nodes.at(3).output.pins.at("Out");
	REQUIRE(out.size() == 1);
	REQUIRE(out[0].points.points.size() == 2);
	// Source default count = 2; node 2 default dx = 1; node 3 dx = 10.
	CHECK(out[0].points.points[0].position.x == Approx(11.0f));
	CHECK(out[0].points.points[1].position.x == Approx(12.0f));

	// Node seed = Hash(instanceSeed, node id).
	CHECK(out[0].points.points[0].seed == pcg::Hash(ctx.instanceSeed, 1));
}

TEST_CASE("Inputs of a pin concatenate linked outputs in link order", "[graph_eval]")
{
	Fixture f;
	Graph g;
	g.nodes.push_back(Node(1, "Test.Source"));
	g.nodes.push_back(Node(2, "Test.Params"));
	g.nodes.push_back(Node(3, "Test.Combine"));
	Connect(g, 2, 3);
	Connect(g, 1, 3);

	const EvalResult result = Evaluate(g, f.registry, EvalContext(), nullptr);
	REQUIRE(result.ok);
	const Collection& out = result.nodes.at(3).output.pins.at("Out");
	REQUIRE(out.size() == 2);
	CHECK(out[0].kind == ItemKind::Params);
	CHECK(out[1].kind == ItemKind::Points);
}

TEST_CASE("Cycle is reported and nothing executes", "[graph_eval]")
{
	Fixture f;
	Graph g;
	g.nodes.push_back(Node(1, "Test.Source"));
	g.nodes.push_back(Node(2, "Test.Combine"));
	g.nodes.push_back(Node(3, "Test.Combine"));
	Connect(g, 1, 2);
	Connect(g, 3, 2);
	Connect(g, 2, 3);

	const auto issues = ValidateGraph(g, f.registry);
	CHECK(HasIssueFor(issues, 2));
	CHECK(HasIssueFor(issues, 3));
	CHECK_FALSE(HasIssueFor(issues, 1));

	const EvalResult result = Evaluate(g, f.registry, EvalContext(), nullptr);
	CHECK_FALSE(result.ok);
	CHECK_FALSE(result.issues.empty());
	CHECK(result.nodes.empty());
	CHECK(f.log->empty());
}

TEST_CASE("Self link is a cycle", "[graph_eval]")
{
	Fixture f;
	Graph g;
	g.nodes.push_back(Node(1, "Test.Combine"));
	Connect(g, 1, 1);
	CHECK(HasIssueFor(ValidateGraph(g, f.registry), 1));
}

TEST_CASE("Kind mismatch is reported", "[graph_eval]")
{
	Fixture f;
	Graph g;
	g.nodes.push_back(Node(1, "Test.Spline"));
	g.nodes.push_back(Node(2, "Test.Offset"));
	Connect(g, 1, 2);
	const auto issues = ValidateGraph(g, f.registry);
	CHECK(HasIssueFor(issues, 2));

	const EvalResult result = Evaluate(g, f.registry, EvalContext(), nullptr);
	CHECK_FALSE(result.ok);
	CHECK(f.log->empty());
}

TEST_CASE("Missing required input is reported", "[graph_eval]")
{
	Fixture f;
	Graph g;
	g.nodes.push_back(Node(1, "Test.Offset"));
	CHECK(HasIssueFor(ValidateGraph(g, f.registry), 1));
	CHECK_FALSE(Evaluate(g, f.registry, EvalContext(), nullptr).ok);
	CHECK(f.log->empty());
}

TEST_CASE("Unknown types, pins, dangling links, duplicate ids and setting types are reported", "[graph_eval]")
{
	Fixture f;

	SECTION("unknown node type")
	{
		Graph g;
		g.nodes.push_back(Node(1, "Test.DoesNotExist"));
		CHECK(HasIssueFor(ValidateGraph(g, f.registry), 1));
	}
	SECTION("unknown pins")
	{
		Graph g;
		g.nodes.push_back(Node(1, "Test.Source"));
		g.nodes.push_back(Node(2, "Test.Combine"));
		g.links.push_back(Link{ 1, "Nope", 2, "In" });
		g.links.push_back(Link{ 1, "Out", 2, "Nope" });
		const auto issues = ValidateGraph(g, f.registry);
		CHECK(HasIssueFor(issues, 1));
		CHECK(HasIssueFor(issues, 2));
	}
	SECTION("dangling link")
	{
		Graph g;
		g.nodes.push_back(Node(2, "Test.Combine"));
		Connect(g, 9, 2);
		CHECK_FALSE(ValidateGraph(g, f.registry).empty());
	}
	SECTION("duplicate and zero ids")
	{
		Graph g;
		g.nodes.push_back(Node(1, "Test.Source"));
		g.nodes.push_back(Node(1, "Test.Source"));
		g.nodes.push_back(Node(0, "Test.Source"));
		const auto issues = ValidateGraph(g, f.registry);
		CHECK(HasIssueFor(issues, 1));
		CHECK(HasIssueFor(issues, 0));
	}
	SECTION("setting type mismatch")
	{
		Graph g;
		g.nodes.push_back(Node(1, "Test.Source"));
		g.nodes[0].settings["count"] = String("three");
		CHECK(HasIssueFor(ValidateGraph(g, f.registry), 1));
	}
	SECTION("single input pin with two links")
	{
		NodeTypeDef def;
		def.type = "Test.Single";
		def.inputs.push_back(PinDef{ "In", {}, false });
		f.registry.Register(def);
		Graph g;
		g.nodes.push_back(Node(1, "Test.Source"));
		g.nodes.push_back(Node(2, "Test.Source"));
		g.nodes.push_back(Node(3, "Test.Single"));
		Connect(g, 1, 3);
		Connect(g, 2, 3);
		CHECK(HasIssueFor(ValidateGraph(g, f.registry), 3));
	}
	SECTION("valid graph has no issues")
	{
		CHECK(ValidateGraph(MakeBranchyGraph(), f.registry).empty());
	}
}

TEST_CASE("Deterministic re-run gives equal hashes", "[graph_eval]")
{
	Fixture f;
	const Graph g = MakeBranchyGraph();
	EvalContext ctx;
	ctx.instanceSeed = 77;
	ctx.parameters["p"] = 1.0f;

	const EvalResult a = Evaluate(g, f.registry, ctx, nullptr);
	const EvalResult b = Evaluate(g, f.registry, ctx, nullptr);
	REQUIRE(a.ok);
	REQUIRE(b.ok);
	REQUIRE(a.nodes.size() == 7);
	for (const auto& [id, r] : a.nodes)
	{
		CHECK(r.hash != 0);
		CHECK(r.hash == b.nodes.at(id).hash);
		CHECK_FALSE(r.fromCache);
	}

	// A different instance seed changes seed-dependent outputs.
	ctx.instanceSeed = 78;
	const EvalResult c = Evaluate(g, f.registry, ctx, nullptr);
	CHECK(c.nodes.at(1).hash != a.nodes.at(1).hash);
}

TEST_CASE("Cache serves unchanged nodes and re-runs exactly the affected ones", "[graph_eval]")
{
	Fixture f;
	Graph g = MakeBranchyGraph();
	EvalContext ctx;
	ctx.parameters["p"] = 1.0f;
	GraphCache cache;

	const EvalResult first = Evaluate(g, f.registry, ctx, &cache);
	REQUIRE(first.ok);
	CHECK(f.log->size() == 7);
	CHECK(cache.Size() == 7);

	SECTION("no change: everything from cache, nothing executes")
	{
		f.Reset();
		const EvalResult second = Evaluate(g, f.registry, ctx, &cache);
		REQUIRE(second.ok);
		CHECK(f.log->empty());
		for (const auto& [id, r] : second.nodes)
		{
			CHECK(r.fromCache);
			CHECK(r.hash == first.nodes.at(id).hash);
			CHECK(ContentHash(r.output.pins.at("Out")) == ContentHash(first.nodes.at(id).output.pins.at("Out")));
		}
	}

	SECTION("changing one node's setting re-runs it and its downstream, siblings cached")
	{
		f.Reset();
		g.nodes[1].settings["dx"] = 5.0f;   // node 2
		const EvalResult second = Evaluate(g, f.registry, ctx, &cache);
		REQUIRE(second.ok);
		CHECK(f.Runs(2) == 1);
		CHECK(f.Runs(5) == 1);
		CHECK(f.log->size() == 2);
		CHECK(second.nodes.at(1).fromCache);
		CHECK(second.nodes.at(3).fromCache);
		CHECK(second.nodes.at(4).fromCache);
		CHECK(second.nodes.at(6).fromCache);
		CHECK(second.nodes.at(7).fromCache);
		CHECK_FALSE(second.nodes.at(2).fromCache);
		CHECK_FALSE(second.nodes.at(5).fromCache);
	}

	SECTION("setting a value equal to the default does not invalidate")
	{
		f.Reset();
		g.nodes[1].settings["dx"] = 1.0f;   // node 2, default 1
		const EvalResult second = Evaluate(g, f.registry, ctx, &cache);
		CHECK(f.log->empty());
	}

	SECTION("a re-run producing identical output keeps downstream cached")
	{
		// Editor-only fields (comment, canvas position) are not part of the key.
		f.Reset();
		g.nodes[0].comment = "changed";
		g.nodes[0].editorX = 100;
		Evaluate(g, f.registry, ctx, &cache);
		CHECK(f.log->empty());

		// An extra setting the node ignores re-runs node 4, but its output hash is unchanged.
		f.Reset();
		g.nodes[3].settings["extra"] = int64(1);
		const EvalResult second = Evaluate(g, f.registry, ctx, &cache);
		CHECK(f.Runs(4) == 1);
		CHECK(f.Runs(5) == 0);   // node 4's output is unchanged, so node 5 is served from cache
		CHECK(second.nodes.at(5).fromCache);
	}

	SECTION("changing ctx.parameters re-runs only readsContext nodes and their downstream")
	{
		f.Reset();
		ctx.parameters["p"] = 2.0f;
		const EvalResult second = Evaluate(g, f.registry, ctx, &cache);
		REQUIRE(second.ok);
		CHECK(f.Runs(3) == 1);
		CHECK(f.Runs(4) == 1);
		CHECK(f.Runs(5) == 1);
		CHECK(f.log->size() == 3);
		CHECK(second.nodes.at(1).fromCache);
		CHECK(second.nodes.at(2).fromCache);
		CHECK(second.nodes.at(6).fromCache);
		CHECK(second.nodes.at(7).fromCache);
	}

	SECTION("changing the controls revision, terrain or region invalidates readsContext nodes only")
	{
		f.Reset();
		ctx.controlsRevision = 5;
		Evaluate(g, f.registry, ctx, &cache);
		// Node 3 re-runs, but its output (the parameters) is unchanged, so its downstream stays cached.
		CHECK(f.Runs(3) == 1);
		CHECK(f.log->size() == 1);

		f.Reset();
		ctx.region.points.push_back(SplinePoint());
		Evaluate(g, f.registry, ctx, &cache);
		CHECK(f.Runs(3) == 1);
		CHECK(f.log->size() == 1);

		FlatTerrain terrain;
		f.Reset();
		ctx.terrain = &terrain;
		Evaluate(g, f.registry, ctx, &cache);
		CHECK(f.Runs(3) == 1);
		CHECK(f.log->size() == 1);

		f.Reset();
		terrain.revision = 2;
		Evaluate(g, f.registry, ctx, &cache);
		CHECK(f.Runs(3) == 1);
		CHECK(f.log->size() == 1);

		f.Reset();
		Evaluate(g, f.registry, ctx, &cache);
		CHECK(f.log->empty());
	}

	SECTION("changing the instance seed re-runs everything")
	{
		f.Reset();
		ctx.instanceSeed = 99;
		Evaluate(g, f.registry, ctx, &cache);
		CHECK(f.log->size() == 7);
	}

	SECTION("Clear empties the cache")
	{
		cache.Clear();
		CHECK(cache.Size() == 0);
		f.Reset();
		Evaluate(g, f.registry, ctx, &cache);
		CHECK(f.log->size() == 7);
	}

	SECTION("removed nodes are dropped from the cache")
	{
		g.nodes.pop_back();   // node 7
		g.links.pop_back();
		Evaluate(g, f.registry, ctx, &cache);
		CHECK(cache.Size() == 6);
	}
}

TEST_CASE("Node error stops downstream nodes", "[graph_eval]")
{
	Fixture f;
	Graph g;
	g.nodes.push_back(Node(1, "Test.Source"));
	g.nodes.push_back(Node(2, "Test.Fail"));
	g.nodes.push_back(Node(3, "Test.Offset"));
	g.nodes.push_back(Node(4, "Test.Combine"));
	g.nodes.push_back(Node(5, "Test.Offset"));
	Connect(g, 1, 2);
	Connect(g, 2, 3);
	Connect(g, 3, 4);
	Connect(g, 1, 5);

	GraphCache cache;
	const EvalResult result = Evaluate(g, f.registry, EvalContext(), &cache);
	CHECK(result.ok);   // the graph itself is valid; node errors are per node
	CHECK(result.nodes.at(2).output.error == "boom");
	CHECK(result.nodes.at(3).output.error == "upstream failed");
	CHECK(result.nodes.at(4).output.error == "upstream failed");
	CHECK(result.nodes.at(5).output.error.empty());
	CHECK(f.Runs(2) == 1);
	CHECK(f.Runs(3) == 0);
	CHECK(f.Runs(4) == 0);
	CHECK(f.Runs(5) == 1);

	// Failed nodes are not cached: they run again; healthy nodes come from cache.
	f.Reset();
	Evaluate(g, f.registry, EvalContext(), &cache);
	CHECK(f.Runs(2) == 1);
	CHECK(f.Runs(1) == 0);
	CHECK(f.Runs(5) == 0);
}

TEST_CASE("Declared output pins always exist, undeclared ones are dropped with a warning", "[graph_eval]")
{
	Fixture f;
	NodeTypeDef def;
	def.type = "Test.Odd";
	def.outputs.push_back(PinDef{ "A", {} });
	def.outputs.push_back(PinDef{ "B", {} });
	def.execute = [](const EvalContext&, uint64, const AttributeSet&, const std::map<String, Collection>&)
	{
		NodeOutput out;
		out.pins["A"].push_back(DataItem());
		out.pins["Stray"].push_back(DataItem());
		return out;
	};
	f.registry.Register(def);

	Graph g;
	g.nodes.push_back(Node(1, "Test.Odd"));
	const EvalResult result = Evaluate(g, f.registry, EvalContext(), nullptr);
	REQUIRE(result.ok);
	const NodeOutput& out = result.nodes.at(1).output;
	CHECK(out.pins.size() == 2);
	CHECK(out.pins.at("A").size() == 1);
	CHECK(out.pins.at("B").empty());
	CHECK(out.warnings.size() == 1);
}
