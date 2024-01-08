
#include "shader_compiler_d3d11.h"

#include <d3d11.h>
#include <d3d11Shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <iterator>

using Microsoft::WRL::ComPtr;

namespace mmo
{
	const String ShaderFormat_D3D_SM5 = "D3D_SM5";

	void ShaderCompilerD3D11::Compile(const ShaderCompileInput& input, ShaderCompileResult& output)
	{
		UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR;
#if defined( DEBUG ) || defined( _DEBUG )
		flags |= D3DCOMPILE_DEBUG;
#endif
		
		String profile;
		switch(input.shaderType)
		{
		case ShaderType::ComputeShader:
			profile = "cs_5_0";
			break;
		case ShaderType::DomainShader:
			profile = "ds_5_0";
			break;
		case ShaderType::GeometryShader:
			profile = "gs_5_0";
			break;
		case ShaderType::HullShader:
			profile = "hs_5_0";
			break;
		case ShaderType::PixelShader:
			profile = "ps_5_0";
			break;
		case ShaderType::VertexShader:
			profile = "vs_5_0";
			break;
		}
		
		const D3D_SHADER_MACRO defines[] = 
		{
		    //"EXAMPLE_DEFINE", "1",
		    nullptr, nullptr
		};

		ComPtr<ID3DBlob> shaderBlob;
		ComPtr<ID3DBlob> errorBlob;
		const HRESULT hr = D3DCompile(input.shaderCode.c_str(), input.shaderCode.length(), "name", defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		                                 "main", profile.c_str(),
		                                 flags, 0, &shaderBlob, &errorBlob );
		output.succeeded = SUCCEEDED(hr);
		if (output.succeeded)
		{
			output.code.format = GetShaderFormat();

			output.code.data.clear();
			std::copy(
				static_cast<const uint8*>(shaderBlob->GetBufferPointer()), 
				static_cast<const uint8*>(shaderBlob->GetBufferPointer()) + shaderBlob->GetBufferSize(),
				std::back_inserter(output.code.data));
		}
		
		if (errorBlob)
		{
			output.errorMessage = String(static_cast<char*>(errorBlob->GetBufferPointer()));
		}
	}
}
