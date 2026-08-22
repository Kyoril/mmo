// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "graphics/graphics_device.h"
#include "null_device.h"
#include "math/math_utils.h"
#include "math/sphere.h"
#include "math/degree.h"
#include "scene_graph/scene.h"
#include "scene_graph/camera.h"
#include "scene_graph/scene_node.h"

using namespace mmo;

using mmo::test::EnsureNullDevice;

// These tests pin down the camera's lazy-evaluation contract: every setter that
// affects the projection or view matrix must invalidate the cached state, so that
// the matrices, frustum planes and world-space corners returned afterwards are
// up to date even though UpdateFrustum() caches its results.

TEST_CASE("Camera projection matrix tracks perspective parameter changes", "[camera_frustum]")
{
	GraphicsDevice& device = EnsureNullDevice();

	Camera camera("ProjectionParamCamera");
	camera.SetFOVy(Radian(Degree(60.0f)));
	camera.SetAspectRatio(16.0f / 9.0f);
	camera.SetNearClipDistance(0.5f);
	camera.SetFarClipDistance(500.0f);

	CHECK(camera.GetProjectionMatrix().IsNearlyEqual(
		device.MakeProjectionMatrix(Radian(Degree(60.0f)), 16.0f / 9.0f, 0.5f, 500.0f)));

	// Query again without any change: result must be stable.
	CHECK(camera.GetProjectionMatrix().IsNearlyEqual(
		device.MakeProjectionMatrix(Radian(Degree(60.0f)), 16.0f / 9.0f, 0.5f, 500.0f)));

	// Each parameter setter must invalidate the cached projection matrix.
	camera.SetFOVy(Radian(Degree(45.0f)));
	CHECK(camera.GetProjectionMatrix().IsNearlyEqual(
		device.MakeProjectionMatrix(Radian(Degree(45.0f)), 16.0f / 9.0f, 0.5f, 500.0f)));

	camera.SetAspectRatio(4.0f / 3.0f);
	CHECK(camera.GetProjectionMatrix().IsNearlyEqual(
		device.MakeProjectionMatrix(Radian(Degree(45.0f)), 4.0f / 3.0f, 0.5f, 500.0f)));

	camera.SetNearClipDistance(1.0f);
	CHECK(camera.GetProjectionMatrix().IsNearlyEqual(
		device.MakeProjectionMatrix(Radian(Degree(45.0f)), 4.0f / 3.0f, 1.0f, 500.0f)));

	camera.SetFarClipDistance(1000.0f);
	CHECK(camera.GetProjectionMatrix().IsNearlyEqual(
		device.MakeProjectionMatrix(Radian(Degree(45.0f)), 4.0f / 3.0f, 1.0f, 1000.0f)));
}

TEST_CASE("Camera projection matrix tracks orthographic parameter changes", "[camera_frustum]")
{
	GraphicsDevice& device = EnsureNullDevice();

	Camera camera("OrthoParamCamera");
	camera.SetNearClipDistance(0.1f);
	camera.SetFarClipDistance(100.0f);

	// Prime the cache in perspective mode, then switch: the switch must invalidate.
	(void)camera.GetProjectionMatrix();

	camera.SetProjectionType(ProjectionType::Orthographic);
	camera.SetOrthoWindow(200.0f, 100.0f);

	CHECK(camera.GetProjectionMatrix().IsNearlyEqual(
		device.MakeOrthographicMatrix(-100.0f, 50.0f, 100.0f, -50.0f, 0.1f, 100.0f)));

	// SetOrthoWindowHeight keeps the aspect ratio (2:1 here).
	camera.SetOrthoWindowHeight(50.0f);
	CHECK(camera.GetProjectionMatrix().IsNearlyEqual(
		device.MakeOrthographicMatrix(-50.0f, 25.0f, 50.0f, -25.0f, 0.1f, 100.0f)));
}

TEST_CASE("Camera custom projection matrix overrides and releases", "[camera_frustum]")
{
	GraphicsDevice& device = EnsureNullDevice();

	Camera camera("CustomProjCamera");
	camera.SetFOVy(Radian(Degree(45.0f)));
	camera.SetAspectRatio(1.0f);
	camera.SetNearClipDistance(0.5f);
	camera.SetFarClipDistance(200.0f);

	// Prime the cache before enabling the custom matrix.
	(void)camera.GetProjectionMatrix();

	Matrix4 custom = Matrix4::Identity;
	custom[0][0] = 2.0f;
	custom[1][1] = 3.0f;

	camera.SetCustomProjMatrix(true, custom);
	CHECK(camera.GetProjectionMatrix().IsNearlyEqual(custom));

	// Disabling the custom matrix must fall back to the computed projection.
	camera.SetCustomProjMatrix(false);
	CHECK(camera.GetProjectionMatrix().IsNearlyEqual(
		device.MakeProjectionMatrix(Radian(Degree(45.0f)), 1.0f, 0.5f, 200.0f)));
}

TEST_CASE("Camera view matrix follows SetOrientation on a parentless camera", "[camera_frustum]")
{
	EnsureNullDevice();

	Camera camera("ParentlessViewCamera");

	// Prime the cached view matrix first so the setter has to invalidate it.
	CHECK(camera.GetViewMatrix().IsNearlyEqual(MakeViewMatrix(Vector3::Zero, Quaternion::Identity)));

	Quaternion rotation(Degree(90.0f), Vector3::UnitY);
	rotation.Normalize();
	camera.SetOrientation(rotation);

	CHECK(camera.GetViewMatrix().IsNearlyEqual(MakeViewMatrix(Vector3::Zero, rotation)));
}

TEST_CASE("Camera view matrix follows parent scene node movement", "[camera_frustum]")
{
	EnsureNullDevice();

	Scene scene;
	Camera* camera = scene.CreateCamera("NodeViewCamera");
	REQUIRE(camera);

	SceneNode* node = scene.GetRootSceneNode().CreateChildSceneNode("NodeViewCameraNode");
	REQUIRE(node);
	node->AttachObject(*camera);

	node->SetPosition(Vector3(0.0f, 0.0f, 20.0f));
	CHECK(camera->GetViewMatrix().IsNearlyEqual(
		MakeViewMatrix(Vector3(0.0f, 0.0f, 20.0f), Quaternion::Identity)));

	// Move and rotate the node without an explicit scene graph update: the camera
	// must still pick up the new derived transform on the next query.
	Quaternion rotation(Degree(45.0f), Vector3::UnitY);
	rotation.Normalize();
	node->SetPosition(Vector3(5.0f, 3.0f, -2.0f));
	node->SetOrientation(rotation);

	CHECK(camera->GetViewMatrix().IsNearlyEqual(
		MakeViewMatrix(Vector3(5.0f, 3.0f, -2.0f), rotation)));

	scene.DestroySceneNode(*node);
	scene.DestroyCamera(*camera);
}

TEST_CASE("Camera frustum planes refresh after the view matrix was re-queried", "[camera_frustum]")
{
	EnsureNullDevice();

	Camera camera("PlaneRefreshCamera");
	camera.SetFOVy(Radian(Degree(60.0f)));
	camera.SetAspectRatio(1.0f);
	camera.SetNearClipDistance(0.1f);
	camera.SetFarClipDistance(1000.0f);

	// Cameras look down -Z by default: a sphere in front is visible.
	const Sphere inFront(Vector3(0.0f, 0.0f, -10.0f), 1.0f);
	CHECK(camera.IsVisible(inFront));

	// Turn the camera around, and query the view matrix BEFORE the visibility
	// check: the frustum planes must still be re-derived afterwards.
	Quaternion turnAround(Degree(180.0f), Vector3::UnitY);
	turnAround.Normalize();
	camera.SetOrientation(turnAround);
	(void)camera.GetViewMatrix();

	CHECK_FALSE(camera.IsVisible(inFront));
	CHECK(camera.IsVisible(Sphere(Vector3(0.0f, 0.0f, 10.0f), 1.0f)));
}

TEST_CASE("Camera world space corners track clip distance changes", "[camera_frustum]")
{
	EnsureNullDevice();

	Camera camera("CornerCamera");
	camera.SetFOVy(Radian(Degree(60.0f)));
	camera.SetAspectRatio(1.0f);
	camera.SetNearClipDistance(0.5f);
	camera.SetFarClipDistance(100.0f);

	const Vector3* corners = camera.GetWorldSpaceCorners();
	REQUIRE(corners);

	Vector3 initialCorners[8];
	for (int i = 0; i < 8; ++i)
	{
		initialCorners[i] = corners[i];
	}

	camera.SetFarClipDistance(200.0f);

	corners = camera.GetWorldSpaceCorners();
	REQUIRE(corners);

	bool anyCornerChanged = false;
	for (int i = 0; i < 8; ++i)
	{
		if (!initialCorners[i].IsNearlyEqual(corners[i]))
		{
			anyCornerChanged = true;
			break;
		}
	}

	CHECK(anyCornerChanged);
}
