
#include "material_compiler.h"
#include "material.h"
#include "graphics/shader_compiler.h"
#include "log/default_log_levels.h"

namespace mmo
{
	void MaterialCompiler::GenerateShaderCode(Material& material, ShaderCompiler& compiler)
	{
		GenerateVertexShaderCode();
		GeneratePixelShaderCode();

		ShaderCompileInput vertexInput;
		vertexInput.shaderCode = m_vertexShaderCode;
		vertexInput.shaderType = ShaderType::VertexShader;
		ShaderCompileResult vertexOutput;
		compiler.Compile(vertexInput, vertexOutput);

		if (!vertexOutput.succeeded)
		{
			ELOG("Error compiling vertex shader: " << vertexOutput.errorMessage);
		}
		else
		{
			DLOG("Successfully compiled vertex shader. Size: " << vertexOutput.code.data.size());
		}
		
		ShaderCompileInput pixelInput;
		pixelInput.shaderCode = m_pixelShaderCode;
		pixelInput.shaderType = ShaderType::PixelShader;
		ShaderCompileResult pixelOutput;
		compiler.Compile(pixelInput, pixelOutput);

		if (!pixelOutput.succeeded)
		{
			ELOG("Error compiling pixel shader: " << pixelOutput.errorMessage);
		}
		else
		{
			DLOG("Successfully compiled pixel shader. Size: " << pixelOutput.code.data.size());
		}
	}

	uint32 MaterialCompiler::AddTexture(std::string_view texture)
	{
		if (texture.empty())
		{
			return 0;
		}

		const auto textureIt = std::find(m_textures.begin(), m_textures.end(), texture);
		if (textureIt == m_textures.end())
		{
			m_textures.emplace_back(texture);
			return m_textures.size() - 1;
		}

		return 0;
	}

	void MaterialCompiler::NotifyTextureCoordinateIndex(const uint32 texCoordIndex)
	{
		m_numTexCoordinates = std::max(texCoordIndex, m_numTexCoordinates);
	}

	void MaterialCompiler::GenerateVertexShaderCode()
	{
		m_vertexShaderStream.clear();
		
		// VertexIn struct
		m_vertexShaderStream
			<< "struct VertexIn\n"
			<< "{\n"
			<< "float4 pos : SV_POSITION;\n"
			<< "float4 color : COLOR;\n"
			<< "float3 normal : NORMAL;\n";

		for (uint32 i = 0; i < m_numTexCoordinates; ++i)
		{
			m_vertexShaderStream
				<< "float2 uv" << i << " : TEXCOORD" << i << "\n";
		}

		m_vertexShaderStream
			<< "};\n";
		
		// VertexOut struct
		m_vertexShaderStream
			<< "struct VertexOut\n"
			<< "{\n"
			<< "float4 pos : SV_POSITION;\n"
			<< "float4 color : COLOR;\n"
			<< "float3 normal : NORMAL;\n";
		
		for (uint32 i = 0; i < m_numTexCoordinates; ++i)
		{
			m_vertexShaderStream
				<< "float2 uv" << i << " : TEXCOORD" << i << "\n";
		}

		m_vertexShaderStream
			<< "};\n";

		// Matrix constant buffer
		m_vertexShaderStream
			<< "cbuffer Matrices\n"
			<< "{\n"
			<< "matrix matWorld;\n"
			<< "matrix matView;\n"
			<< "matrix matProj;\n"
			<< "}\n";

		// Main procedure start
		m_vertexShaderStream
			<< "VertexOut main(VertexIn input)\n"
			<< "{\n"
			<< "VertexOut output;\n";

		// TODO: Extend with custom code

		// Basic transformations
		m_vertexShaderStream
			<< "input.pos.w = 1.0;\n"
			<< "output.pos = mul(input.pos, matWorld);\n"
			<< "output.pos = mul(input.pos, matView);\n"
			<< "output.pos = mul(input.pos, matProj);\n"
			<< "output.color = input.color;\n";
		
		for (uint32 i = 0; i < m_numTexCoordinates; ++i)
		{
			m_vertexShaderStream
				<< "output.uv" << i << " = input.uv" << i << ";\n";
		}

		m_vertexShaderStream
			<< "output.normal = mul(input.normal, (float3x3)matWorld);\n"
			<< "output.normal = normalize(output.normal);\n";

		// Main procedure end
		m_vertexShaderStream
			<< "return output;\n"
			<< "}"
			<< std::endl;
		
		m_vertexShaderCode = m_vertexShaderStream.str();
		m_vertexShaderStream.clear();
	}

	void MaterialCompiler::GeneratePixelShaderCode()
	{
		m_pixelShaderStream.clear();
		
		// VertexOut struct
		m_pixelShaderStream
			<< "struct VertexOut\n"
			<< "{\n"
			<< "float4 pos : SV_POSITION;\n"
			<< "float4 color : COLOR;\n"
			<< "float3 normal : NORMAL;\n";
		
		for (uint32 i = 0; i < m_numTexCoordinates; ++i)
		{
			m_pixelShaderStream
				<< "float2 uv" << i << " : TEXCOORD" << i << "\n";
		}

		m_pixelShaderStream << "};\n";

		// Add texture samplers
		for (size_t i = 1; i <= m_textures.size(); ++i)
		{
			m_pixelShaderStream << "// " << m_textures[i] << "\n";
			m_pixelShaderStream << "Texture2D tex" << i << ";\n";
			m_pixelShaderStream << "SamplerState sampler" << i << ";\n";
		}

		// Start of main function
		m_pixelShaderStream
			<< "float4 main(VertexOut input) : SV_Target\n"
			<< "{"
			<< "float4 outputColor = float4(1, 1, 1, 1);\n";

		// Find material pin


		m_pixelShaderStream
			<< "float3 lightDir = normalize(-float3(1.0, -0.5, 1.0));\n"
			<< "float4 ambient = float4(0.05, 0.15, 0.25, 1.0);\n";
			
		// Texture samples
		for (uint32 i = 0; i < m_textures.size(); ++i)
		{
			m_pixelShaderStream
				<< "float4 texSample" << i << " = tex" << i << ".Sample(sampler" << i << ", input.uv0" << ");";
		}
		
		// TODO: Execute shader graphs
		
		// End of main function
		m_pixelShaderStream
			<< "return outputColor;\n"
			<< "}"
			<< std::endl;

#if 0
float4 main(VertexIn input) : SV_Target
{
	float4 ambient = float4(0.05, 0.15, 0.25, 1.0);
    float3 lightDir = normalize(-float3(1.0, -0.5, 1.0));
    float4 texColor = tex.Sample(texSampler, input.uv1);
    
    // Calculate the amount of light on this pixel.
    float lightIntensity = saturate(dot(input.normal, lightDir));

    float4 color = float4(saturate(input.color * lightIntensity).xyz, 1.0);

	return (ambient + color) * texColor;
}
#endif

		m_pixelShaderCode = m_pixelShaderStream.str();
		m_pixelShaderStream.clear();

	}
}
