
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

	DXGI_FORMAT MapDeclarationFormatD3D11(VertexElementType type)
	{
		switch(type)
		{
			case VertexElementType::Float1:
				return DXGI_FORMAT_R32_FLOAT;
			case VertexElementType::Float2:
				return DXGI_FORMAT_R32G32_FLOAT;
			case VertexElementType::Float3:
				return DXGI_FORMAT_R32G32B32_FLOAT;
			case VertexElementType::Float4:
				return DXGI_FORMAT_R32G32B32A32_FLOAT;

			case VertexElementType::Color:
			case VertexElementType::ColorAbgr:
			case VertexElementType::ColorArgb:
				return DXGI_FORMAT_R8G8B8A8_UNORM;

			case VertexElementType::UByte4:
				return DXGI_FORMAT_R8G8B8A8_UINT;

			case VertexElementType::Int1:
				return DXGI_FORMAT_R32_SINT;
			case VertexElementType::Int2:
				return DXGI_FORMAT_R32G32_SINT;
			case VertexElementType::Int3:
				return DXGI_FORMAT_R32G32B32_SINT;
			case VertexElementType::Int4:
				return DXGI_FORMAT_R32G32B32A32_SINT;

			case VertexElementType::UInt1:
				return DXGI_FORMAT_R32_UINT;
			case VertexElementType::UInt2:
				return DXGI_FORMAT_R32G32_UINT;
			case VertexElementType::UInt3:
				return DXGI_FORMAT_R32G32B32_UINT;
			case VertexElementType::UInt4:
				return DXGI_FORMAT_R32G32B32A32_UINT;

			case VertexElementType::Short1:
				return DXGI_FORMAT_R16_SINT;
			case VertexElementType::Short2:
				return DXGI_FORMAT_R16G16_SINT;
			case VertexElementType::Short4:
				return DXGI_FORMAT_R16G16B16A16_SINT;

			case VertexElementType::UShort1:
				return DXGI_FORMAT_R16_UINT;
			case VertexElementType::UShort2:
				return DXGI_FORMAT_R16G16_UINT;
			case VertexElementType::UShort4:
				return DXGI_FORMAT_R16G16B16A16_UINT;

		default:
			ASSERT(!! "Unsupported vertex element type!");
			return DXGI_FORMAT_UNKNOWN;
		}
	}

	const String& MapSemanticNameD3D11(VertexElementSemantic semantic)
	{
		static String semanticNames[8] = {
			"POSITION",
			"BLENDWEIGHT",
			"BLENDINDICES",
			"NORMAL",
			"COLOR",
			"TEXCOORD",
			"BINORMAL",
			"TANGENT"
		};

		ASSERT(static_cast<uint8>(semantic) < 8);
		return semanticNames[static_cast<uint8>(semantic)];
	}

	ID3D11InputLayout* VertexDeclarationD3D11::GetILayoutByShader(VertexShaderD3D11& boundVertexProgram, VertexBufferBinding* binding)
	{
		// For backward compatibility, check the local map first
		if (const auto it = m_shaderToILayoutMap.find(&boundVertexProgram); it != m_shaderToILayoutMap.end())
		{
			return it->second.Get();
		}
		
		// Create the input element descriptions
		std::vector<D3D11_INPUT_ELEMENT_DESC> inputElements;
		inputElements.reserve(m_elementList.size());
		
		for (auto element : m_elementList)
		{
			D3D11_INPUT_ELEMENT_DESC elementDesc;
			
			elementDesc.SemanticName = MapSemanticNameD3D11(element.GetSemantic()).c_str();
			elementDesc.SemanticIndex = element.GetIndex();
			elementDesc.Format = MapDeclarationFormatD3D11(element.GetType());
			elementDesc.InputSlot = element.GetSource();
			elementDesc.AlignedByteOffset = element.GetOffset();
			elementDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
			elementDesc.InstanceDataStepRate = 0;
			inputElements.push_back(elementDesc);
		}
		
		// Get the input layout from the global cache
		ID3D11InputLayout* layout = m_device.GetOrCreateInputLayout(
			this, &boundVertexProgram, inputElements);
		
		if (layout)
		{
			// Store in the local map for backward compatibility
			m_shaderToILayoutMap[&boundVertexProgram] = layout;
		}
		
		return layout;
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

	void VertexDeclarationD3D11::Bind(VertexShaderD3D11& boundVertexProgram, VertexBufferBinding* binding)
	{
		ID3D11InputLayout* pVertexLayout = GetILayoutByShader(boundVertexProgram, binding);

		ID3D11DeviceContext& context = m_device;
		context.IASetInputLayout(pVertexLayout);

	}
}
