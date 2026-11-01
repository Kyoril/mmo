// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

#include "graphics/material.h"
#include "graphics/shader_build_info.h"
#include "material_graph/material_shader_cache.h"
#include "material_graph/material_shader_pack.h"

using namespace mmo;

namespace
{
	/// A fresh directory under the system temp folder, removed again at the end of the test.
	struct TempDir
	{
		TempDir()
		{
			std::random_device random;
			path = std::filesystem::temp_directory_path() / ("mmo_shader_cache_" + std::to_string(random()));
			std::filesystem::create_directories(path);
		}

		~TempDir()
		{
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}

		std::filesystem::path path;
	};

	ShaderBuildInfo MakeInfo(const uint8 graphByte)
	{
		ShaderBuildInfo info;
		info.graphHash.fill(graphByte);
		info.generatorVersion = material_compiler_version::Generator;
		info.backendVersion = material_compiler_version::GetBackendVersion("TEST");
		info.functions.push_back({ "Functions/F.hmf", {} });
		return info;
	}

	void FillMaterial(Material& material, const ShaderBuildInfo& info)
	{
		std::vector<uint8> vs = { 1, 2, 3 };
		std::vector<uint8> ps = { 4, 5 };
		material.SetVertexShaderCode("TEST", VertexShaderType::Default, vs);
		material.SetPixelShaderCode("TEST", PixelShaderType::Forward, ps);
		material.SetShaderBuildInfo("TEST", info);
	}
}

TEST_CASE("cache hit requires current build info", "[shader_cache]")
{
	TempDir dir;
	const MaterialShaderCache cache(dir.path / "Shaders");

	Material source("Models/M.hmat");
	FillMaterial(source, MakeInfo(7));
	cache.Store("Models/M.hmat", "TEST", source);

	Material hit("Models/M.hmat");
	REQUIRE(cache.TryLoad("Models/M.hmat", "TEST", MakeInfo(7), hit));
	const auto vs = hit.GetVertexShaderCode("TEST", VertexShaderType::Default);
	CHECK(std::vector<uint8>(vs.begin(), vs.end()) == std::vector<uint8>{ 1, 2, 3 });
	REQUIRE(hit.GetShaderBuildInfo("TEST") != nullptr);
	CHECK(hit.GetGraphHash() == MakeInfo(7).graphHash);

	// The graph changed since the shaders were cached
	Material miss("Models/M.hmat");
	CHECK_FALSE(cache.TryLoad("Models/M.hmat", "TEST", MakeInfo(8), miss));
	CHECK(miss.GetVertexShaderCode("TEST", VertexShaderType::Default).empty());

	// A function changed
	ShaderBuildInfo changedFunction = MakeInfo(7);
	changedFunction.functions[0].hash.fill(1);
	CHECK_FALSE(cache.TryLoad("Models/M.hmat", "TEST", changedFunction, miss));

	// Nothing cached for another platform
	CHECK_FALSE(cache.TryLoad("Models/M.hmat", "OTHER", MakeInfo(7), miss));
}

TEST_CASE("cache key distinguishes folders", "[shader_cache]")
{
	const MaterialShaderCache cache("Cache/Shaders");
	CHECK(cache.GetFilePath("A/M.hmat", "TEST") != cache.GetFilePath("B/M.hmat", "TEST"));
	CHECK(cache.GetFilePath("A/M.hmat", "TEST") != cache.GetFilePath("A/M.hmat", "OTHER"));
	CHECK(cache.GetFilePath("A/M.hmat", "TEST") == cache.GetFilePath("A/M.hmat", "TEST"));
}

TEST_CASE("corrupt cache entry is a miss", "[shader_cache]")
{
	TempDir dir;
	const MaterialShaderCache cache(dir.path);
	{
		std::ofstream file(cache.GetFilePath("M.hmat", "TEST"), std::ios::binary);
		file << "SPK1garbage";
	}

	Material material("M.hmat");
	CHECK_FALSE(cache.TryLoad("M.hmat", "TEST", MakeInfo(7), material));
}
