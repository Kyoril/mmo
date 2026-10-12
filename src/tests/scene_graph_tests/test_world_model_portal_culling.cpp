// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "memory_source.h"
#include "vector_sink.h"
#include "reader.h"
#include "writer.h"

#include "math/constants.h"
#include "math/degree.h"
#include "math/matrix4.h"
#include "math/quaternion.h"
#include "math/radian.h"
#include "scene_graph/world_model.h"
#include "scene_graph/world_model_portal_culling.h"
#include "scene_graph/world_model_serializer.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <cmath>
#include <vector>

using namespace mmo;

namespace
{
	// Rooms in these tests are boxes four units tall, laid out along X, with portals in the YZ
	// plane between them. The eye is at head height (y = 2).

	/// A 90 degree frustum looking along a horizontal direction from the eye: everything whose offset
	/// v from the eye has dir.v >= |side.v| and dir.v >= |up.v|, plus a near plane just in front.
	PortalFrustum LookAlong(const Vector3& eye, const Vector3& direction)
	{
		const Vector3 side(-direction.z, 0.0f, direction.x);
		const float s = 1.0f / std::sqrt(2.0f);
		PortalFrustum frustum;
		frustum.origin = eye;
		frustum.planes.emplace_back((direction + side) * s, eye);
		frustum.planes.emplace_back((direction - side) * s, eye);
		frustum.planes.emplace_back((direction + Vector3::UnitY) * s, eye);
		frustum.planes.emplace_back((direction - Vector3::UnitY) * s, eye);
		frustum.planes.emplace_back(direction, eye + direction * 0.01f);
		return frustum;
	}

	PortalFrustum LookAlongX(const Vector3& eye, const float sign)
	{
		return LookAlong(eye, Vector3(sign, 0.0f, 0.0f));
	}

	size_t AddRoom(WorldModel& model, const float minX, const float maxX, const float minZ = -10.0f, const float maxZ = 10.0f)
	{
		WorldModelGroup& group = model.AddGroup();
		group.SetFlags(WorldModelGroupFlags::Interior);
		const AABB box(Vector3(minX, 0.0f, minZ), Vector3(maxX, 4.0f, maxZ));
		group.SetBoundingBox(box);
		group.AddContainmentVolume(ContainmentVolume::FromAABB(box));
		return model.GetGroupCount() - 1;
	}

	/// A portal in the plane x = `x`, `width` wide along Z around `centerZ`, linking two rooms.
	void AddPortal(WorldModel& model, const float x, const float centerZ, const float width,
		const size_t groupA, const size_t groupB)
	{
		Portal& portal = model.AddPortal();
		portal.SetTransform(Vector3(x, 2.0f, centerZ), Quaternion(Radian(Pi * 0.5f), Vector3::UnitY), Vector3::UnitScale);
		portal.SetDimensions(width, 4.0f);

		const uint16 index = static_cast<uint16>(model.GetPortals().size() - 1);
		model.GetGroup(groupA)->GetPortalRefs().push_back(WorldModelPortalRef{ index, static_cast<uint16>(groupB), 1 });
		model.GetGroup(groupB)->GetPortalRefs().push_back(WorldModelPortalRef{ index, static_cast<uint16>(groupA), -1 });
	}

	std::vector<int32> Visible(const WorldModel& model, const PortalFrustum& frustum)
	{
		std::vector<int32> visible;
		CollectVisiblePortalGroups(model, Matrix4::Identity, frustum, visible);
		std::sort(visible.begin(), visible.end());
		return visible;
	}

	bool Contains(const std::vector<int32>& groups, const size_t group)
	{
		return std::find(groups.begin(), groups.end(), static_cast<int32>(group)) != groups.end();
	}
}

TEST_CASE("Portal culling hides rooms whose portals are out of view", "[world_model_portal_culling]")
{
	WorldModel model;
	const size_t a = AddRoom(model, 0.0f, 10.0f);
	const size_t b = AddRoom(model, 10.0f, 20.0f);
	const size_t c = AddRoom(model, 20.0f, 30.0f);
	AddPortal(model, 10.0f, 0.0f, 2.0f, a, b);
	AddPortal(model, 20.0f, 0.0f, 2.0f, b, c);

	// Looking down the row of rooms, every one is seen through the aligned doors
	CHECK(Visible(model, LookAlongX(Vector3(2.0f, 2.0f, 0.0f), 1.0f)) == std::vector<int32>{ 0, 1, 2 });

	// Turned around, the doors are behind the eye: only the eye's room
	CHECK(Visible(model, LookAlongX(Vector3(5.0f, 2.0f, 0.0f), -1.0f)) == std::vector<int32>{ 0 });
}

TEST_CASE("Portal culling starts in every room that contains the eye", "[world_model_portal_culling]")
{
	// Room A's volume reaches 2 units into room B. The eye stands in that overlap looking further
	// into B: A's only door is behind it, so starting from A alone would hide both B and C.
	WorldModel model;
	const size_t a = AddRoom(model, 4.0f, 12.0f);
	const size_t b = AddRoom(model, 10.0f, 20.0f);
	const size_t c = AddRoom(model, 20.0f, 30.0f);
	AddPortal(model, 10.0f, 0.0f, 2.0f, a, b);
	AddPortal(model, 20.0f, 0.0f, 2.0f, b, c);

	const std::vector<int32> visible = Visible(model, LookAlongX(Vector3(11.5f, 2.0f, 0.0f), 1.0f));
	CHECK(Contains(visible, b));
	CHECK(Contains(visible, c));
}

TEST_CASE("Portal culling counts both rooms of a doorway the eye stands in", "[world_model_portal_culling]")
{
	// The volumes meet 0.3 units past the portal plane: the eye, just through the door, is still
	// credited to A only, and the door it walked through is behind it.
	WorldModel model;
	const size_t a = AddRoom(model, 0.0f, 10.3f);
	const size_t b = AddRoom(model, 10.3f, 20.0f);
	AddPortal(model, 10.0f, 0.0f, 2.0f, a, b);

	const std::vector<int32> visible = Visible(model, LookAlongX(Vector3(10.2f, 2.0f, 0.0f), 1.0f));
	CHECK(Contains(visible, a));
	CHECK(Contains(visible, b));
}

TEST_CASE("Portal culling enters a room again through a wider door", "[world_model_portal_culling]")
{
	// A and B share two doors: a slit far to the side (listed first) and a wide door ahead. C is only
	// visible through the wide one. Marking B as done after the slit would lose C.
	WorldModel model;
	const size_t a = AddRoom(model, 0.0f, 10.0f);
	const size_t b = AddRoom(model, 10.0f, 20.0f);
	const size_t c = AddRoom(model, 20.0f, 30.0f);
	AddPortal(model, 10.0f, -6.0f, 0.5f, a, b);
	AddPortal(model, 10.0f, 2.0f, 6.0f, a, b);
	AddPortal(model, 20.0f, 6.0f, 2.0f, b, c);

	const std::vector<int32> visible = Visible(model, LookAlongX(Vector3(2.0f, 2.0f, 0.0f), 1.0f));
	CHECK(visible == std::vector<int32>{ 0, 1, 2 });
}

TEST_CASE("Portal culling outside every room draws the rooms in view", "[world_model_portal_culling]")
{
	WorldModel model;
	AddRoom(model, 0.0f, 10.0f);
	AddRoom(model, 10.0f, 20.0f);

	CHECK(Visible(model, LookAlongX(Vector3(-5.0f, 2.0f, 0.0f), 1.0f)) == std::vector<int32>{ 0, 1 });
	CHECK(Visible(model, LookAlongX(Vector3(-5.0f, 2.0f, 0.0f), -1.0f)).empty());
}

TEST_CASE("Portal culling applies the world model's transform", "[world_model_portal_culling]")
{
	// The same row of rooms, moved 100 units along Z: the eye's room is found in model space and the
	// portals are tested in world space.
	WorldModel model;
	const size_t a = AddRoom(model, 0.0f, 10.0f);
	const size_t b = AddRoom(model, 10.0f, 20.0f);
	AddPortal(model, 10.0f, 0.0f, 2.0f, a, b);

	Matrix4 transform = Matrix4::Identity;
	transform.MakeTrans(Vector3(0.0f, 0.0f, 100.0f));

	std::vector<int32> visible;
	CollectVisiblePortalGroups(model, transform, LookAlongX(Vector3(2.0f, 2.0f, 100.0f), 1.0f), visible);
	std::sort(visible.begin(), visible.end());
	CHECK(visible == std::vector<int32>{ 0, 1 });
}

TEST_CASE("Doorway test covers the portal's opening and a margin around it", "[world_model_portal_culling]")
{
	const std::vector<Vector3> door{
		Vector3(10.0f, 0.0f, -1.0f), Vector3(10.0f, 0.0f, 1.0f), Vector3(10.0f, 4.0f, 1.0f), Vector3(10.0f, 4.0f, -1.0f) };

	CHECK(IsInPortalDoorway(Vector3(10.0f, 2.0f, 0.0f), door));
	CHECK(IsInPortalDoorway(Vector3(10.4f, 2.0f, 0.0f), door));
	CHECK(IsInPortalDoorway(Vector3(9.6f, 2.0f, 1.3f), door));
	CHECK_FALSE(IsInPortalDoorway(Vector3(10.6f, 2.0f, 0.0f), door));
	CHECK_FALSE(IsInPortalDoorway(Vector3(10.0f, 2.0f, 1.6f), door));
	CHECK_FALSE(IsInPortalDoorway(Vector3(10.0f, 5.0f, 0.0f), door));
}

TEST_CASE("Portal rotations survive a save and load", "[world_model_portal_culling]")
{
	// A portal turned 30 degrees about Y is not symmetric under the rotation a misordered quaternion
	// gives it, so its corners show whether the rotation was read back in the order it was written.
	WorldModel source;
	Portal& portal = source.AddPortal();
	portal.SetTransform(Vector3(3.0f, 2.0f, -4.0f), Quaternion(Radian(Degree(30.0f)), Vector3::UnitY), Vector3::UnitScale);
	portal.SetDimensions(3.0f, 5.0f);
	const std::vector<Vector3> expected = portal.GetWorldVertices();

	std::vector<char> buffer;
	{
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		WorldModelSerializer serializer;
		serializer.Serialize(source, writer, world_model_version::Latest);
	}

	WorldModel loaded;
	io::MemorySource memory{ buffer.data(), buffer.data() + buffer.size() };
	io::Reader reader{ memory };
	WorldModelDeserializer deserializer{ loaded };
	REQUIRE(deserializer.Read(reader));
	REQUIRE(loaded.GetPortals().size() == 1);

	const std::vector<Vector3>& actual = loaded.GetPortals()[0]->GetWorldVertices();
	REQUIRE(actual.size() == expected.size());
	for (size_t i = 0; i < expected.size(); ++i)
	{
		CHECK(actual[i].x == Approx(expected[i].x).margin(0.001f));
		CHECK(actual[i].y == Approx(expected[i].y).margin(0.001f));
		CHECK(actual[i].z == Approx(expected[i].z).margin(0.001f));
	}
}

TEST_CASE("Hollow Choir rooms keep the eye's room and every door of it in view", "[world_model_portal_culling]")
{
	// Monastery_001 is the Hollow Choir dungeon. Its rooms once had no containment volumes, so the
	// eye was placed by overlapping bounding boxes - in the wrong room in most of the entrance hall,
	// which then hid the hall itself. The volumes are derived from the floors (scene_graph/world_model_rooms.h).
	std::ifstream file(MMO_SOURCE_DIR "/data/client/Models/Dungeon/Monastery_001.hwmo", std::ios::binary);
	if (!file)
	{
		WARN("data/client is not checked out, skipping");
		return;
	}
	const std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	WorldModel model;
	io::MemorySource memory{ buffer.data(), buffer.data() + buffer.size() };
	io::Reader reader{ memory };
	WorldModelDeserializer deserializer{ model };
	REQUIRE(deserializer.Read(reader));
	REQUIRE(model.GetGroupCount() > 0);

	for (size_t i = 0; i < model.GetGroupCount(); ++i)
	{
		INFO("group " << i << " " << model.GetGroup(i)->GetName());
		CHECK_FALSE(model.GetGroup(i)->GetContainmentVolumes().empty());
	}

	std::vector<std::vector<Vector3>> portals;
	for (const auto& portal : model.GetPortals())
	{
		portals.push_back(portal->GetWorldVertices());
	}

	const Vector3 directions[] = {
		Vector3(1, 0, 0), Vector3(-1, 0, 0), Vector3(0, 0, 1), Vector3(0, 0, -1),
		Vector3(0.7071f, 0, 0.7071f), Vector3(-0.7071f, 0, 0.7071f), Vector3(0.7071f, 0, -0.7071f), Vector3(-0.7071f, 0, -0.7071f) };

	size_t samples = 0;
	size_t shared = 0;
	size_t failures = 0;
	std::ostringstream firstFailure;
	std::vector<int32> visible;

	for (size_t g = 0; g < model.GetGroupCount(); ++g)
	{
		for (const ContainmentVolume& volume : model.GetGroup(g)->GetContainmentVolumes())
		{
			// Eye heights a standing player's camera covers: head height, and a raised third-person camera
			for (const float height : { 3.2f, 5.0f })
			{
				const float y = volume.boundingBox.min.y + height;
				if (y > volume.boundingBox.max.y)
				{
					continue;
				}

				for (float x = volume.boundingBox.min.x + 0.5f; x < volume.boundingBox.max.x; x += 1.0f)
				{
					for (float z = volume.boundingBox.min.z + 0.5f; z < volume.boundingBox.max.z; z += 1.0f)
					{
						const Vector3 eye(x, y, z);
						std::vector<int32> rooms;
						for (size_t i = 0; i < model.GetGroupCount(); ++i)
						{
							if (model.GetGroup(i)->ContainsPoint(eye))
							{
								rooms.push_back(static_cast<int32>(i));
							}
						}

						++samples;
						if (rooms.size() > 1)
						{
							++shared;
						}

						for (const Vector3& direction : directions)
						{
							const PortalFrustum frustum = LookAlong(eye, direction);
							CollectVisiblePortalGroups(model, Matrix4::Identity, frustum, visible);

							for (const int32 room : rooms)
							{
								bool ok = Contains(visible, room);
								int32 missing = room;
								for (const auto& ref : model.GetGroup(room)->GetPortalRefs())
								{
									if (ok && frustum.IsPortalVisible(portals[ref.portalIndex]) && !Contains(visible, ref.groupIndex))
									{
										ok = false;
										missing = ref.groupIndex;
									}
								}

								if (!ok && failures++ == 0)
								{
									firstFailure << "eye (" << x << ", " << y << ", " << z << ") in group " << room
										<< " looking (" << direction.x << ", " << direction.z << "): group " << missing << " culled";
								}
							}
						}
					}
				}
			}
		}
	}

	INFO(firstFailure.str());
	CHECK(failures == 0);

	// Rooms meet at their doors; a few square units of overlap there are fine, rooms swallowing
	// each other (as their bounding boxes did) are not.
	INFO(shared << " of " << samples << " eye positions lie in more than one room");
	CHECK(shared * 50 <= samples);

	// Looking from the Wake (Room_002) through each window into the entrance hall (Room_001), the
	// window straight across opens onto the south yard (Room_003). Its windows once had no portals,
	// so the yard vanished (and the sky showed) whenever no portalled opening was in view as well.
	for (const float windowZ : { -2.0f, -8.0f, -26.0f })
	{
		INFO("window at z " << windowZ);
		CollectVisiblePortalGroups(model, Matrix4::Identity, LookAlong(Vector3(10.0f, 5.0f, windowZ), Vector3(-1, 0, 0)), visible);
		CHECK(Contains(visible, 0));
		CHECK(Contains(visible, 2));
	}
}
