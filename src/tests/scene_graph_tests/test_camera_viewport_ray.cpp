// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "null_device.h"
#include "math/degree.h"
#include "math/ray.h"
#include "scene_graph/scene.h"
#include "scene_graph/camera.h"
#include "scene_graph/scene_node.h"

#include <cmath>

using namespace mmo;

using mmo::test::EnsureNullDevice;

namespace
{
	/// The exact picking direction for a viewport point of a perspective camera, built in camera space.
	Vector3 ExpectedDirection(const Camera& camera, const Quaternion& orientation, const float viewportX, const float viewportY)
	{
		const float tanY = std::tan(camera.GetFOVy().GetValueRadians() * 0.5f);
		const float tanX = tanY * camera.GetAspectRatio();
		const float nx = viewportX * 2.0f - 1.0f;
		const float ny = 1.0f - viewportY * 2.0f;
		return (orientation * Vector3(nx * tanX, ny * tanY, -1.0f)).NormalizedCopy();
	}
}

// The world editor's camera has a near plane of 0.01 and flies thousands of units from the
// origin. A picking ray unprojected through the inverse view-projection there takes the
// difference of two world points a few millimetres apart, each rounded at the magnitude of the
// camera position, which leaves the direction off by up to a degree: the terrain brush lands
// metres beside the cursor and jumps as the mouse moves.
TEST_CASE("Camera viewport rays stay exact far from the origin with a tiny near plane", "[camera_viewport_ray]")
{
	EnsureNullDevice();

	Scene scene;
	Camera* camera = scene.CreateCamera("ViewportRayCamera");
	REQUIRE(camera);
	camera->SetFOVy(Radian(Degree(60.0f)));
	camera->SetAspectRatio(16.0f / 9.0f);
	camera->SetNearClipDistance(0.01f);
	camera->SetFarClipDistance(2400.0f);

	SceneNode* node = scene.GetRootSceneNode().CreateChildSceneNode("ViewportRayCameraNode");
	REQUIRE(node);
	node->AttachObject(*camera);

	Quaternion orientation = Quaternion(Degree(30.0f), Vector3::UnitY) * Quaternion(Degree(-35.0f), Vector3::UnitX);
	orientation.Normalize();

	const Vector3 position(3466.25f, 120.5f, 8266.75f);
	node->SetPosition(position);
	node->SetOrientation(orientation);

	float worstAngle = 0.0f;
	float worstOriginOffset = 0.0f;
	for (int32 j = 0; j <= 40; ++j)
	{
		for (int32 i = 0; i <= 40; ++i)
		{
			const float viewportX = static_cast<float>(i) / 40.0f;
			const float viewportY = static_cast<float>(j) / 40.0f;

			const Ray ray = camera->GetCameraToViewportRay(viewportX, viewportY, 50000.0f);
			const Vector3 expected = ExpectedDirection(*camera, orientation, viewportX, viewportY);

			worstAngle = std::max(worstAngle, ray.GetDirection().Cross(expected).GetLength());

			// The origin lies on the expected line through the eye, within the near plane distance.
			const Vector3 toOrigin = ray.origin - position;
			worstOriginOffset = std::max(worstOriginOffset, toOrigin.Cross(expected).GetLength());
			CHECK(toOrigin.GetLength() <= 0.05f);
		}
	}

	// 1e-5 rad is 5 mm at 500 units, well below a terrain cell.
	CHECK(worstAngle < 1e-5f);
	CHECK(worstOriginOffset < 0.01f);

	scene.DestroySceneNode(*node);
	scene.DestroyCamera(*camera);
}

TEST_CASE("Orthographic camera viewport rays are parallel and offset across the window", "[camera_viewport_ray]")
{
	EnsureNullDevice();

	Camera camera("OrthoViewportRayCamera");
	camera.SetProjectionType(ProjectionType::Orthographic);
	camera.SetOrthoWindow(200.0f, 100.0f);
	camera.SetNearClipDistance(1.0f);
	camera.SetFarClipDistance(1000.0f);

	// Parentless and unrotated: looks down -Z from the origin.
	const Ray centre = camera.GetCameraToViewportRay(0.5f, 0.5f, 1000.0f);
	const Ray corner = camera.GetCameraToViewportRay(1.0f, 0.0f, 1000.0f);

	CHECK(centre.GetDirection().Cross(Vector3::NegativeUnitZ).GetLength() < 1e-5f);
	CHECK(corner.GetDirection().Cross(Vector3::NegativeUnitZ).GetLength() < 1e-5f);
	CHECK(std::abs(corner.origin.x - centre.origin.x - 100.0f) < 1e-3f);
	CHECK(std::abs(corner.origin.y - centre.origin.y - 50.0f) < 1e-3f);
}
