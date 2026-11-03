// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <map>
#include <vector>

#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "graphics/material.h"
#include "graphics/material_compiler_hlsl.h"
#include "graphics/shader_compiler.h"
#include "material_graph/material_graph.h"
#include "material_graph/material_node.h"
#include "material_graph/node_layout_store.h"

using namespace mmo;

namespace
{
	/// @brief Shader compiler which "compiles" by returning its input, as the graph tests only look at generated code.
	class PassthroughShaderCompiler final : public ShaderCompiler
	{
	public:
		[[nodiscard]] const String& GetShaderFormat() const override
		{
			static const String format("TEST_FORMAT");
			return format;
		}

		[[nodiscard]] std::string_view GetPlatform() const override
		{
			return "TEST";
		}

		void Compile(const ShaderCompileInput& input, ShaderCompileResult& output) override
		{
			output.succeeded = true;
			output.code.format = GetShaderFormat();
			output.code.data.assign(input.shaderCode.begin(), input.shaderCode.end());
		}
	};

	/// @brief Layout store which keeps node bounds in a map.
	class MapLayoutStore final : public INodeLayoutStore
	{
	public:
		struct Bounds
		{
			float x, y, w, h;
		};

		std::map<uint32, Bounds> bounds;

		bool GetNodeBounds(const uint32 nodeId, float& x, float& y, float& w, float& h) const override
		{
			const auto it = bounds.find(nodeId);
			if (it == bounds.end())
			{
				return false;
			}

			x = it->second.x;
			y = it->second.y;
			w = it->second.w;
			h = it->second.h;
			return true;
		}

		void SetNodeBounds(const uint32 nodeId, const float x, const float y, const float w, const float h) override
		{
			bounds[nodeId] = Bounds{ x, y, w, h };
		}
	};

	/// @brief The root node type is registered by whoever loads a material, as it is not available in material functions.
	void RegisterRootNode(MaterialGraph& graph)
	{
		graph.GetNodeRegistry()->RegisterNodeType(MaterialNode::GetStaticTypeInfo());
	}

	/// @brief Builds a graph in which a constant float feeds the base color of the root node.
	/// @return The const float node.
	GraphNode* BuildGraph(MaterialGraph& graph)
	{
		RegisterRootNode(graph);
		GraphNode* root = graph.CreateNode<MaterialNode>(true);
		REQUIRE(root != nullptr);

		GraphNode* constant = graph.CreateNode<ConstFloatNode>();
		REQUIRE(constant != nullptr);

		REQUIRE(root->GetInputPins()[0]->LinkTo(*constant->GetOutputPins()[0]));
		return constant;
	}

	void SerializeGraph(const MaterialGraph& graph, std::vector<char>& buffer)
	{
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		graph.Serialize(writer);
	}

	void DeserializeGraph(MaterialGraph& graph, const std::vector<char>& buffer)
	{
		// Serialize wraps the graph in a chunk (magic and size, 4 bytes each) which the owner of the file
		// consumes before handing the content to Deserialize.
		constexpr size_t chunkHeaderSize = 8;
		REQUIRE(buffer.size() > chunkHeaderSize);
		io::MemorySource source{ buffer.data() + chunkHeaderSize, buffer.data() + buffer.size() };
		io::Reader reader{ source };

		ExecutableMaterialGraphLoadContext context;
		graph.Deserialize(reader, context);
		REQUIRE(context.PerformAfterLoadActions());
	}
}

TEST_CASE("graph round-trips and compiles without an editor", "[material_graph]")
{
	MaterialGraph graph;
	BuildGraph(graph);

	std::vector<char> buffer;
	SerializeGraph(graph, buffer);
	REQUIRE(!buffer.empty());

	MaterialGraph loaded;
	RegisterRootNode(loaded);
	DeserializeGraph(loaded, buffer);
	REQUIRE(loaded.GetNodes().size() == 2);

	MaterialCompilerHlsl compiler;
	Material material("GraphTest");
	PassthroughShaderCompiler shaderCompiler;
	loaded.Compile(compiler);
	compiler.Compile(material, shaderCompiler);

	const String& code = compiler.GetPixelShaderCode(PixelShaderType::Forward);
	REQUIRE(!code.empty());
	REQUIRE(code.find("baseColor = expr_") != std::string::npos);
}

TEST_CASE("node bounds go out on serialize and come back on deserialize", "[material_graph]")
{
	MaterialGraph graph;
	MapLayoutStore store;
	graph.SetLayoutStore(&store);
	GraphNode* constant = BuildGraph(graph);
	store.bounds[constant->GetId()] = { 12.5f, -40.0f, 120.0f, 64.0f };

	std::vector<char> buffer;
	SerializeGraph(graph, buffer);

	MaterialGraph loaded;
	MapLayoutStore loadedStore;
	loaded.SetLayoutStore(&loadedStore);
	RegisterRootNode(loaded);
	DeserializeGraph(loaded, buffer);

	const auto it = loadedStore.bounds.find(constant->GetId());
	REQUIRE(it != loadedStore.bounds.end());
	REQUIRE(it->second.x == 12.5f);
	REQUIRE(it->second.y == -40.0f);
	REQUIRE(it->second.w == 120.0f);
	REQUIRE(it->second.h == 64.0f);
}

TEST_CASE("node colors keep the values ImColor produced", "[material_graph]")
{
	// Hex values computed with ImGui's ImColor (IM_COL32 byte order, red in the low byte) before the
	// colors moved out of the editor.
	STATIC_REQUIRE(PackNodeColor(0.57f, 0.88f, 0.29f, 0.25f) == 0x404AE091u);
	STATIC_REQUIRE(PackNodeColor(114.0f / 255.0f, 92.0f / 255.0f, 71.0f / 255.0f, 0.50f) == 0x80475C72u);
	REQUIRE(ConstFloatNode::Color == 0x404AE091u);
}
