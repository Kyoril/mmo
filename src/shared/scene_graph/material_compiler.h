// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <map>
#include <sstream>
#include <vector>

#include "base/typedefs.h"

namespace mmo
{
	class ShaderCompiler;
	class Material;

	enum { IndexNone = -1 };

	class MaterialCompiler
	{
	public:
		void GenerateShaderCode(Material& material, ShaderCompiler& shaderCompiler);

	public:
		[[nodiscard]] const String& GetVertexShaderCode() const noexcept { return m_vertexShaderCode; }

		[[nodiscard]] const String& GetPixelShaderCode() const noexcept { return m_pixelShaderCode; }
		
		void AddGlobalFunction(std::string_view name, std::string_view code);

		int32 AddExpression(std::string_view code);

		void NotifyTextureCoordinateIndex(uint32 texCoordIndex);

		void SetBaseColorExpression(int32 expression);

		int32 AddTextureCoordinate(int32 coordinateIndex);

		int32 AddTextureSample(std::string_view texture, int32 coordinates);

		int32 AddMultiply(int32 first, int32 second);

		int32 AddAddition(int32 first, int32 second);
		
		int32 AddLerp(int32 first, int32 second, int32 alpha);

	private:
		void GenerateVertexShaderCode();

		void GeneratePixelShaderCode();

	protected:
		std::vector<std::string> m_textures;
		uint32 m_numTexCoordinates { 0 };
		
		std::map<String, String> m_globalFunctions;
		std::vector<String> m_expressions;
		int32 m_baseColorExpression { IndexNone };

		Material* m_material { nullptr };
		String m_vertexShaderCode;
		String m_pixelShaderCode;
		std::ostringstream m_vertexShaderStream;
		std::ostringstream m_pixelShaderStream;
	};
}
