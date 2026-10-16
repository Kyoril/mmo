// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "memory_source.h"
#include "vector_sink.h"
#include "reader.h"
#include "writer.h"

#include "base/chunk_writer.h"
#include "scene_graph/prop_kit.h"
#include "scene_graph/prop_kit_manager.h"
#include "scene_graph/prop_kit_serializer.h"

#include <vector>

using namespace mmo;

namespace
{
	PropKit makeKit()
	{
		PropKit kit;
		kit.name = "Small House - Default";
		kit.previewWorldModel = "Models/FalwynPlains/Buildings/HouseTest.hwmo";

		WorldModelMeshRef bed;
		bed.meshPath = "Models/FalwynPlains/Props/SM_hc_Bed_A.hmsh";
		bed.name = "SM_hc_Bed_A";
		bed.materialOverride = "Textures/FalwynPlains/Props/Bed_Wood.hmi";
		bed.position = Vector3(-4.69f, 1.0f, 5.69f);
		bed.rotation = Quaternion(0.9f, 0.1f, 0.4f, 0.1f);
		bed.scale = Vector3(1.0f, 1.0f, 1.0f);
		bed.visible = true;
		kit.props.push_back(bed);

		WorldModelMeshRef chair;
		chair.meshPath = "Models/FalwynPlains/Props/SM_hc_Chair_A.hmsh";
		chair.name = "SM_hc_Chair_A";
		chair.position = Vector3(1.0f, 0.0f, -2.0f);
		chair.scale = Vector3(1.5f, 1.5f, 1.5f);
		chair.visible = false;
		kit.props.push_back(chair);

		WorldModelLight light;
		light.type = WorldModelLight::LightType::Omni;
		light.useAttenuation = true;
		light.color = 0xffce8115;
		light.position = Vector3(0.0f, 2.5f, 1.0f);
		light.intensity = 2.2f;
		light.rotation = Quaternion::Identity;
		light.attenuationStart = 0.0f;
		light.attenuationEnd = 11.0f;
		light.fogScattering = 0.5f;
		kit.lights.push_back(light);
		return kit;
	}

	std::vector<char> serialize(const PropKit& kit)
	{
		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		PropKitSerializer serializer;
		serializer.Serialize(kit, writer);
		return buffer;
	}

	bool deserialize(const std::vector<char>& buffer, PropKit& kit)
	{
		io::MemorySource source{ buffer.data(), buffer.data() + buffer.size() };
		io::Reader reader{ source };
		PropKitDeserializer deserializer{ kit };
		return deserializer.Read(reader);
	}

	void checkProp(const WorldModelMeshRef& a, const WorldModelMeshRef& b)
	{
		CHECK(a.meshPath == b.meshPath);
		CHECK(a.name == b.name);
		CHECK(a.materialOverride == b.materialOverride);
		CHECK(a.position.x == Approx(b.position.x));
		CHECK(a.position.y == Approx(b.position.y));
		CHECK(a.position.z == Approx(b.position.z));
		CHECK(a.rotation.w == Approx(b.rotation.w));
		CHECK(a.rotation.x == Approx(b.rotation.x));
		CHECK(a.rotation.y == Approx(b.rotation.y));
		CHECK(a.rotation.z == Approx(b.rotation.z));
		CHECK(a.scale.x == Approx(b.scale.x));
		CHECK(a.scale.y == Approx(b.scale.y));
		CHECK(a.scale.z == Approx(b.scale.z));
		CHECK(a.visible == b.visible);
	}
}

TEST_CASE("Prop kit round-trips name, preview, props and lights", "[prop_kit]")
{
	const PropKit source = makeKit();

	PropKit loaded;
	REQUIRE(deserialize(serialize(source), loaded));

	CHECK(loaded.name == source.name);
	CHECK(loaded.previewWorldModel == source.previewWorldModel);

	REQUIRE(loaded.props.size() == 2);
	checkProp(loaded.props[0], source.props[0]);
	checkProp(loaded.props[1], source.props[1]);
	CHECK(loaded.props[1].visible == false);

	REQUIRE(loaded.lights.size() == 1);
	const WorldModelLight& a = loaded.lights[0];
	const WorldModelLight& b = source.lights[0];
	CHECK(a.type == b.type);
	CHECK(a.useAttenuation == b.useAttenuation);
	CHECK(a.color == 0xffce8115);
	CHECK(a.position.x == Approx(b.position.x));
	CHECK(a.position.y == Approx(b.position.y));
	CHECK(a.position.z == Approx(b.position.z));
	CHECK(a.intensity == Approx(2.2f));
	CHECK(a.rotation.w == Approx(b.rotation.w));
	CHECK(a.attenuationStart == Approx(b.attenuationStart));
	CHECK(a.attenuationEnd == Approx(11.0f));
	CHECK(a.innerConeAngle == Approx(b.innerConeAngle));
	CHECK(a.outerConeAngle == Approx(b.outerConeAngle));
	CHECK(a.fogScattering == Approx(0.5f));
}

TEST_CASE("Prop kit reader skips unknown chunks", "[prop_kit]")
{
	std::vector<char> buffer;
	{
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };

		{
			ChunkWriter chunk(MakeChunkMagic('KVER'), writer);
			writer << io::write<uint32>(1);
			chunk.Finish();
		}
		{
			ChunkWriter chunk(MakeChunkMagic('KNAM'), writer);
			writer << io::write_dynamic_range<uint16>(String("Kit"));
			chunk.Finish();
		}
		{
			ChunkWriter chunk(MakeChunkMagic('KZZZ'), writer);
			writer << io::write<uint32>(0xdeadbeef) << io::write<uint32>(0x12345678);
			chunk.Finish();
		}
		{
			ChunkWriter chunk(MakeChunkMagic('KPRP'), writer);
			writer << io::write<uint32>(1)
				<< io::write_dynamic_range<uint16>(String("mesh.hmsh"))
				<< io::write_dynamic_range<uint16>(String("Prop"))
				<< io::write_dynamic_range<uint16>(String(""))
				<< io::write<float>(1.0f) << io::write<float>(2.0f) << io::write<float>(3.0f)
				<< io::write<float>(1.0f) << io::write<float>(0.0f) << io::write<float>(0.0f) << io::write<float>(0.0f)
				<< io::write<float>(1.0f) << io::write<float>(1.0f) << io::write<float>(1.0f)
				<< io::write<uint8>(1);
			chunk.Finish();
		}
	}

	PropKit loaded;
	REQUIRE(deserialize(buffer, loaded));
	CHECK(loaded.name == "Kit");
	REQUIRE(loaded.props.size() == 1);
	CHECK(loaded.props[0].meshPath == "mesh.hmsh");
	CHECK(loaded.props[0].position.z == Approx(3.0f));
}

TEST_CASE("Prop kit without a version chunk fails to load", "[prop_kit]")
{
	std::vector<char> buffer;
	{
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		ChunkWriter chunk(MakeChunkMagic('KNAM'), writer);
		writer << io::write_dynamic_range<uint16>(String("Kit"));
		chunk.Finish();
	}

	PropKit loaded;
	CHECK_FALSE(deserialize(buffer, loaded));
}

TEST_CASE("Prop kit manager returns the cached kit for the same path", "[prop_kit]")
{
	PropKitManager::Get().Clear();

	auto kit = std::make_shared<PropKit>(makeKit());
	PropKitManager::Get().Insert("Models/Kits/Test.hkit", kit);

	CHECK(PropKitManager::Get().Find("Models/Kits/Test.hkit") == kit);
	CHECK(PropKitManager::Get().Load("models/kits/test.hkit") == kit);

	PropKitManager::Get().Remove("Models/Kits/Test.hkit");
	CHECK(PropKitManager::Get().Find("Models/Kits/Test.hkit") == nullptr);
	PropKitManager::Get().Clear();
}
