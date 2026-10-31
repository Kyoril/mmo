// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_shader_resolver.h"

#include <cstdlib>
#include <memory>

#include "startup_error.h"
#include "graphics/material.h"
#include "log/default_log_levels.h"
#include "scene_graph/material_manager.h"

#ifdef MMO_MATERIAL_RUNTIME_COMPILE
#	include <vector>

#	include "graphics/graphics_device.h"
#	include "graphics/shader_compiler.h"
#	include "material_graph/material_graph.h"
#	include "material_graph/material_graph_hash.h"
#	include "material_graph/material_shader_build.h"
#	include "material_graph/material_shader_cache.h"
#endif

namespace mmo
{
#ifdef MMO_MATERIAL_RUNTIME_COMPILE
	namespace
	{
		std::unique_ptr<MaterialShaderCache> s_cache;
		std::unique_ptr<ShaderCompiler> s_compiler;

		void ResolveShaders(Material& material, const std::span<const uint8> fileBytes)
		{
			std::vector<uint8> graphChunk;
			if (!ReadMaterialGraphChunk(fileBytes, graphChunk))
			{
				// Without a graph there is nothing to build from: the stored shaders are all there is.
				return;
			}

			MaterialGraph graph;
			if (!LoadMaterialGraph(graphChunk, graph))
			{
				WLOG("Material " << material.GetName() << ": unable to load its graph, using the stored shaders");
				return;
			}

			ShaderBuildInfo expected;
			if (!CollectFunctionHashes(graph, expected.functions))
			{
				WLOG("Material " << material.GetName() << ": a material function is missing, using the stored shaders");
				return;
			}

			if (!s_compiler)
			{
				s_compiler = GraphicsDevice::Get().CreateShaderCompiler();
			}

			const std::string_view platform = s_compiler->GetPlatform();
			expected.graphHash = ComputeGraphHash(graph);
			if (const ShaderBuildInfo* stored = material.GetShaderBuildInfo(platform);
				stored && IsShaderBuildCurrent(*stored, platform, expected.graphHash, expected.functions))
			{
				return;
			}

			const String materialPath(material.GetName());
			if (s_cache->TryLoad(materialPath, platform, expected, material))
			{
				return;
			}

			ILOG("Material " << materialPath << ": " << platform << " shaders are missing or out of date, compiling");
			const MaterialShaderBuildResult result = BuildMaterialShaders(material, graph, *s_compiler);
			if (!result.cookable)
			{
				return;
			}

			for (const String& variant : result.failedVariants)
			{
				WLOG("Material " << materialPath << ": shader variant " << variant << " failed to compile");
			}

			s_cache->Store(materialPath, platform, material);
		}
	}

	void InstallMaterialShaderResolver(const std::filesystem::path& dataPath)
	{
		s_cache = std::make_unique<MaterialShaderCache>(dataPath / "Cache" / "Shaders");
		MaterialManager::Get().SetPostLoadHook(&ResolveShaders);
	}

	void UninstallMaterialShaderResolver()
	{
		MaterialManager::Get().SetPostLoadHook(nullptr);
		s_compiler.reset();
		s_cache.reset();
	}
#else
	void InstallMaterialShaderResolver(const std::filesystem::path& /*dataPath*/)
	{
		// Release clients run on cooked shaders and never compile. A missing variant is broken game
		// data: say which material it is instead of silently drawing nothing.
		Material::SetMissingShaderHandler([](const String& materialName, const std::string_view variant)
		{
			const std::string details = "Material " + materialName + " has no " + std::string(variant) + " shader for this platform.";
			ELOG(details);
			ShowStartupError(details, "Game data error",
				"Repairing the game in the launcher usually fixes this. If it keeps happening, please report it together with the material name.");
			std::exit(1);
		});
	}

	void UninstallMaterialShaderResolver()
	{
		Material::SetMissingShaderHandler(nullptr);
	}
#endif
}
