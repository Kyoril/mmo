// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <vector>

#include "base/chunk_writer.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "graphics/material.h"
#include "graphics/shader_build_info.h"
#include "graphics/shader_compiler.h"
#include "material_graph/material_graph.h"
#include "material_graph/material_graph_hash.h"
#include "material_graph/material_node.h"
#include "material_graph/material_shader_build.h"

using namespace mmo;

namespace
{
	/// @brief Compiler for an arbitrary platform that turns input text into bytes. One call can be made to fail.
	class FakeShaderCompiler final : public ShaderCompiler
	{
	public:
		explicit FakeShaderCompiler(String platform, const int failCall = -1)
			: m_platform(std::move(platform))
			, m_failCall(failCall)
		{
		}

		[[nodiscard]] const String& GetShaderFormat() const override
		{
			static const String format("FAKE");
			return format;
		}

		[[nodiscard]] std::string_view GetPlatform() const override
		{
			return m_platform;
		}

		void Compile(const ShaderCompileInput& input, ShaderCompileResult& output) override
		{
			const int call = m_calls++;
			output.succeeded = call != m_failCall;
			output.code.format = GetShaderFormat();
			if (output.succeeded)
			{
				output.code.data.assign(input.shaderCode.begin(), input.shaderCode.end());
			}
			else
			{
				output.errorMessage = "forced failure";
			}
		}

	private:
		String m_platform;
		int m_failCall;
		int m_calls { 0 };
	};

	void BuildRootedGraph(MaterialGraph& graph)
	{
		graph.GetNodeRegistry()->RegisterNodeType(MaterialNode::GetStaticTypeInfo());
		GraphNode* root = graph.CreateNode<MaterialNode>(true);
		REQUIRE(root != nullptr);
		GraphNode* constant = graph.CreateNode<ConstFloatNode>();
		REQUIRE(constant != nullptr);
		REQUIRE(root->GetInputPins()[0]->LinkTo(*constant->GetOutputPins()[0]));
	}

	std::vector<uint8> Bytes(const std::string_view text)
	{
		return { text.begin(), text.end() };
	}
}

TEST_CASE("build replaces only the compiler's platform", "[shader_build]")
{
	MaterialGraph graph;
	BuildRootedGraph(graph);

	Material material("test");
	auto d3d = Bytes("d3d11 bytes");
	material.SetVertexShaderCode("D3D11", VertexShaderType::Default, d3d);
	material.SetPixelShaderCode("D3D11", PixelShaderType::Forward, d3d);
	// Stale Metal shaders get replaced
	auto stale = Bytes("stale");
	material.SetVertexShaderCode("METAL", VertexShaderType::Default, stale);

	FakeShaderCompiler compiler("METAL");
	const MaterialShaderBuildResult result = BuildMaterialShaders(material, graph, compiler);
	REQUIRE(result.cookable);
	CHECK(result.failedVariants.empty());

	const auto vs = material.GetVertexShaderCode("D3D11", VertexShaderType::Default);
	CHECK(std::vector<uint8>(vs.begin(), vs.end()) == d3d);
	const auto ps = material.GetPixelShaderCode("D3D11", PixelShaderType::Forward);
	CHECK(std::vector<uint8>(ps.begin(), ps.end()) == d3d);

	const auto metalVs = material.GetVertexShaderCode("METAL", VertexShaderType::Default);
	CHECK_FALSE(metalVs.empty());
	CHECK(std::vector<uint8>(metalVs.begin(), metalVs.end()) != stale);
	CHECK_FALSE(material.GetPixelShaderCode("METAL", PixelShaderType::GBuffer).empty());

	const ShaderBuildInfo* info = material.GetShaderBuildInfo("METAL");
	REQUIRE(info != nullptr);
	CHECK(info->generatorVersion == material_compiler_version::Generator);
	CHECK(info->backendVersion == material_compiler_version::GetBackendVersion("METAL"));
	CHECK(info->graphHash == ComputeGraphHash(graph));
	REQUIRE(material.GetGraphHash().has_value());
	CHECK(*material.GetGraphHash() == ComputeGraphHash(graph));
	CHECK(material.GetShaderBuildInfo("D3D11") == nullptr);
}

TEST_CASE("failed variant stays empty", "[shader_build]")
{
	MaterialGraph graph;
	BuildRootedGraph(graph);

	Material material("test");
	// Stale bytes in the failing slot must be cleared, never kept
	auto stale = Bytes("stale");
	material.SetVertexShaderCode("METAL", static_cast<VertexShaderType>(1), stale);
	// The second compile call is the second vertex variant
	FakeShaderCompiler compiler("METAL", 1);
	const MaterialShaderBuildResult result = BuildMaterialShaders(material, graph, compiler);

	REQUIRE(result.cookable);
	REQUIRE(result.failedVariants.size() == 1);
	CHECK(material.GetVertexShaderCode("METAL", static_cast<VertexShaderType>(1)).empty());
	CHECK_FALSE(material.GetVertexShaderCode("METAL", VertexShaderType::Default).empty());
	CHECK_FALSE(material.GetPixelShaderCode("METAL", PixelShaderType::Forward).empty());
}

TEST_CASE("graph without root is uncookable", "[shader_build]")
{
	MaterialGraph graph;
	graph.GetNodeRegistry()->RegisterNodeType(MaterialNode::GetStaticTypeInfo());

	Material material("test");
	FakeShaderCompiler compiler("METAL");
	const MaterialShaderBuildResult result = BuildMaterialShaders(material, graph, compiler);

	CHECK_FALSE(result.cookable);
	CHECK(material.GetVertexShaderCode("METAL", VertexShaderType::Default).empty());
	CHECK(material.GetShaderBuildInfo("METAL") == nullptr);
	CHECK_FALSE(material.GetGraphHash().has_value());
}

TEST_CASE("texture list mismatch is uncookable", "[shader_build]")
{
	MaterialGraph graph;
	BuildRootedGraph(graph);

	Material material("test");
	material.AddTexture("Textures/Stored.htex");

	FakeShaderCompiler compiler("METAL");
	const MaterialShaderBuildResult result = BuildMaterialShaders(material, graph, compiler);
	CHECK_FALSE(result.cookable);
	CHECK(material.GetVertexShaderCode("METAL", VertexShaderType::Default).empty());
}

TEST_CASE("legacy material is stale", "[shader_build]")
{
	MaterialGraph graph;
	BuildRootedGraph(graph);

	Material material("test");
	const ShaderBuildInfo* info = material.GetShaderBuildInfo("D3D11");
	REQUIRE(info == nullptr);

	// No build info at all: treated as a default (empty) info, which can never match
	const ShaderBuildInfo none;
	CHECK_FALSE(IsShaderBuildCurrent(none, "D3D11", ComputeGraphHash(graph), {}));
}

TEST_CASE("graph chunk is found in a material file and loads", "[shader_build]")
{
	MaterialGraph graph;
	BuildRootedGraph(graph);

	std::vector<char> graphBytes;
	{
		io::VectorSink sink(graphBytes);
		io::Writer writer(sink);
		graph.Serialize(writer);
	}

	std::vector<char> file;
	{
		io::VectorSink sink(file);
		io::Writer writer(sink);
		// Stand-in for the material's own chunks, which are skipped
		ChunkWriter other(ChunkMagic({ 'M', 'A', 'T', 'X' }), writer);
		writer << io::write<uint32>(42);
		other.Finish();
		// Serialize writes the GRPH chunk itself
		graph.Serialize(writer);
	}

	std::vector<uint8> chunk;
	const std::span<const uint8> fileSpan(reinterpret_cast<const uint8*>(file.data()), file.size());
	REQUIRE(ReadMaterialGraphChunk(fileSpan, chunk));
	CHECK(chunk.size() == graphBytes.size() - 8);

	MaterialGraph loaded;
	REQUIRE(LoadMaterialGraph(chunk, loaded));
	CHECK(ComputeGraphHash(loaded) == ComputeGraphHash(graph));

	// A file without the chunk reports so
	std::vector<char> noGraph(file.begin(), file.end() - static_cast<std::ptrdiff_t>(graphBytes.size()));
	std::vector<uint8> none;
	CHECK_FALSE(ReadMaterialGraphChunk(std::span<const uint8>(reinterpret_cast<const uint8*>(noGraph.data()), noGraph.size()), none));
}

TEST_CASE("rebuilding an unchanged material is byte-identical", "[shader_build]")
{
	MaterialGraph graph;
	BuildRootedGraph(graph);

	Material first("test");
	FakeShaderCompiler firstCompiler("D3D11");
	REQUIRE(BuildMaterialShaders(first, graph, firstCompiler).cookable);

	Material second("test");
	FakeShaderCompiler secondCompiler("D3D11");
	REQUIRE(BuildMaterialShaders(second, graph, secondCompiler).cookable);
	// A second build on top of the first must not change anything either
	FakeShaderCompiler thirdCompiler("D3D11");
	REQUIRE(BuildMaterialShaders(second, graph, thirdCompiler).cookable);

	for (int i = 0; i < 6; ++i)
	{
		const auto a = first.GetVertexShaderCode("D3D11", static_cast<VertexShaderType>(i));
		const auto b = second.GetVertexShaderCode("D3D11", static_cast<VertexShaderType>(i));
		CHECK(std::vector<uint8>(a.begin(), a.end()) == std::vector<uint8>(b.begin(), b.end()));
	}
	for (int i = 0; i < 4; ++i)
	{
		const auto a = first.GetPixelShaderCode("D3D11", static_cast<PixelShaderType>(i));
		const auto b = second.GetPixelShaderCode("D3D11", static_cast<PixelShaderType>(i));
		CHECK(std::vector<uint8>(a.begin(), a.end()) == std::vector<uint8>(b.begin(), b.end()));
	}

	const ShaderBuildInfo* a = first.GetShaderBuildInfo("D3D11");
	const ShaderBuildInfo* b = second.GetShaderBuildInfo("D3D11");
	REQUIRE(a != nullptr);
	REQUIRE(b != nullptr);
	CHECK(a->graphHash == b->graphHash);
	CHECK(a->generatorVersion == b->generatorVersion);
	CHECK(a->backendVersion == b->backendVersion);
	CHECK(a->functions.size() == b->functions.size());
	CHECK(first.GetGraphHash() == second.GetGraphHash());
}

TEST_CASE("material build is current only until the graph changes", "[shader_build]")
{
	MaterialGraph graph;
	BuildRootedGraph(graph);

	Material material("test");
	CHECK_FALSE(IsMaterialShaderBuildCurrent(material, graph, "D3D11"));

	FakeShaderCompiler compiler("D3D11");
	REQUIRE(BuildMaterialShaders(material, graph, compiler).cookable);
	CHECK(IsMaterialShaderBuildCurrent(material, graph, "D3D11"));
	// Only the built platform is current
	CHECK_FALSE(IsMaterialShaderBuildCurrent(material, graph, "METAL"));

	REQUIRE(graph.CreateNode<ConstFloatNode>() != nullptr);
	CHECK_FALSE(IsMaterialShaderBuildCurrent(material, graph, "D3D11"));

	// Recording the build after compiling elsewhere makes it current again
	RecordShaderBuildInfo(material, graph, "D3D11", {});
	CHECK(IsMaterialShaderBuildCurrent(material, graph, "D3D11"));
}
