// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#if false

#include "catch.hpp"
#include "scene_graph/material_manager.h"
#include "scene_graph/scene.h"
#include "terrain/terrain.h"

using namespace mmo;


TEST_CASE("GetPageIndexByWorldPosition_Returns_Correct_Page", "[terrain]")
{
	// Arrange
	MaterialManager::Get().CreateManual("Models/Default.hmat");

	Scene scene;
	const terrain::Terrain terrain(scene, nullptr, 64, 64);
	Vector3 worldPosition(14.0f, 0.0f, 12.0f);

	// Act
	int32 pageX, pageY;
	bool result = terrain.GetPageIndexByWorldPosition(worldPosition, pageX, pageY);

	// Assert
	CHECK(result);
	CHECK(pageX == 32);
	CHECK(pageY == 32);

	worldPosition.x = -200.0f;
	result = terrain.GetPageIndexByWorldPosition(worldPosition, pageX, pageY);
	CHECK(result);
	CHECK(pageX == 31);
	CHECK(pageY == 32);
}

TEST_CASE("GetPageIndexByWorldPosition_Returns_False_When_Out_Of_Bounds_Positive", "[terrain]")
{
	// Arrange
	MaterialManager::Get().CreateManual("Models/Default.hmat");

	Scene scene;
	const terrain::Terrain terrain(scene, nullptr, 64, 64);
	const Vector3 worldPosition(terrain::constants::PageSize * 32, 0.0f, 12.0f);

	// Act
	int32 pageX, pageY;
	const bool result = terrain.GetPageIndexByWorldPosition(worldPosition, pageX, pageY);

	// Assert
	CHECK(!result);
}

TEST_CASE("GetPageIndexByWorldPosition_Returns_False_When_Out_Of_Bounds_Negative", "[terrain]")
{
	// Arrange
	MaterialManager::Get().CreateManual("Models/Default.hmat");

	Scene scene;
	const terrain::Terrain terrain(scene, nullptr, 64, 64);
	const Vector3 worldPosition(terrain::constants::PageSize * -33, 0.0f, 12.0f);

	// Act
	int32 pageX, pageY;
	const bool result = terrain.GetPageIndexByWorldPosition(worldPosition, pageX, pageY);

	// Assert
	CHECK(!result);
}

#endif
