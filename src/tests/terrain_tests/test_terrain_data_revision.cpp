// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Terrain::GetDataRevision tells caches (the PCG terrain sampler) that terrain data was edited. It must bump on every
// edit and must not bump when pages stream in, or every page load would invalidate them.
//
// A Terrain needs a Scene, a MaterialManager and a graphics device, so this case only exists in client and editor
// builds (terrain_tests links the terrain library and a null device there, see CMakeLists.txt); the headless server
// build compiles this file to nothing.

#ifdef MMO_TERRAIN_TESTS_WITH_SCENE

#include "catch.hpp"

#include "assets/asset_registry.h"
#include "graphics/graphics_device.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/scene.h"
#include "terrain/page.h"
#include "terrain/terrain.h"

#include <filesystem>
#include <system_error>

using namespace mmo;

namespace
{
	/// The null device is a process-wide singleton; this is the only file of the suite that needs one.
	GraphicsDevice& EnsureNullDevice()
	{
		static GraphicsDevice& device = GraphicsDevice::CreateNull(GraphicsDeviceDesc());
		return device;
	}

	/// An asset registry over a fresh temporary folder, destroyed (folder and all) at the end of the scope.
	struct ScopedAssetFolder
	{
		std::filesystem::path path;

		ScopedAssetFolder()
			: path(std::filesystem::temp_directory_path() / "mmo_terrain_data_revision_test")
		{
			std::error_code error;
			std::filesystem::remove_all(path, error);
			std::filesystem::create_directories(path / "Rev");
			AssetRegistry::Initialize(path, {});
		}

		~ScopedAssetFolder()
		{
			AssetRegistry::Destroy();
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	};
}

TEST_CASE("Terrain data revision bumps on edits but not on page streaming", "[terrain][data_revision]")
{
	EnsureNullDevice();
	MaterialManager::Get().CreateManual("Models/Default.hmat");
	const ScopedAssetFolder assets;

	{
		Scene scene;
		terrain::Terrain terrain(scene, nullptr, 4, 4);
		terrain.SetBaseFileName("Rev");
		terrain::Page* page = terrain.GetPage(1, 1);
		REQUIRE(page != nullptr);

		// No page file yet: preparing gives an ocean placeholder, which is not an edit.
		uint64 revision = terrain.GetDataRevision();
		terrain.PreparePage(1, 1);
		REQUIRE(page->IsPrepared());
		CHECK(terrain.GetDataRevision() == revision);

		// The editor materialises a placeholder when a brush first touches it: that is an edit.
		REQUIRE(page->MaterializeOceanPlaceholder(-4.0f, 0.0f));
		CHECK(terrain.GetDataRevision() > revision);

		// Page (1, 1) starts at vertex OuterVerticesPerPageSide - 1 on both axes; pick one inside it.
		const int vertex = static_cast<int>(terrain::constants::OuterVerticesPerPageSide - 1) + 4;
		revision = terrain.GetDataRevision();
		terrain.SetHeightAt(vertex, vertex, 7.5f);
		CHECK(terrain.GetDataRevision() > revision);

		const uint32 pixel = (terrain::constants::PixelsPerPage - 1) + 4;
		revision = terrain.GetDataRevision();
		terrain.SetLayersAt(pixel, pixel, 0x00FF0000u);
		CHECK(terrain.GetDataRevision() > revision);

		revision = terrain.GetDataRevision();
		terrain.SetSeaLevel(3.0f);
		CHECK(terrain.GetDataRevision() > revision);

		// Setting the same sea level again changes nothing.
		revision = terrain.GetDataRevision();
		terrain.SetSeaLevel(3.0f);
		CHECK(terrain.GetDataRevision() == revision);

		// Saving is not an edit either.
		REQUIRE(page->Save());
		CHECK(terrain.GetDataRevision() == revision);
	}

	// A fresh terrain streaming the saved page in from its file: parsing and loading the page are not edits.
	{
		Scene scene;
		terrain::Terrain terrain(scene, nullptr, 4, 4);
		terrain.SetBaseFileName("Rev");
		REQUIRE(AssetRegistry::HasFile("Rev/1_1.tile"));

		const uint64 revision = terrain.GetDataRevision();
		terrain.PreparePage(1, 1);
		REQUIRE(terrain.GetPage(1, 1)->IsPrepared());
		REQUIRE_FALSE(terrain.GetPage(1, 1)->IsOceanPlaceholder());
		CHECK(terrain.GetDataRevision() == revision);

		const int vertex = static_cast<int>(terrain::constants::OuterVerticesPerPageSide - 1) + 4;
		CHECK(terrain.GetAt(vertex, vertex) == 7.5f);

		terrain.LoadPage(1, 1);
		CHECK(terrain.GetDataRevision() == revision);
	}
}

#endif
