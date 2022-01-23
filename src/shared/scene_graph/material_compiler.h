#pragma once

#include <sstream>

#include "base/typedefs.h"

namespace mmo
{
	class Material;

	class MaterialCompiler
	{
	public:
		void GenerateShaderCode(Material& material);

	public:
		[[nodiscard]] const String& GetVertexShaderCode() const noexcept { return m_vertexShaderCode; }

		[[nodiscard]] const String& GetPixelShaderCode() const noexcept { return m_pixelShaderCode; }

	private:
		void GenerateVertexShaderCode();

		void GeneratePixelShaderCode();

	protected:
		Material* m_material { nullptr };
		String m_vertexShaderCode;
		String m_pixelShaderCode;
		std::ostringstream m_vertexShaderStream;
		std::ostringstream m_pixelShaderStream;
	};
}
