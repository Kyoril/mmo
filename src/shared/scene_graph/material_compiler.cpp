
#include "material_compiler.h"
#include "material.h"

namespace mmo
{
	void MaterialCompiler::GenerateShaderCode(Material& material)
	{
		GenerateVertexShaderCode();
		GeneratePixelShaderCode();
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
			<< "float3 normal : NORMAL;\n"
			<< "float2 uv1 : TEXCOORD0;\n"
			<< "};\n";
		
		// VertexOut struct
		m_vertexShaderStream
			<< "struct VertexOut\n"
			<< "{\n"
			<< "float4 pos : SV_POSITION;\n"
			<< "float4 color : COLOR;\n"
			<< "float3 normal : NORMAL;\n"
			<< "float2 uv1 : TEXCOORD0;\n"
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
			<< "output.color = input.color;\n"
			<< "output.uv1 = input.uv1;\n"
			<< "output.normal = mul(input.normal, (float3x3)matWorld);\n"
			<< "output.normal = normalize(output.normal);\n";

		// Main procedure end
		m_vertexShaderStream
			<< "return output;\n"
			<< "}"
			<< std::endl;
		
		m_pixelShaderCode = m_pixelShaderStream.str();
		m_vertexShaderStream.clear();
	}

	void MaterialCompiler::GeneratePixelShaderCode()
	{
		m_pixelShaderStream.clear();

		// Add texture samplers
		for (size_t i = 1; i <= m_material->GetTextureFiles().size(); ++i)
		{
			m_pixelShaderStream << "Texture2D tex" << i << ";\n";
			m_pixelShaderStream << "SamplerState sampler" << i << ";\n";
		}

		// Start of main function
		m_pixelShaderStream
			<< "float4 main(VertexOut input) : SV_Target\n"
			<< "{"
			<< "float4 outputColor = float4(0, 0, 0, 1);\n";

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
