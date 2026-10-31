// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <array>
#include <filesystem>
#include <map>

#include "assets/asset_registry.h"
#include "base/chunk_writer.h"
#include "binary_io/stream_sink.h"
#include "binary_io/writer.h"
#include "material_graph/material_graph.h"
#include "material_graph/material_graph_hash.h"
#include "material_graph/material_node.h"
#include "material_graph/node_layout_store.h"

using namespace mmo;

namespace
{
	class HashLayoutStore final : public INodeLayoutStore
	{
	public:
		std::map<uint32, std::array<float, 4>> bounds;

		bool GetNodeBounds(const uint32 nodeId, float& x, float& y, float& w, float& h) const override
		{
			const auto it = bounds.find(nodeId);
			if (it == bounds.end())
			{
				return false;
			}

			x = it->second[0];
			y = it->second[1];
			w = it->second[2];
			h = it->second[3];
			return true;
		}

		void SetNodeBounds(const uint32 nodeId, const float x, const float y, const float w, const float h) override
		{
			bounds[nodeId] = { x, y, w, h };
		}
	};

	ConstFloatNode* BuildConstGraph(MaterialGraph& graph, const float value)
	{
		auto* constant = graph.CreateNode<ConstFloatNode>();
		REQUIRE(constant != nullptr);
		static_cast<FloatProperty*>(constant->GetProperties()[0])->SetValue(value);
		return constant;
	}

	MaterialFunctionNode* AddFunctionNode(MaterialGraph& graph, const std::string& path)
	{
		auto* node = graph.CreateNode<MaterialFunctionNode>();
		REQUIRE(node != nullptr);
		// Through the property, like the editor does: it is what gets serialized
		static_cast<AssetPathProperty*>(node->GetProperties()[0])->SetValue(AssetPathValue(path, ".hmf"));
		return node;
	}

	SHA1Hash MakeHash(const uint8 seed)
	{
		SHA1Hash hash{};
		hash.fill(seed);
		return hash;
	}

	/// @brief Temp asset root; writes material function files holding a stored hash and a graph.
	struct FunctionFixture
	{
		std::filesystem::path root;

		FunctionFixture()
		{
			root = std::filesystem::temp_directory_path() / "mmo_material_graph_hash_test";
			std::filesystem::remove_all(root);
			std::filesystem::create_directories(root / "Functions");
			AssetRegistry::Initialize(root, {});
		}

		~FunctionFixture()
		{
			AssetRegistry::Destroy();
			std::filesystem::remove_all(root);
		}

		/// @param hash Stored hash, or nullptr to write a file without one.
		/// @param fill Fills the function's graph.
		template<typename Fn>
		void WriteFunction(const std::string& path, const SHA1Hash* hash, Fn&& fill) const
		{
			MaterialGraph graph;
			fill(graph);

			const auto file = AssetRegistry::CreateNewFile(path);
			REQUIRE(file);
			io::StreamSink sink{ *file };
			io::Writer writer{ sink };

			if (hash)
			{
				ChunkWriter chunk{ ChunkMagic({ 'F', 'H', 'S', 'H' }), writer };
				writer << io::write_range(*hash);
				chunk.Finish();
			}

			graph.Serialize(writer);
		}
	};
}

TEST_CASE("moving nodes keeps the graph hash", "[hash]")
{
	MaterialGraph graph;
	HashLayoutStore store;
	graph.SetLayoutStore(&store);
	const GraphNode* constant = BuildConstGraph(graph, 1.0f);
	store.bounds[constant->GetId()] = { 1.0f, 2.0f, 3.0f, 4.0f };

	const SHA1Hash before = ComputeGraphHash(graph);
	store.bounds[constant->GetId()] = { 500.0f, -20.0f, 90.0f, 70.0f };
	CHECK(ComputeGraphHash(graph) == before);

	// Without any store the hash is the same as well
	graph.SetLayoutStore(nullptr);
	CHECK(ComputeGraphHash(graph) == before);
}

TEST_CASE("changing a constant changes the graph hash", "[hash]")
{
	MaterialGraph graph;
	auto* constant = BuildConstGraph(graph, 1.0f);
	const SHA1Hash before = ComputeGraphHash(graph);

	static_cast<FloatProperty*>(constant->GetProperties()[0])->SetValue(2.0f);
	CHECK(ComputeGraphHash(graph) != before);
}

TEST_CASE("function edit makes build info stale", "[hash]")
{
	ShaderBuildInfo info;
	info.graphHash = MakeHash(1);
	info.generatorVersion = material_compiler_version::Generator;
	info.backendVersion = material_compiler_version::GetBackendVersion("D3D11");
	info.functions = { { "Functions/A.hmf", MakeHash(2) }, { "Functions/B.hmf", MakeHash(3) } };

	REQUIRE(info.backendVersion != 0);
	CHECK(IsShaderBuildCurrent(info, "D3D11", info.graphHash, info.functions));

	auto changed = info.functions;
	changed[1].hash = MakeHash(4);
	CHECK_FALSE(IsShaderBuildCurrent(info, "D3D11", info.graphHash, changed));

	auto fewer = info.functions;
	fewer.pop_back();
	CHECK_FALSE(IsShaderBuildCurrent(info, "D3D11", info.graphHash, fewer));

	CHECK_FALSE(IsShaderBuildCurrent(info, "D3D11", MakeHash(9), info.functions));
}

TEST_CASE("generator version bump makes build info stale", "[hash]")
{
	ShaderBuildInfo info;
	info.graphHash = MakeHash(1);
	info.generatorVersion = material_compiler_version::Generator - 1;
	info.backendVersion = material_compiler_version::GetBackendVersion("D3D11");
	CHECK_FALSE(IsShaderBuildCurrent(info, "D3D11", info.graphHash, {}));

	info.generatorVersion = material_compiler_version::Generator;
	CHECK(IsShaderBuildCurrent(info, "D3D11", info.graphHash, {}));

	info.backendVersion += 1;
	CHECK_FALSE(IsShaderBuildCurrent(info, "D3D11", info.graphHash, {}));
}

TEST_CASE("hash with missing function", "[hash]")
{
	FunctionFixture fixture;

	MaterialGraph graph;
	AddFunctionNode(graph, "Functions/Missing.hmf");

	std::vector<FunctionHash> out;
	CHECK_FALSE(CollectFunctionHashes(graph, out));
}

TEST_CASE("function without stored hash fails", "[hash]")
{
	FunctionFixture fixture;
	fixture.WriteFunction("Functions/NoHash.hmf", nullptr, [](MaterialGraph& g) { BuildConstGraph(g, 1.0f); });

	MaterialGraph graph;
	AddFunctionNode(graph, "Functions/NoHash.hmf");

	std::vector<FunctionHash> out;
	CHECK_FALSE(CollectFunctionHashes(graph, out));
}

TEST_CASE("function hashes are collected recursively and sorted", "[hash]")
{
	FunctionFixture fixture;
	const SHA1Hash hashA = MakeHash(0xA);
	const SHA1Hash hashB = MakeHash(0xB);
	const SHA1Hash hashC = MakeHash(0xC);

	fixture.WriteFunction("Functions/C.hmf", &hashC, [](MaterialGraph& g) { BuildConstGraph(g, 1.0f); });
	fixture.WriteFunction("Functions/B.hmf", &hashB, [](MaterialGraph& g) { AddFunctionNode(g, "Functions/C.hmf"); });
	fixture.WriteFunction("Functions/A.hmf", &hashA, [](MaterialGraph& g) { AddFunctionNode(g, "Functions/B.hmf"); });

	MaterialGraph graph;
	AddFunctionNode(graph, "Functions/B.hmf");
	AddFunctionNode(graph, "Functions/A.hmf");
	AddFunctionNode(graph, "Functions/A.hmf");

	std::vector<FunctionHash> out;
	REQUIRE(CollectFunctionHashes(graph, out));
	REQUIRE(out.size() == 3);
	CHECK(out[0].path == "Functions/A.hmf");
	CHECK(out[0].hash == hashA);
	CHECK(out[1].path == "Functions/B.hmf");
	CHECK(out[1].hash == hashB);
	CHECK(out[2].path == "Functions/C.hmf");
	CHECK(out[2].hash == hashC);
}

TEST_CASE("function cycles terminate", "[hash]")
{
	FunctionFixture fixture;
	const SHA1Hash hash = MakeHash(0x7);

	fixture.WriteFunction("Functions/X.hmf", &hash, [](MaterialGraph& g) { AddFunctionNode(g, "Functions/Y.hmf"); });
	fixture.WriteFunction("Functions/Y.hmf", &hash, [](MaterialGraph& g) { AddFunctionNode(g, "Functions/X.hmf"); });

	MaterialGraph graph;
	AddFunctionNode(graph, "Functions/X.hmf");

	std::vector<FunctionHash> out;
	REQUIRE(CollectFunctionHashes(graph, out));
	CHECK(out.size() == 2);
}
