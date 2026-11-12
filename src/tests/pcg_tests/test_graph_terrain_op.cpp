// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/graph/controls.h"
#include "pcg/graph/evaluator.h"
#include "pcg/graph/graph_output.h"
#include "pcg/graph/json_util.h"
#include "pcg/graph/node_registry.h"
#include "pcg/graph/terrain_op.h"

#include <memory>

using namespace mmo;
using namespace mmo::pcg::graph;

namespace
{
	DataItem MakeTerrainItem()
	{
		DataItem item;
		item.kind = ItemKind::Terrain;
		item.spline.points = { { 0, 0 }, { 10, 0 }, { 10, 10 } };
		item.pointIds = { 1, 2, 3 };
		item.op.type = TerrainOpType::SectionPath;
		item.op.asset = "Sections/Road.hsec";
		item.op.params["width"] = 4.0f;
		item.op.order = 7;
		return item;
	}

	uint64 HashOf(const DataItem& item)
	{
		return ContentHash(Collection{ item });
	}

	/// The first JSON object (depth first) that has the given key, or null.
	const jsonutil::Json* FindObjectWithKey(const jsonutil::Json& json, const char* key)
	{
		if (json.is_object() && json.contains(key))
		{
			return &json;
		}
		if (json.is_object() || json.is_array())
		{
			for (const auto& child : json)
			{
				if (const jsonutil::Json* found = FindObjectWithKey(child, key))
				{
					return found;
				}
			}
		}
		return nullptr;
	}
}

TEST_CASE("Equal terrain items hash equal", "[graph_terrain_op]")
{
	CHECK(HashOf(MakeTerrainItem()) == HashOf(MakeTerrainItem()));
}

TEST_CASE("Terrain item hash covers every op field, param, order and point id", "[graph_terrain_op]")
{
	const uint64 base = HashOf(MakeTerrainItem());

	SECTION("type")
	{
		DataItem item = MakeTerrainItem();
		item.op.type = TerrainOpType::Carve;
		CHECK(HashOf(item) != base);
	}
	SECTION("asset")
	{
		DataItem item = MakeTerrainItem();
		item.op.asset = "Sections/Other.hsec";
		CHECK(HashOf(item) != base);
	}
	SECTION("param value")
	{
		DataItem item = MakeTerrainItem();
		item.op.params["width"] = 5.0f;
		CHECK(HashOf(item) != base);
	}
	SECTION("extra param")
	{
		DataItem item = MakeTerrainItem();
		item.op.params["depth"] = 1.0f;
		CHECK(HashOf(item) != base);
	}
	SECTION("order")
	{
		DataItem item = MakeTerrainItem();
		item.op.order = 8;
		CHECK(HashOf(item) != base);
	}
	SECTION("stack order")
	{
		DataItem item = MakeTerrainItem();
		item.op.stackOrder = -1;
		CHECK(HashOf(item) != base);
		item.op.stackOrder = 1;
		CHECK(HashOf(item) != base);
	}
	SECTION("point id")
	{
		DataItem item = MakeTerrainItem();
		item.pointIds[1] = 9;
		CHECK(HashOf(item) != base);
	}
	SECTION("point id count")
	{
		DataItem item = MakeTerrainItem();
		item.pointIds.pop_back();
		CHECK(HashOf(item) != base);
	}
	SECTION("kind")
	{
		DataItem item = MakeTerrainItem();
		item.kind = ItemKind::Spline;
		CHECK(HashOf(item) != base);
	}
}

TEST_CASE("GetControls carries the control's point ids in order", "[graph_terrain_op]")
{
	ControlType type;
	type.name = "Road";
	type.shape = ControlShape::OpenSpline;

	ControlInstance inst;
	inst.id = 5;
	inst.typePath = "Controls/Road.hctl";
	inst.spline.closed = false;
	inst.spline.points = { { 0, 0 }, { 5, 0 }, { 10, 3 } };
	inst.pointIds = { 11, 4, 8 };
	inst.nextPointId = 12;

	ResolvedControl rc;
	std::vector<String> warnings;
	REQUIRE(ResolveControl(type, inst, rc, warnings));
	const std::vector<ResolvedControl> resolved{ rc };

	Graph g;
	NodeInstance n;
	n.id = 1;
	n.type = "GetControls";
	g.nodes.push_back(n);
	g.nextNodeId = 2;

	EvalContext ctx;
	ctx.controls = &resolved;
	const EvalResult result = Evaluate(g, NodeRegistry::Default(), ctx, nullptr);
	REQUIRE(result.ok);
	const Collection& out = result.nodes.at(1).output.pins.at("Out");
	REQUIRE(out.size() == 1);
	CHECK(out[0].pointIds == std::vector<uint64>{ 11, 4, 8 });
}

TEST_CASE("The eval dump names the Terrain kind and lists the op", "[graph_terrain_op]")
{
	NodeRegistry registry = NodeRegistry::Default();
	{
		NodeTypeDef def;
		def.type = "Test.Terrain";
		def.category = "Test";
		def.title = "Terrain";
		def.outputs.push_back(PinDef{ "Out", { ItemKind::Terrain } });
		def.execute = [](const EvalContext&, const uint64, const AttributeSet&, const std::map<String, Collection>&)
		{
			NodeOutput out;
			out.pins["Out"].push_back(MakeTerrainItem());
			return out;
		};
		registry.Register(def);
	}

	Graph g;
	NodeInstance n;
	n.id = 1;
	n.type = "Test.Terrain";
	g.nodes.push_back(n);
	g.nextNodeId = 2;

	const EvalResult result = Evaluate(g, registry, EvalContext{}, nullptr);
	REQUIRE(result.ok);
	const String dump = WriteEvalDump(g, result, false);
	CHECK(dump.find("\"Terrain\"") != String::npos);
	CHECK(dump.find("SectionPath") != String::npos);
	CHECK(dump.find("Sections/Road.hsec") != String::npos);
	CHECK(dump.find("width") != String::npos);

	// The item's point ids and the op order (stamped with the node id by the evaluator).
	const jsonutil::Json json = jsonutil::Json::parse(dump, nullptr, false);
	REQUIRE_FALSE(json.is_discarded());
	const jsonutil::Json* item = FindObjectWithKey(json, "op");
	REQUIRE(item != nullptr);
	REQUIRE(item->contains("pointIds"));
	CHECK(item->at("pointIds") == jsonutil::Json::array({ 1, 2, 3 }));
	REQUIRE(item->at("op").contains("order"));
	CHECK(item->at("op").at("order") == 1);
}

TEST_CASE("The eval dump writes the stack order only when it is not 0", "[graph_terrain_op]")
{
	for (const int32 stackOrder : { 0, -3, 12 })
	{
		NodeRegistry registry = NodeRegistry::Default();
		{
			NodeTypeDef def;
			def.type = "Test.Terrain";
			def.category = "Test";
			def.title = "Terrain";
			def.outputs.push_back(PinDef{ "Out", { ItemKind::Terrain } });
			def.execute = [stackOrder](const EvalContext&, const uint64, const AttributeSet&, const std::map<String, Collection>&)
			{
				NodeOutput out;
				DataItem item = MakeTerrainItem();
				item.op.stackOrder = stackOrder;
				out.pins["Out"].push_back(item);
				return out;
			};
			registry.Register(def);
		}

		Graph g;
		NodeInstance n;
		n.id = 1;
		n.type = "Test.Terrain";
		g.nodes.push_back(n);
		g.nextNodeId = 2;

		const EvalResult result = Evaluate(g, registry, EvalContext{}, nullptr);
		REQUIRE(result.ok);
		const String dump = WriteEvalDump(g, result, false);
		if (stackOrder == 0)
		{
			CHECK(dump.find("stackOrder") == String::npos);
		}
		else
		{
			const jsonutil::Json json = jsonutil::Json::parse(dump, nullptr, false);
			REQUIRE_FALSE(json.is_discarded());
			const jsonutil::Json* item = FindObjectWithKey(json, "op");
			REQUIRE(item != nullptr);
			REQUIRE(item->at("op").contains("stackOrder"));
			CHECK(item->at("op").at("stackOrder") == stackOrder);
		}
	}
}
