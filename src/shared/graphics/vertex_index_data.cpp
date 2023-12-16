
#include "vertex_index_data.h"

#include <set>

#include "graphics_device.h"

namespace mmo
{
	VertexData::VertexData(GraphicsDevice* device)
	{
		m_device = device ? device : &GraphicsDevice::Get();
		vertexBufferBinding = m_device->CreateVertexBufferBinding();
		vertexDeclaration = m_device->CreateVertexDeclaration();
		deleteDeclarationBinding = true;
		vertexCount = 0;
		vertexStart = 0;
		m_hardwareAnimationDataItemsUsed = 0;
	}

	VertexData::VertexData(VertexDeclaration& declaration, VertexBufferBinding& binding)
	{
		m_device = &GraphicsDevice::Get();
		vertexBufferBinding = &binding;
		vertexDeclaration = &declaration;
		deleteDeclarationBinding = false;
		vertexCount = 0;
		vertexStart = 0;
		m_hardwareAnimationDataItemsUsed = 0;
	}

	VertexData::~VertexData()
	{
		if (deleteDeclarationBinding)
		{
			ASSERT(vertexBufferBinding);
			ASSERT(vertexDeclaration);

			m_device->DestroyVertexBufferBinding(*vertexBufferBinding);
			m_device->DestroyVertexDeclaration(*vertexDeclaration);
		}
	}

	VertexData* VertexData::Clone(const bool copyData, GraphicsDevice* device) const
	{
		GraphicsDevice* dev = device ? device : m_device;

		const auto result = new VertexData(dev);

		for (const VertexBufferBinding::VertexBufferBindingMap& bindings = this->vertexBufferBinding->GetBindings(); const auto& [index, buffer] : bindings)
		{
			const VertexBufferPtr sourceBuffer = buffer;
			VertexBufferPtr destBuffer;
			if (copyData)
			{
				// create new buffer with the same settings
				destBuffer = dev->CreateVertexBuffer(sourceBuffer->GetVertexSize(), sourceBuffer->GetVertexCount(), sourceBuffer->IsDynamic(), nullptr);

				// copy data
				// TODO: dstBuf->copyData(*sourceBuffer, 0, 0, sourceBuffer->GetVertexSize(), true);
			}
			else
			{
				// don't copy, point at existing buffer
				destBuffer = sourceBuffer;
			}

			// Copy binding
			result->vertexBufferBinding->SetBinding(index, destBuffer);
		}

		return result;
	}

	void VertexData::ReorganizeBuffers(VertexDeclaration& newDeclaration, const BufferUsageList& bufferUsage,
		GraphicsDevice* device)
	{
		// TODO
		TODO("Implement");
	}

	void VertexData::ReorganizeBuffers(VertexDeclaration& newDeclaration, GraphicsDevice* device)
	{
		// TODO
		TODO("Implement");
	}

	void VertexData::CloseGapsInBindings() const
	{
		if (!vertexBufferBinding->HasGaps())
		{
			return;
		}

		// Check for error first
		const auto& allElements = vertexDeclaration->GetElements();

#ifdef _DEBUG
		for (const auto& elem : allElements)
		{
			ASSERT(vertexBufferBinding->IsBufferBound(elem.GetSource()));
		}
#endif

		// Close gaps in the vertex buffer bindings
		VertexBufferBinding::BindingIndexMap bindingIndexMap;
		vertexBufferBinding->CloseGaps(bindingIndexMap);

		// Modify vertex elements to reference to new buffer index
		unsigned short elemIndex = 0;
		for (auto ai = allElements.begin(); ai != allElements.end(); ++ai, ++elemIndex)
		{
			const VertexElement& elem = *ai;
			auto it = bindingIndexMap.find(elem.GetSource());
			ASSERT(it != bindingIndexMap.end());

			if (const uint16 targetSource = it->second; elem.GetSource() != targetSource)
			{
				vertexDeclaration->ModifyElement(elemIndex, targetSource, elem.GetOffset(), elem.GetType(), elem.GetSemantic(), elem.GetIndex());
			}
		}
	}

	void VertexData::RemoveUnusedBuffers() const
	{
		std::set<uint16> usedBuffers;

		// Collect used buffers
		const auto& allElements = vertexDeclaration->GetElements();
		for (const auto& elem : allElements)
		{
			usedBuffers.insert(elem.GetSource());
		}

		// Unset unused buffer bindings
		const uint16 count = vertexBufferBinding->GetLastBoundIndex();
		for (uint16 index = 0; index < count; ++index)
		{
			if (!usedBuffers.contains(index) &&
				vertexBufferBinding->IsBufferBound(index))
			{
				vertexBufferBinding->UnsetBinding(index);
			}
		}

		// Close gaps
		CloseGapsInBindings();
	}

	void VertexData::ConvertPackedColor(VertexElementType srcType, VertexElementType destType)
	{
		// TODO
		TODO("Implement");
	}

	uint16 VertexData::AllocateHardwareAnimationElements(const uint16 count, const bool animateNormals)
	{
		// Find first free texture coord set
		unsigned short texCoord = vertexDeclaration->GetNextFreeTextureCoordinate();
		unsigned short freeCount = (8 - texCoord);
		if (animateNormals)
		{
			freeCount /= 2;
		}

		const unsigned short supportedCount = std::min(freeCount, count);

		// Increase to correct size
		for (size_t c = m_hardwareAnimationDataList.size(); c < supportedCount; ++c)
		{
			// Create a new 3D texture coordinate set
			HardwareAnimationData data;
			data.targetBufferIndex = vertexBufferBinding->GetNextIndex();
			vertexDeclaration->AddElement(data.targetBufferIndex, 0, VertexElementType::Float3, VertexElementSemantic::TextureCoordinate, texCoord++);
			if (animateNormals)
			{
				vertexDeclaration->AddElement(data.targetBufferIndex, sizeof(float) * 3, VertexElementType::Float3, VertexElementSemantic::TextureCoordinate, texCoord++);
			}
			
			m_hardwareAnimationDataList.push_back(data);
			// Vertex buffer will not be bound yet, we expect this to be done by the
			// caller when it becomes appropriate (e.g. through a VertexAnimationTrack)
		}

		return supportedCount;
	}

	IndexData* IndexData::Clone(bool copyData) const
	{
		TODO("Implement");
		return nullptr;
	}
}
