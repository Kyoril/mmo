// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "graph_io.h"
#include "json_util.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <set>
#include <tuple>

namespace mmo::pcg::graph
{
	namespace
	{
		using Json = nlohmann::json;
		using namespace jsonutil;

		constexpr int kFileVersion = 1;

		bool ParseNode(const Json& j, const size_t index, NodeInstance& node, String& error)
		{
			const String context = "nodes[" + std::to_string(index) + "]";
			if (!j.is_object())
			{
				error = context + ": must be an object";
				return false;
			}

			if (!ReadUInt(j, "id", node.id, context, error, true) || !ReadString(j, "type", node.type, context, error, true))
			{
				return false;
			}
			if (node.id == 0 || node.id > MaxNodeId)
			{
				error = context + ": node id " + std::to_string(node.id) + " is invalid";
				return false;
			}
			if (!ReadNumber(j, "editorX", node.editorX, context, error) || !ReadNumber(j, "editorY", node.editorY, context, error) || !ReadString(j, "comment", node.comment, context, error, false))
			{
				return false;
			}

			if (const Json* debug = Find(j, "debug"))
			{
				if (!debug->is_boolean())
				{
					error = context + ": 'debug' must be a boolean";
					return false;
				}
				node.debug = debug->get<bool>();
			}

			if (const Json* settings = Find(j, "settings"))
			{
				if (!settings->is_object())
				{
					error = context + ": 'settings' must be an object";
					return false;
				}
				for (auto it = settings->begin(); it != settings->end(); ++it)
				{
					AttributeValue value;
					String valueError;
					if (!ValueFromJson(it.value(), value, valueError))
					{
						error = context + ": setting '" + it.key() + "': " + valueError;
						return false;
					}
					node.settings[it.key()] = value;
				}
			}
			return true;
		}
	}

	bool ParseGraph(const String& json, Graph& out, String& error)
	{
		const Json root = Json::parse(json, nullptr, false);
		if (root.is_discarded())
		{
			error = "malformed JSON";
			return false;
		}
		if (!root.is_object())
		{
			error = "graph file must be a JSON object";
			return false;
		}

		const Json* version = Find(root, "version");
		if (version == nullptr || !IsInt64(*version) || version->get<int64_t>() != kFileVersion)
		{
			error = "unsupported or missing graph file version (expected 1)";
			return false;
		}

		Graph graph;
		if (!ReadString(root, "name", graph.name, "graph", error, false))
		{
			return false;
		}

		uint32 maxId = 0;
		std::set<uint32> ids;
		if (const Json* nodes = Find(root, "nodes"))
		{
			if (!nodes->is_array())
			{
				error = "'nodes' must be an array";
				return false;
			}
			for (size_t i = 0; i < nodes->size(); ++i)
			{
				NodeInstance node;
				if (!ParseNode((*nodes)[i], i, node, error))
				{
					return false;
				}
				if (!ids.insert(node.id).second)
				{
					error = "duplicate node id " + std::to_string(node.id);
					return false;
				}
				maxId = std::max(maxId, node.id);
				graph.nodes.push_back(std::move(node));
			}
		}

		if (const Json* links = Find(root, "links"))
		{
			if (!links->is_array())
			{
				error = "'links' must be an array";
				return false;
			}
			for (size_t i = 0; i < links->size(); ++i)
			{
				const String context = "links[" + std::to_string(i) + "]";
				const Json& j = (*links)[i];
				if (!j.is_object())
				{
					error = context + ": must be an object";
					return false;
				}
				Link link;
				if (!ReadUInt(j, "fromNode", link.fromNode, context, error, true) || !ReadString(j, "fromPin", link.fromPin, context, error, true) ||
					!ReadUInt(j, "toNode", link.toNode, context, error, true) || !ReadString(j, "toPin", link.toPin, context, error, true))
				{
					return false;
				}
				if (link.fromNode == 0 || link.fromNode > MaxNodeId || link.toNode == 0 || link.toNode > MaxNodeId)
				{
					error = context + ": node ids must be between 1 and " + std::to_string(MaxNodeId);
					return false;
				}
				graph.links.push_back(std::move(link));
			}
		}

		if (const Json* params = Find(root, "parameters"))
		{
			if (!params->is_array())
			{
				error = "'parameters' must be an array";
				return false;
			}
			for (size_t i = 0; i < params->size(); ++i)
			{
				const String context = "parameters[" + std::to_string(i) + "]";
				const Json& j = (*params)[i];
				if (!j.is_object())
				{
					error = context + ": must be an object";
					return false;
				}
				ParameterDef def;
				if (!ReadString(j, "name", def.name, context, error, true) || !ReadString(j, "description", def.description, context, error, false))
				{
					return false;
				}
				const Json* value = Find(j, "default");
				if (value == nullptr)
				{
					error = context + ": missing 'default'";
					return false;
				}
				String valueError;
				if (!ValueFromJson(*value, def.defaultValue, valueError))
				{
					error = context + ": " + valueError;
					return false;
				}
				graph.parameters.push_back(std::move(def));
			}
		}

		graph.nextNodeId = maxId + 1;   // maxId <= MaxNodeId, so this cannot wrap
		if (const Json* next = Find(root, "nextNodeId"))
		{
			if (!IsInt64(*next) || next->get<int64_t>() < 1 || next->get<int64_t>() > 0xFFFFFFFFll)
			{
				error = "'nextNodeId' must be a positive integer";
				return false;
			}
			graph.nextNodeId = std::max(static_cast<uint32>(next->get<int64_t>()), maxId + 1);
		}

		out = std::move(graph);
		return true;
	}

	String WriteGraph(const Graph& graph)
	{
		Json root = Json::object();
		root["version"] = kFileVersion;
		root["name"] = graph.name;
		root["nextNodeId"] = graph.nextNodeId;

		std::vector<const NodeInstance*> nodes;
		for (const NodeInstance& n : graph.nodes)
		{
			nodes.push_back(&n);
		}
		std::sort(nodes.begin(), nodes.end(), [](const NodeInstance* a, const NodeInstance* b) { return a->id < b->id; });

		Json nodeArray = Json::array();
		for (const NodeInstance* n : nodes)
		{
			Json j = Json::object();
			j["id"] = n->id;
			j["type"] = n->type;
			j["editorX"] = SafeDouble(n->editorX);
			j["editorY"] = SafeDouble(n->editorY);
			j["debug"] = n->debug;
			j["comment"] = n->comment;
			Json settings = Json::object();
			for (const auto& entry : n->settings)
			{
				settings[entry.first] = ValueToJson(entry.second);
			}
			j["settings"] = std::move(settings);
			nodeArray.push_back(std::move(j));
		}
		root["nodes"] = std::move(nodeArray);

		std::vector<Link> links = graph.links;
		std::sort(links.begin(), links.end(), [](const Link& a, const Link& b)
		{
			return std::tie(a.fromNode, a.fromPin, a.toNode, a.toPin) < std::tie(b.fromNode, b.fromPin, b.toNode, b.toPin);
		});
		Json linkArray = Json::array();
		for (const Link& l : links)
		{
			Json j = Json::object();
			j["fromNode"] = l.fromNode;
			j["fromPin"] = l.fromPin;
			j["toNode"] = l.toNode;
			j["toPin"] = l.toPin;
			linkArray.push_back(std::move(j));
		}
		root["links"] = std::move(linkArray);

		Json paramArray = Json::array();
		for (const ParameterDef& p : graph.parameters)
		{
			Json j = Json::object();
			j["name"] = p.name;
			j["default"] = ValueToJson(p.defaultValue);
			j["description"] = p.description;
			paramArray.push_back(std::move(j));
		}
		root["parameters"] = std::move(paramArray);

		return Dump(root);
	}
}
