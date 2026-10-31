// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <sstream>

#include "binary_io/stream_sink.h"
#include "binary_io/stream_source.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "graphics/material.h"
#include "graphics/shader_build_info.h"
#include "scene_graph/material_serializer.h"

using namespace mmo;

namespace
{
	std::string Serialize(const Material& material)
	{
		std::ostringstream stream;
		io::StreamSink sink{ stream };
		io::Writer writer{ sink };
		MaterialSerializer serializer{};
		serializer.Export(material, writer);
		return stream.str();
	}

	bool Deserialize(const std::string& bytes, Material& outMaterial)
	{
		std::istringstream stream{ bytes };
		io::StreamSource source{ stream };
		io::Reader reader{ source };
		MaterialDeserializer deserializer{ outMaterial };
		return deserializer.Read(reader);
	}

	SHA1Hash MakeHash(unsigned char seed)
	{
		SHA1Hash hash{};
		for (size_t i = 0; i < hash.size(); ++i)
		{
			hash[i] = static_cast<unsigned char>(seed + i);
		}
		return hash;
	}
}

TEST_CASE("build info round-trips per platform", "[material]")
{
	Material source("build_info");

	ShaderBuildInfo d3d;
	d3d.graphHash = MakeHash(1);
	d3d.generatorVersion = 1;
	d3d.backendVersion = 1;
	d3d.functions.push_back({ "Functions/Zeta.hfn", MakeHash(10) });
	d3d.functions.push_back({ "Functions/Alpha.hfn", MakeHash(20) });

	ShaderBuildInfo metal;
	metal.graphHash = MakeHash(2);
	metal.generatorVersion = 1;
	metal.backendVersion = 3;

	source.SetGraphHash(MakeHash(1));
	source.SetShaderBuildInfo("METAL", metal);
	source.SetShaderBuildInfo("D3D11", d3d);

	// Setting keeps the function list sorted by path.
	REQUIRE(source.GetShaderBuildInfo("D3D11")->functions.front().path == "Functions/Alpha.hfn");

	const std::string bytes = Serialize(source);

	Material loaded("loaded");
	REQUIRE(Deserialize(bytes, loaded));

	REQUIRE(loaded.GetGraphHash().has_value());
	REQUIRE(*loaded.GetGraphHash() == MakeHash(1));

	const ShaderBuildInfo* readD3d = loaded.GetShaderBuildInfo("D3D11");
	REQUIRE(readD3d != nullptr);
	REQUIRE(readD3d->graphHash == MakeHash(1));
	REQUIRE(readD3d->generatorVersion == 1);
	REQUIRE(readD3d->backendVersion == 1);
	REQUIRE(readD3d->functions.size() == 2);
	REQUIRE(readD3d->functions[0].path == "Functions/Alpha.hfn");
	REQUIRE(readD3d->functions[0].hash == MakeHash(20));
	REQUIRE(readD3d->functions[1].path == "Functions/Zeta.hfn");
	REQUIRE(readD3d->functions[1].hash == MakeHash(10));

	const ShaderBuildInfo* readMetal = loaded.GetShaderBuildInfo("METAL");
	REQUIRE(readMetal != nullptr);
	REQUIRE(readMetal->graphHash == MakeHash(2));
	REQUIRE(readMetal->backendVersion == 3);
	REQUIRE(readMetal->functions.empty());

	REQUIRE(loaded.GetShaderBuildInfo("VULKAN") == nullptr);
}

TEST_CASE("legacy material has no build info", "[material]")
{
	Material source("legacy");
	const std::string bytes = Serialize(source);

	// v0.7 / v0.8 files do not carry build info, so the version stays below 0.9.
	REQUIRE(bytes.find("DLBS") == std::string::npos);

	Material loaded("loaded");
	REQUIRE(Deserialize(bytes, loaded));
	REQUIRE(loaded.GetShaderBuildInfo("D3D11") == nullptr);
	REQUIRE_FALSE(loaded.GetGraphHash().has_value());
	REQUIRE(Serialize(loaded) == bytes);
}

TEST_CASE("backend versions", "[material]")
{
	REQUIRE(material_compiler_version::GetBackendVersion("D3D11") == 1);
	REQUIRE(material_compiler_version::GetBackendVersion("METAL") == 1);
	REQUIRE(material_compiler_version::GetBackendVersion("OTHER") == 0);
}
