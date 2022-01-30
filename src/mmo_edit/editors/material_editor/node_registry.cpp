
#include "node_registry.h"
#include "material_node.h"

#include <algorithm>

namespace mmo
{
	NodeRegistry::NodeRegistry()
		: m_BuildInNodes(
		{
			ConstFloatNode::GetStaticTypeInfo(),
			MaterialNode::GetStaticTypeInfo(),
			TextureNode::GetStaticTypeInfo(),
			TextureCoordNode::GetStaticTypeInfo(),
			MultiplyNode::GetStaticTypeInfo(),
			LerpNode::GetStaticTypeInfo(),
			AddNode::GetStaticTypeInfo(),
			ConstVectorNode::GetStaticTypeInfo()
		})
	{
		RebuildTypes();
	}

	uint32 NodeRegistry::RegisterNodeType(std::string_view name, NodeTypeInfo::Factory factory)
	{
		auto id = detail::fnv_1a_hash(name.data(), name.size());

		const auto it = std::find_if(m_CustomNodes.begin(), m_CustomNodes.end(), [id](const NodeTypeInfo& typeInfo)
		{
			return typeInfo.id == id;
		});

	    if (it != m_CustomNodes.end())
	        m_CustomNodes.erase(it);

	    NodeTypeInfo typeInfo;
	    typeInfo.id       = id;
	    typeInfo.name     = name;
	    typeInfo.factory  = factory;

	    m_CustomNodes.emplace_back(std::move(typeInfo));

	    RebuildTypes();

	    return id;
	}

	void NodeRegistry::UnregisterNodeType(std::string_view name)
	{
		const auto it = std::find_if(m_CustomNodes.begin(), m_CustomNodes.end(), [name](const NodeTypeInfo& typeInfo)
		{
			return typeInfo.name == name;
		});

	    if (it == m_CustomNodes.end())
	        return;

	    m_CustomNodes.erase(it);

	    RebuildTypes();
	}

	Node* NodeRegistry::Create(const uint32_t typeId, MaterialGraph& material) const
	{
		for (const auto& nodeInfo : m_Types)
	    {
	        if (nodeInfo->id != typeId)
	            continue;

	        return nodeInfo->factory(material);
	    }

	    return nullptr;
	}

	Node* NodeRegistry::Create(std::string_view typeName, MaterialGraph& material) const
	{
		for (auto& nodeInfo : m_Types)
	    {
	        if (nodeInfo->name != typeName)
	            continue;

	        return nodeInfo->factory(material);
	    }

	    return nullptr;
	}

	std::span<const NodeTypeInfo* const> NodeRegistry::GetTypes() const
	{
	    const NodeTypeInfo* const* begin = m_Types.data();
	    const NodeTypeInfo* const* end   = m_Types.data() + m_Types.size();
		return { begin, end };
	}

	void NodeRegistry::RebuildTypes()
	{
		 m_Types.resize(0);
	    m_Types.reserve(m_CustomNodes.size() + std::distance(std::begin(m_BuildInNodes), std::end(m_BuildInNodes)));

	    for (auto& typeInfo : m_CustomNodes)
	        m_Types.push_back(&typeInfo);

	    for (auto& typeInfo : m_BuildInNodes)
	        m_Types.push_back(&typeInfo);

	    std::sort(m_Types.begin(), m_Types.end(), [](const NodeTypeInfo* lhs, const NodeTypeInfo* rhs) { return lhs->id < rhs->id; });
	    m_Types.erase(std::unique(m_Types.begin(), m_Types.end()), m_Types.end());
	}
}
