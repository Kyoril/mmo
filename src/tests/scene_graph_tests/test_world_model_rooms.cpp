// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "math/constants.h"
#include "math/quaternion.h"
#include "math/radian.h"
#include "scene_graph/world_model.h"
#include "scene_graph/world_model_rooms.h"

#include <map>
#include <vector>

using namespace mmo;

namespace
{
	// Meshes in these tests: a 5 x 0.1 x 5 floor tile centred on its origin, and a 2 x 6 x 2 wall
	// standing on it.
	const std::map<String, AABB> s_meshes{
		{ "Models/Test/Floor_01.hmsh", AABB(Vector3(-2.5f, 0.0f, -2.5f), Vector3(2.5f, 0.1f, 2.5f)) },
		{ "Models/Test/Wall_01.hmsh", AABB(Vector3(-1.0f, 0.0f, -1.0f), Vector3(1.0f, 6.0f, 1.0f)) },
	};

	const AABB* LookupMesh(const String& path)
	{
		const auto it = s_meshes.find(path);
		return it == s_meshes.end() ? nullptr : &it->second;
	}

	void AddPiece(WorldModelGroup& group, const String& mesh, const Vector3& position)
	{
		WorldModelMeshRef ref;
		ref.meshPath = mesh;
		ref.position = position;
		group.AddMeshRef(ref);
	}

	/// Room A has floors over x 0..10, room B over x 10..20 (both z -5..5). Each also owns a wall far
	/// inside the other room, so their bounding boxes are the same 0..20 box. Room C is far away.
	/// A door stands in the plane x = 10.
	void BuildTwoRoomsAndAStranger(WorldModel& model, const size_t doorLinksAWith)
	{
		WorldModelGroup& a = model.AddGroup();
		a.SetName("A");
		WorldModelGroup& b = model.AddGroup();
		b.SetName("B");
		WorldModelGroup& c = model.AddGroup();
		c.SetName("C");

		for (const float z : { -2.5f, 2.5f })
		{
			AddPiece(*model.GetGroup(0), "Models/Test/Floor_01.hmsh", Vector3(2.5f, 0.0f, z));
			AddPiece(*model.GetGroup(0), "Models/Test/Floor_01.hmsh", Vector3(7.5f, 0.0f, z));
			AddPiece(*model.GetGroup(1), "Models/Test/Floor_01.hmsh", Vector3(12.5f, 0.0f, z));
			AddPiece(*model.GetGroup(1), "Models/Test/Floor_01.hmsh", Vector3(17.5f, 0.0f, z));
		}
		AddPiece(*model.GetGroup(0), "Models/Test/Wall_01.hmsh", Vector3(15.0f, 0.0f, 0.0f));
		AddPiece(*model.GetGroup(1), "Models/Test/Wall_01.hmsh", Vector3(5.0f, 0.0f, 0.0f));
		AddPiece(*model.GetGroup(2), "Models/Test/Floor_01.hmsh", Vector3(2.5f, 0.0f, 40.0f));

		const AABB shared(Vector3(0.0f, 0.0f, -5.0f), Vector3(20.0f, 6.0f, 5.0f));
		model.GetGroup(0)->SetBoundingBox(shared);
		model.GetGroup(1)->SetBoundingBox(shared);
		model.GetGroup(2)->SetBoundingBox(AABB(Vector3(0.0f, 0.0f, 37.5f), Vector3(5.0f, 6.0f, 42.5f)));

		Portal& door = model.AddPortal();
		door.SetTransform(Vector3(10.0f, 1.5f, 0.0f), Quaternion(Radian(Pi * 0.5f), Vector3::UnitY), Vector3::UnitScale);
		door.SetDimensions(2.0f, 3.0f);
		model.GetGroup(0)->GetPortalRefs().push_back(WorldModelPortalRef{ 0, static_cast<uint16>(doorLinksAWith), 1 });
		model.GetGroup(doorLinksAWith)->GetPortalRefs().push_back(WorldModelPortalRef{ 0, 0, -1 });
	}

	std::vector<size_t> GroupsAt(const WorldModel& model, const Vector3& point)
	{
		std::vector<size_t> groups;
		for (size_t i = 0; i < model.GetGroupCount(); ++i)
		{
			if (model.GetGroup(i)->ContainsPoint(point))
			{
				groups.push_back(i);
			}
		}
		return groups;
	}
}

TEST_CASE("Walkable room pieces are recognised by name", "[world_model_rooms]")
{
	CHECK(IsWalkableRoomPiece("Models/Dungeon/Floor_01.hmsh"));
	CHECK(IsWalkableRoomPiece("Models\\Dungeon\\Staircase_03.hmsh"));
	CHECK(IsWalkableRoomPiece("Models/Dungeon/PLATFORM_01.hmsh"));
	CHECK_FALSE(IsWalkableRoomPiece("Models/Dungeon/FP_Wall_02.hmsh"));
	CHECK_FALSE(IsWalkableRoomPiece("Models/Floors/Column_08.hmsh"));
}

TEST_CASE("Room volumes follow the floors, not the walls", "[world_model_rooms]")
{
	WorldModel model;
	BuildTwoRoomsAndAStranger(model, 1);

	// Without volumes the rooms are their shared bounding box: the eye is in both everywhere
	CHECK(GroupsAt(model, Vector3(5.0f, 1.5f, 0.0f)) == std::vector<size_t>{ 0, 1 });

	CHECK(DeriveAllRoomVolumes(model, LookupMesh) == 3);
	CHECK(GroupsAt(model, Vector3(5.0f, 1.5f, 0.0f)) == std::vector<size_t>{ 0 });
	CHECK(GroupsAt(model, Vector3(15.0f, 1.5f, 0.0f)) == std::vector<size_t>{ 1 });
	CHECK(GroupsAt(model, Vector3(5.0f, 1.5f, 7.0f)).empty());

	// The four tiles merge into one box from just below the floor to the room's ceiling
	const auto& volumes = model.GetGroup(0)->GetContainmentVolumes();
	REQUIRE(volumes.size() == 1);
	CHECK(volumes[0].boundingBox.min.x == Approx(0.0f));
	CHECK(volumes[0].boundingBox.min.y == Approx(-RoomVolumeBelowFloor));
	CHECK(volumes[0].boundingBox.min.z == Approx(-5.0f));
	CHECK(volumes[0].boundingBox.max.x == Approx(10.0f));
	CHECK(volumes[0].boundingBox.max.y == Approx(6.5f));
	CHECK(volumes[0].boundingBox.max.z == Approx(5.0f));
	CHECK(volumes[0].name == "A floor 1");
}

TEST_CASE("Room volumes cover an L-shaped floor without spilling into its corner", "[world_model_rooms]")
{
	WorldModel model;
	WorldModelGroup& group = model.AddGroup();
	for (const float x : { 2.5f, 7.5f, 12.5f })
	{
		AddPiece(group, "Models/Test/Floor_01.hmsh", Vector3(x, 0.0f, 2.5f));
	}
	AddPiece(group, "Models/Test/Floor_01.hmsh", Vector3(2.5f, 0.0f, 7.5f));
	group.SetBoundingBox(AABB(Vector3(0.0f, 0.0f, 0.0f), Vector3(15.0f, 4.0f, 10.0f)));

	const std::vector<ContainmentVolume> volumes = DeriveRoomVolumes(group, LookupMesh);
	CHECK(volumes.size() == 2);

	group.GetContainmentVolumes() = volumes;
	CHECK(group.ContainsPoint(Vector3(14.0f, 1.0f, 1.0f)));
	CHECK(group.ContainsPoint(Vector3(1.0f, 1.0f, 9.0f)));
	CHECK_FALSE(group.ContainsPoint(Vector3(10.0f, 1.0f, 8.0f)));
}

TEST_CASE("A group without walkable pieces keeps its volumes", "[world_model_rooms]")
{
	WorldModel model;
	WorldModelGroup& group = model.AddGroup();
	AddPiece(group, "Models/Test/Wall_01.hmsh", Vector3::Zero);
	AddPiece(group, "Models/Test/Floor_Missing.hmsh", Vector3::Zero);
	group.AddContainmentVolume(ContainmentVolume::FromAABB(AABB(Vector3::Zero, Vector3(1, 1, 1)), "kept"));

	CHECK(DeriveRoomVolumes(group, LookupMesh).empty());
	CHECK(DeriveAllRoomVolumes(model, LookupMesh) == 0);
	REQUIRE(group.GetContainmentVolumes().size() == 1);
	CHECK(group.GetContainmentVolumes()[0].name == "kept");
}

TEST_CASE("Portal link check relinks a door to the rooms on its sides", "[world_model_rooms]")
{
	WorldModel model;
	BuildTwoRoomsAndAStranger(model, 2);
	DeriveAllRoomVolumes(model, LookupMesh);

	// Report only: nothing changes
	std::vector<PortalLinkFinding> findings = CheckPortalLinks(model, false);
	REQUIRE(findings.size() == 1);
	CHECK(findings[0].result == PortalLinkFinding::Result::Relinked);
	CHECK(findings[0].linkedGroups == std::vector<int32>{ 0, 2 });
	CHECK(model.GetGroup(2)->GetPortalRefs().size() == 1);

	findings = CheckPortalLinks(model, true);
	CHECK(findings[0].result == PortalLinkFinding::Result::Relinked);
	REQUIRE(model.GetGroup(0)->GetPortalRefs().size() == 1);
	CHECK(model.GetGroup(0)->GetPortalRefs()[0].groupIndex == 1);
	REQUIRE(model.GetGroup(1)->GetPortalRefs().size() == 1);
	CHECK(model.GetGroup(1)->GetPortalRefs()[0].groupIndex == 0);
	CHECK(model.GetGroup(2)->GetPortalRefs().empty());

	findings = CheckPortalLinks(model, false);
	CHECK(findings[0].result == PortalLinkFinding::Result::Ok);
}

TEST_CASE("Portal link check leaves a door alone when its sides are unclear", "[world_model_rooms]")
{
	// Without volumes both rooms are on both sides of the door
	WorldModel model;
	BuildTwoRoomsAndAStranger(model, 2);

	const std::vector<PortalLinkFinding> findings = CheckPortalLinks(model, true);
	REQUIRE(findings.size() == 1);
	CHECK(findings[0].result == PortalLinkFinding::Result::Undecided);
	CHECK(model.GetGroup(2)->GetPortalRefs().size() == 1);
}
