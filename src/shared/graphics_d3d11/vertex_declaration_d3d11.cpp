
#include "vertex_declaration_d3d11.h"
#include "graphics_device_d3d11.h"
#include "vertex_shader_d3d11.h"

namespace mmo
{
	VertexDeclarationD3D11::VertexDeclarationD3D11(GraphicsDeviceD3D11& device)
		: VertexDeclaration()
		, m_device(device)
	{
	}

	VertexDeclarationD3D11::~VertexDeclarationD3D11() = default;

	ID3D11InputLayout* VertexDeclarationD3D11::GetILayoutByShader(VertexShaderD3D11& boundVertexProgram, VertexBufferBinding* binding)
	{
		return nullptr;
	}

	const VertexElement& VertexDeclarationD3D11::AddElement(uint16 source, uint32 offset, VertexElementType theType, VertexElementSemantic semantic, uint16 index)
	{
		m_needsRebuild = true;
		return VertexDeclaration::AddElement(source, offset, theType, semantic, index);
	}

	const VertexElement& VertexDeclarationD3D11::InsertElement(uint16 atPosition, uint16 source, uint32 offset, VertexElementType theType, VertexElementSemantic semantic, uint16 index)
	{
		m_needsRebuild = true;
		return VertexDeclaration::InsertElement(atPosition, source, offset, theType, semantic, index);
	}

	void VertexDeclarationD3D11::RemoveElement(uint16 index)
	{
		m_needsRebuild = true;
		VertexDeclaration::RemoveElement(index);
	}

	void VertexDeclarationD3D11::RemoveElement(VertexElementSemantic semantic, uint16 index)
	{
		m_needsRebuild = true;
		VertexDeclaration::RemoveElement(semantic, index);
	}

	void VertexDeclarationD3D11::RemoveAllElements()
	{
		m_needsRebuild = true;
		VertexDeclaration::RemoveAllElements();
	}

	void VertexDeclarationD3D11::ModifyElement(uint16 elementIndex, uint16 source, uint32 offset, VertexElementType theType, VertexElementSemantic semantic, uint16 index)
	{
		m_needsRebuild = true;
		VertexDeclaration::ModifyElement(elementIndex, source, offset, theType, semantic, index);
	}

	D3D11_INPUT_ELEMENT_DESC* VertexDeclarationD3D11::GetD3DVertexDeclaration(VertexShaderD3D11& boundVertexProgram, VertexBufferBinding* binding)
	{
		return nullptr;
	}

	void VertexDeclarationD3D11::Bind(VertexShaderD3D11& boundVertexProgram, VertexBufferBinding* binding)
	{
		ID3D11InputLayout* pVertexLayout = GetILayoutByShader(boundVertexProgram, binding);

		ID3D11DeviceContext& context = m_device;
		context.IASetInputLayout(pVertexLayout);

	}
}
