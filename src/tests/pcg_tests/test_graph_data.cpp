// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/graph/graph_data.h"

using namespace mmo;
using namespace mmo::pcg::graph;

namespace
{
	Collection MakeCollection()
	{
		DataItem item;
		item.kind = ItemKind::Points;
		item.tags.insert("tree");
		item.attributes["radius"] = 2.5f;
		item.sourceId = 7;
		item.points.points.resize(3);
		item.points.points[1].position = Vector3(1, 2, 3);
		item.points.SetAttribute(0, "kind", String("oak"));
		item.points.SetAttribute(2, "kind", String("pine"));

		DataItem spline;
		spline.kind = ItemKind::Spline;
		spline.spline.points.resize(2);
		spline.spline.points[1].x = 10.0f;
		return { item, spline };
	}
}

TEST_CASE("SetAttribute creates a column and refuses a type change", "[graph_data]")
{
	PointSet set;
	set.points.resize(3);

	CHECK(set.SetAttribute(1, "h", 4.0f));
	REQUIRE(set.columns.count("h") == 1);
	CHECK(set.columns["h"].first == AttributeType::Float);
	CHECK(set.columns["h"].second.size() == 3);
	CHECK(std::get<float>(*set.GetAttribute(1, "h")) == 4.0f);
	CHECK(std::get<float>(*set.GetAttribute(0, "h")) == 0.0f);

	CHECK_FALSE(set.SetAttribute(0, "h", String("x")));
	CHECK_FALSE(set.SetAttribute(5, "h", 1.0f));
	CHECK(set.GetAttribute(0, "missing") == nullptr);
	CHECK(set.GetAttribute(9, "h") == nullptr);
	CHECK(TypeOf(AttributeValue(int64(3))) == AttributeType::Int);
	CHECK(TypeOf(AttributeValue(Vector3())) == AttributeType::Vector3);
}

TEST_CASE("Append merges columns with defaults", "[graph_data]")
{
	PointSet a;
	a.points.resize(2);
	a.SetAttribute(0, "a", int64(5));

	PointSet b;
	b.points.resize(1);
	b.SetAttribute(0, "b", String("x"));

	a.Append(b);
	REQUIRE(a.points.size() == 3);
	CHECK(a.columns["a"].second.size() == 3);
	CHECK(a.columns["b"].second.size() == 3);
	CHECK(std::get<int64>(*a.GetAttribute(0, "a")) == 5);
	CHECK(std::get<int64>(*a.GetAttribute(2, "a")) == 0);
	CHECK(std::get<String>(*a.GetAttribute(0, "b")).empty());
	CHECK(std::get<String>(*a.GetAttribute(2, "b")) == "x");
}

TEST_CASE("Keep preserves order and columns", "[graph_data]")
{
	PointSet set;
	set.points.resize(4);
	for (size_t i = 0; i < 4; ++i)
	{
		set.points[i].seed = i;
		set.SetAttribute(i, "n", static_cast<int64>(i * 10));
	}

	set.Keep({ 1, 0, 1, 1 });
	REQUIRE(set.points.size() == 3);
	CHECK(set.points[0].seed == 0);
	CHECK(set.points[1].seed == 2);
	CHECK(set.points[2].seed == 3);
	REQUIRE(set.columns.count("n") == 1);
	CHECK(std::get<int64>(*set.GetAttribute(1, "n")) == 20);
	CHECK(std::get<int64>(*set.GetAttribute(2, "n")) == 30);
}

TEST_CASE("ContentHash is stable and sensitive to every field", "[graph_data]")
{
	const Collection base = MakeCollection();
	CHECK(ContentHash(base) == ContentHash(MakeCollection()));
	CHECK(ContentHash(Collection()) == ContentHash(Collection()));
	CHECK(ContentHash(Collection()) != ContentHash(base));

	const uint64 h = ContentHash(base);
	auto differs = [&](auto&& mutate)
	{
		Collection c = MakeCollection();
		mutate(c);
		return ContentHash(c) != h;
	};

	CHECK(differs([](Collection& c) { c[0].points.points[1].position.x += 0.1f; }));
	CHECK(differs([](Collection& c) { c[0].points.points[0].rotation = Quaternion(0, 1, 0, 0); }));
	CHECK(differs([](Collection& c) { c[0].points.points[0].scale.y = 2.0f; }));
	CHECK(differs([](Collection& c) { c[0].points.points[0].density = 0.5f; }));
	CHECK(differs([](Collection& c) { c[0].points.points[0].seed = 99; }));
	CHECK(differs([](Collection& c) { c[0].points.points[0].boundsMin.z = -2.0f; }));
	CHECK(differs([](Collection& c) { c[0].points.points[0].boundsMax.x = 2.0f; }));
	CHECK(differs([](Collection& c) { c[0].points.SetAttribute(1, "kind", String("elm")); }));
	CHECK(differs([](Collection& c) { c[0].attributes["radius"] = 2.6f; }));
	CHECK(differs([](Collection& c) { c[0].attributes["extra"] = true; }));
	CHECK(differs([](Collection& c) { c[0].tags.insert("rock"); }));
	CHECK(differs([](Collection& c) { c[0].sourceId = 8; }));
	CHECK(differs([](Collection& c) { c[1].spline.points[1].x = 11.0f; }));
	CHECK(differs([](Collection& c) { c[1].spline.points[0].width = 1.0f; }));
	CHECK(differs([](Collection& c) { c[1].spline.closed = true; }));
	CHECK(differs([](Collection& c) { c[1].kind = ItemKind::Area; }));
	CHECK(differs([](Collection& c) { std::swap(c[0], c[1]); }));
	CHECK(differs([](Collection& c) { c.pop_back(); }));
}

TEST_CASE("Append with a column type conflict keeps the receiver's column", "[graph_data]")
{
	PointSet a;
	a.points.resize(1);
	a.SetAttribute(0, "c", 3.0f);

	PointSet b;
	b.points.resize(2);
	b.SetAttribute(0, "c", String("s"));
	b.SetAttribute(1, "c", String("t"));

	a.Append(b);
	REQUIRE(a.points.size() == 3);
	CHECK(a.columns["c"].first == AttributeType::Float);
	CHECK(a.columns["c"].second.size() == 3);
	CHECK(std::get<float>(*a.GetAttribute(0, "c")) == 3.0f);
	CHECK(std::get<float>(*a.GetAttribute(2, "c")) == 0.0f);
}

TEST_CASE("Keep with a short mask drops the points beyond it", "[graph_data]")
{
	PointSet set;
	set.points.resize(4);
	for (size_t i = 0; i < 4; ++i)
	{
		set.points[i].seed = i;
		set.SetAttribute(i, "n", static_cast<int64>(i));
	}

	set.Keep({ 0, 1 });
	REQUIRE(set.points.size() == 1);
	CHECK(set.points[0].seed == 1);
	CHECK(set.columns["n"].second.size() == 1);
}
