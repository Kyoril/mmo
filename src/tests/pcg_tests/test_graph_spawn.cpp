// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "graph_node_test_util.h"

#include "pcg/graph/evaluator.h"
#include "pcg/graph/graph_output.h"
#include "pcg/graph/node_registry.h"

#include <set>

using namespace mmo;
using namespace mmo::pcg::graph;

namespace
{
	DataItem ManyPoints(const size_t count)
	{
		std::vector<std::pair<float, float>> xz;
		for (size_t i = 0; i < count; ++i)
		{
			xz.emplace_back(static_cast<float>(i % 100), static_cast<float>(i / 100));
		}
		return MakePoints(xz);
	}

	NodeOutput Spawn(const AttributeSet& settings, const DataItem& points)
	{
		std::map<String, Collection> inputs;
		inputs["In"].push_back(points);
		return ExecNode("SpawnMeshes", settings, inputs, EvalContext());
	}

	String AssetOf(const DataItem& item, const size_t i)
	{
		const AttributeValue* v = item.points.GetAttribute(i, "asset");
		REQUIRE(v != nullptr);
		return std::get<String>(*v);
	}

	/// An EvalResult in which node `id` produced `items` on its Spawns pin.
	EvalResult MakeResult(const uint32 id, Collection items)
	{
		EvalResult result;
		result.ok = true;
		result.nodes[id].output.pins["Spawns"] = std::move(items);
		return result;
	}

	/// A spawn item with one asset per point.
	DataItem SpawnItem(const std::vector<std::pair<float, float>>& xz, const String& kind = "Foliage", const bool collides = true, const uint64 sourceId = 0)
	{
		DataItem item = MakePoints(xz);
		for (size_t i = 0; i < xz.size(); ++i)
		{
			item.points.SetAttribute(i, "asset", String("rock.hmsh"));
		}
		item.attributes["kind"] = kind;
		item.attributes["collides"] = collides;
		item.sourceId = sourceId;
		return item;
	}

	PathSpline Region10()
	{
		PathSpline region;
		region.points = { { 0, 0 }, { 10, 0 }, { 10, 10 }, { 0, 10 } };
		region.closed = true;
		return region;
	}
}

TEST_CASE("SpawnMeshes picks by weight, deterministically", "[graph_spawn]")
{
	AttributeSet settings;
	settings["meshes"] = String("a.hmsh|3;b.hmsh|1");
	const NodeOutput first = Spawn(settings, ManyPoints(4000));
	const NodeOutput second = Spawn(settings, ManyPoints(4000));

	REQUIRE(first.error.empty());
	REQUIRE(first.pins.at("Spawns").size() == 1);
	const DataItem& item = first.pins.at("Spawns")[0];
	REQUIRE(item.points.points.size() == 4000);

	size_t a = 0;
	size_t b = 0;
	for (size_t i = 0; i < item.points.points.size(); ++i)
	{
		const String asset = AssetOf(item, i);
		a += asset == "a.hmsh" ? 1 : 0;
		b += asset == "b.hmsh" ? 1 : 0;
	}
	REQUIRE(a + b == 4000);
	const double ratio = static_cast<double>(a) / static_cast<double>(b);
	CHECK(ratio > 2.7);
	CHECK(ratio < 3.3);

	CHECK(ContentHash(first.pins.at("Spawns")) == ContentHash(second.pins.at("Spawns")));
	CHECK(std::get<String>(item.attributes.at("kind")) == "Foliage");
	CHECK(std::get<bool>(item.attributes.at("collides")) == true);
}

TEST_CASE("SpawnMeshes assetAttribute overrides the pick", "[graph_spawn]")
{
	DataItem points = MakePoints({ { 0, 0 }, { 1, 0 }, { 2, 0 } });
	points.points.SetAttribute(1, "wanted", String("special.hmsh"));
	AttributeSet settings;
	settings["meshes"] = String("a.hmsh");
	settings["assetAttribute"] = String("wanted");

	const NodeOutput out = Spawn(settings, points);
	REQUIRE(out.pins.at("Spawns").size() == 1);
	const DataItem& item = out.pins.at("Spawns")[0];
	REQUIRE(item.points.points.size() == 3);
	CHECK(AssetOf(item, 0) == "a.hmsh");
	CHECK(AssetOf(item, 1) == "special.hmsh");
	CHECK(AssetOf(item, 2) == "a.hmsh");
}

TEST_CASE("SpawnMeshes with no meshes gives no output and one warning", "[graph_spawn]")
{
	const NodeOutput out = Spawn(AttributeSet(), ManyPoints(50));
	CHECK(out.error.empty());
	CHECK(CountPoints(out.pins.at("Spawns")) == 0);
	CHECK(out.warnings.size() == 1);

	AttributeSet zero;
	zero["meshes"] = String("a.hmsh|0");
	const NodeOutput zeroOut = Spawn(zero, ManyPoints(50));
	CHECK(CountPoints(zeroOut.pins.at("Spawns")) == 0);
	CHECK(zeroOut.warnings.size() == 1);
}

TEST_CASE("SpawnMeshes unknown kind is a validation issue", "[graph_spawn]")
{
	Graph g;
	NodeInstance node;
	node.id = 1;
	node.type = "SpawnMeshes";
	node.settings["kind"] = String("Banana");
	g.nodes.push_back(node);
	g.nextNodeId = 2;

	bool kindIssue = false;
	for (const GraphIssue& issue : ValidateGraph(g, NodeRegistry::Default()))
	{
		kindIssue = kindIssue || (issue.node == 1 && issue.message.find("kind") != String::npos);
	}
	CHECK(kindIssue);
}

TEST_CASE("SpawnMeshes settings declare asset widgets", "[graph_spawn]")
{
	const NodeTypeDef* def = NodeRegistry::Default().Find("SpawnMeshes");
	REQUIRE(def != nullptr);
	CHECK(def->category == "Spawners");
	bool found = false;
	for (const SettingDef& s : def->settings)
	{
		if (s.name == "meshes")
		{
			found = true;
			CHECK(s.widget == "assetList:.hmsh;.hwmo");
		}
	}
	CHECK(found);
}

TEST_CASE("CollectPlacements clips to the region", "[graph_spawn]")
{
	Graph g;
	const EvalResult result = MakeResult(3, { SpawnItem({ { 5, 5 }, { 20, 5 } }) });
	const CollectedPlacements c = CollectPlacements(g, result, Region10());
	REQUIRE(c.placements.size() == 1);
	CHECK(c.clipped == 1);
	CHECK(c.placements[0].position.x == 5.0f);
}

TEST_CASE("CollectPlacements edge rule: lower edges inside, upper edges outside", "[graph_spawn]")
{
	Graph g;
	// Same containment as the samplers (RowIndexedPolygon, even-odd, "x < crossX" and half-open in z):
	// x = min and z = min are inside, x = max and z = max are outside.
	const EvalResult result = MakeResult(1, { SpawnItem({ { 0, 5 }, { 10, 5 }, { 5, 0 }, { 5, 10 } }) });
	const CollectedPlacements c = CollectPlacements(g, result, Region10());
	REQUIRE(c.placements.size() == 2);
	CHECK(c.clipped == 2);

	std::set<std::pair<float, float>> kept;
	for (const pcg::Placement& p : c.placements)
	{
		kept.emplace(p.position.x, p.position.z);
	}
	CHECK(kept.count({ 0.0f, 5.0f }) == 1);
	CHECK(kept.count({ 5.0f, 0.0f }) == 1);

	// Agrees with the containment the sampler nodes use.
	const SampledPath path(Region10(), 0.25f);
	CHECK(path.Contains(0.0f, 5.0f));
	CHECK_FALSE(path.Contains(10.0f, 5.0f));
	CHECK(path.Contains(5.0f, 0.0f));
	CHECK_FALSE(path.Contains(5.0f, 10.0f));
}

TEST_CASE("CollectPlacements keys are stable when an unrelated point is removed", "[graph_spawn]")
{
	Graph g;
	const CollectedPlacements full = CollectPlacements(g, MakeResult(2, { SpawnItem({ { 1, 1 }, { 2, 2 }, { 3, 3 } }) }), Region10());
	DataItem reduced = SpawnItem({ { 1, 1 }, { 2, 2 }, { 3, 3 } });
	reduced.points.Keep({ 1, 0, 1 });
	const CollectedPlacements fewer = CollectPlacements(g, MakeResult(2, { reduced }), Region10());

	REQUIRE(full.placements.size() == 3);
	REQUIRE(fewer.placements.size() == 2);
	std::set<uint64> fullKeys;
	for (const pcg::Placement& p : full.placements)
	{
		fullKeys.insert(p.key);
	}
	for (const pcg::Placement& p : fewer.placements)
	{
		CHECK(fullKeys.count(p.key) == 1);
	}
	for (size_t i = 1; i < full.placements.size(); ++i)
	{
		CHECK(full.placements[i - 1].key < full.placements[i].key);
	}
}

TEST_CASE("CollectPlacements resolves identical points to distinct keys", "[graph_spawn]")
{
	Graph g;
	DataItem item = SpawnItem({ { 4, 4 }, { 4, 4 } });
	item.points.points[1].seed = item.points.points[0].seed;
	const CollectedPlacements c = CollectPlacements(g, MakeResult(1, { item }), Region10());
	REQUIRE(c.placements.size() == 2);
	CHECK(c.placements[0].key != c.placements[1].key);
	CHECK(c.warnings.size() == 1);
}

TEST_CASE("CollectPlacements maps kind, collides and the transform", "[graph_spawn]")
{
	Graph g;
	DataItem item = SpawnItem({ { 2, 3 } }, "WorldModel", false);
	item.points.points[0].position.y = 7.0f;
	item.points.points[0].scale = Vector3(2, 3, 4);
	item.points.SetAttribute(0, "asset", String("hall.hwmo"));

	const CollectedPlacements c = CollectPlacements(g, MakeResult(1, { item }), Region10());
	REQUIRE(c.placements.size() == 1);
	const pcg::Placement& p = c.placements[0];
	CHECK(p.kind == pcg::PlacementKind::WorldModel);
	CHECK(p.collides == false);
	CHECK(p.asset == "hall.hwmo");
	CHECK(p.position == Vector3(2, 7, 3));
	CHECK(p.scale == Vector3(2, 3, 4));

	const CollectedPlacements entity = CollectPlacements(g, MakeResult(1, { SpawnItem({ { 2, 3 } }, "Entity") }), Region10());
	REQUIRE(entity.placements.size() == 1);
	CHECK(entity.placements[0].kind == pcg::PlacementKind::Entity);
	CHECK(entity.placements[0].collides == true);
}

TEST_CASE("CollectPlacements skips items with an unknown kind", "[graph_spawn]")
{
	Graph g;
	const CollectedPlacements c = CollectPlacements(g, MakeResult(1, { SpawnItem({ { 2, 3 } }, "Banana") }), Region10());
	CHECK(c.placements.empty());
	CHECK(c.warnings.size() == 1);
}

TEST_CASE("WriteEvalDump is byte-identical across evaluations", "[graph_spawn]")
{
	NodeRegistry registry = NodeRegistry::Default();
	{
		NodeTypeDef def;
		def.type = "Test.Grid";
		def.category = "Test";
		def.title = "Grid";
		def.outputs.push_back(PinDef{ "Out", { ItemKind::Points } });
		def.execute = [](const EvalContext&, const uint64, const AttributeSet&, const std::map<String, Collection>&)
		{
			NodeOutput out;
			out.pins["Out"].push_back(ManyPoints(40));
			return out;
		};
		registry.Register(def);
	}

	Graph g;
	NodeInstance source;
	source.id = 1;
	source.type = "Test.Grid";
	NodeInstance spawn;
	spawn.id = 2;
	spawn.type = "SpawnMeshes";
	spawn.settings["meshes"] = String("a.hmsh|1;b.hmsh|1");
	g.nodes = { source, spawn };
	g.links.push_back(Link{ 1, "Out", 2, "In" });
	g.nextNodeId = 3;

	EvalContext ctx;
	ctx.region = Region10();
	const EvalResult first = Evaluate(g, registry, ctx, nullptr);
	const EvalResult second = Evaluate(g, registry, ctx, nullptr);
	REQUIRE(first.ok);

	const String a = WriteEvalDump(g, first, false);
	const String b = WriteEvalDump(g, second, false);
	CHECK(a == b);
	CHECK(a.find("SpawnMeshes") != String::npos);
	CHECK(a.find("\"ms\"") == String::npos);
	CHECK(WriteEvalDump(g, first, true).find("\"ms\"") != String::npos);

	const CollectedPlacements c = CollectPlacements(g, first, Region10());
	CHECK(c.placements.size() + c.clipped == 40);
}

TEST_CASE("CollectPlacements warns when a Spawns pin is linked onward", "[graph_spawn]")
{
	// "Spawns" pins are terminal: their items are collected as the node emits them, whatever downstream nodes do.
	Graph g;
	g.links.push_back(Link{ 1, "Spawns", 2, "In" });
	g.links.push_back(Link{ 1, "Spawns", 3, "In" });
	g.links.push_back(Link{ 2, "Out", 3, "Other" });

	const CollectedPlacements c = CollectPlacements(g, MakeResult(1, { SpawnItem({ { 4, 4 } }) }), Region10());
	REQUIRE(c.placements.size() == 1);
	REQUIRE(c.warnings.size() == 1);
	CHECK(c.warnings[0].find("node 1") != String::npos);
	CHECK(c.warnings[0].find("terminal") != String::npos);

	Graph unlinked;
	unlinked.links.push_back(Link{ 2, "Out", 1, "In" });
	CHECK(CollectPlacements(unlinked, MakeResult(1, { SpawnItem({ { 4, 4 } }) }), Region10()).warnings.empty());
}
