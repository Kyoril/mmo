// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "assets/asset_registry.h"
#include "nav_mesh/map.h"
#include "terrain/constants.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mmo;

namespace
{
	constexpr uint32 FileMap = 'MAP1';
	constexpr uint32 FileSignature = 'NAVM';

	constexpr size_t PageBitmapSize = terrain::constants::MaxPages * terrain::constants::MaxPages / 8;

	/// Writes nav files into a fresh directory and points the asset registry at it for the lifetime of the
	/// fixture. The registry enumerates its files once, so every file has to be written before Mount().
	class NavDataDirectory final
	{
	public:
		NavDataDirectory()
		{
			static std::atomic<uint32> s_counter{ 0 };
			const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
			m_root = std::filesystem::temp_directory_path() / ("mmo_nav_mesh_tests_" + std::to_string(stamp) + "_" + std::to_string(s_counter++));
			std::filesystem::create_directories(m_root);
		}

		~NavDataDirectory()
		{
			if (m_mounted)
			{
				AssetRegistry::Destroy();
			}

			std::error_code error;
			std::filesystem::remove_all(m_root, error);
		}

		void Write(const std::string& relativePath, const std::vector<char>& bytes) const
		{
			const std::filesystem::path path = m_root / relativePath;
			std::filesystem::create_directories(path.parent_path());

			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		}

		void Mount()
		{
			AssetRegistry::Initialize(m_root, {});
			m_mounted = true;
		}

	private:
		std::filesystem::path m_root;
		bool m_mounted = false;
	};

	void Append(std::vector<char>& bytes, const uint32 value)
	{
		const auto* raw = reinterpret_cast<const char*>(&value);
		bytes.insert(bytes.end(), raw, raw + sizeof(value));
	}

	/// A .map file header: the magic, then optionally the page flag and a page bitmap.
	std::vector<char> MapFile(const bool withFlag, const uint8 flag, const size_t bitmapBytes, const std::vector<std::pair<int, int>>& pages = {})
	{
		std::vector<char> bytes;
		Append(bytes, FileMap);

		if (withFlag)
		{
			bytes.push_back(static_cast<char>(flag));
		}

		std::vector<char> bitmap(PageBitmapSize, 0);
		for (const auto& [x, y] : pages)
		{
			const int offset = y * static_cast<int>(terrain::constants::MaxPages) + x;
			bitmap[offset / 8] = static_cast<char>(bitmap[offset / 8] | (1 << (offset % 8)));
		}

		bytes.insert(bytes.end(), bitmap.begin(), bitmap.begin() + static_cast<std::ptrdiff_t>(bitmapBytes));
		return bytes;
	}

	/// Asserts that the map behaves as a map without navigation, and that asking it does not crash.
	void RequireNoNavigation(nav::Map& map)
	{
		const Vector3 start(1.f, 0.f, 1.f);
		const Vector3 end(20.f, 0.f, 20.f);

		std::vector<Vector3> path;
		CHECK_FALSE(map.FindPath(start, end, path, true));
		CHECK(path.empty());

		Vector3 randomPoint;
		CHECK_FALSE(map.FindRandomPointAroundCircle(start, 10.f, randomPoint));

		Vector3 hitPoint;
		CHECK(map.LineOfSightEx(start, end, hitPoint));
		CHECK(hitPoint == end);
	}
}

TEST_CASE("A nav map that ends after its magic has no navigation", "[nav_mesh]")
{
	// What nav_builder wrote for a world without terrain until 2026-10: the magic and nothing else.
	NavDataDirectory data;
	data.Write("Test.map", MapFile(false, 0, 0));
	data.Mount();

	nav::Map map("Test");

	CHECK_FALSE(map.IsValid());
	CHECK(map.LoadAllPages() == 0);
	RequireNoNavigation(map);
}

TEST_CASE("A missing nav map has no navigation", "[nav_mesh]")
{
	NavDataDirectory data;
	data.Mount();

	nav::Map map("Missing");

	CHECK_FALSE(map.IsValid());
	RequireNoNavigation(map);
}

TEST_CASE("A nav map with a foreign magic has no navigation", "[nav_mesh]")
{
	NavDataDirectory data;
	std::vector<char> bytes;
	Append(bytes, FileSignature);
	data.Write("Foreign.map", bytes);
	data.Mount();

	nav::Map map("Foreign");

	CHECK_FALSE(map.IsValid());
	RequireNoNavigation(map);
}

TEST_CASE("A nav map without pages has no navigation", "[nav_mesh]")
{
	NavDataDirectory data;
	data.Write("Empty.map", MapFile(true, 0, 0));
	data.Mount();

	nav::Map map("Empty");

	CHECK_FALSE(map.IsValid());
	RequireNoNavigation(map);
}

TEST_CASE("A nav map cut short inside its page bitmap has no navigation", "[nav_mesh]")
{
	NavDataDirectory data;
	data.Write("Cut.map", MapFile(true, 1, PageBitmapSize / 2));
	data.Mount();

	nav::Map map("Cut");

	CHECK_FALSE(map.IsValid());
	RequireNoNavigation(map);
}

TEST_CASE("A complete nav map is valid and answers queries without tiles", "[nav_mesh]")
{
	NavDataDirectory data;
	data.Write("Complete.map", MapFile(true, 1, PageBitmapSize, { { 32, 32 } }));
	data.Mount();

	nav::Map map("Complete");

	CHECK(map.IsValid());
	CHECK(map.HasPage(32, 32));
	CHECK_FALSE(map.HasPage(31, 32));

	// The page file is missing, so the mesh stays empty: nothing is found, and nothing crashes.
	CHECK(map.LoadAllPages() == 0);
	RequireNoNavigation(map);
}
