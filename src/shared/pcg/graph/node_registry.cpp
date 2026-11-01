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
		RegisterFilterNodes(registry);
		RegisterPointNodes(registry);
		RegisterSpawnNodes(registry);
		RegisterSectionNodes(registry);
		RegisterTerrainNodes(registry);
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

		const ParameterDef* FindParameter(const Graph& g, const String& name)
		{
			for (const ParameterDef& def : g.parameters)
			{
				if (def.name == name)
				{
					return &def;
				}
			}
			return nullptr;
		}

		/// Whether a value is allowed by a setting's enumValues (always true for free settings and non-Strings).
		bool EnumAllows(const SettingDef& setting, const AttributeValue& value)
		{
			const String* s = std::get_if<String>(&value);
			return setting.enumValues.empty() || s == nullptr || std::find(setting.enumValues.begin(), setting.enumValues.end(), *s) != setting.enumValues.end();
		}

		String EnumIssue(const SettingDef& setting, const AttributeValue& value)
		{
			String allowed;
			for (const String& v : setting.enumValues)
			{
				allowed += (allowed.empty() ? "" : ", ") + v;
			}
			return "setting '" + setting.name + "' has the unknown value '" + std::get<String>(value) + "' (allowed: " + allowed + ")";
		}

		String LinkText(const Link& link)
		{
			return std::to_string(link.fromNode) + "." + link.fromPin + " -> " + std::to_string(link.toNode) + "." + link.toPin;
		}
	}

	bool IsParameterBinding(const AttributeValue& value, String& parameterName)
	{
		const String* s = std::get_if<String>(&value);
		if (s == nullptr || s->empty() || (*s)[0] != '$' || (s->size() > 1 && (*s)[1] == '$'))
		{
			return false;
		}
		parameterName = s->substr(1);
		return true;
	}

	AttributeValue UnescapeSettingValue(const AttributeValue& value)
	{
		const String* s = std::get_if<String>(&value);
		if (s != nullptr && s->size() >= 2 && (*s)[0] == '$' && (*s)[1] == '$')
		{
			return AttributeValue(s->substr(1));
		}
		return value;
	}

	std::vector<GraphIssue> ValidateGraph(const Graph& g, const NodeRegistry& r)
	{
		std::vector<GraphIssue> issues;

		// Node ids and types. The first node with a given id is the one links refer to.
		std::map<uint32, const NodeInstance*> nodes;
		std::map<uint32, const NodeTypeDef*> defs;
		for (const NodeInstance& node : g.nodes)
		{
			if (node.id == 0 || node.id > MaxNodeId)
			{
				issues.push_back({ 0, "node of type '" + node.type + "' has the invalid id " + std::to_string(node.id) });
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
				if (it == node.settings.end())
				{
					continue;
				}
				const AttributeType wanted = TypeOf(setting.defaultValue);
				String parameterName;
				if (IsParameterBinding(it->second, parameterName))
				{
					const ParameterDef* parameter = FindParameter(g, parameterName);
					if (parameter == nullptr)
					{
						issues.push_back({ node.id, "setting '" + setting.name + "' is bound to the unknown parameter '" + parameterName + "'" });
					}
					else if (TypeOf(parameter->defaultValue) != wanted)
					{
						issues.push_back({ node.id, "setting '" + setting.name + "' must be " + TypeName(wanted) + ", but parameter '" + parameterName +
							"' is " + TypeName(TypeOf(parameter->defaultValue)) });
					}
					else if (!EnumAllows(setting, parameter->defaultValue))
					{
						issues.push_back({ node.id, EnumIssue(setting, parameter->defaultValue) + " via parameter '" + parameterName + "'" });
					}
					continue;
				}
				const AttributeValue value = UnescapeSettingValue(it->second);
				if (TypeOf(value) != wanted)
				{
					issues.push_back({ node.id, "setting '" + setting.name + "' must be " + TypeName(wanted) + ", not " + TypeName(TypeOf(value)) });
				}
				else if (!EnumAllows(setting, value))
				{
					issues.push_back({ node.id, EnumIssue(setting, value) });
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
