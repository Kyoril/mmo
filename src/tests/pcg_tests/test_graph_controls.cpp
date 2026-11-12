// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "pcg/graph/controls.h"

#include <limits>

using namespace mmo;
using namespace mmo::pcg::graph;

namespace
{
	PropertyDef Prop(const String& name, const PropertyType type, const AttributeValue& def)
	{
		PropertyDef p;
		p.name = name;
		p.type = type;
		p.defaultValue = def;
		return p;
	}

	ControlType MakeForestType()
	{
		ControlType t;
		t.name = "Forest Area";
		t.shape = ControlShape::ClosedLoop;
		t.defaultTags = { "forest", "area" };

		PropertyGroup trees;
		trees.name = "Trees";
		PropertyDef density = Prop("density", PropertyType::Float, 0.5f);
		density.min = 0.0f;
		density.max = 1.0f;
		density.description = "Tree density";
		PropertyDef count = Prop("maxCount", PropertyType::Int, static_cast<int64>(10));
		count.min = 1;
		count.max = 50;
		PropertyDef species = Prop("species", PropertyType::Enum, String("oak"));
		species.enumValues = { "oak", "pine" };
		trees.properties = { density, count, species, Prop("mixed", PropertyType::Bool, true) };

		PropertyGroup look;
		look.name = "Look";
		look.properties = {
			Prop("label", PropertyType::String, String("woods")),
			Prop("groundMesh", PropertyType::Asset, String("Terrain/Grass.hmat")),
			Prop("trees", PropertyType::AssetList, String("Trees/Oak.hmsh|2;Trees/Pine.hmsh|1")),
			Prop("tint", PropertyType::Color, Vector3(0.2f, 0.6f, 0.1f))
		};
		t.groups = { trees, look };
		return t;
	}

	ControlInstance MakeLoop()
	{
		ControlInstance i;
		i.id = 42;
		i.typePath = "Controls/ForestArea.hctl";
		i.name = "North woods";
		i.extraTags = { "north" };
		i.spline.closed = true;
		i.spline.interpolation = SplineInterpolation::CatmullRom;
		i.spline.points.resize(4);
		i.spline.points[0].x = 0; i.spline.points[0].z = 0;
		i.spline.points[1].x = 10; i.spline.points[1].z = 0; i.spline.points[1].y = 2; i.spline.points[1].width = 3;
		i.spline.points[2].x = 10; i.spline.points[2].z = 10; i.spline.points[2].autoTangents = false; i.spline.points[2].outX = 1.5f;
		i.spline.points[3].x = 0; i.spline.points[3].z = 10;
		i.pointIds = { 3, 5, 6, 9 };
		i.nextPointId = 10;
		i.position = Vector3(1, 2, 3);
		i.rotation = Quaternion(0.5f, 0.5f, 0.5f, 0.5f);
		i.halfExtents = Vector3(1, 2, 3);
		i.overrides["density"] = 0.75f;
		i.overrides["species"] = String("pine");
		return i;
	}

	bool Has(const std::vector<String>& warnings, const String& needle)
	{
		for (const String& w : warnings)
		{
			if (w.find(needle) != String::npos)
			{
				return true;
			}
		}
		return false;
	}

	uint64 HashOf(const ControlType& t, const ControlInstance& i, const std::set<String>& tags, const AttributeSet& attrs)
	{
		ResolvedControl rc;
		rc.type = &t;
		rc.instance = &i;
		rc.tags = tags;
		rc.attributes = attrs;
		return ControlsHash({ rc });
	}
}

TEST_CASE("control type round trip keeps every property type", "[graph_controls]")
{
	const ControlType type = MakeForestType();
	const String text = WriteControlType(type);

	ControlType loaded;
	String error;
	REQUIRE(ParseControlType(text, loaded, error));
	CHECK(loaded.name == "Forest Area");
	CHECK(loaded.shape == ControlShape::ClosedLoop);
	CHECK(loaded.defaultTags == type.defaultTags);
	REQUIRE(loaded.groups.size() == 2);
	CHECK(loaded.groups[0].name == "Trees");
	REQUIRE(loaded.groups[0].properties.size() == 4);
	CHECK(loaded.groups[0].properties[0].description == "Tree density");
	CHECK(loaded.groups[0].properties[0].max == 1.0f);
	CHECK(loaded.groups[0].properties[2].enumValues == std::vector<String>({ "oak", "pine" }));
	REQUIRE(loaded.groups[1].properties.size() == 4);
	CHECK(loaded.groups[1].properties[3].type == PropertyType::Color);
	CHECK(std::get<Vector3>(loaded.groups[1].properties[3].defaultValue).y == 0.6f);
	CHECK(loaded.groups[1].properties[2].type == PropertyType::AssetList);

	CHECK(WriteControlType(loaded) == text);
}

TEST_CASE("control type parsing rejects bad files", "[graph_controls]")
{
	ControlType out;
	String error;
	CHECK_FALSE(ParseControlType("nope", out, error));
	CHECK_FALSE(ParseControlType("{\"version\":2}", out, error));
	CHECK_FALSE(ParseControlType("{\"version\":1,\"shape\":\"blob\"}", out, error));

	ControlType bad = MakeForestType();
	bad.groups[0].properties[0].defaultValue = String("not a float");
	CHECK_FALSE(ParseControlType(WriteControlType(bad), out, error));
	CHECK(error.find("density") != String::npos);

	ControlType dup = MakeForestType();
	dup.groups[1].properties[0].name = "density";
	CHECK_FALSE(ParseControlType(WriteControlType(dup), out, error));
}

TEST_CASE("control instance round trip keeps point ids and overrides", "[graph_controls]")
{
	const ControlInstance inst = MakeLoop();
	const String text = WriteControlInstance(inst);

	ControlInstance loaded;
	String error;
	REQUIRE(ParseControlInstance(text, loaded, error));
	CHECK(loaded.id == 42);
	CHECK(loaded.typePath == inst.typePath);
	CHECK(loaded.name == "North woods");
	CHECK(loaded.extraTags == inst.extraTags);
	CHECK(loaded.spline.closed);
	CHECK(loaded.spline.interpolation == SplineInterpolation::CatmullRom);
	REQUIRE(loaded.spline.points.size() == 4);
	CHECK(loaded.spline.points[1].y == 2.0f);
	CHECK(loaded.spline.points[1].width == 3.0f);
	CHECK_FALSE(loaded.spline.points[2].autoTangents);
	CHECK(loaded.spline.points[2].outX == 1.5f);
	CHECK(loaded.pointIds == inst.pointIds);
	CHECK(loaded.nextPointId == 10);
	CHECK(loaded.position.z == 3.0f);
	CHECK(loaded.rotation.w == 0.5f);
	CHECK(loaded.halfExtents.y == 2.0f);
	CHECK(std::get<float>(loaded.overrides.at("density")) == 0.75f);
	CHECK(std::get<String>(loaded.overrides.at("species")) == "pine");

	CHECK(WriteControlInstance(loaded) == text);
}

TEST_CASE("control instance parsing rejects bad files", "[graph_controls]")
{
	ControlInstance out;
	String error;
	CHECK_FALSE(ParseControlInstance("{", out, error));
	CHECK_FALSE(ParseControlInstance("{\"version\":3}", out, error));

	ControlInstance mismatch = MakeLoop();
	mismatch.pointIds.pop_back();
	CHECK_FALSE(ParseControlInstance(WriteControlInstance(mismatch), out, error));

	ControlInstance dup = MakeLoop();
	dup.pointIds[1] = dup.pointIds[0];
	CHECK_FALSE(ParseControlInstance(WriteControlInstance(dup), out, error));

	String text = WriteControlInstance(MakeLoop());
	const size_t at = text.find("\"x\": 10.0");
	REQUIRE(at != String::npos);
	text.replace(at, 9, "\"x\": 1e999");
	CHECK_FALSE(ParseControlInstance(text, out, error));
}

TEST_CASE("point ids are fixed up below nextPointId", "[graph_controls]")
{
	ControlInstance inst = MakeLoop();
	inst.nextPointId = 2;
	ControlInstance loaded;
	String error;
	REQUIRE(ParseControlInstance(WriteControlInstance(inst), loaded, error));
	CHECK(loaded.nextPointId == 10);
}

TEST_CASE("resolve merges tags and applies defaults and overrides", "[graph_controls]")
{
	const ControlType type = MakeForestType();
	const ControlInstance inst = MakeLoop();

	ResolvedControl rc;
	std::vector<String> warnings;
	REQUIRE(ResolveControl(type, inst, rc, warnings));
	CHECK(warnings.empty());
	CHECK(rc.type == &type);
	CHECK(rc.instance == &inst);
	CHECK(rc.tags == std::set<String>({ "area", "forest", "north" }));
	CHECK(std::get<float>(rc.attributes.at("density")) == 0.75f);
	CHECK(std::get<String>(rc.attributes.at("species")) == "pine");
	CHECK(std::get<int64>(rc.attributes.at("maxCount")) == 10);
	CHECK(std::get<bool>(rc.attributes.at("mixed")));
	CHECK(std::get<String>(rc.attributes.at("trees")) == "Trees/Oak.hmsh|2;Trees/Pine.hmsh|1");
	CHECK(std::get<Vector3>(rc.attributes.at("tint")).x == 0.2f);
	CHECK(rc.attributes.size() == 8);
}

TEST_CASE("resolve clamps and warns on bad overrides", "[graph_controls]")
{
	const ControlType type = MakeForestType();
	ControlInstance inst = MakeLoop();
	inst.overrides.clear();
	inst.overrides["density"] = 7.0f;
	inst.overrides["maxCount"] = static_cast<int64>(-4);
	inst.overrides["mixed"] = 1.0f;
	inst.overrides["species"] = String("birch");
	inst.overrides["label"] = std::numeric_limits<float>::quiet_NaN();
	inst.overrides["mystery"] = static_cast<int64>(3);

	ResolvedControl rc;
	std::vector<String> warnings;
	REQUIRE(ResolveControl(type, inst, rc, warnings));
	CHECK(std::get<float>(rc.attributes.at("density")) == 1.0f);
	CHECK(std::get<int64>(rc.attributes.at("maxCount")) == 1);
	CHECK(std::get<bool>(rc.attributes.at("mixed")));
	CHECK(std::get<String>(rc.attributes.at("species")) == "oak");
	CHECK(std::get<String>(rc.attributes.at("label")) == "woods");
	CHECK(std::get<int64>(rc.attributes.at("mystery")) == 3);

	CHECK(Has(warnings, "density"));
	CHECK(Has(warnings, "maxCount"));
	CHECK(Has(warnings, "mixed"));
	CHECK(Has(warnings, "species"));
	CHECK(Has(warnings, "label"));
	CHECK(Has(warnings, "mystery"));
}

TEST_CASE("resolve refuses geometry that does not fit the shape", "[graph_controls]")
{
	const ControlType type = MakeForestType();

	ControlInstance open = MakeLoop();
	open.spline.closed = false;
	ResolvedControl rc;
	std::vector<String> warnings;
	CHECK_FALSE(ResolveControl(type, open, rc, warnings));
	CHECK_FALSE(warnings.empty());
	CHECK(rc.type == nullptr);

	ControlInstance tiny = MakeLoop();
	tiny.spline.points.resize(2);
	tiny.pointIds.resize(2);
	warnings.clear();
	CHECK_FALSE(ResolveControl(type, tiny, rc, warnings));
	CHECK_FALSE(warnings.empty());

	ControlInstance badIds = MakeLoop();
	badIds.pointIds.pop_back();
	warnings.clear();
	CHECK_FALSE(ResolveControl(type, badIds, rc, warnings));

	ControlType road = MakeForestType();
	road.shape = ControlShape::OpenSpline;
	ControlInstance path = MakeLoop();
	warnings.clear();
	CHECK_FALSE(ResolveControl(road, path, rc, warnings));
	path.spline.closed = false;
	warnings.clear();
	CHECK(ResolveControl(road, path, rc, warnings));

	ControlType volume = MakeForestType();
	volume.shape = ControlShape::Volume;
	ControlInstance box;
	box.typePath = "x";
	warnings.clear();
	CHECK(ResolveControl(volume, box, rc, warnings));
	volume.shape = ControlShape::Point;
	CHECK(ResolveControl(volume, box, rc, warnings));
}

TEST_CASE("controls hash covers every field", "[graph_controls]")
{
	const ControlType type = MakeForestType();
	const ControlInstance inst = MakeLoop();
	ResolvedControl base;
	std::vector<String> warnings;
	REQUIRE(ResolveControl(type, inst, base, warnings));
	const uint64 h0 = HashOf(type, inst, base.tags, base.attributes);
	CHECK(h0 == HashOf(type, inst, base.tags, base.attributes));
	CHECK(ControlsHash({}) != h0);
	CHECK(ControlsHash({ base, base }) != ControlsHash({ base }));

	std::vector<uint64> hashes;
	hashes.push_back(h0);

	const auto instVariant = [&](auto mutate)
	{
		ControlInstance i = inst;
		mutate(i);
		hashes.push_back(HashOf(type, i, base.tags, base.attributes));
	};
	instVariant([](ControlInstance& i) { i.id = 43; });
	instVariant([](ControlInstance& i) { i.typePath += "2"; });
	instVariant([](ControlInstance& i) { i.name += "x"; });
	instVariant([](ControlInstance& i) { i.extraTags.insert("zzz"); });
	instVariant([](ControlInstance& i) { i.spline.closed = false; });
	instVariant([](ControlInstance& i) { i.spline.interpolation = SplineInterpolation::Linear; });
	instVariant([](ControlInstance& i) { i.spline.points.pop_back(); });
	instVariant([](ControlInstance& i) { i.spline.points[0].x += 1; });
	instVariant([](ControlInstance& i) { i.spline.points[0].z += 1; });
	instVariant([](ControlInstance& i) { i.spline.points[0].y += 1; });
	instVariant([](ControlInstance& i) { i.spline.points[0].width += 1; });
	instVariant([](ControlInstance& i) { i.spline.points[2].autoTangents = true; });
	instVariant([](ControlInstance& i) { i.spline.points[2].inX += 1; });
	instVariant([](ControlInstance& i) { i.spline.points[2].inY += 1; });
	instVariant([](ControlInstance& i) { i.spline.points[2].inZ += 1; });
	instVariant([](ControlInstance& i) { i.spline.points[2].outX += 1; });
	instVariant([](ControlInstance& i) { i.spline.points[2].outY += 1; });
	instVariant([](ControlInstance& i) { i.spline.points[2].outZ += 1; });
	instVariant([](ControlInstance& i) { i.pointIds[2] = 7; });
	instVariant([](ControlInstance& i) { i.nextPointId = 11; });
	instVariant([](ControlInstance& i) { i.position.x += 1; });
	instVariant([](ControlInstance& i) { i.position.y += 1; });
	instVariant([](ControlInstance& i) { i.position.z += 1; });
	instVariant([](ControlInstance& i) { i.rotation.w = 1; });
	instVariant([](ControlInstance& i) { i.rotation.x = 1; });
	instVariant([](ControlInstance& i) { i.rotation.y = 1; });
	instVariant([](ControlInstance& i) { i.rotation.z = 1; });
	instVariant([](ControlInstance& i) { i.halfExtents.x += 1; });
	instVariant([](ControlInstance& i) { i.halfExtents.y += 1; });
	instVariant([](ControlInstance& i) { i.halfExtents.z += 1; });
	instVariant([](ControlInstance& i) { i.overrides["density"] = 0.1f; });
	instVariant([](ControlInstance& i) { i.overrides["extra"] = true; });

	const auto typeVariant = [&](auto mutate)
	{
		ControlType t = type;
		mutate(t);
		hashes.push_back(HashOf(t, inst, base.tags, base.attributes));
	};
	typeVariant([](ControlType& t) { t.name += "x"; });
	typeVariant([](ControlType& t) { t.shape = ControlShape::OpenSpline; });
	typeVariant([](ControlType& t) { t.defaultTags.insert("new"); });
	typeVariant([](ControlType& t) { t.groups.pop_back(); });
	typeVariant([](ControlType& t) { t.groups[0].name += "x"; });
	typeVariant([](ControlType& t) { t.groups[0].properties[0].name += "x"; });
	typeVariant([](ControlType& t) { t.groups[0].properties[0].type = PropertyType::Int; });
	typeVariant([](ControlType& t) { t.groups[0].properties[0].defaultValue = 0.25f; });
	typeVariant([](ControlType& t) { t.groups[0].properties[0].min = -1; });
	typeVariant([](ControlType& t) { t.groups[0].properties[0].max = 2; });
	typeVariant([](ControlType& t) { t.groups[0].properties[2].enumValues.push_back("elm"); });
	typeVariant([](ControlType& t) { t.groups[0].properties[0].description += "x"; });

	{
		std::set<String> tags = base.tags;
		tags.insert("extra");
		hashes.push_back(HashOf(type, inst, tags, base.attributes));
		AttributeSet attrs = base.attributes;
		attrs["density"] = 0.2f;
		hashes.push_back(HashOf(type, inst, base.tags, attrs));
		attrs = base.attributes;
		attrs["added"] = String("a");
		hashes.push_back(HashOf(type, inst, base.tags, attrs));
	}

	for (size_t a = 0; a < hashes.size(); ++a)
	{
		for (size_t b = a + 1; b < hashes.size(); ++b)
		{
			INFO("variants " << a << " and " << b);
			CHECK(hashes[a] != hashes[b]);
		}
	}
}
