// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "node_registry.h"
#include "editors/material_editor/material_node.h"

#include <algorithm>

namespace mmo
{
	NodeRegistry::NodeRegistry()
		: m_BuildInNodes(
		{
			ConstFloatNode::GetStaticTypeInfo(),
			TextureNode::GetStaticTypeInfo(),
			TextureCoordNode::GetStaticTypeInfo(),
			MultiplyNode::GetStaticTypeInfo(),
			LerpNode::GetStaticTypeInfo(),
			AddNode::GetStaticTypeInfo(),
			ConstVectorNode::GetStaticTypeInfo(),
			ClampNode::GetStaticTypeInfo(),
			DotNode::GetStaticTypeInfo(),
			OneMinusNode::GetStaticTypeInfo(),
			PowerNode::GetStaticTypeInfo(),
			WorldPositionNode::GetStaticTypeInfo(),
			MaskNode::GetStaticTypeInfo(),
			VertexNormalNode::GetStaticTypeInfo(),
			AbsNode::GetStaticTypeInfo(),
			DivideNode::GetStaticTypeInfo(),
			CameraVectorNode::GetStaticTypeInfo(),
			SubtractNode::GetStaticTypeInfo(),
			NormalizeNode::GetStaticTypeInfo(),
			VertexColorNode::GetStaticTypeInfo(),
			AppendNode::GetStaticTypeInfo(),
			WorldToTangentNormalNode::GetStaticTypeInfo(),
			MaterialFunctionNode::GetStaticTypeInfo(),
			TextureParameterNode::GetStaticTypeInfo(),
			ScalarParameterNode::GetStaticTypeInfo(),
			VectorParameterNode::GetStaticTypeInfo(),
			IfNode::GetStaticTypeInfo(),
			SineNode::GetStaticTypeInfo(),
			CosineNode::GetStaticTypeInfo(),
			TangentNode::GetStaticTypeInfo(),
			ArcTangent2Node::GetStaticTypeInfo(),
			FracNode::GetStaticTypeInfo()
		})
	{
		RebuildTypes();
	}

	uint32 NodeRegistry::RegisterNodeType(std::string_view name, std::string_view displayName, NodeTypeInfo::Factory factory)
	{
		auto id = detail::fnv_1a_hash(name.data(), name.size());

		NodeTypeInfo typeInfo;
		typeInfo.id = id;
		typeInfo.name = name;
		typeInfo.displayName = displayName;
		typeInfo.factory = factory;
		RegisterNodeType(typeInfo);

		return id;
	}

	void NodeRegistry::RegisterNodeType(const NodeTypeInfo& typeInfo)
	{
		const auto it = std::find_if(m_CustomNodes.begin(), m_CustomNodes.end(), [typeInfo](const NodeTypeInfo& info)
			{
				return info.id == typeInfo.id;
			});

		if (it != m_CustomNodes.end())
			m_CustomNodes.erase(it);

		m_CustomNodes.push_back(typeInfo);
		RebuildTypes();
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

	GraphNode* NodeRegistry::Create(const uint32_t typeId, MaterialGraph& material) const
	{
		for (const auto& nodeInfo : m_Types)
	    {
	        if (nodeInfo->id != typeId)
	            continue;

	        return nodeInfo->factory(material);
	    }

	    return nullptr;
	}

	GraphNode* NodeRegistry::Create(std::string_view typeName, MaterialGraph& material) const
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
