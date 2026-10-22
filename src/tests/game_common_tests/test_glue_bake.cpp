// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_common/glue_bake.h"

#include <cmath>
#include <vector>

using namespace mmo;

namespace
{
	GlueSceneDefinition makeDefinition()
	{
		GlueSceneDefinition def;
		def.flightPath = { { { 0, 10, 0 }, { 0, 0, 10 } }, { { 10, 8, 10 }, { 0, 0, 20 } }, { { 20, 6, 20 }, { 5, 1, 30 } } };
		def.front.position = Vector3(3, 0, 25);
		for (size_t i = 0; i < def.spots.size(); ++i)
		{
			def.spots[i].position = Vector3(static_cast<float>(i), 0.0f, 30.0f);
		}
		def.entrance = Vector3(0, 0, 0);
		return def;
	}

	bool same(const Matrix4& a, const Matrix4& b)
	{
		for (int i = 0; i < 16; ++i)
		{
			if (a[i / 4][i % 4] != b[i / 4][i % 4])
			{
				return false;
			}
		}
		return true;
	}

	GlueBakeManifest makeManifest()
	{
		GlueBakeManifest m;
		m.sceneHash = "0123456789abcdef";
		m.inputs = { { "a/b.wmo", "1111111111111111" }, { "c.json", "2222222222222222" } };
		Matrix4 t = Matrix4::Identity;
		t[0][3] = 1.5f;
		t[1][3] = -2.25f;
		m.meshes = { { "Meshes/x.hmsh", t } };
		m.terrainTiles = { { "t0.hmsh", "mat.hmat", "splat.hmat" }, { "far.hmsh", "far.hmat", "" } };
		Matrix4 w = Matrix4::Identity;
		w[2][3] = 7.0f;
		m.water = { { "Glue/Water_30_33.hmsh", w } };
		m.foliage = { { "Meshes/grass.hmsh", Vector3(1, 2, 3), Quaternion(0.5f, 0.5f, 0.5f, 0.5f), Vector3(1.1f, 1.2f, 1.3f) } };
		GlueBakeLight l;
		l.type = 1;
		l.useAttenuation = true;
		l.color = 0xFFAA8800u;
		l.position = Vector3(4, 5, 6);
		l.intensity = 2.5f;
		l.rotation = Quaternion(0.7071f, 0.0f, 0.7071f, 0.0f);
		l.attenuationStart = 1.0f;
		l.attenuationEnd = 9.0f;
		l.innerConeAngle = 20.0f;
		l.outerConeAngle = 40.0f;
		l.fogScattering = 0.3f;
		m.lights = { l };
		m.sounds = { { 300, Vector3(7, 8, 9), 0.8f, 12.0f } };
		m.particles = { { "fire.hpar", t } };
		m.bounds = AABB(Vector3(-1, -2, -3), Vector3(100, 50, 200));
		return m;
	}

	AABB boxAt(const Vector3& c, float half)
	{
		return AABB(c - Vector3(half, half, half), c + Vector3(half, half, half));
	}
}

TEST_CASE("Pose sampling covers the path and the extra views", "[glue_bake]")
{
	const GlueSceneDefinition def = makeDefinition();
	const auto poses = SampleGlueCameraPoses(def);

	// 48 path + gate + 2 drift + plaza + 5 spot push-ins + front push-in + 8 entrance
	CHECK(poses.size() == 48 + 1 + 2 + 1 + 5 + 1 + 8);

	const GlueCameraKey first = EvaluateGluePath(def.flightPath, 0.0f);
	CHECK(poses.front().position.x == Approx(first.position.x));
	CHECK(poses.front().position.y == Approx(first.position.y));
	CHECK(poses.front().position.z == Approx(first.position.z));
	CHECK(poses.front().lookAt.z == Approx(first.lookAt.z));

	const auto few = SampleGlueCameraPoses(def, 4);
	CHECK(few.size() == 4 + 1 + 2 + 1 + 5 + 1 + 8);
}

TEST_CASE("Frustum set includes the 21:9 edge that 16:9 misses", "[glue_bake]")
{
	// Looking down -Z, vertical FOV 60 degrees. At distance 10 the half width is
	// 10 * tan(30) * aspect: 16:9 -> 10.26, 21:9 -> 13.47.
	const GlueBakeFrustumSet set = BuildGlueBakeFrusta({ { Vector3(0, 0, 0), Vector3(0, 0, -1) } }, 60.0f, 500.0f);
	REQUIRE(set.frusta.size() == 3);

	const AABB wide = boxAt(Vector3(12.5f, 0, -10), 0.5f);
	CHECK(set.Intersects(wide, 0.0f));

	// Without the 21:9 frustum it is outside.
	GlueBakeFrustumSet narrow;
	narrow.frusta = { set.frusta[0], set.frusta[1] };
	CHECK_FALSE(narrow.Intersects(wide, 0.0f));

	// Far outside every aspect.
	CHECK_FALSE(set.Intersects(boxAt(Vector3(40, 0, -10), 0.5f), 0.0f));
}

TEST_CASE("Frustum set rejects boxes behind the camera and beyond the far plane", "[glue_bake]")
{
	const GlueBakeFrustumSet set = BuildGlueBakeFrusta({ { Vector3(0, 0, 0), Vector3(0, 0, -1) } }, 60.0f, 500.0f);
	CHECK_FALSE(set.Intersects(boxAt(Vector3(0, 0, 20), 1.0f), 0.0f));
	CHECK_FALSE(set.Intersects(boxAt(Vector3(0, 0, -600), 1.0f), 0.0f));
	CHECK(set.Intersects(boxAt(Vector3(0, 0, -100), 1.0f), 0.0f));
	CHECK(set.Intersects(boxAt(Vector3(0, 0, -501.5f), 1.0f), 1.0f));
}

TEST_CASE("Projected size of a distant box", "[glue_bake]")
{
	const GlueBakeFrustumSet set = BuildGlueBakeFrusta({ { Vector3(0, 0, 0), Vector3(0, 0, -1) } }, 60.0f, 500.0f);
	const AABB box = boxAt(Vector3(0, 0, -100), 0.5f);
	CHECK(set.MaxProjectedSizePixels(box, 1080.0f) == Approx(9.35).epsilon(0.05));
	CHECK(set.MaxProjectedSizePixels(boxAt(Vector3(0, 0, 100), 0.5f), 1080.0f) == 0.0f);
}

TEST_CASE("Projected size counts frusta the margin reaches, at the unmargined size", "[glue_bake]")
{
	const GlueBakeFrustumSet set = BuildGlueBakeFrusta({ { Vector3(0, 0, 0), Vector3(0, 0, -1) } }, 60.0f, 500.0f);

	// Just beyond the far plane: invisible without the margin, visible with it.
	const AABB box = boxAt(Vector3(0, 0, -501.5f), 1.0f);
	CHECK(set.MaxProjectedSizePixels(box, 1080.0f) == 0.0f);

	const float withMargin = set.MaxProjectedSizePixels(box, 1080.0f, 1.0f);
	CHECK(withMargin > 0.0f);

	// The size is the 2 m box's, not the 4 m grown one's: 2 * 935 / 500.5.
	CHECK(withMargin == Approx(2.0f * 935.3f / 500.5f).epsilon(0.05));
}

TEST_CASE("Interior group detection", "[glue_bake]")
{
	CHECK(IsGlueBakeInteriorGroup(0x2000));
	CHECK_FALSE(IsGlueBakeInteriorGroup(0x2000 | 0x8));
	CHECK_FALSE(IsGlueBakeInteriorGroup(0x8));
	CHECK_FALSE(IsGlueBakeInteriorGroup(0));
}

TEST_CASE("Manifest round trips", "[glue_bake]")
{
	const GlueBakeManifest m = makeManifest();
	const auto parsed = ParseGlueBakeManifest(WriteGlueBakeManifest(m));
	REQUIRE(parsed.has_value());

	CHECK(parsed->version == 1);
	CHECK(parsed->sceneHash == m.sceneHash);
	REQUIRE(parsed->inputs.size() == 2);
	CHECK(parsed->inputs[1].path == "c.json");
	CHECK(parsed->inputs[1].hash == "2222222222222222");
	REQUIRE(parsed->meshes.size() == 1);
	CHECK(parsed->meshes[0].mesh == "Meshes/x.hmsh");
	CHECK(same(parsed->meshes[0].transform, m.meshes[0].transform));
	REQUIRE(parsed->terrainTiles.size() == 2);
	CHECK(parsed->terrainTiles[0].splatting == "splat.hmat");
	CHECK(parsed->terrainTiles[1].splatting.empty());
	REQUIRE(parsed->water.size() == 1);
	CHECK(parsed->water[0].mesh == "Glue/Water_30_33.hmsh");
	CHECK(same(parsed->water[0].transform, m.water[0].transform));
	REQUIRE(parsed->foliage.size() == 1);
	CHECK(parsed->foliage[0].position == m.foliage[0].position);
	CHECK((parsed->foliage[0].rotation == m.foliage[0].rotation));
	CHECK(parsed->foliage[0].scale == m.foliage[0].scale);
	REQUIRE(parsed->lights.size() == 1);
	const GlueBakeLight& a = parsed->lights[0];
	const GlueBakeLight& b = m.lights[0];
	CHECK(a.type == b.type);
	CHECK(a.useAttenuation == b.useAttenuation);
	CHECK(a.color == b.color);
	CHECK(a.position == b.position);
	CHECK(a.intensity == b.intensity);
	CHECK((a.rotation == b.rotation));
	CHECK(a.attenuationStart == b.attenuationStart);
	CHECK(a.attenuationEnd == b.attenuationEnd);
	CHECK(a.innerConeAngle == b.innerConeAngle);
	CHECK(a.outerConeAngle == b.outerConeAngle);
	CHECK(a.fogScattering == b.fogScattering);
	REQUIRE(parsed->sounds.size() == 1);
	CHECK(parsed->sounds[0].soundEntry == 300);
	CHECK(parsed->sounds[0].position == m.sounds[0].position);
	CHECK(parsed->sounds[0].volume == m.sounds[0].volume);
	CHECK(parsed->sounds[0].radius == m.sounds[0].radius);
	REQUIRE(parsed->particles.size() == 1);
	CHECK(parsed->particles[0].file == "fire.hpar");
	CHECK(same(parsed->particles[0].transform, m.particles[0].transform));
	CHECK(parsed->bounds.min == m.bounds.min);
	CHECK(parsed->bounds.max == m.bounds.max);
}

TEST_CASE("Manifest parsing rejects bad input", "[glue_bake]")
{
	const String good = WriteGlueBakeManifest(makeManifest());
	REQUIRE(ParseGlueBakeManifest(good).has_value());

	const auto replaced = [&good](const String& from, const String& to)
	{
		String s = good;
		const size_t pos = s.find(from);
		REQUIRE(pos != String::npos);
		s.replace(pos, from.size(), to);
		return s;
	};

	CHECK_FALSE(ParseGlueBakeManifest(replaced("\"sceneHash\"", "\"sceneHashX\"")).has_value());
	CHECK_FALSE(ParseGlueBakeManifest(replaced("\"version\":1", "\"version\":2")).has_value());
	CHECK_FALSE(ParseGlueBakeManifest(replaced("\"meshes\":[", "\"meshes\":{\"a\":[")).has_value());
	CHECK_FALSE(ParseGlueBakeManifest(good.substr(0, good.size() / 2)).has_value());
	CHECK_FALSE(ParseGlueBakeManifest(replaced("\"water\":[", "\"waterX\":[")).has_value());
	CHECK_FALSE(ParseGlueBakeManifest(replaced("\"mesh\":\"Glue/Water_30_33.hmsh\"", "\"mesh\":3")).has_value());
	CHECK_FALSE(ParseGlueBakeManifest("").has_value());
	CHECK_FALSE(ParseGlueBakeManifest("[]").has_value());
}

TEST_CASE("Manifest parsing rejects a non-array meshes field", "[glue_bake]")
{
	String s = WriteGlueBakeManifest(GlueBakeManifest{});
	const String from = "\"meshes\":[]";
	const size_t pos = s.find(from);
	REQUIRE(pos != String::npos);
	s.replace(pos, from.size(), "\"meshes\":5");
	CHECK_FALSE(ParseGlueBakeManifest(s).has_value());
}

TEST_CASE("Manifest mesh material overrides round trip and stay optional", "[glue_bake]")
{
	GlueBakeManifest m;
	m.sceneHash = "0123456789abcdef";
	m.meshes = { { "Meshes/plain.hmsh", Matrix4::Identity }, { "Meshes/lamp.hmsh", Matrix4::Identity, { "", "Materials/Red.hmat" } } };

	const String json = WriteGlueBakeManifest(m);

	// Written only where there are overrides
	size_t count = 0;
	for (size_t pos = json.find("\"materialOverrides\""); pos != String::npos; pos = json.find("\"materialOverrides\"", pos + 1))
	{
		++count;
	}
	CHECK(count == 1);

	const auto parsed = ParseGlueBakeManifest(json);
	REQUIRE(parsed.has_value());
	REQUIRE(parsed->meshes.size() == 2);
	CHECK(parsed->meshes[0].materialOverrides.empty());
	CHECK(parsed->meshes[1].materialOverrides == std::vector<String>{ "", "Materials/Red.hmat" });

	// A wrong type is rejected: not an array, or an array holding something other than strings
	const auto replaced = [&json](const String& to)
	{
		String s = json;
		const String from = "[\"\",\"Materials/Red.hmat\"]";
		const size_t pos = s.find(from);
		REQUIRE(pos != String::npos);
		s.replace(pos, from.size(), to);
		return s;
	};
	CHECK_FALSE(ParseGlueBakeManifest(replaced("\"Materials/Red.hmat\"")).has_value());
	CHECK_FALSE(ParseGlueBakeManifest(replaced("[\"\",5]")).has_value());
	CHECK(ParseGlueBakeManifest(replaced("[]")).has_value());
}

TEST_CASE("Byte hashing is FNV-1a 64", "[glue_bake]")
{
	CHECK(HashGlueBakeBytes("abc", 3) == "e71fa2190541574b");
	CHECK(HashGlueBakeBytes("", 0) == "cbf29ce484222325");
}

TEST_CASE("Stale inputs are the changed and the missing ones", "[glue_bake]")
{
	GlueBakeManifest m;
	m.inputs = { { "same", "aa" }, { "changed", "bb" }, { "missing", "cc" } };
	const auto stale = FindStaleGlueBakeInputs(m, [](const String& path) -> std::optional<String>
	{
		if (path == "same")
		{
			return String("aa");
		}
		if (path == "changed")
		{
			return String("zz");
		}
		return std::nullopt;
	});
	REQUIRE(stale.size() == 2);
	CHECK(stale[0] == "changed");
	CHECK(stale[1] == "missing");
}

TEST_CASE("The bake is used only for a parsed manifest of the current scene", "[glue_bake]")
{
	CHECK_FALSE(ShouldUseGlueBake(std::nullopt, "94f0b479e45f9382"));

	GlueBakeManifest manifest;
	manifest.sceneHash = "94f0b479e45f9382";
	CHECK_FALSE(ShouldUseGlueBake(manifest, "0000000000000000"));
	CHECK_FALSE(ShouldUseGlueBake(manifest, ""));
	CHECK(ShouldUseGlueBake(manifest, "94f0b479e45f9382"));

	manifest.sceneHash.clear();
	CHECK_FALSE(ShouldUseGlueBake(manifest, ""));
}

TEST_CASE("Baked mesh placements group by mesh, overrides, mirroring and cell", "[glue_bake]")
{
	const auto place = [](const String& mesh, const Vector3& position, const Vector3& scale = Vector3(1, 1, 1), std::vector<String> overrides = {})
	{
		GlueBakeMesh placement;
		placement.mesh = mesh;
		placement.transform.MakeTransform(position, scale, Quaternion::Identity);
		placement.materialOverrides = std::move(overrides);
		return placement;
	};

	const std::vector<GlueBakeMesh> meshes = {
		place("B.hmsh", Vector3(1, 0, 1)),
		place("A.hmsh", Vector3(2, 0, 2)),
		place("A.hmsh", Vector3(5, 3, 7)),
		place("A.hmsh", Vector3(150, 0, 2)),
		place("A.hmsh", Vector3(3, 0, 3), Vector3(-1, 1, 1)),
		place("A.hmsh", Vector3(4, 0, 4), Vector3(1, 1, 1), { "", "Other.hmat" }),
		place("A.hmsh", Vector3(-1, 0, 4)),
	};

	const std::vector<GlueBakeMeshGroup> groups = GroupGlueBakeMeshes(meshes, 100.0f);

	// A in cell (0,0): two plain placements, one mirrored, one overridden; A in cell (1,0); A in
	// cell (-1,0); B in cell (0,0).
	REQUIRE(groups.size() == 6);

	size_t total = 0;
	for (const GlueBakeMeshGroup& group : groups)
	{
		total += group.placements.size();
		for (const size_t index : group.placements)
		{
			CHECK(meshes[index].mesh == group.mesh);
			CHECK(meshes[index].materialOverrides == group.materialOverrides);
			CHECK(meshes[index].transform.HasNegativeScale() == group.mirrored);
		}
	}
	CHECK(total == meshes.size());

	const auto find = [&groups](const String& mesh, const int32 cellX, const bool mirrored, const size_t overrideCount) -> const GlueBakeMeshGroup*
	{
		for (const GlueBakeMeshGroup& group : groups)
		{
			if (group.mesh == mesh && group.cellX == cellX && group.mirrored == mirrored && group.materialOverrides.size() == overrideCount)
			{
				return &group;
			}
		}
		return nullptr;
	};

	const GlueBakeMeshGroup* plain = find("A.hmsh", 0, false, 0);
	REQUIRE(plain != nullptr);
	CHECK(plain->placements == std::vector<size_t>{ 1, 2 });
	REQUIRE(find("A.hmsh", 1, false, 0) != nullptr);
	REQUIRE(find("A.hmsh", -1, false, 0) != nullptr);
	REQUIRE(find("A.hmsh", 0, true, 0) != nullptr);
	REQUIRE(find("A.hmsh", 0, false, 2) != nullptr);
	REQUIRE(find("B.hmsh", 0, false, 0) != nullptr);

	// Deterministic: the same input groups the same way.
	const std::vector<GlueBakeMeshGroup> again = GroupGlueBakeMeshes(meshes, 100.0f);
	REQUIRE(again.size() == groups.size());
	for (size_t i = 0; i < groups.size(); ++i)
	{
		CHECK(again[i].mesh == groups[i].mesh);
		CHECK(again[i].placements == groups[i].placements);
	}

	// A cell size of 0 or less puts every placement in one cell.
	CHECK(GroupGlueBakeMeshes(meshes, 0.0f).size() == 4);
}
