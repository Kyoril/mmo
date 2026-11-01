// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "assets/asset_registry.h"
#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/stream_sink.h"
#include "binary_io/writer.h"
#include "content_cook/cook_report.h"
#include "content_cook/material_assembly.h"
#include "content_cook/overlay.h"
#include "content_cook/shader_cook.h"
#include "graphics/material.h"
#include "graphics/material_serializer.h"
#include "graphics/shader_compiler.h"
#include "material_graph/material_graph.h"
#include "material_graph/material_node.h"
#include "material_graph/material_shader_build.h"

using namespace mmo;

namespace
{
	/// Compiler for an arbitrary platform that turns input text into bytes.
	class FakeShaderCompiler final : public ShaderCompiler
	{
	public:
		explicit FakeShaderCompiler(String platform)
			: m_platform(std::move(platform))
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
			output.succeeded = true;
			output.code.format = GetShaderFormat();
			output.code.data.assign(input.shaderCode.begin(), input.shaderCode.end());
		}

	private:
		String m_platform;
	};

	/// A data directory with an asset registry on it, removed again at the end of the test.
	struct DataFixture
	{
		std::filesystem::path root;
		std::filesystem::path data;

		DataFixture()
		{
			root = std::filesystem::temp_directory_path() / ("mmo_content_cook_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			data = root / "data";
			std::filesystem::create_directories(data);
			AssetRegistry::Initialize(data, {});
		}

		~DataFixture()
		{
			AssetRegistry::Destroy();
			std::error_code error;
			std::filesystem::remove_all(root, error);
		}

		/// Writes a material with D3D11 "repository" shaders and, unless withGraph is false, a graph whose
		/// root takes a constant (the constant's value makes two graphs differ).
		void WriteMaterial(const std::string& path, const float constant, const bool withGraph = true, const std::string& functionPath = std::string()) const
		{
			Material material(path);
			std::vector<uint8> repository = { 'r', 'e', 'p', 'o' };
			material.SetVertexShaderCode("D3D11", VertexShaderType::Default, repository);
			material.SetPixelShaderCode("D3D11", PixelShaderType::Forward, repository);

			std::filesystem::create_directories((data / path).parent_path());
			std::ofstream file(data / path, std::ios::binary);
			io::StreamSink sink(file);
			io::Writer writer(sink);
			MaterialSerializer serializer;
			serializer.Export(material, writer);

			if (withGraph)
			{
				MaterialGraph graph;
				graph.GetNodeRegistry()->RegisterNodeType(MaterialNode::GetStaticTypeInfo());
				GraphNode* root = graph.CreateNode<MaterialNode>(true);
				auto* value = graph.CreateNode<ConstFloatNode>();
				REQUIRE(value != nullptr);
				value->GetProperties()[0]->SetValue(constant);
				REQUIRE(root->GetInputPins()[0]->LinkTo(*value->GetOutputPins()[0]));
				if (!functionPath.empty())
				{
					auto* function = graph.CreateNode<MaterialFunctionNode>();
					static_cast<AssetPathProperty*>(function->GetProperties()[0])->SetValue(AssetPathValue(functionPath, ".hmf"));
				}
				graph.Serialize(writer);
			}
		}

		std::filesystem::path Dir(const std::string& name) const
		{
			return root / name;
		}
	};

	std::vector<char> ReadFile(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
	}

	bool LoadMaterial(const std::filesystem::path& path, Material& material)
	{
		const std::vector<char> bytes = ReadFile(path);
		io::MemorySource source(bytes.data(), bytes.data() + bytes.size());
		io::Reader reader(source);
		MaterialDeserializer deserializer(material);
		return deserializer.Read(reader);
	}

	bool HasWarning(const CookReport& report, const std::string& asset)
	{
		for (const CookWarning& warning : report.GetWarnings())
		{
			if (warning.asset == asset)
			{
				return true;
			}
		}
		return false;
	}
}

TEST_CASE("cook writes one pack per material and platform", "[content_cook]")
{
	DataFixture fixture;
	fixture.WriteMaterial("Models/A.hmat", 1.0f);
	fixture.WriteMaterial("Models/Sub/B.hmat", 2.0f);

	FakeShaderCompiler compiler("TEST");
	CookReport report;
	REQUIRE(CookShaders(fixture.data, compiler, fixture.Dir("packs"), report));

	CHECK(std::filesystem::is_regular_file(fixture.Dir("packs") / "Models/A.hmat.TEST.shp"));
	CHECK(std::filesystem::is_regular_file(fixture.Dir("packs") / "Models/Sub/B.hmat.TEST.shp"));
	CHECK(std::filesystem::is_regular_file(fixture.Dir("packs") / "warnings.json"));
	CHECK(report.GetWarnings().empty());
}

TEST_CASE("cook reports uncookable material", "[content_cook]")
{
	DataFixture fixture;
	fixture.WriteMaterial("Models/NoGraph.hmat", 1.0f, false);

	FakeShaderCompiler compiler("TEST");
	CookReport report;
	REQUIRE(CookShaders(fixture.data, compiler, fixture.Dir("packs"), report));

	CHECK(HasWarning(report, "Models/NoGraph.hmat"));
	CHECK_FALSE(std::filesystem::exists(fixture.Dir("packs") / "Models/NoGraph.hmat.TEST.shp"));

	std::vector<CookWarning> written;
	REQUIRE(ReadWarningsFile(fixture.Dir("packs") / "warnings.json", written));
	REQUIRE(written.size() == 1);
	CHECK(written[0].asset == "Models/NoGraph.hmat");
}

TEST_CASE("cook reports missing function", "[content_cook]")
{
	DataFixture fixture;
	fixture.WriteMaterial("Models/UsesMissing.hmat", 1.0f, true, "Functions/Missing.hmf");

	FakeShaderCompiler compiler("TEST");
	CookReport report;
	REQUIRE(CookShaders(fixture.data, compiler, fixture.Dir("packs"), report));

	CHECK(HasWarning(report, "Models/UsesMissing.hmat"));
	CHECK_FALSE(std::filesystem::exists(fixture.Dir("packs") / "Models/UsesMissing.hmat.TEST.shp"));
}

TEST_CASE("assembly merges packs and keeps other platforms", "[content_cook]")
{
	DataFixture fixture;
	fixture.WriteMaterial("Models/A.hmat", 1.0f);
	fixture.WriteMaterial("Models/NoGraph.hmat", 1.0f, false);

	FakeShaderCompiler metal("METAL");
	CookReport report;
	REQUIRE(CookShaders(fixture.data, metal, fixture.Dir("metal"), report));

	CookReport assemblyReport;
	REQUIRE(AssembleMaterials(fixture.data, { fixture.Dir("metal") }, fixture.Dir("overlay"), assemblyReport));

	Material cooked("Models/A.hmat");
	REQUIRE(LoadMaterial(fixture.Dir("overlay") / "Models/A.hmat", cooked));
	CHECK_FALSE(cooked.GetVertexShaderCode("METAL", VertexShaderType::Default).empty());
	REQUIRE(cooked.GetShaderBuildInfo("METAL") != nullptr);
	const auto d3d = cooked.GetVertexShaderCode("D3D11", VertexShaderType::Default);
	CHECK(std::vector<uint8>(d3d.begin(), d3d.end()) == std::vector<uint8>{ 'r', 'e', 'p', 'o' });

	// The graph travels along unchanged
	const std::vector<char> bytes = ReadFile(fixture.Dir("overlay") / "Models/A.hmat");
	std::vector<uint8> graphChunk;
	CHECK(ReadMaterialGraphChunk(std::span<const uint8>(reinterpret_cast<const uint8*>(bytes.data()), bytes.size()), graphChunk));

	// Materials without packs are not part of the overlay; the pack's warnings reach overlay.json
	CHECK_FALSE(std::filesystem::exists(fixture.Dir("overlay") / "Models/NoGraph.hmat"));
	CHECK(HasWarning(assemblyReport, "Models/NoGraph.hmat"));
	CHECK(std::filesystem::is_regular_file(fixture.Dir("overlay") / "overlay.json"));
}

TEST_CASE("assembly is deterministic", "[content_cook]")
{
	DataFixture fixture;
	fixture.WriteMaterial("Models/A.hmat", 1.0f);
	fixture.WriteMaterial("Models/B.hmat", 2.0f);
	fixture.WriteMaterial("Models/NoGraph.hmat", 1.0f, false);

	FakeShaderCompiler d3d("D3D11");
	FakeShaderCompiler metal("METAL");
	CookReport report;
	REQUIRE(CookShaders(fixture.data, d3d, fixture.Dir("d3d"), report));
	REQUIRE(CookShaders(fixture.data, metal, fixture.Dir("metal"), report));

	CookReport first;
	CookReport second;
	REQUIRE(AssembleMaterials(fixture.data, { fixture.Dir("d3d"), fixture.Dir("metal") }, fixture.Dir("overlay1"), first));
	REQUIRE(AssembleMaterials(fixture.data, { fixture.Dir("metal"), fixture.Dir("d3d") }, fixture.Dir("overlay2"), second));

	for (const char* file : { "Models/A.hmat", "Models/B.hmat", "overlay.json" })
	{
		INFO(file);
		const std::vector<char> a = ReadFile(fixture.Dir("overlay1") / file);
		CHECK_FALSE(a.empty());
		CHECK(a == ReadFile(fixture.Dir("overlay2") / file));
	}
}

TEST_CASE("same file name in two folders cooks separately", "[content_cook]")
{
	DataFixture fixture;
	fixture.WriteMaterial("A/M.hmat", 1.0f);
	fixture.WriteMaterial("B/M.hmat", 2.0f);

	FakeShaderCompiler compiler("TEST");
	CookReport report;
	REQUIRE(CookShaders(fixture.data, compiler, fixture.Dir("packs"), report));
	REQUIRE(AssembleMaterials(fixture.data, { fixture.Dir("packs") }, fixture.Dir("overlay"), report));

	const std::vector<char> a = ReadFile(fixture.Dir("packs") / "A/M.hmat.TEST.shp");
	const std::vector<char> b = ReadFile(fixture.Dir("packs") / "B/M.hmat.TEST.shp");
	CHECK_FALSE(a.empty());
	CHECK_FALSE(b.empty());
	CHECK(a != b);
	CHECK(ReadFile(fixture.Dir("overlay") / "A/M.hmat") != ReadFile(fixture.Dir("overlay") / "B/M.hmat"));
}

TEST_CASE("overlay manifest is sorted and versioned", "[content_cook]")
{
	DataFixture fixture;
	REQUIRE(WriteOverlayManifest(fixture.Dir("overlay"), { "b.wobj", "a.wobj" }, { { "Z.hmat", "late" }, { "A.hmat", "early" } }));

	const std::vector<char> bytes = ReadFile(fixture.Dir("overlay") / "overlay.json");
	const std::string text(bytes.begin(), bytes.end());
	CHECK(text.find("\"schema\": 1") != std::string::npos);
	CHECK(text.find("a.wobj") < text.find("b.wobj"));
	CHECK(text.find("A.hmat") < text.find("Z.hmat"));
}
