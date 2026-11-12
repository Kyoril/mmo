// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "fake_terrain.h"
#include "pcg/terrain_contribution.h"

#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <thread>

using namespace mmo;
using namespace mmo::pcg;

namespace
{
	/// Bowl of radius 10 around (8,8) with grass splat, a pond of radius 3 and a tile material.
	TerrainTarget Bowl(float x, float z, float)
	{
		const float d = std::sqrt((x - 8.0f) * (x - 8.0f) + (z - 8.0f) * (z - 8.0f));
		TerrainTarget t;
		if (d < 10.0f)
		{
			t.height = 5.0f;
			t.heightWeight = 1.0f;
			t.splat = { 0, 255, 0, 0 };
			t.splatWeight = 1.0f;
			t.setMaterial = true;
		}
		if (d < 3.0f)
		{
			t.waterType = 1;
			t.waterHeight = 6.0f;
		}
		return t;
	}

	const WorldRect FullRect{ 0.0f, 0.0f, 16.0f, 16.0f };
}

TEST_CASE("Apply then Unapply restores exactly", "[terrain_contribution]")
{
	FakeTerrain terrain;
	const FakeTerrain::Data original = terrain.m_data;

	TerrainContribution c;
	Apply(terrain, FullRect, Bowl, "Grass", c);
	CHECK_FALSE(c.IsEmpty());
	CHECK_FALSE(terrain.m_data == original);
	CHECK(!c.outer.empty());
	CHECK(!c.inner.empty());
	CHECK(!c.splat.empty());
	CHECK(!c.waterQuads.empty());
	CHECK(!c.waterHeights.empty());
	CHECK(!c.materials.empty());

	Unapply(terrain, c);
	CHECK(terrain.m_data == original);
	CHECK(c.IsEmpty());
}

TEST_CASE("A channel target writes what the plain target writes, one channel per pass", "[terrain_contribution]")
{
	FakeTerrain plainTerrain;
	TerrainContribution plain;
	Apply(plainTerrain, FullRect, Bowl, "Grass", plain);

	// Fills only the asked channel, and counts the calls per channel.
	std::map<uint8, size_t> calls;
	const ChannelTerrainTargetFunction channelBowl = [&calls](const float x, const float z, const float baseHeight, const terrain_channel::Type channels)
	{
		++calls[static_cast<uint8>(channels)];
		const TerrainTarget full = Bowl(x, z, baseHeight);
		TerrainTarget t;
		if (channels == terrain_channel::Heights)
		{
			t.height = full.height;
			t.heightWeight = full.heightWeight;
		}
		else if (channels == terrain_channel::Splat)
		{
			t.splat = full.splat;
			t.splatWeight = full.splatWeight;
		}
		else if (channels == terrain_channel::Water)
		{
			t.waterType = full.waterType;
			t.waterHeight = full.waterHeight;
		}
		else if (channels == terrain_channel::Material)
		{
			t.setMaterial = full.setMaterial;
		}
		return t;
	};
	FakeTerrain channelTerrain;
	TerrainContribution channel;
	Apply(channelTerrain, FullRect, channelBowl, "Grass", channel);

	CHECK(channelTerrain.m_data == plainTerrain.m_data);
	CHECK(channel.outer.size() == plain.outer.size());
	CHECK(channel.inner.size() == plain.inner.size());
	CHECK(channel.splat.size() == plain.splat.size());
	CHECK(channel.waterQuads.size() == plain.waterQuads.size());
	CHECK(channel.waterHeights.size() == plain.waterHeights.size());
	CHECK(channel.materials.size() == plain.materials.size());
	CHECK(channel.touchedOuter.minX == plain.touchedOuter.minX);
	CHECK(channel.touchedOuter.maxZ == plain.touchedOuter.maxZ);

	// Only the four single channels were asked for, each at least once.
	CHECK(calls.size() == 4);
	CHECK(calls[terrain_channel::Heights] > 0);
	CHECK(calls[terrain_channel::Splat] > 0);
	CHECK(calls[terrain_channel::Water] > 0);
	CHECK(calls[terrain_channel::Material] > 0);
}

TEST_CASE("Apply with several worker targets writes what one target writes", "[terrain_contribution]")
{
	// Heights and splat depend on the position and the base, so a sample written from the wrong row would show.
	const auto ripple = [](const float x, const float z, const float baseHeight)
	{
		TerrainTarget t = Bowl(x, z, baseHeight);
		t.height = baseHeight + std::sin(x * 0.7f) + std::cos(z * 0.3f);
		t.splatWeight = 0.25f + 0.5f * std::abs(std::sin(x + 2.0f * z));
		return t;
	};

	FakeTerrain singleTerrain;
	TerrainContribution single;
	Apply(singleTerrain, FullRect, ripple, "Grass", single);

	// Each worker target notes whether it was ever entered while already running, and the threads it ran on.
	constexpr size_t workers = 4;
	std::vector<std::atomic<int>> running(workers);
	std::atomic<bool> overlapped{ false };
	std::mutex threadsLock;
	std::set<std::thread::id> threads;
	std::vector<ChannelTerrainTargetFunction> targets;
	for (size_t i = 0; i < workers; ++i)
	{
		targets.push_back([&, i](const float x, const float z, const float baseHeight, terrain_channel::Type)
		{
			if (running[i]++ != 0)
			{
				overlapped = true;
			}
			{
				const std::lock_guard<std::mutex> lock(threadsLock);
				threads.insert(std::this_thread::get_id());
			}
			const TerrainTarget t = ripple(x, z, baseHeight);
			--running[i];
			return t;
		});
	}
	FakeTerrain parallelTerrain;
	TerrainContribution parallel;
	Apply(parallelTerrain, FullRect, targets, "Grass", parallel);

	CHECK(parallelTerrain.m_data == singleTerrain.m_data);
	const auto sameEntries = [](const auto& a, const auto& b)
	{
		if (a.size() != b.size())
		{
			return false;
		}
		for (size_t i = 0; i < a.size(); ++i)
		{
			if (a[i].x != b[i].x || a[i].z != b[i].z || a[i].base != b[i].base || a[i].written != b[i].written)
			{
				return false;
			}
		}
		return true;
	};
	CHECK(sameEntries(parallel.outer, single.outer));
	CHECK(sameEntries(parallel.inner, single.inner));
	CHECK(sameEntries(parallel.splat, single.splat));
	CHECK(sameEntries(parallel.waterQuads, single.waterQuads));
	CHECK(sameEntries(parallel.waterHeights, single.waterHeights));
	CHECK(sameEntries(parallel.materials, single.materials));
	CHECK(parallel.touchedOuter.minX == single.touchedOuter.minX);
	CHECK(parallel.touchedOuter.minZ == single.touchedOuter.minZ);
	CHECK(parallel.touchedOuter.maxX == single.touchedOuter.maxX);
	CHECK(parallel.touchedOuter.maxZ == single.touchedOuter.maxZ);

	// A worker target is never called from two threads at once (how the rows were shared out is up to the threads).
	CHECK_FALSE(overlapped);
	CHECK_FALSE(threads.empty());

	// No targets: nothing is written.
	FakeTerrain untouched;
	const FakeTerrain::Data original = untouched.m_data;
	TerrainContribution none;
	Apply(untouched, FullRect, std::vector<ChannelTerrainTargetFunction>{}, "Grass", none);
	CHECK(none.IsEmpty());
	CHECK(untouched.m_data == original);
}

TEST_CASE("Computed samples written later write what Apply writes", "[terrain_contribution]")
{
	FakeTerrain applied;
	TerrainContribution direct;
	Apply(applied, FullRect, Bowl, "Grass", direct);

	// Computing changes nothing; the write then gives Apply's terrain and record.
	FakeTerrain split;
	const FakeTerrain::Data original = split.m_data;
	const ChannelTerrainTargetFunction bowl = [](const float x, const float z, const float baseHeight, terrain_channel::Type)
	{
		return Bowl(x, z, baseHeight);
	};
	TerrainSamples samples;
	ComputeTerrainSamples(split, FullRect, { bowl, bowl }, samples);
	CHECK(samples.computed);
	CHECK(split.m_data == original);

	TerrainContribution written;
	REQUIRE(WriteTerrainSamples(split, samples, "Grass", written));
	CHECK(split.m_data == applied.m_data);
	CHECK(written.outer.size() == direct.outer.size());
	CHECK(written.inner.size() == direct.inner.size());
	CHECK(written.splat.size() == direct.splat.size());
	CHECK(written.waterQuads.size() == direct.waterQuads.size());
	CHECK(written.waterHeights.size() == direct.waterHeights.size());
	CHECK(written.materials.size() == direct.materials.size());
	CHECK(written.touchedOuter.minX == direct.touchedOuter.minX);
	CHECK(written.touchedOuter.maxZ == direct.touchedOuter.maxZ);

	// Unapply restores the terrain the samples were computed on.
	Unapply(split, written);
	CHECK(split.m_data == original);

	// A default (not computed) set writes nothing.
	TerrainContribution none;
	CHECK(WriteTerrainSamples(split, TerrainSamples{}, "Grass", none));
	CHECK(none.IsEmpty());
	CHECK(split.m_data == original);
}

TEST_CASE("Computed samples are refused when a base height changed since", "[terrain_contribution]")
{
	FakeTerrain terrain;
	TerrainSamples samples;
	ComputeTerrainSamples(terrain, FullRect, { [](const float x, const float z, const float baseHeight, terrain_channel::Type)
	{
		return Bowl(x, z, baseHeight);
	} }, samples);

	// A vertex the bowl writes (the centre) changes after the samples were computed.
	const int32 cx = static_cast<int32>(8.0 / terrain.GetLayout().vertexSpacing);
	terrain.SetOuterHeight(cx, cx, terrain.GetOuterHeight(cx, cx) + 1.0f);
	const FakeTerrain::Data changed = terrain.m_data;

	TerrainContribution out;
	CHECK_FALSE(WriteTerrainSamples(terrain, samples, "Grass", out));
	CHECK(out.IsEmpty());
	CHECK(terrain.m_data == changed);
}

TEST_CASE("Rebuild twice is identical", "[terrain_contribution]")
{
	FakeTerrain terrain;
	TerrainContribution c;

	Apply(terrain, FullRect, Bowl, "Grass", c);
	const FakeTerrain::Data first = terrain.m_data;
	const TerrainContribution firstC = c;

	Unapply(terrain, c);
	Apply(terrain, FullRect, Bowl, "Grass", c);

	CHECK(terrain.m_data == first);
	REQUIRE(c.outer.size() == firstC.outer.size());
	for (size_t i = 0; i < c.outer.size(); ++i)
	{
		CHECK(c.outer[i].x == firstC.outer[i].x);
		CHECK(c.outer[i].z == firstC.outer[i].z);
		CHECK(c.outer[i].base == firstC.outer[i].base);
		CHECK(c.outer[i].written == firstC.outer[i].written);
	}
	CHECK(c.inner.size() == firstC.inner.size());
	CHECK(c.splat.size() == firstC.splat.size());
	CHECK(c.waterQuads.size() == firstC.waterQuads.size());
	CHECK(c.waterHeights.size() == firstC.waterHeights.size());
	CHECK(c.materials.size() == firstC.materials.size());
}

TEST_CASE("Entries are sorted by z then x", "[terrain_contribution]")
{
	FakeTerrain terrain;
	TerrainContribution c;
	Apply(terrain, FullRect, Bowl, "Grass", c);

	auto sorted = [](const auto& list)
	{
		for (size_t i = 1; i < list.size(); ++i)
		{
			const auto& a = list[i - 1];
			const auto& b = list[i];
			if (a.z > b.z || (a.z == b.z && a.x >= b.x))
			{
				return false;
			}
		}
		return true;
	};
	CHECK(sorted(c.outer));
	CHECK(sorted(c.inner));
	CHECK(sorted(c.splat));
	CHECK(sorted(c.waterQuads));
	CHECK(sorted(c.waterHeights));
	CHECK(sorted(c.materials));
}

TEST_CASE("Hand sculpt under the result is kept", "[terrain_contribution]")
{
	FakeTerrain terrain;
	const FakeTerrain::Data original = terrain.m_data;
	TerrainContribution c;
	Apply(terrain, FullRect, Bowl, "Grass", c);

	REQUIRE(!c.outer.empty());
	const auto e = c.outer[c.outer.size() / 2];
	terrain.SetOuterHeight(e.x, e.z, terrain.GetOuterHeight(e.x, e.z) + 2.0f);

	Unapply(terrain, c);

	FakeTerrain::Data expected = original;
	expected.outer[e.z * FakeTerrain::N + e.x] += 2.0f;
	CHECK(terrain.m_data == expected);
}

TEST_CASE("RestoreSamples undoes an Unapply exactly, hand sculpt included", "[terrain_contribution]")
{
	FakeTerrain terrain;
	TerrainContribution c;
	Apply(terrain, FullRect, Bowl, "Grass", c);

	REQUIRE(!c.outer.empty());
	const auto e = c.outer[c.outer.size() / 2];
	terrain.SetOuterHeight(e.x, e.z, terrain.GetOuterHeight(e.x, e.z) + 2.0f);
	const FakeTerrain::Data before = terrain.m_data;
	const TerrainContribution recorded = c;

	const TerrainContribution captured = CaptureSamples(terrain, c);
	CHECK(captured.outer.size() == c.outer.size());
	CHECK(captured.materials.size() == c.materials.size());

	Unapply(terrain, c);
	REQUIRE_FALSE(terrain.m_data == before);

	const int finishes = terrain.m_finishCount;
	RestoreSamples(terrain, captured);
	CHECK(terrain.m_data == before);
	CHECK(terrain.m_finishCount == finishes + 1);

	// The restored terrain carries the recorded contribution again: a later Unapply works as before.
	c = recorded;
	CHECK(ContributionMatchRatio(terrain, c) > kMinContributionMatchRatio);
	Unapply(terrain, c);
	FakeTerrain::Data expected = FakeTerrain().m_data;
	expected.outer[e.z * FakeTerrain::N + e.x] += 2.0f;
	CHECK(terrain.m_data == expected);
}

TEST_CASE("RestoreSamples of an empty capture changes nothing", "[terrain_contribution]")
{
	FakeTerrain terrain;
	const FakeTerrain::Data before = terrain.m_data;
	const TerrainContribution captured = CaptureSamples(terrain, TerrainContribution());
	CHECK(captured.IsEmpty());
	RestoreSamples(terrain, captured);
	CHECK(terrain.m_data == before);
	CHECK(terrain.m_finishCount == 0);
}

TEST_CASE("Hand paint is kept as new base", "[terrain_contribution]")
{
	FakeTerrain terrain;
	const FakeTerrain::Data original = terrain.m_data;
	TerrainContribution c;
	Apply(terrain, FullRect, Bowl, "Grass", c);

	REQUIRE(!c.splat.empty());
	const auto e = c.splat.front();
	const uint32 handPainted = 0x01020304u;
	terrain.SetSplat(e.x, e.z, handPainted);

	Unapply(terrain, c);

	FakeTerrain::Data expected = original;
	expected.splat[e.z * FakeTerrain::N + e.x] = handPainted;
	CHECK(terrain.m_data == expected);
}

TEST_CASE("Splat normalisation sums to 255", "[terrain_contribution]")
{
	FakeTerrain terrain;
	TerrainContribution c;
	const auto blend = [](float, float, float)
	{
		TerrainTarget t;
		t.splat = { 33, 33, 34, 0 };
		t.splatWeight = 0.5f;
		return t;
	};
	Apply(terrain, WorldRect{ 0.0f, 0.0f, 16.0f, 16.0f }, blend, "", c);

	REQUIRE(!c.splat.empty());
	for (const auto& e : c.splat)
	{
		const uint32 v = e.written;
		const uint32 sum = (v & 0xFF) + ((v >> 8) & 0xFF) + ((v >> 16) & 0xFF) + ((v >> 24) & 0xFF);
		CHECK(sum == 255);
	}
}

TEST_CASE("Outside the rect nothing changes", "[terrain_contribution]")
{
	FakeTerrain terrain;
	const FakeTerrain::Data before = terrain.m_data;
	TerrainContribution c;
	Apply(terrain, WorldRect{ 4.0f, 4.0f, 12.0f, 12.0f }, Bowl, "Grass", c);
	REQUIRE(!c.outer.empty());

	const auto outside = [](int32 v) { return v < 4 || v > 12; };
	for (int32 z = 0; z < FakeTerrain::N; ++z)
	{
		for (int32 x = 0; x < FakeTerrain::N; ++x)
		{
			if (outside(x) || outside(z))
			{
				const size_t i = z * FakeTerrain::N + x;
				CHECK(terrain.m_data.outer[i] == before.outer[i]);
				CHECK(terrain.m_data.splat[i] == before.splat[i]);
				CHECK(terrain.m_data.water[i] == before.water[i]);
			}
		}
	}
	for (int32 z = 0; z < FakeTerrain::Q; ++z)
	{
		for (int32 x = 0; x < FakeTerrain::Q; ++x)
		{
			if (z < 4 || z > 11 || x < 4 || x > 11)
			{
				const size_t i = z * FakeTerrain::Q + x;
				CHECK(terrain.m_data.inner[i] == before.inner[i]);
			}
		}
	}
}

TEST_CASE("RectsOverlap", "[terrain_contribution]")
{
	CHECK(RectsOverlap({ 0, 0, 10, 10 }, { 5, 5, 15, 15 }));
	CHECK(RectsOverlap({ 0, 0, 10, 10 }, { 10, 10, 20, 20 }));
	CHECK(RectsOverlap({ 0, 0, 10, 10 }, { 2, 2, 3, 3 }));
	CHECK_FALSE(RectsOverlap({ 0, 0, 10, 10 }, { 11, 0, 20, 10 }));
	CHECK_FALSE(RectsOverlap({ 0, 0, 10, 10 }, { 0, -20, 10, -1 }));
}

TEST_CASE("Match ratio is 1 when the terrain carries the contribution", "[terrain_contribution]")
{
	FakeTerrain terrain;
	TerrainContribution c;
	CHECK(ContributionMatchRatio(terrain, c) == 1.0f);

	Apply(terrain, FullRect, Bowl, "Grass", c);
	size_t matching = 0, total = 0;
	CHECK(ContributionMatchRatio(terrain, c, &matching, &total) == 1.0f);
	CHECK(total == c.outer.size() + c.inner.size());
	CHECK(matching == total);
	CHECK(total > 0);
}

TEST_CASE("Match ratio is 0 when the terrain was reset to the base", "[terrain_contribution]")
{
	FakeTerrain terrain;
	const FakeTerrain::Data original = terrain.m_data;
	TerrainContribution c;
	Apply(terrain, FullRect, Bowl, "Grass", c);

	terrain.m_data = original;
	CHECK(ContributionMatchRatio(terrain, c) == 0.0f);
}

TEST_CASE("Match ratio is in between for partly sculpted terrain", "[terrain_contribution]")
{
	FakeTerrain terrain;
	TerrainContribution c;
	Apply(terrain, FullRect, Bowl, "Grass", c);
	REQUIRE(c.outer.size() >= 4);

	const size_t half = c.outer.size() / 2;
	for (size_t i = 0; i < half; ++i)
	{
		terrain.SetOuterHeight(c.outer[i].x, c.outer[i].z, c.outer[i].written + 1.0f);
	}

	size_t matching = 0, total = 0;
	const float ratio = ContributionMatchRatio(terrain, c, &matching, &total);
	CHECK(ratio > 0.0f);
	CHECK(ratio < 1.0f);
	CHECK(matching == total - half);
}

TEST_CASE("a target can name a different material per tile", "[terrain_contribution]")
{
	FakeTerrain terrain;
	const FakeTerrain::Data original = terrain.m_data;
	const TerrainTargetFunction target = [](float x, float z, float) -> TerrainTarget
	{
		TerrainTarget t;
		if (z < 8.0f)
		{
			t.material = x < 8.0f ? "A.hmi" : "B.hmi";
		}
		return t;
	};

	TerrainContribution c;
	Apply(terrain, FullRect, target, "", c);

	CHECK(terrain.GetTileMaterial(0, 0) == "A.hmi");
	CHECK(terrain.GetTileMaterial(1, 0) == "B.hmi");
	CHECK(terrain.GetTileMaterial(0, 1) == "Base");
	CHECK(terrain.GetTileMaterial(1, 1) == "Base");
	REQUIRE(c.materials.size() == 2);
	CHECK(c.materials[0].base == "Base");
	CHECK(c.materials[0].written == "A.hmi");
	CHECK(c.materials[1].base == "Base");
	CHECK(c.materials[1].written == "B.hmi");

	Unapply(terrain, c);
	CHECK(terrain.m_data == original);
}

TEST_CASE("a tile that already has the named material is not recorded", "[terrain_contribution]")
{
	FakeTerrain terrain;
	terrain.SetTileMaterial(0, 0, "A.hmi");
	const TerrainTargetFunction target = [](float x, float z, float) -> TerrainTarget
	{
		TerrainTarget t;
		if (z < 8.0f)
		{
			t.material = x < 8.0f ? "A.hmi" : "B.hmi";
		}
		return t;
	};

	TerrainContribution c;
	Apply(terrain, FullRect, target, "", c);

	REQUIRE(c.materials.size() == 1);
	CHECK(c.materials[0].x == 1);
	CHECK(c.materials[0].z == 0);
	CHECK(terrain.GetTileMaterial(0, 0) == "A.hmi");
	CHECK(terrain.GetTileMaterial(1, 0) == "B.hmi");
}

TEST_CASE("setMaterial with the call's material still works", "[terrain_contribution]")
{
	FakeTerrain terrain;
	const TerrainTargetFunction target = [](float x, float z, float) -> TerrainTarget
	{
		TerrainTarget t;
		t.setMaterial = true;
		// A named tile wins over the call's material.
		if (x > 8.0f && z < 8.0f)
		{
			t.material = "B.hmi";
		}
		return t;
	};

	TerrainContribution c;
	Apply(terrain, FullRect, target, "Grass", c);

	CHECK(terrain.GetTileMaterial(0, 0) == "Grass");
	CHECK(terrain.GetTileMaterial(1, 0) == "B.hmi");
	CHECK(terrain.GetTileMaterial(0, 1) == "Grass");
	CHECK(terrain.GetTileMaterial(1, 1) == "Grass");
	CHECK(c.materials.size() == 4);
}
