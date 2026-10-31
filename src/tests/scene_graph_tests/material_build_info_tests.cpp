// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <cstring>
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

	/// Reads the version out of the file-version chunk (magic, size, version).
	uint32 ReadVersion(const std::string& bytes)
	{
		REQUIRE(bytes.size() >= 12);
		REQUIRE(bytes.compare(0, 4, "HMAT") == 0);
		uint32 version = 0;
		std::memcpy(&version, bytes.data() + 8, sizeof(version));
		return version;
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

	// Files without build info keep the old version and carry no SBLD chunk.
	REQUIRE(ReadVersion(bytes) == material_version::Version_0_7);
	REQUIRE(bytes.find("SBLD") == std::string::npos);

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

TEST_CASE("layer bindings without build info write v0.8 and no SBLD chunk", "[material]")
{
	Material source("bound");
	source.SetLayerBlendSharpnessParam("Sharpness");

	const std::string bytes = Serialize(source);
	REQUIRE(ReadVersion(bytes) == material_version::Version_0_8);
	REQUIRE(bytes.find("SBLD") == std::string::npos);
}

TEST_CASE("build info forces v0.9 and writes an SBLD chunk", "[material]")
{
	Material source("built");
	source.SetGraphHash(MakeHash(5));

	const std::string bytes = Serialize(source);
	REQUIRE(ReadVersion(bytes) == material_version::Version_0_9);
	REQUIRE(bytes.find("SBLD") != std::string::npos);
}

TEST_CASE("truncated build info chunk fails gracefully", "[material]")
{
	Material source("truncated");
	ShaderBuildInfo info;
	info.graphHash = MakeHash(1);
	info.functions.push_back({ "Functions/Alpha.hfn", MakeHash(2) });
	source.SetGraphHash(MakeHash(1));
	source.SetShaderBuildInfo("D3D11", info);

	const std::string bytes = Serialize(source);
	const size_t chunkPos = bytes.find("SBLD");
	REQUIRE(chunkPos != std::string::npos);

	// Cut in the middle of the first platform entry (past the graph hash and platform count).
	const std::string cut = bytes.substr(0, chunkPos + 8 + 1 + 20 + 1 + 3);

	Material loaded("loaded");
	REQUIRE_FALSE(Deserialize(cut, loaded));
	REQUIRE(loaded.GetShaderBuildInfo("D3D11") == nullptr);
}
