
#include "vertex_declaration.h"

#include "graphics_device.h"
#include "base/macros.h"

namespace mmo
{
	bool VertexDeclaration::VertexElementLess(const VertexElement& e1, const VertexElement& e2)
	{
        if (e1.GetSource() < e2.GetSource())
        {
            return true;
        }

        if (e1.GetSource() == e2.GetSource())
        {
            if (e1.GetSemantic() < e2.GetSemantic())
            {
                return true;
            }

        	if (e1.GetSemantic() == e2.GetSemantic())
            {
                if (e1.GetIndex() < e2.GetIndex())
                {
                    return true;
                }
            }
        }

        return false;
	}

	VertexElement::VertexElement(uint16 source, uint32 offset, VertexElementType type, VertexElementSemantic semantic,
		unsigned short index)
	{
		m_source = source;
		m_offset = offset;
		m_type = type;
		m_semantic = semantic;
		m_index = index;
	}

	uint32 VertexElement::GetSize() const
	{
        return GetTypeSize(m_type);
	}

	uint32 VertexElement::GetTypeSize(VertexElementType type)
	{
        switch (type)
        {
        case VertexElementType::Color:
        case VertexElementType::ColorAbgr:
        case VertexElementType::ColorArgb:
            return sizeof(RGBA);
        case VertexElementType::Float1:
            return sizeof(float);
        case VertexElementType::Float2:
            return sizeof(float) * 2;
        case VertexElementType::Float3:
            return sizeof(float) * 3;
        case VertexElementType::Float4:
            return sizeof(float) * 4;
        case VertexElementType::Double1:
            return sizeof(double);
        case VertexElementType::Double2:
            return sizeof(double) * 2;
        case VertexElementType::Double3:
            return sizeof(double) * 3;
        case VertexElementType::Double4:
            return sizeof(double) * 4;
        case VertexElementType::Short1:
            return sizeof(short);
        case VertexElementType::Short2:
            return sizeof(short) * 2;
        case VertexElementType::Short3:
            return sizeof(short) * 3;
        case VertexElementType::Short4:
            return sizeof(short) * 4;
        case VertexElementType::UShort1:
            return sizeof(unsigned short);
        case VertexElementType::UShort2:
            return sizeof(unsigned short) * 2;
        case VertexElementType::UShort3:
            return sizeof(unsigned short) * 3;
        case VertexElementType::UShort4:
            return sizeof(unsigned short) * 4;
        case VertexElementType::Int1:
            return sizeof(int);
        case VertexElementType::Int2:
            return sizeof(int) * 2;
        case VertexElementType::Int3:
            return sizeof(int) * 3;
        case VertexElementType::Int4:
            return sizeof(int) * 4;
        case VertexElementType::UInt1:
            return sizeof(unsigned int);
        case VertexElementType::UInt2:
            return sizeof(unsigned int) * 2;
        case VertexElementType::UInt3:
            return sizeof(unsigned int) * 3;
        case VertexElementType::UInt4:
            return sizeof(unsigned int) * 4;
        case VertexElementType::UByte4:
            return sizeof(unsigned char) * 4;
        }

        return 0;
	}

	uint16 VertexElement::GetTypeCount(VertexElementType type)
	{
		switch (type)
		{
        case VertexElementType::Color:
		case VertexElementType::ColorAbgr:
		case VertexElementType::ColorArgb:
			return 1;
		case VertexElementType::Float1:
		case VertexElementType::Short1:
		case VertexElementType::UShort1:
		case VertexElementType::Int1:
		case VertexElementType::UInt1:
			return 1;
		case VertexElementType::Float2:
		case VertexElementType::Short2:
		case VertexElementType::UShort2:
		case VertexElementType::Int2:
		case VertexElementType::UInt2:
			return 2;
		case VertexElementType::Float3:
		case VertexElementType::Short3:
		case VertexElementType::UShort3:
		case VertexElementType::Int3:
		case VertexElementType::UInt3:
			return 3;
		case VertexElementType::Float4:
		case VertexElementType::Short4:
		case VertexElementType::UShort4:
		case VertexElementType::Int4:
		case VertexElementType::UInt4:
		case VertexElementType::UByte4:
			return 4;
		case VertexElementType::Double1:
			return 1;
		case VertexElementType::Double2:
			return 2;
		case VertexElementType::Double3:
			return 3;
		case VertexElementType::Double4:
			return 4;
		}

        ASSERT(false && "Invalid type");
        return 0;
	}

	VertexElementType VertexElement::MultiplyTypeCount(VertexElementType baseType, unsigned short count)
	{
        switch (baseType)
        {
        case VertexElementType::Float1:
            switch (count)
            {
            case 1:
                return VertexElementType::Float1;
            case 2:
                return VertexElementType::Float2;
            case 3:
                return VertexElementType::Float3;
            case 4:
                return VertexElementType::Float4;
            default:
                break;
            }
            break;
        case VertexElementType::Short1:
            switch (count)
            {
            case 1:
                return VertexElementType::Short1;
            case 2:
                return VertexElementType::Short2;
            case 3:
                return VertexElementType::Short3;
            case 4:
                return VertexElementType::Short4;
            default:
                break;
            }
            break;
        default:
            break;
        }

        ASSERT(false && "Invalid base type");
        return baseType;
	}

	VertexElementType VertexElement::GetBaseType(VertexElementType multiType)
	{
		switch (multiType)
		{
        case VertexElementType::Float1:
		case VertexElementType::Float2:
		case VertexElementType::Float3:
		case VertexElementType::Float4:
			return VertexElementType::Float1;
		case VertexElementType::Short1:
		case VertexElementType::Short2:
		case VertexElementType::Short3:
		case VertexElementType::Short4:
			return VertexElementType::Short1;
		case VertexElementType::UShort1:
		case VertexElementType::UShort2:
		case VertexElementType::UShort3:
		case VertexElementType::UShort4:
			return VertexElementType::UShort1;
		case VertexElementType::Int1:
		case VertexElementType::Int2:
		case VertexElementType::Int3:
		case VertexElementType::Int4:
			return VertexElementType::Int1;
		case VertexElementType::UInt1:
		case VertexElementType::UInt2:
		case VertexElementType::UInt3:
		case VertexElementType::UInt4:
			return VertexElementType::UInt1;
		case VertexElementType::UByte4:
			return VertexElementType::UByte4;
		case VertexElementType::Double1:
		case VertexElementType::Double2:
		case VertexElementType::Double3:
		case VertexElementType::Double4:
			return VertexElementType::Double1;
		case VertexElementType::Color:
		case VertexElementType::ColorAbgr:
		case VertexElementType::ColorArgb:
			return VertexElementType::Color;
		}

		ASSERT(false && "Invalid type");
		return VertexElementType::Float1;
	}

	void VertexElement::ConvertColourValue(VertexElementType srcType, VertexElementType dstType, uint32* ptr)
	{
        if (srcType == dstType)
        {
            return;
        }

        // Conversion between ARGB and ABGR is always a case of flipping R/B
        *ptr =
            ((*ptr & 0x00FF0000) >> 16) | ((*ptr & 0x000000FF) << 16) | (*ptr & 0xFF00FF00);
	}

	VertexElementType VertexElement::GetBestColourVertexElementType()
	{
        // TODO: Graphics impl
        return VertexElementType::ColorArgb;
	}

    VertexDeclaration::VertexDeclaration() = default;

	VertexDeclaration::~VertexDeclaration() = default;

	const std::list<VertexElement>& VertexDeclaration::GetElements() const
	{
		return m_elementList;
	}

	const VertexElement* VertexDeclaration::GetElement(uint16 index) const
	{
        ASSERT(index < m_elementList.size() && "Index out of bounds");

        auto i = m_elementList.begin();
        std::advance(i, index);

        return &(*i);
	}

	void VertexDeclaration::Sort()
	{
		m_elementList.sort(VertexElementLess);
	}

	void VertexDeclaration::CloseGapsInSource()
	{
        if (m_elementList.empty())
        {
            return;
        }

        Sort();

        unsigned short targetIdx = 0;
        unsigned short lastIdx = GetElement(0)->GetSource();
        unsigned short c = 0;
        for (auto i = m_elementList.begin(); i != m_elementList.end(); ++i, ++c)
        {
            VertexElement& elem = *i;
            if (lastIdx != elem.GetSource())
            {
                targetIdx++;
                lastIdx = elem.GetSource();
            }
            if (targetIdx != elem.GetSource())
            {
                ModifyElement(c, targetIdx, elem.GetOffset(), elem.GetType(),
                    elem.GetSemantic(), elem.GetIndex());
            }
        }
	}

	VertexDeclaration* VertexDeclaration::GetAutoOrganizedDeclaration(bool skeletalAnimation, bool vertexAnimation,
		bool vertexAnimationNormals) const
	{
        VertexDeclaration* newDecl = Clone();

        // Set all sources to the same buffer (for now)
        const auto& elems = newDecl->GetElements();
        unsigned short c = 0;
        for (auto i = elems.cbegin(); i != elems.cend(); ++i, ++c)
        {
            const VertexElement& elem = *i;
            // Set source & offset to 0 for now, before sort
            newDecl->ModifyElement(c, 0, 0, elem.GetType(), elem.GetSemantic(), elem.GetIndex());
        }

        newDecl->Sort();

        // Now sort out proper buffer assignments and offsets
        size_t offset = 0;
        c = 0;
        unsigned short buffer = 0;
        auto prevSemantic = VertexElementSemantic::Position;
        for (auto i = elems.cbegin(); i != elems.cend(); ++i, ++c)
        {
            const VertexElement& elem = *i;

            bool splitWithPrev = false;
            bool splitWithNext = false;
            switch (elem.GetSemantic())
            {
            case VertexElementSemantic::Position:
                // Split positions if vertex animated with only positions
                // group with normals otherwise
                splitWithPrev = false;
                splitWithNext = vertexAnimation && !vertexAnimationNormals;
                break;
            case VertexElementSemantic::Normal:
                // Normals can't share with blend weights/indices
                splitWithPrev = (prevSemantic == VertexElementSemantic::BlendWeights || prevSemantic == VertexElementSemantic::BlendIndices);
                // All animated meshes have to split after normal
                splitWithNext = (skeletalAnimation || (vertexAnimation && vertexAnimationNormals));
                break;
            case VertexElementSemantic::BlendWeights:
                // Blend weights/indices can be sharing with their own buffer only
                splitWithPrev = true;
                break;
            case VertexElementSemantic::BlendIndices:
                // Blend weights/indices can be sharing with their own buffer only
                splitWithNext = true;
                break;
            default:
            case VertexElementSemantic::Diffuse:
            case VertexElementSemantic::TextureCoordinate:
            case VertexElementSemantic::Binormal:
            case VertexElementSemantic::Tangent:
                // Make sure position is separate if animated & there were no normals
                splitWithPrev = prevSemantic == VertexElementSemantic::Position && (skeletalAnimation || vertexAnimation);
                break;
            }

            if (splitWithPrev && offset)
            {
                ++buffer;
                offset = 0;
            }

            prevSemantic = elem.GetSemantic();
            newDecl->ModifyElement(c, buffer, offset,
                elem.GetType(), elem.GetSemantic(), elem.GetIndex());

            if (splitWithNext)
            {
                ++buffer;
                offset = 0;
            }
            else
            {
                offset += elem.GetSize();
            }
        }

        return newDecl;
	}

	uint16 VertexDeclaration::GetMaxSource() const
	{
		if (m_elementList.empty())
		{
						return 0;
		}

		uint16 ret = 0;
		for (auto i = m_elementList.cbegin(); i != m_elementList.cend(); ++i)
		{
			const VertexElement& elem = *i;
			if (elem.GetSource() > ret)
			{
				ret = elem.GetSource();
			}
		}

		return ret;
	}

	const VertexElement& VertexDeclaration::AddElement(uint16 source, uint32 offset, VertexElementType theType,
		VertexElementSemantic semantic, uint16 index)
	{
		// Refine colour type to a specific type
        if (theType == VertexElementType::Color)
        {
            theType = VertexElement::GetBestColourVertexElementType();
        }

        return m_elementList.emplace_back(source, offset, theType, semantic, index);
	}

	const VertexElement& VertexDeclaration::InsertElement(uint16 atPosition, uint16 source, uint32 offset,
		VertexElementType theType, VertexElementSemantic semantic, uint16 index)
	{
        if (atPosition >= m_elementList.size())
        {
            return AddElement(source, offset, theType, semantic, index);
        }

        auto i = m_elementList.begin();
        std::advance(i, atPosition);

        i = m_elementList.insert(i, VertexElement(source, offset, theType, semantic, index));
        return *i;
	}

	void VertexDeclaration::RemoveElement(uint16 index)
	{
        ASSERT(index < m_elementList.size() && "Index out of bounds");

        auto i = m_elementList.begin();
        std::advance(i, index);
        m_elementList.erase(i);
	}

	void VertexDeclaration::RemoveElement(VertexElementSemantic semantic, uint16 index)
	{
		for (auto i = m_elementList.begin(); i != m_elementList.end(); ++i)
		{
            const VertexElement& elem = *i;
			if (elem.GetSemantic() == semantic && elem.GetIndex() == index)
			{
                m_elementList.erase(i);
				return;
			}
		}
	}

	void VertexDeclaration::RemoveAllElements()
	{
        m_elementList.clear();
	}

	void VertexDeclaration::ModifyElement(uint16 elem_index, uint16 source, uint32 offset, VertexElementType theType,
		VertexElementSemantic semantic, uint16 index)
	{
        ASSERT(elem_index < m_elementList.size() && "Index out of bounds");

		auto i = m_elementList.begin();
		std::advance(i, elem_index);
		*i = VertexElement(source, offset, theType, semantic, index);
	}

	const VertexElement* VertexDeclaration::FindElementBySemantic(VertexElementSemantic sem, uint16 index) const
	{
        for (auto& elem : m_elementList)
        {
	        if (elem.GetSemantic() == sem && elem.GetIndex() == index)
			{
                return &elem;
			}
		}

		return nullptr;
	}

	std::list<VertexElement> VertexDeclaration::FindElementsBySource(uint16 source) const
	{
		std::list<VertexElement> ret;
		for (auto& elem : m_elementList)
		{
	        if (elem.GetSource() == source)
	        {
                ret.push_back(elem);
			}
		}

		return ret;
	}

	size_t VertexDeclaration::GetVertexSize(uint16 source) const
	{
		size_t sz = 0;
		for (auto& elem : m_elementList)
		{
	        if (elem.GetSource() == source)
	        {
                sz += elem.GetSize();
			}
		}

		return sz;
	}

	uint16 VertexDeclaration::GetNextFreeTextureCoordinate() const
	{
		uint16 max = 0;

		for (auto& elem : m_elementList)
		{
	        if (elem.GetSemantic() == VertexElementSemantic::TextureCoordinate)
	        {
                ++max;
			}
		}

		return max;
	}

	VertexDeclaration* VertexDeclaration::Clone(GraphicsDevice* device) const
	{
        GraphicsDevice* dev = device ? device : &GraphicsDevice::Get();
        VertexDeclaration* ret = dev->CreateVertexDeclaration();

        for (const auto& elem : m_elementList)
        {
            ret->AddElement(elem.GetSource(), elem.GetOffset(), elem.GetType(), elem.GetSemantic(), elem.GetIndex());
        }

        return ret;
	}

	VertexBufferBinding::VertexBufferBinding()
		: m_highestIndex(0)
	{
	}

	VertexBufferBinding::~VertexBufferBinding()
	{
		VertexBufferBinding::UnsetAllBindings();
	}

	void VertexBufferBinding::SetBinding(uint16 index, const VertexBufferPtr& buffer)
	{
        m_bindingMap[index] = buffer;
        m_highestIndex = std::max(m_highestIndex, static_cast<uint16>(index + 1));
	}

	void VertexBufferBinding::UnsetBinding(uint16 index)
	{
        const auto it = m_bindingMap.find(index);
        ASSERT(it != m_bindingMap.end() && "Index does not exist");

        m_bindingMap.erase(it);
	}

	void VertexBufferBinding::UnsetAllBindings()
	{
		m_bindingMap.clear();
		m_highestIndex = 0;
	}

	const VertexBufferBinding::VertexBufferBindingMap& VertexBufferBinding::GetBindings() const
	{
		return m_bindingMap;
	}

	const VertexBufferPtr& VertexBufferBinding::GetBuffer(uint16 index) const
	{
		const auto it = m_bindingMap.find(index);
		ASSERT(it != m_bindingMap.end() && "Index does not exist");

		return it->second;
	}

	bool VertexBufferBinding::IsBufferBound(uint16 index) const
	{
		return m_bindingMap.contains(index);
	}

	uint16 VertexBufferBinding::GetLastBoundIndex() const
	{
        return m_bindingMap.empty() ? 0 : m_bindingMap.rbegin()->first + 1;
	}

	bool VertexBufferBinding::HasGaps() const
	{
        if (m_bindingMap.empty())
        {
            return false;
        }

        if (m_bindingMap.rbegin()->first + 1 == static_cast<int>(m_bindingMap.size()))
        {
            return false;
        }

        return true;
	}

	void VertexBufferBinding::CloseGaps(BindingIndexMap& bindingIndexMap)
	{
        bindingIndexMap.clear();

        VertexBufferBindingMap newBindingMap;

        uint16 targetIndex = 0;
        for (auto it = m_bindingMap.cbegin(); it != m_bindingMap.cend(); ++it, ++targetIndex)
        {
            bindingIndexMap[it->first] = targetIndex;
            newBindingMap[targetIndex] = it->second;
        }

        m_bindingMap.swap(newBindingMap);
        m_highestIndex = targetIndex;
	}

	bool VertexBufferBinding::HasInstanceData() const
	{
        // TODO: Support instance data
        return false;
	}
}
