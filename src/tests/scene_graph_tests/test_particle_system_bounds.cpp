// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "null_device.h"
#include "math/aabb.h"
#include "scene_graph/camera.h"
#include "scene_graph/particle_emitter.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"

using namespace mmo;
using mmo::test::EnsureNullDevice;

namespace
{
	/// The emitter node of an effect placed out in the world, far enough from the origin that a
	/// transform applied twice lands nowhere near it.
	const Vector3 EmitterPosition(3130.0f, 40.0f, 9967.0f);

	/// Particles that stay where they are spawned, so the expected bounds are exactly the
	/// emitter position grown by half a particle.
	EmitterParameters StationaryParticles()
	{
		EmitterParameters params;
		params.gravity = Vector3::Zero;
		params.minVelocity = Vector3::Zero;
		params.maxVelocity = Vector3::Zero;
		params.minLifetime = 10.0f;
		params.maxLifetime = 10.0f;
		params.minStartSize = 1.0f;
		params.maxStartSize = 1.0f;
		return params;
	}

	/// The simulation reads the scene's first camera to sort and to build its quads, so a scene
	/// driving particles always has one.
	void AddCamera(Scene& scene, const String& name)
	{
		Camera* camera = scene.CreateCamera(name);
		scene.GetRootSceneNode().CreateChildSceneNode()->AttachObject(*camera);
	}

	/// Runs the system for a second in frame-sized steps, the way the scene drives it.
	void Simulate(ParticleSystem& system)
	{
		for (int i = 0; i < 10; ++i)
		{
			system.Update(0.1f);
		}
	}
}

// A particle system simulates in world space and bakes world positions into its vertex buffer
// (ParticleRenderable::GetWorldTransform returns identity), so the box it reports is already a
// world box. MovableObject::GetWorldBoundingBox transforms the box it gets by the parent node's
// full transform - the same trap WorldModelInstance::EnsureBatchRootNode avoids for its
// world-space batches. Applied to an emitter 10000 units from the origin, that puts the box
// used for frustum culling (RenderQueue::ProcessVisibleObject) twice as far out, so the effect
// is drawn only while the camera happens to look at that phantom location and is culled away
// when it looks at the particles themselves.

TEST_CASE("Particle system world bounds stay at the emitter", "[particle_bounds]")
{
	EnsureNullDevice();

	Scene scene;
	AddCamera(scene, "BoundsTestCamera");

	SceneNode* node = scene.GetRootSceneNode().CreateChildSceneNode();
	node->SetPosition(EmitterPosition);

	ParticleSystem* system = scene.CreateParticleEmitter("BoundsTest");
	REQUIRE(system != nullptr);
	system->SetParameters(StationaryParticles());
	node->AttachObject(*system);
	system->Play();

	Simulate(*system);
	REQUIRE(system->GetTotalParticleCount() > 0);

	const AABB& worldBounds = system->GetWorldBoundingBox(true);
	CHECK(worldBounds.min.x == Approx(EmitterPosition.x - 0.5f));
	CHECK(worldBounds.min.y == Approx(EmitterPosition.y - 0.5f));
	CHECK(worldBounds.min.z == Approx(EmitterPosition.z - 0.5f));
	CHECK(worldBounds.max.x == Approx(EmitterPosition.x + 0.5f));
	CHECK(worldBounds.max.y == Approx(EmitterPosition.y + 0.5f));
	CHECK(worldBounds.max.z == Approx(EmitterPosition.z + 0.5f));
}

// The derived world box is only recomputed when something announces a change, and a placed
// effect never moves its node. The particle cloud, however, grows with every frame, so the box
// has to follow the simulation instead of freezing on whatever the first frame of culling saw.

TEST_CASE("Particle system world bounds follow the particle cloud", "[particle_bounds]")
{
	EnsureNullDevice();

	Scene scene;
	AddCamera(scene, "GrowingBoundsTestCamera");

	SceneNode* node = scene.GetRootSceneNode().CreateChildSceneNode();
	node->SetPosition(EmitterPosition);

	ParticleSystem* system = scene.CreateParticleEmitter("GrowingBoundsTest");
	REQUIRE(system != nullptr);

	EmitterParameters params = StationaryParticles();
	// Rising particles, so the cloud is taller after simulating than it was at the first frame.
	params.minVelocity = Vector3(0.0f, 5.0f, 0.0f);
	params.maxVelocity = Vector3(0.0f, 5.0f, 0.0f);
	system->SetParameters(params);
	node->AttachObject(*system);
	system->Play();

	// The first frame of culling, which is what primes the base class' cache.
	system->Update(0.1f);
	const AABB firstFrame = system->GetWorldBoundingBox(true);

	Simulate(*system);

	const AABB& worldBounds = system->GetWorldBoundingBox(true);
	CHECK(worldBounds.max.y > firstFrame.max.y + 1.0f);
}
