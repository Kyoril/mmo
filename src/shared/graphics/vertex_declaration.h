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
		VertexElement(uint16 source, uint32 offset, VertexElementType type,
			VertexElementSemantic semantic, unsigned short index = 0);

	public:
		uint16 GetSource() const { return m_source; }
		uint32 GetOffset() const { return m_offset; }
		VertexElementType GetType() const { return m_type; }
		VertexElementSemantic GetSemantic() const { return m_semantic; }
		uint16 GetIndex() const { return m_index; }
		uint32 GetSize() const;
		static uint32 GetTypeSize(VertexElementType type);
		static uint16 GetTypeCount(VertexElementType type);
		static VertexElementType MultiplyTypeCount(VertexElementType baseType, unsigned short count);
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
		size_t GetElementCount() const { return m_elementList.size(); }

		const std::list<VertexElement>& GetElements() const;

		const VertexElement* GetElement(uint16 index) const;

		void Sort();

		void CloseGapsInSource();

		VertexDeclaration* GetAutoOrganizedDeclaration(bool skeletalAnimation, bool vertexAnimation, bool vertexAnimationNormals) const;

		/** Gets the index of the highest source value referenced by this declaration. */
		uint16 GetMaxSource() const;

		virtual const VertexElement& AddElement(uint16 source, uint32 offset, VertexElementType theType,
			VertexElementSemantic semantic, uint16 index = 0);

		virtual const VertexElement& InsertElement(uint16 atPosition,
			uint16 source, uint32 offset, VertexElementType theType,
			VertexElementSemantic semantic, uint16 index = 0);

		virtual void RemoveElement(uint16 index);

		virtual void RemoveElement(VertexElementSemantic semantic, uint16 index = 0);

		virtual void RemoveAllElements();

		virtual void ModifyElement(uint16 elem_index, uint16 source, uint32 offset, VertexElementType theType,
			VertexElementSemantic semantic, uint16 index = 0);

		virtual const VertexElement* FindElementBySemantic(VertexElementSemantic sem, uint16 index = 0) const;
		
		virtual std::list<VertexElement> FindElementsBySource(uint16 source) const;

		/** Gets the vertex size defined by this declaration for a given source. */
		virtual size_t GetVertexSize(uint16 source) const;

		virtual uint16 GetNextFreeTextureCoordinate() const;

		virtual VertexDeclaration* Clone(GraphicsDevice* device = nullptr) const;

	public:
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

	class VertexBufferBinding
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
		virtual void SetBinding(uint16 index, const VertexBufferPtr& buffer);

		virtual void UnsetBinding(uint16 index);

		virtual void UnsetAllBindings();

		virtual const VertexBufferBindingMap& GetBindings() const;

		virtual const VertexBufferPtr& GetBuffer(uint16 index) const;

		virtual bool IsBufferBound(uint16 index) const;

		virtual size_t GetBufferCount() const { return m_bindingMap.size(); }

		virtual uint16 GetNextIndex() const { return m_highestIndex++; }

		virtual uint16 GetLastBoundIndex() const;

		virtual bool HasGaps() const;

		virtual void CloseGaps(BindingIndexMap& bindingIndexMap);

		virtual bool HasInstanceData() const;
	};
}
