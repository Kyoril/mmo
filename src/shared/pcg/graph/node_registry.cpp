// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "node_registry.h"

#include "builtin_nodes.h"

#include <algorithm>
#include <set>

namespace mmo::pcg::graph
{
	void NodeRegistry::Register(NodeTypeDef def)
	{
		String key = def.type;
		m_types[key] = std::move(def);
	}

	const NodeTypeDef* NodeRegistry::Find(const String& type) const
	{
		const auto it = m_types.find(type);
		return it != m_types.end() ? &it->second : nullptr;
	}

	std::vector<const NodeTypeDef*> NodeRegistry::All() const
	{
		std::vector<const NodeTypeDef*> result;
		result.reserve(m_types.size());
		for (const auto& entry : m_types)
		{
			result.push_back(&entry.second);
		}

		std::sort(result.begin(), result.end(), [](const NodeTypeDef* a, const NodeTypeDef* b)
		{
			if (a->category != b->category)
			{
				return a->category < b->category;
			}
			if (a->title != b->title)
			{
				return a->title < b->title;
			}
			return a->type < b->type;
		});
		return result;
	}

	NodeRegistry& NodeRegistry::Default()
	{
		static NodeRegistry registry = []()
		{
			NodeRegistry r;
			RegisterBuiltinNodes(r);
			return r;
		}();
		return registry;
	}

	void RegisterBuiltinNodes(NodeRegistry& registry)
	{
		// One explicit call per node family (no static registration: a static library link could drop it).
		RegisterInputNodes(registry);
		RegisterSamplerNodes(registry);
	}

	namespace
	{
		const PinDef* FindPin(const std::vector<PinDef>& pins, const String& name)
		{
			for (const PinDef& pin : pins)
			{
				if (pin.name == name)
				{
					return &pin;
				}
			}
			return nullptr;
		}

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

		const char* TypeName(const AttributeType type)
		{
			switch (type)
			{
			case AttributeType::Float: return "float";
			case AttributeType::Int: return "int";
			case AttributeType::Bool: return "bool";
			case AttributeType::Vector3: return "vector3";
			case AttributeType::String: return "string";
			}
			return "unknown";
		}

		String LinkText(const Link& link)
		{
			return std::to_string(link.fromNode) + "." + link.fromPin + " -> " + std::to_string(link.toNode) + "." + link.toPin;
		}
	}

	std::vector<GraphIssue> ValidateGraph(const Graph& g, const NodeRegistry& r)
	{
		std::vector<GraphIssue> issues;

		// Node ids and types. The first node with a given id is the one links refer to.
		std::map<uint32, const NodeInstance*> nodes;
		std::map<uint32, const NodeTypeDef*> defs;
		for (const NodeInstance& node : g.nodes)
		{
			if (node.id == 0)
			{
				issues.push_back({ 0, "node of type '" + node.type + "' has the invalid id 0" });
				continue;
			}
			if (nodes.count(node.id) != 0)
			{
				issues.push_back({ node.id, "duplicate node id " + std::to_string(node.id) });
				continue;
			}
			nodes[node.id] = &node;

			const NodeTypeDef* def = r.Find(node.type);
			if (def == nullptr)
			{
				issues.push_back({ node.id, "unknown node type '" + node.type + "'" });
				continue;
			}
			defs[node.id] = def;

			for (const SettingDef& setting : def->settings)
			{
				const auto it = node.settings.find(setting.name);
				if (it != node.settings.end() && TypeOf(it->second) != TypeOf(setting.defaultValue))
				{
					issues.push_back({ node.id, "setting '" + setting.name + "' must be " + TypeName(TypeOf(setting.defaultValue)) +
						", not " + TypeName(TypeOf(it->second)) });
				}
			}
		}

		// Links. Only links between existing nodes take part in the cycle check.
		std::map<std::pair<uint32, String>, size_t> linksPerInput;
		std::map<uint32, std::set<uint32>> successors;
		std::map<uint32, std::set<uint32>> predecessors;
		for (const Link& link : g.links)
		{
			const bool fromExists = nodes.count(link.fromNode) != 0;
			const bool toExists = nodes.count(link.toNode) != 0;
			if (!fromExists || !toExists)
			{
				const uint32 reported = toExists ? link.toNode : (fromExists ? link.fromNode : 0);
				issues.push_back({ reported, "link " + LinkText(link) + " refers to a missing node" });
				continue;
			}

			successors[link.fromNode].insert(link.toNode);
			predecessors[link.toNode].insert(link.fromNode);

			const auto fromDef = defs.find(link.fromNode);
			const auto toDef = defs.find(link.toNode);
			const PinDef* fromPin = nullptr;
			const PinDef* toPin = nullptr;
			if (fromDef != defs.end())
			{
				fromPin = FindPin(fromDef->second->outputs, link.fromPin);
				if (fromPin == nullptr)
				{
					issues.push_back({ link.fromNode, "link " + LinkText(link) + ": unknown output pin '" + link.fromPin + "'" });
				}
			}
			if (toDef != defs.end())
			{
				toPin = FindPin(toDef->second->inputs, link.toPin);
				if (toPin == nullptr)
				{
					issues.push_back({ link.toNode, "link " + LinkText(link) + ": unknown input pin '" + link.toPin + "'" });
				}
			}
			if (fromPin != nullptr && toPin != nullptr && !KindsCompatible(fromPin->accepts, toPin->accepts))
			{
				issues.push_back({ link.toNode, "link " + LinkText(link) + ": input pin '" + link.toPin + "' does not accept the kind produced by '" + link.fromPin + "'" });
			}
			if (toPin != nullptr)
			{
				const size_t count = ++linksPerInput[{ link.toNode, link.toPin }];
				if (count == 2 && !toPin->multiple)
				{
					issues.push_back({ link.toNode, "input pin '" + link.toPin + "' accepts a single link" });
				}
			}
		}

		// Required inputs.
		for (const auto& [id, def] : defs)
		{
			for (const PinDef& pin : def->inputs)
			{
				if (pin.required && linksPerInput.count({ id, pin.name }) == 0)
				{
					issues.push_back({ id, "required input '" + pin.name + "' is not connected" });
				}
			}
		}

		// Cycles: peel off nodes without predecessors (forwards), then nodes without successors (backwards);
		// what remains lies on a cycle (or between cycles).
		std::set<uint32> remaining;
		for (const auto& entry : nodes)
		{
			remaining.insert(entry.first);
		}
		const auto peel = [&remaining](const std::map<uint32, std::set<uint32>>& incoming)
		{
			bool changed = true;
			while (changed)
			{
				changed = false;
				for (auto it = remaining.begin(); it != remaining.end();)
				{
					bool hasIncoming = false;
					const auto in = incoming.find(*it);
					if (in != incoming.end())
					{
						for (const uint32 other : in->second)
						{
							if (remaining.count(other) != 0)
							{
								hasIncoming = true;
								break;
							}
						}
					}
					if (!hasIncoming)
					{
						it = remaining.erase(it);
						changed = true;
					}
					else
					{
						++it;
					}
				}
			}
		};
		peel(predecessors);
		peel(successors);
		for (const uint32 id : remaining)
		{
			issues.push_back({ id, "node is part of a cycle" });
		}

		return issues;
	}
}
