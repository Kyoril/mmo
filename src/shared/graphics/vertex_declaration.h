#pragma once
#include <list>
#include <map>

#include "vertex_buffer.h"
#include "base/typedefs.h"

namespace mmo
{
	class GraphicsDevice;
	typedef uint32 RGBA;
	typedef uint32 ARGB;
	typedef uint32 ABGR;
	typedef uint32 BGRA;

	enum class VertexElementType : ::uint8
	{
		Float1,
		Float2,
		Float3,
		Float4,

		Color,

		Short1,
		Short2,
		Short3,
		Short4,

		UByte4,

		ColorArgb,
		ColorAbgr,

		Double1,
		Double2,
		Double3,
		Double4,

		UShort1,
		UShort2,
		UShort3,
		UShort4,

		Int1,
		Int2,
		Int3,
		Int4,

		UInt1,
		UInt2,
		UInt3,
		UInt4
	};

	enum class VertexElementSemantic : uint8
	{
		Position,
		BlendWeights,
		BlendIndices,
		Normal,
		Diffuse,
		TextureCoordinate,
		Binormal,
		Tangent
	};

	class VertexElement
	{
	protected:
		uint16 m_source;
		uint32 m_offset;
		VertexElementType m_type;
		VertexElementSemantic m_semantic;
		uint16 m_index;

	public:
		VertexElement() = default;
		VertexElement(uint16 source, uint32 offset, VertexElementType type, VertexElementSemantic semantic, uint16 index = 0);

	public:
		[[nodiscard]] uint16 GetSource() const { return m_source; }

		[[nodiscard]] uint32 GetOffset() const { return m_offset; }

		[[nodiscard]] VertexElementType GetType() const { return m_type; }

		[[nodiscard]] VertexElementSemantic GetSemantic() const { return m_semantic; }

		[[nodiscard]] uint16 GetIndex() const { return m_index; }

		[[nodiscard]] uint32 GetSize() const;

		static uint32 GetTypeSize(VertexElementType type);

		static uint16 GetTypeCount(VertexElementType type);

		static VertexElementType MultiplyTypeCount(VertexElementType baseType, uint16 count);

		static VertexElementType GetBaseType(VertexElementType multiType);

		static void ConvertColourValue(VertexElementType srcType, VertexElementType dstType, uint32* ptr);

		static VertexElementType GetBestColourVertexElementType();

		bool operator== (const VertexElement& rhs) const
		{
			if (m_type != rhs.m_type ||
				m_index != rhs.m_index ||
				m_offset != rhs.m_offset ||
				m_semantic != rhs.m_semantic ||
				m_source != rhs.m_source)
				return false;

			return true;

		}

		void BaseVertexPointerToElement(void* pBase, void** pElem) const
		{
			*pElem = static_cast<void*>(static_cast<unsigned char*>(pBase) + m_offset);
		}

		void BaseVertexPointerToElement(void* pBase, float** pElem) const
		{
			*pElem = static_cast<float*>(
				static_cast<void*>(
					static_cast<unsigned char*>(pBase) + m_offset));
		}

		void BaseVertexPointerToElement(void* pBase, RGBA** pElem) const
		{
			*pElem = static_cast<RGBA*>(
				static_cast<void*>(
					static_cast<unsigned char*>(pBase) + m_offset));
		}

		void BaseVertexPointerToElement(void* pBase, unsigned char** pElem) const
		{
			*pElem = static_cast<unsigned char*>(pBase) + m_offset;
		}

		void BaseVertexPointerToElement(void* pBase, unsigned short** pElem) const
		{
			*pElem = static_cast<unsigned short*>(
				static_cast<void*>(
					static_cast<unsigned char*>(pBase) + m_offset));
		}
	};

	class VertexDeclaration
	{
	protected:
		std::list<VertexElement> m_elementList;

	public:
		VertexDeclaration();
		virtual ~VertexDeclaration();

	public:
		static bool VertexElementLess(const VertexElement& e1, const VertexElement& e2);

	public:
		[[nodiscard]] virtual size_t GetElementCount() const { return m_elementList.size(); }

		[[nodiscard]] virtual const std::list<VertexElement>& GetElements() const;

		[[nodiscard]] virtual const VertexElement* GetElement(uint16 index) const;

		virtual void Sort();

		virtual void CloseGapsInSource();

		[[nodiscard]] virtual VertexDeclaration* GetAutoOrganizedDeclaration(bool skeletalAnimation, bool vertexAnimation, bool vertexAnimationNormals) const;

		[[nodiscard]] virtual uint16 GetMaxSource() const;

		virtual const VertexElement& AddElement(uint16 source, uint32 offset, VertexElementType theType, VertexElementSemantic semantic, uint16 index = 0);

		virtual const VertexElement& InsertElement(uint16 atPosition, uint16 source, uint32 offset, VertexElementType theType, VertexElementSemantic semantic, uint16 index = 0);

		virtual void RemoveElement(uint16 index);

		virtual void RemoveElement(VertexElementSemantic semantic, uint16 index = 0);

		virtual void RemoveAllElements();

		virtual void ModifyElement(uint16 elementIndex, uint16 source, uint32 offset, VertexElementType theType, VertexElementSemantic semantic, uint16 index = 0);

		[[nodiscard]] virtual const VertexElement* FindElementBySemantic(VertexElementSemantic sem, uint16 index = 0) const;
		
		[[nodiscard]] virtual  std::list<VertexElement> FindElementsBySource(uint16 source) const;

		[[nodiscard]] virtual uint32 GetVertexSize(uint16 source) const;

		[[nodiscard]] virtual uint16 GetNextFreeTextureCoordinate() const;

		virtual VertexDeclaration* Clone(GraphicsDevice* device = nullptr) const;

	public:
	size_t GetHash() const
	{
		size_t hash = m_elementList.size();
		for (const auto& element : m_elementList)
		{
			// Combine all element properties into the hash
			hash = hash * 31 + static_cast<size_t>(element.GetSemantic());
			hash = hash * 31 + element.GetIndex();
			hash = hash * 31 + static_cast<size_t>(element.GetType());
			hash = hash * 31 + element.GetSource();
			hash = hash * 31 + element.GetOffset();
		}
		return hash;
	}

		bool operator== (const VertexDeclaration& rhs) const
		{
			if (m_elementList.size() != rhs.m_elementList.size())
			{
				return false;
			}

			auto it = m_elementList.begin();
			auto otherIt = rhs.m_elementList.begin();
			for (; it != m_elementList.end(); ++it, ++otherIt)
			{
				if (*it != *otherIt)
				{
					return false;
				}
			}

			return true;
		}

		bool operator!= (const VertexDeclaration& rhs) const
		{
			return !(*this == rhs);
		}
	};

	class VertexBufferBinding final
	{
	public:
		typedef std::map<uint16, VertexBufferPtr> VertexBufferBindingMap;
		typedef std::map<uint16, uint16> BindingIndexMap;

	protected:
		VertexBufferBindingMap m_bindingMap;
		mutable uint16 m_highestIndex;

	public:
		VertexBufferBinding();
		virtual ~VertexBufferBinding();

	public:
		void SetBinding(uint16 index, const VertexBufferPtr& buffer);

		void UnsetBinding(uint16 index);

		void UnsetAllBindings();

		const VertexBufferBindingMap& GetBindings() const;

		const VertexBufferPtr& GetBuffer(uint16 index) const;

		bool IsBufferBound(uint16 index) const;

		size_t GetBufferCount() const { return m_bindingMap.size(); }

		uint16 GetNextIndex() const { return m_highestIndex++; }

		uint16 GetLastBoundIndex() const;

		bool HasGaps() const;

		void CloseGaps(BindingIndexMap& bindingIndexMap);

		bool HasInstanceData() const;
	};
}
