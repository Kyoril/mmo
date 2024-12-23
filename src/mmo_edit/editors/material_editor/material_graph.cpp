// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_graph.h"

#include "base/chunk_writer.h"
#include "base/macros.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"

namespace mmo
{
	MaterialGraph::MaterialGraph(std::shared_ptr<NodeRegistry> nodeRegistry)
		: m_nodeRegistry(std::move(nodeRegistry))
	{
		if (!m_nodeRegistry)
		{
			m_nodeRegistry = std::make_shared<NodeRegistry>();
		}

		Clear();
	}

	MaterialGraph::MaterialGraph(const MaterialGraph& other)
		: m_nodeRegistry(other.m_nodeRegistry)
	{
		Clear();
	}

	MaterialGraph::MaterialGraph(MaterialGraph&& other) noexcept
		: m_nodeRegistry(std::move(other.m_nodeRegistry))
		, m_idGenerator(std::move(other.m_idGenerator))
		, m_nodes(std::move(other.m_nodes))
		, m_pins(std::move(other.m_pins))
		, m_rootNode(other.m_rootNode)
	{
		other.m_rootNode = nullptr;

		for (const auto& node : m_nodes)
		{
			node->m_material = this; 
		}
	}

	MaterialGraph::~MaterialGraph()
	{
		Clear(true);
	}

	io::Writer& MaterialGraph::Serialize(io::Writer& writer) const
	{
		ChunkWriter chunkWriter { ChunkMagic({'G', 'R', 'P', 'H'}), writer };
		{
			writer
				<< io::write<uint32>(m_nodes.size())
				<< io::write<uint32>(m_idGenerator.GetCurrentId())
				<< io::write<uint32>(m_rootNode ? m_rootNode->GetId() : 0xffffffff);
			
			for (const auto& node : m_nodes)
			{
				writer
					<< io::write<uint32>(node->GetTypeInfo().id);
				
				node->Serialize(writer);
			}
		}
		chunkWriter.Finish();
		return writer;
	}

	io::Reader& MaterialGraph::Deserialize(io::Reader& reader, IMaterialGraphLoadContext& context)
	{
		Clear(true);

		uint32 nodeCount, nextNodeId, rootNodeId;
		if (!(reader 
			>> io::read<uint32>(nodeCount)
			>> io::read<uint32>(nextNodeId)
			>> io::read<uint32>(rootNodeId)))
		{
			ELOG("Unable to deserialize material graph!");
			return reader;
		}

		for (uint32 i = 0; i < nodeCount; ++i)
		{
			uint32 nodeTypeId;
			if (!(reader >> io::read<uint32>(nodeTypeId)))
			{
				ELOG("Unable to deserialize material graph!");
				return reader;
			}

			auto* node = CreateNode(nodeTypeId);
			if (!node)
			{
				ELOG("Unable to create node type " << nodeTypeId << " received from deserialization!");
				return reader;
			}

			if (!node->Deserialize(reader, context))
			{
				ELOG("Unable to deserialize node from file!");
				return reader;
			}
		}

		context.AddPostLoadAction([this, rootNodeId, nextNodeId]()
		{
			if (rootNodeId != 0xffffffff)
			{
				m_rootNode = FindNode(rootNodeId);
				if (!m_rootNode)
				{
					ELOG("Unable to find old root node!");
					return false;
				}
			}
			
			m_idGenerator.Reset();
			m_idGenerator.NotifyId(nextNodeId);

			return true;
		});

		return reader;
	}

	MaterialGraph& MaterialGraph::operator=(const MaterialGraph& other)
	{
		if (this == &other)
	        return *this;

	    Clear();

		m_rootNode = other.m_rootNode;
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
		m_rootNode = other.m_rootNode;
		other.m_rootNode = nullptr;

	    for (const auto& node : m_nodes)
	    {
		    node->m_material = this;
	    }

	    return *this;
	}

	GraphNode* MaterialGraph::CreateNode(const uint32 nodeTypeId, bool allowRootNode)
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

		if (allowRootNode && m_rootNode == nullptr)
		{
			m_rootNode = node;
		}

	    m_nodes.push_back(node);
	    return node;
	}

	GraphNode* MaterialGraph::CreateNode(const std::string_view nodeTypeName, bool allowRootNode)
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

		if (allowRootNode && m_rootNode == nullptr)
		{
			m_rootNode = node;
		}

	    m_nodes.push_back(node);
	    return node;
	}

	void MaterialGraph::DeleteNode(const GraphNode* node)
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
	
	void MaterialGraph::Clear(const bool destroy)
	{
		for (const auto node : m_nodes)
		{
			delete node;
		}

		m_rootNode = nullptr;
	    m_nodes.resize(0);
	    m_pins.resize(0);
	    m_idGenerator.Reset();
	}

	std::span<const GraphNode* const> MaterialGraph::GetNodes() const
	{
		const GraphNode* const* begin = m_nodes.data();
	    const GraphNode* const* end   = m_nodes.data() + m_nodes.size();
	    return {begin, end};
	}

	std::span<const Pin* const> MaterialGraph::GetPins() const
	{
		const Pin* const* begin = m_pins.data();
	    const Pin* const* end   = m_pins.data() + m_pins.size();
	    return {begin, end};
	}

	GraphNode* MaterialGraph::FindNode(const uint32 nodeId)
	{
		return const_cast<GraphNode*>(const_cast<const MaterialGraph*>(this)->FindNode(nodeId));
	}

	const GraphNode* MaterialGraph::FindNode(const uint32 nodeId) const
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

	uint32 MaterialGraph::MakeNodeId(GraphNode* node)
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

	void MaterialGraph::Compile(MaterialCompiler& compiler) const
	{
		for (const auto& node : m_nodes)
		{
			node->NotifyCompilationStarted();
		}

		if (!m_rootNode)
		{
			return;
		}

		m_rootNode->Compile(compiler, nullptr);
	}

	bool MaterialGraph::IsRootNode(const uint32 nodeId) const noexcept
	{
		if (!m_rootNode)
		{
			return false;
		}

		return nodeId == m_rootNode->GetId();
	}
}
