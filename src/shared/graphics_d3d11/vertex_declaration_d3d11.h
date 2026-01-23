#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include "graphics/vertex_declaration.h"

namespace mmo
{
	class VertexShaderD3D11;
}

namespace mmo
{
	class GraphicsDeviceD3D11;

	class VertexDeclarationD3D11 final : public VertexDeclaration
	{
		typedef std::map<VertexShaderD3D11*, Microsoft::WRL::ComPtr<ID3D11InputLayout>> ShaderToILayoutMap;
		typedef std::map<VertexShaderD3D11*, D3D11_INPUT_ELEMENT_DESC*> ShaderToInputDesc;

		ShaderToILayoutMap m_shaderToILayoutMap;
		ShaderToInputDesc m_d3dElements;

	public:
		VertexDeclarationD3D11(GraphicsDeviceD3D11& device);
		~VertexDeclarationD3D11() override;

	private:
		ID3D11InputLayout* GetILayoutByShader(VertexShaderD3D11& boundVertexProgram, VertexBufferBinding* binding);

	public:
		const VertexElement& AddElement(uint16 source, uint32 offset, VertexElementType theType, VertexElementSemantic semantic, uint16 index) override;

		const VertexElement& InsertElement(uint16 atPosition, uint16 source, uint32 offset, VertexElementType theType, VertexElementSemantic semantic, uint16 index) override;

		void RemoveElement(uint16 index) override;

		void RemoveElement(VertexElementSemantic semantic, uint16 index) override;

		void RemoveAllElements() override;

		void ModifyElement(uint16 elementIndex, uint16 source, uint32 offset, VertexElementType theType,VertexElementSemantic semantic, uint16 index) override;

	public:
		void Bind(VertexShaderD3D11& boundVertexProgram, VertexBufferBinding* binding);

		/// @brief Binds the vertex declaration with instance buffer support for instanced rendering.
		/// @param boundVertexProgram The vertex shader to use.
		/// @param binding The vertex buffer binding.
		/// @param instanceSlot The slot where the instance buffer is bound.
		void BindInstanced(VertexShaderD3D11& boundVertexProgram, VertexBufferBinding* binding, uint16 instanceSlot);

	private:
		GraphicsDeviceD3D11& m_device;
		bool m_needsRebuild{ true };

	};
}
