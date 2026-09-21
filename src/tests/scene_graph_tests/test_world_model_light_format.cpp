// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "memory_source.h"
#include "vector_sink.h"
#include "reader.h"
#include "writer.h"

#include "scene_graph/world_model.h"
#include "scene_graph/world_model_serializer.h"

#include <vector>

using namespace mmo;

namespace
{
	WorldModelLight makeSpot()
	{
		WorldModelLight light;
		light.type = WorldModelLight::LightType::Spot;
		light.useAttenuation = true;
		light.color = 0xFFFF8040;
		light.position = Vector3(1.0f, 2.0f, 3.0f);
		light.intensity = 2.5f;
		light.rotation = Quaternion::Identity;
		light.attenuationStart = 0.0f;
		light.attenuationEnd = 12.0f;
		light.innerConeAngle = 20.0f;
		light.outerConeAngle = 70.0f;
		light.fogScattering = 3.0f;
		return light;
	}

	std::vector<char> serialize(const WorldModel& model, const WorldModelVersion version)
	{
		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		WorldModelSerializer serializer;
		serializer.Serialize(model, writer, version);
		return buffer;
	}

	bool deserialize(const std::vector<char>& buffer, WorldModel& model)
	{
		io::MemorySource source{ buffer.data(), buffer.data() + buffer.size() };
		io::Reader reader{ source };
		WorldModelDeserializer deserializer{ model };
		return deserializer.Read(reader);
	}
}

TEST_CASE("World model lights round-trip cone angles and fog scattering", "[world_model]")
{
	WorldModel source;
	source.GetLights().push_back(makeSpot());

	WorldModel loaded;
	REQUIRE(deserialize(serialize(source, world_model_version::Latest), loaded));
	REQUIRE(loaded.GetLights().size() == 1);

	const WorldModelLight& light = loaded.GetLights().front();
	CHECK(light.type == WorldModelLight::LightType::Spot);
	CHECK(light.attenuationEnd == Approx(12.0f));
	CHECK(light.innerConeAngle == Approx(20.0f));
	CHECK(light.outerConeAngle == Approx(70.0f));
	CHECK(light.fogScattering == Approx(3.0f));
}

TEST_CASE("Version 2.0 world model lights load with default cone and fog values", "[world_model]")
{
	WorldModel source;
	source.GetLights().push_back(makeSpot());

	WorldModel loaded;
	REQUIRE(deserialize(serialize(source, world_model_version::Version_2_0), loaded));
	REQUIRE(loaded.GetLights().size() == 1);

	const WorldModelLight& light = loaded.GetLights().front();
	CHECK(light.intensity == Approx(2.5f));
	CHECK(light.attenuationEnd == Approx(12.0f));
	CHECK(light.innerConeAngle == Approx(30.0f));
	CHECK(light.outerConeAngle == Approx(45.0f));
	CHECK(light.fogScattering == Approx(1.0f));
}
