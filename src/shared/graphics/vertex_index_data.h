#pragma once
#include <vector>

#include "index_buffer.h"
#include "vertex_declaration.h"
#include "base/typedefs.h"

namespace mmo
{
	class GraphicsDevice;
	class VertexBufferBinding;
	class VertexDeclaration;

	/// @brief Collects data describing vertex source information for render operations.
	class VertexData final : public NonCopyable
	{
	private:
		GraphicsDevice* m_device;

	public:
		VertexData(GraphicsDevice* device = nullptr);
		VertexData(VertexDeclaration& declaration, VertexBufferBinding& binding);
		~VertexData() override;

	public:
		VertexDeclaration* vertexDeclaration;
		VertexBufferBinding* vertexBufferBinding;
		bool deleteDeclarationBinding;
		uint32 vertexStart;
		uint32 vertexCount;

		struct HardwareAnimationData
		{
			uint16 targetBufferIndex;
			float parametric;
		};

		typedef std::vector<HardwareAnimationData> HardwareAnimationDataList;
		HardwareAnimationDataList m_hardwareAnimationDataList;
		size_t m_hardwareAnimationDataItemsUsed;

		VertexData* Clone(bool copyData = true, GraphicsDevice* device = nullptr) const;

		void ReorganizeBuffers(VertexDeclaration& newDeclaration, const BufferUsageList& bufferUsage, GraphicsDevice* device = nullptr);

		void ReorganizeBuffers(VertexDeclaration& newDeclaration, GraphicsDevice* device = nullptr);

		void CloseGapsInBindings() const;

		void RemoveUnusedBuffers() const;

		void ConvertPackedColor(VertexElementType srcType, VertexElementType destType);

		uint16 AllocateHardwareAnimationElements(uint16 count, bool animateNormals);
	};


	class IndexData final : public NonCopyable
	{
	public:
		IndexData() = default;
		~IndexData() override = default;

	public:
		IndexBufferPtr indexBuffer { nullptr };

		size_t indexStart = 0;

		size_t indexCount = 0;

	public:
		IndexData* Clone(bool copyData = true) const;
	};
}
