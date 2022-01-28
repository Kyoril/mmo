// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "material_graph.h"

namespace mmo
{
	MaterialGraph::MaterialGraph(std::shared_ptr<NodeRegistry> nodeRegistry)
		: m_nodeRegistry(std::move(nodeRegistry))
	{
		if (!m_nodeRegistry)
		{
			m_nodeRegistry = std::make_shared<NodeRegistry>();
		}
	}

	MaterialGraph::MaterialGraph(const MaterialGraph& other): m_nodeRegistry(other.m_nodeRegistry)
	{
	}

	MaterialGraph::MaterialGraph(MaterialGraph&& other) noexcept
		: m_nodeRegistry(std::move(other.m_nodeRegistry))
		, m_idGenerator(std::move(other.m_idGenerator))
		, m_nodes(std::move(other.m_nodes))
		, m_pins(std::move(other.m_pins))
	{
		for (const auto& node : m_nodes)
		{
			node->m_material = this; 
		}
	}

	MaterialGraph::~MaterialGraph()
	{
		Clear();
	}

	MaterialGraph& MaterialGraph::operator=(const MaterialGraph& other)
	{
		if (this == &other)
	        return *this;

	    Clear();

	    m_nodeRegistry = other.m_nodeRegistry;
	    return *this;
	}

	MaterialGraph& MaterialGraph::operator=(MaterialGraph&& other) noexcept
	{
	    if (this == &other)
	    {
		    return *this;
	    }

	    m_nodeRegistry = std::move(other.m_nodeRegistry);
	    m_idGenerator = std::move(other.m_idGenerator);
	    m_nodes = std::move(other.m_nodes);
	    m_pins = std::move(other.m_pins);

	    for (const auto& node : m_nodes)
	    {
		    node->m_material = this;
	    }

	    return *this;
	}

	Node* MaterialGraph::CreateNode(const uint32 nodeTypeId)
	{
	    if (!m_nodeRegistry)
	    {
		    return nullptr;
	    }

	    const auto node = m_nodeRegistry->Create(nodeTypeId, *this);
	    if (!node)
	    {
		    return nullptr;
	    }

	    m_nodes.push_back(node);
	    return node;
	}

	Node* MaterialGraph::CreateNode(const std::string_view nodeTypeName)
	{
		if (!m_nodeRegistry)
		{
			return nullptr;
		}

		const auto node = m_nodeRegistry->Create(nodeTypeName, *this);
	    if (!node)
	    {
		    return nullptr;
	    }

	    m_nodes.push_back(node);
	    return node;
	}

	void MaterialGraph::DeleteNode(const Node* node)
	{
		const auto nodeIt = std::find(m_nodes.begin(), m_nodes.end(), node);
	    if (nodeIt == m_nodes.end())
	    {
	    	return;
		}

	    delete *nodeIt;

	    m_nodes.erase(nodeIt);
	}

	void MaterialGraph::ForgetPin(const Pin* pin)
	{
		const auto pinIt = std::find(m_pins.begin(), m_pins.end(), pin);
	    if (pinIt == m_pins.end())
	    {
		    return;
	    }

	    m_pins.erase(pinIt);
	}

	void MaterialGraph::Clear()
	{
		for (const auto node : m_nodes)
		{
			delete node;
		}

	    m_nodes.resize(0);
	    m_pins.resize(0);
	    m_idGenerator.Reset();
	}

	std::span<const Node* const> MaterialGraph::GetNodes() const
	{
		const Node* const* begin = m_nodes.data();
	    const Node* const* end   = m_nodes.data() + m_nodes.size();
	    return {begin, end};
	}

	std::span<const Pin* const> MaterialGraph::GetPins() const
	{
		const Pin* const* begin = m_pins.data();
	    const Pin* const* end   = m_pins.data() + m_pins.size();
	    return {begin, end};
	}

	Node* MaterialGraph::FindNode(const uint32 nodeId)
	{
		return const_cast<Node*>(const_cast<const MaterialGraph*>(this)->FindNode(nodeId));
	}

	const Node* MaterialGraph::FindNode(const uint32 nodeId) const
	{
	    for (auto& node : m_nodes)
	    {
	        if (node->m_id == nodeId)
	        {
		        return node;
	        }
	    }

	    return nullptr;
	}

	Pin* MaterialGraph::FindPin(const uint32 pinId)
	{
		return const_cast<Pin*>(const_cast<const MaterialGraph*>(this)->FindPin(pinId));
	}

	const Pin* MaterialGraph::FindPin(const uint32 pinId) const
	{
	    for (auto& pin : m_pins)
	    {
	        if (pin->GetId() == pinId)
	        {
	        	return pin;
			}
	    }

	    return nullptr;
	}

	uint32 MaterialGraph::MakeNodeId(Node* node)
	{
		return m_idGenerator.GenerateId();
	}

	uint32 MaterialGraph::MakePinId(Pin* pin)
	{
		m_pins.push_back(pin);
		return m_idGenerator.GenerateId();
	}

	bool MaterialGraph::HasPinAnyLink(const Pin& pin) const
	{
		if (pin.IsLinked())
		{
			return true;
		}

	    for (auto& p : m_pins)
	    {
		    const auto linkedPin = p->GetLink();
	        if (linkedPin && linkedPin->GetId() == pin.GetId())
	        {
		        return true;
	        }
	    }

	    return false;
	}

	std::vector<Pin*> MaterialGraph::FindPinsLinkedTo(const Pin& pin) const
	{
		std::vector<Pin*> result;

	    for (auto& p : m_pins)
	    {
		    const auto linkedPin = p->GetLink();

	        if (linkedPin && linkedPin->GetId() == pin.GetId())
	        {
		        result.push_back(p);
	        }
	    }

	    return result;
	}
}
