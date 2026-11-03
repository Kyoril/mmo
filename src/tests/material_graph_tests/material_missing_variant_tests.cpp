// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <string>
#include <vector>

#include "graphics/graphics_device.h"
#include "graphics/material.h"
#include "graphics/shader_platform.h"

using namespace mmo;

namespace
{
	struct Reported
	{
		std::string material;
		std::string variant;
	};

	/// Installs a recording missing shader handler and removes it again at the end of the test.
	struct HandlerScope
	{
		HandlerScope()
		{
			if (!GraphicsDevice::HasInstance())
			{
				GraphicsDevice::CreateNull(GraphicsDeviceDesc());
			}

			Material::SetMissingShaderHandler([this](const String& material, const std::string_view variant)
			{
				calls.push_back({ material, std::string(variant) });
			});
		}

		~HandlerScope()
		{
			Material::SetMissingShaderHandler(nullptr);
		}

		std::vector<Reported> calls;
	};
}

TEST_CASE("missing variant calls the handler once with name and variant", "[missing_shader]")
{
	HandlerScope scope;

	// Default variants exist, the UI vertex slot is empty
	Material material("Interface/Button.hmat");
	std::vector<uint8> code = { 1 };
	material.SetVertexShaderCode(VertexShaderType::Default, code);
	material.SetPixelShaderCode(PixelShaderType::UI, code);
	material.Update();

	// The user interface path draws nothing without the UI vertex shader
	material.Apply(GraphicsDevice::Get(), MaterialDomain::UserInterface);
	material.Apply(GraphicsDevice::Get(), MaterialDomain::UserInterface);

	REQUIRE(scope.calls.size() == 1);
	CHECK(scope.calls[0].material == "Interface/Button.hmat");
	CHECK(scope.calls[0].variant == "UI vertex");
}

TEST_CASE("legacy material without build info still renders without runtime compile", "[missing_shader]")
{
	HandlerScope scope;

	// Shaders of the running platform only, as every repository material has today, and no build info
	Material material("Models/Legacy.hmat");
	std::vector<uint8> code = { 1 };
	material.SetVertexShaderCode(VertexShaderType::Default, code);
	material.SetPixelShaderCode(PixelShaderType::Forward, code);
	material.Update();
	REQUIRE(material.GetShaderBuildInfo(shader_platform::GetCurrentPlatform()) == nullptr);

	material.Apply(GraphicsDevice::Get(), MaterialDomain::Surface, PixelShaderType::Forward);

	CHECK(scope.calls.empty());
	CHECK(material.GetVertexShader(VertexShaderType::Default) != nullptr);
}
