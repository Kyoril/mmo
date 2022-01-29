#pragma once

#include <sstream>
#include <vector>

#include "base/typedefs.h"

namespace mmo
{
	class ShaderCompiler;
	class Material;

	class MaterialCompiler
	{
	public:
		void GenerateShaderCode(Material& material, ShaderCompiler& shaderCompiler);

	public:
		[[nodiscard]] const String& GetVertexShaderCode() const noexcept { return m_vertexShaderCode; }

		[[nodiscard]] const String& GetPixelShaderCode() const noexcept { return m_pixelShaderCode; }

		uint32 AddTexture(std::string_view texture);

		void NotifyTextureCoordinateIndex(uint32 texCoordIndex);

	private:
		void GenerateVertexShaderCode();

		void GeneratePixelShaderCode();

	protected:
		std::vector<std::string> m_textures;
		uint32 m_numTexCoordinates { 0 };

		Material* m_material { nullptr };
		String m_vertexShaderCode;
		String m_pixelShaderCode;
		std::ostringstream m_vertexShaderStream;
		std::ostringstream m_pixelShaderStream;
	};
}
