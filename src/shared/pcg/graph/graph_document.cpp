// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "graph_document.h"

#include "graph_io.h"
#include "hashing.h"

#include <algorithm>
#include <utility>

namespace mmo::pcg::graph
{
	namespace
	{
		/// Kinds are compatible if either side accepts any kind or the two lists share a kind.
		bool KindsCompatible(const std::vector<ItemKind>& produced, const std::vector<ItemKind>& accepted)
		{
			if (produced.empty() || accepted.empty())
			{
				return true;
			}
			for (const ItemKind kind : produced)
			{
				if (std::find(accepted.begin(), accepted.end(), kind) != accepted.end())
				{
					return true;
				}
			}
			return false;
		}

		bool DeclaresPin(const NodeTypeDef* def, const bool isOutput, const String& name)
		{
			if (def == nullptr)
			{
				return false;
			}
			const std::vector<PinDef>& pins = isOutput ? def->outputs : def->inputs;
			return std::any_of(pins.begin(), pins.end(), [&name](const PinDef& pin)
			{
				return pin.name == name;
			});
		}

		void InsertSorted(std::vector<String>& names, const String& name)
		{
			const auto it = std::lower_bound(names.begin(), names.end(), name);
			if (it == names.end() || *it != name)
			{
				names.insert(it, name);
			}
		}
	}

	GraphDocument::GraphDocument()
		: m_registry(&NodeRegistry::Default())
	{
	}

	uint64 GraphDocument::MakePinId(const uint32 node, const bool isOutput, const uint32 index)
	{
		return (static_cast<uint64>(node) << 16) | (isOutput ? (uint64(1) << 15) : 0) | (index & MaxPinIndex);
	}

	GraphDocument::PinRef GraphDocument::DecodePinId(const uint64 pinId)
	{
		PinRef ref;
		ref.node = static_cast<uint32>(pinId >> 16);
		ref.isOutput = ((pinId >> 15) & 1) != 0;
		ref.index = static_cast<uint32>(pinId & MaxPinIndex);
		return ref;
	}

	uint64 GraphDocument::MakeLinkId(const uint64 fromPin, const uint64 toPin)
	{
		Hasher hasher;
		hasher.U64(fromPin);
		hasher.U64(toPin);
		const uint64 id = hasher.Result();
		return id != 0 ? id : 1;
	}

	bool GraphDocument::Load(const String& json, const NodeRegistry& registry, String& error)
	{
		Graph parsed;
		if (!ParseGraph(json, parsed, error))
		{
			return false;
		}

		m_registry = &registry;
		m_graph = std::move(parsed);
		RebuildExtraPins();
		Touch(true);
		return true;
	}

	String GraphDocument::Save() const
	{
		return WriteGraph(m_graph);
	}

	uint32 GraphDocument::AddNode(const String& type, const float x, const float y)
	{
		const NodeTypeDef* def = m_registry->Find(type);
		if (def == nullptr)
		{
			return 0;
		}

		// Never reuse an id, even if the file's nextNodeId lags behind its nodes.
		uint32 id = m_graph.nextNodeId;
		for (const NodeInstance& node : m_graph.nodes)
		{
			if (node.id >= id && node.id <= MaxNodeId)
			{
				id = node.id + 1;
			}
		}
		if (id == 0 || id > MaxNodeId)
		{
			return 0;
		}

		NodeInstance node;
		node.id = id;
		node.type = type;
		node.editorX = x;
		node.editorY = y;
		for (const SettingDef& setting : def->settings)
		{
			node.settings[setting.name] = setting.defaultValue;
		}
		m_graph.nodes.push_back(std::move(node));
		m_graph.nextNodeId = id + 1;
		Touch(true);
		return id;
	}

	bool GraphDocument::RemoveNode(const uint32 id)
	{
		const auto it = std::find_if(m_graph.nodes.begin(), m_graph.nodes.end(), [id](const NodeInstance& node)
		{
			return node.id == id;
		});
		if (it == m_graph.nodes.end())
		{
			return false;
		}

		m_graph.nodes.erase(it);
		std::erase_if(m_graph.links, [id](const Link& link)
		{
			return link.fromNode == id || link.toNode == id;
		});
		m_extraPins.erase(id);
		Touch(true);
		return true;
	}

	bool GraphDocument::CanLink(const uint64 fromPin, const uint64 toPin, String& reason) const
	{
		const PinRef from = DecodePinId(fromPin);
		const PinRef to = DecodePinId(toPin);
		if (!from.isOutput || to.isOutput)
		{
			reason = "links go from an output to an input";
			return false;
		}

		const String* fromName = FindPinName(fromPin);
		const String* toName = FindPinName(toPin);
		if (fromName == nullptr || toName == nullptr)
		{
			reason = "unknown pin";
			return false;
		}
		if (from.node == to.node)
		{
			reason = "a node cannot link to itself";
			return false;
		}

		for (const Link& link : m_graph.links)
		{
			if (link.fromNode == from.node && link.fromPin == *fromName && link.toNode == to.node && link.toPin == *toName)
			{
				reason = "the pins are already linked";
				return false;
			}
		}

		const PinDef* fromDef = FindPinDef(fromPin);
		const PinDef* toDef = FindPinDef(toPin);
		if (fromDef != nullptr && toDef != nullptr && !KindsCompatible(fromDef->accepts, toDef->accepts))
		{
			reason = "the output's item kinds are not accepted by the input";
			return false;
		}

		if (Reaches(to.node, from.node))
		{
			reason = "the link would create a cycle";
			return false;
		}

		return true;
	}

	bool GraphDocument::AddLink(const uint64 fromPin, const uint64 toPin)
	{
		String reason;
		if (!CanLink(fromPin, toPin, reason))
		{
			return false;
		}

		const PinRef from = DecodePinId(fromPin);
		const PinRef to = DecodePinId(toPin);
		const String fromName = *FindPinName(fromPin);
		const String toName = *FindPinName(toPin);

		const PinDef* toDef = FindPinDef(toPin);
		if (toDef != nullptr && !toDef->multiple)
		{
			std::erase_if(m_graph.links, [&](const Link& link)
			{
				return link.toNode == to.node && link.toPin == toName;
			});
		}

		m_graph.links.push_back(Link{ from.node, fromName, to.node, toName });
		Touch(true);
		return true;
	}

	bool GraphDocument::RemoveLink(const uint64 linkId)
	{
		for (const ResolvedLink& link : ResolveLinks())
		{
			if (link.id == linkId)
			{
				m_graph.links.erase(m_graph.links.begin() + static_cast<std::ptrdiff_t>(link.index));
				Touch(true);
				return true;
			}
		}
		return false;
	}

	bool GraphDocument::SetSetting(const uint32 node, const String& name, const AttributeValue& value)
	{
		NodeInstance* instance = FindMutableNode(node);
		if (instance == nullptr)
		{
			return false;
		}

		if (const NodeTypeDef* def = m_registry->Find(instance->type))
		{
			for (const SettingDef& setting : def->settings)
			{
				String parameter;
				if (setting.name == name && TypeOf(value) != TypeOf(setting.defaultValue) && !IsParameterBinding(value, parameter))
				{
					return false;
				}
			}
		}

		instance->settings[name] = value;
		Touch(true);
		return true;
	}

	bool GraphDocument::SetNodePosition(const uint32 node, const float x, const float y)
	{
		NodeInstance* instance = FindMutableNode(node);
		if (instance == nullptr || (instance->editorX == x && instance->editorY == y))
		{
			return false;
		}

		instance->editorX = x;
		instance->editorY = y;
		Touch(false);
		return true;
	}

	const Graph& GraphDocument::GetGraph() const
	{
		return m_graph;
	}

	Graph& GraphDocument::EditGraph()
	{
		Touch(true);
		return m_graph;
	}

	bool GraphDocument::SetNodeComment(const uint32 node, const String& comment)
	{
		NodeInstance* instance = FindMutableNode(node);
		if (instance == nullptr || instance->comment == comment)
		{
			return false;
		}

		instance->comment = comment;
		Touch(false);
		return true;
	}

	void GraphDocument::SetNodeDebug(const uint32 id, const bool on)
	{
		NodeInstance* instance = FindMutableNode(id);
		if (instance == nullptr || instance->debug == on)
		{
			return;
		}

		instance->debug = on;
		Touch(false);
	}

	uint64 GraphDocument::Revision() const
	{
		return m_revision;
	}

	uint64 GraphDocument::EvalRevision() const
	{
		return m_evalRevision;
	}

	void GraphDocument::Touch(const bool affectsEvaluation)
	{
		++m_revision;
		if (affectsEvaluation)
		{
			++m_evalRevision;
		}
	}

	bool GraphDocument::IsUnknownNode(const uint32 id) const
	{
		const NodeInstance* node = FindNode(id);
		return node != nullptr && m_registry->Find(node->type) == nullptr;
	}

	const NodeInstance* GraphDocument::FindNode(const uint32 id) const
	{
		for (const NodeInstance& node : m_graph.nodes)
		{
			if (node.id == id)
			{
				return &node;
			}
		}
		return nullptr;
	}

	const NodeTypeDef* GraphDocument::FindNodeType(const uint32 id) const
	{
		const NodeInstance* node = FindNode(id);
		return node != nullptr ? m_registry->Find(node->type) : nullptr;
	}

	const NodeRegistry& GraphDocument::GetRegistry() const
	{
		return *m_registry;
	}

	std::vector<String> GraphDocument::GetPinNames(const uint32 node, const bool isOutput) const
	{
		std::vector<String> names;
		if (FindNode(node) == nullptr)
		{
			return names;
		}

		if (const NodeTypeDef* def = FindNodeType(node))
		{
			for (const PinDef& pin : isOutput ? def->outputs : def->inputs)
			{
				names.push_back(pin.name);
			}
		}

		const auto extra = m_extraPins.find(node);
		if (extra != m_extraPins.end())
		{
			const std::vector<String>& extraNames = isOutput ? extra->second.outputs : extra->second.inputs;
			names.insert(names.end(), extraNames.begin(), extraNames.end());
		}

		if (names.size() > MaxPinIndex + 1)
		{
			names.resize(MaxPinIndex + 1);
		}
		return names;
	}

	uint64 GraphDocument::FindPinId(const uint32 node, const bool isOutput, const String& name) const
	{
		const std::vector<String> names = GetPinNames(node, isOutput);
		const auto it = std::find(names.begin(), names.end(), name);
		if (it == names.end())
		{
			return 0;
		}
		return MakePinId(node, isOutput, static_cast<uint32>(it - names.begin()));
	}

	const PinDef* GraphDocument::FindPinDef(const uint64 pinId) const
	{
		const PinRef ref = DecodePinId(pinId);
		const NodeTypeDef* def = FindNodeType(ref.node);
		if (def == nullptr)
		{
			return nullptr;
		}

		const std::vector<PinDef>& pins = ref.isOutput ? def->outputs : def->inputs;
		return ref.index < pins.size() ? &pins[ref.index] : nullptr;
	}

	const String* GraphDocument::FindPinName(const uint64 pinId) const
	{
		const PinRef ref = DecodePinId(pinId);
		if (FindNode(ref.node) == nullptr)
		{
			return nullptr;
		}

		uint32 index = ref.index;
		if (const NodeTypeDef* def = FindNodeType(ref.node))
		{
			const std::vector<PinDef>& pins = ref.isOutput ? def->outputs : def->inputs;
			if (index < pins.size())
			{
				return &pins[index].name;
			}
			index -= static_cast<uint32>(pins.size());
		}

		const auto extra = m_extraPins.find(ref.node);
		if (extra == m_extraPins.end())
		{
			return nullptr;
		}
		const std::vector<String>& names = ref.isOutput ? extra->second.outputs : extra->second.inputs;
		return index < names.size() ? &names[index] : nullptr;
	}

	std::vector<GraphDocument::ResolvedLink> GraphDocument::ResolveLinks() const
	{
		std::vector<ResolvedLink> result;
		result.reserve(m_graph.links.size());
		for (size_t i = 0; i < m_graph.links.size(); ++i)
		{
			const Link& link = m_graph.links[i];
			const uint64 fromPin = FindPinId(link.fromNode, true, link.fromPin);
			const uint64 toPin = FindPinId(link.toNode, false, link.toPin);
			if (fromPin == 0 || toPin == 0)
			{
				continue;
			}

			ResolvedLink resolved;
			resolved.id = MakeLinkId(fromPin, toPin);
			resolved.fromPin = fromPin;
			resolved.toPin = toPin;
			resolved.index = i;
			result.push_back(resolved);
		}
		return result;
	}

	NodeInstance* GraphDocument::FindMutableNode(const uint32 id)
	{
		return const_cast<NodeInstance*>(FindNode(id));
	}

	bool GraphDocument::Reaches(const uint32 start, const uint32 target) const
	{
		std::vector<uint32> pending { start };
		std::vector<uint32> visited;
		while (!pending.empty())
		{
			const uint32 node = pending.back();
			pending.pop_back();
			if (node == target)
			{
				return true;
			}
			if (std::find(visited.begin(), visited.end(), node) != visited.end())
			{
				continue;
			}
			visited.push_back(node);

			for (const Link& link : m_graph.links)
			{
				if (link.fromNode == node)
				{
					pending.push_back(link.toNode);
				}
			}
		}
		return false;
	}

	void GraphDocument::RebuildExtraPins()
	{
		m_extraPins.clear();
		for (const Link& link : m_graph.links)
		{
			const NodeInstance* from = FindNode(link.fromNode);
			if (from != nullptr && !DeclaresPin(m_registry->Find(from->type), true, link.fromPin))
			{
				InsertSorted(m_extraPins[link.fromNode].outputs, link.fromPin);
			}

			const NodeInstance* to = FindNode(link.toNode);
			if (to != nullptr && !DeclaresPin(m_registry->Find(to->type), false, link.toPin))
			{
				InsertSorted(m_extraPins[link.toNode].inputs, link.toPin);
			}
		}
	}
}
