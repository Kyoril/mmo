// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "evaluator.h"

#include "pcg/pcg_random.h"

#include <chrono>
#include <set>

namespace mmo::pcg::graph
{
	void GraphCache::Clear()
	{
		m_entries.clear();
	}

	size_t GraphCache::Size() const
	{
		return m_entries.size();
	}

	namespace
	{
		/// FNV-1a over a string; platform independent.
		uint64 StringHash(const String& s)
		{
			uint64 h = 14695981039346656037ull;
			for (const char c : s)
			{
				h ^= static_cast<uint8>(c);
				h *= 1099511628211ull;
			}
			return h;
		}

		/// Folds a value into a running hash.
		uint64 Mix(const uint64 h, const uint64 a, const uint64 b = 0)
		{
			return Hash(h, static_cast<int64>(a), static_cast<int64>(b));
		}

		uint64 AttributesHash(const AttributeSet& attributes)
		{
			Collection c(1);
			c[0].attributes = attributes;
			return ContentHash(c);
		}

		uint64 SplineHash(const PathSpline& spline)
		{
			Collection c(1);
			c[0].spline = spline;
			return ContentHash(c);
		}

		uint64 ContextHash(const EvalContext& ctx)
		{
			uint64 h = Mix(0x9e3779b97f4a7c15ull, AttributesHash(ctx.parameters), SplineHash(ctx.region));
			h = Mix(h, ctx.controlsRevision, ctx.terrain != nullptr ? ctx.terrain->Revision() : 0);
			h = Mix(h, ctx.terrain != nullptr ? 1 : 0, static_cast<uint64>(ctx.pointLimit));
			for (const String& warning : ctx.parameterWarnings)
			{
				h = Mix(h, StringHash(warning), 2);
			}
			return h;
		}

		/// Declared settings take the instance value or the def's default; undeclared instance settings are kept.
		AttributeSet EffectiveSettings(const NodeTypeDef& def, const NodeInstance& node)
		{
			AttributeSet settings = node.settings;
			for (const SettingDef& setting : def.settings)
			{
				settings.emplace(setting.name, setting.defaultValue);
			}
			return settings;
		}

		/// Ensures every declared output pin exists and drops undeclared ones (with a warning).
		void NormalizeOutputs(const NodeTypeDef& def, NodeOutput& output)
		{
			std::map<String, Collection> pins;
			for (const PinDef& pin : def.outputs)
			{
				const auto it = output.pins.find(pin.name);
				pins[pin.name] = it != output.pins.end() ? std::move(it->second) : Collection();
			}
			for (const auto& entry : output.pins)
			{
				if (pins.count(entry.first) == 0)
				{
					output.warnings.push_back("dropped output on undeclared pin '" + entry.first + "'");
				}
			}
			output.pins = std::move(pins);
		}

		uint64 OutputHash(const std::map<String, uint64>& pinHashes)
		{
			uint64 h = 0x51ed270b27a3c9f1ull;
			for (const auto& [name, pinHash] : pinHashes)
			{
				h = Mix(h, StringHash(name), pinHash);
			}
			return h;
		}
	}

	AttributeSet MergeParameters(const std::vector<ParameterDef>& defaults, const AttributeSet& overrides, std::vector<String>* warnings)
	{
		AttributeSet merged = overrides;
		for (const ParameterDef& def : defaults)
		{
			const auto it = merged.find(def.name);
			if (it == merged.end())
			{
				merged.emplace(def.name, def.defaultValue);
			}
			else if (TypeOf(it->second) != TypeOf(def.defaultValue))
			{
				if (warnings != nullptr)
				{
					warnings->push_back("parameter '" + def.name + "': override has the wrong type, the default is used");
				}
				it->second = def.defaultValue;
			}
		}
		return merged;
	}

	EvalResult Evaluate(const Graph& g, const NodeRegistry& r, const EvalContext& callerCtx, GraphCache* cache)
	{
		// Nodes see the overrides merged over the graph's parameter defaults.
		EvalContext ctx = callerCtx;
		ctx.parameterWarnings.clear();
		ctx.parameters = MergeParameters(g.parameters, callerCtx.parameters, &ctx.parameterWarnings);

		EvalResult result;
		result.issues = ValidateGraph(g, r);
		if (!result.issues.empty())
		{
			return result;
		}

		// After validation ids are unique and non-zero, every type resolves and every link is valid.
		std::map<uint32, const NodeInstance*> nodes;
		for (const NodeInstance& node : g.nodes)
		{
			nodes[node.id] = &node;
		}

		std::map<uint32, std::vector<const Link*>> incoming;
		std::map<uint32, std::vector<uint32>> successors;
		std::map<uint32, size_t> pending;
		for (const auto& entry : nodes)
		{
			pending[entry.first] = 0;
		}
		for (const Link& link : g.links)
		{
			incoming[link.toNode].push_back(&link);
			successors[link.fromNode].push_back(link.toNode);
			pending[link.toNode]++;
		}

		std::set<uint32> ready;
		for (const auto& [id, count] : pending)
		{
			if (count == 0)
			{
				ready.insert(id);
			}
		}

		const uint64 contextHash = ContextHash(ctx);
		std::map<uint32, std::map<String, uint64>> pinHashes;
		std::set<uint32> failed;

		while (!ready.empty())
		{
			const uint32 id = *ready.begin();
			ready.erase(ready.begin());

			for (const uint32 next : successors[id])
			{
				if (--pending[next] == 0)
				{
					ready.insert(next);
				}
			}

			const NodeInstance& node = *nodes[id];
			const NodeTypeDef& def = *r.Find(node.type);
			NodeResult& nodeResult = result.nodes[id];

			const std::vector<const Link*>& links = incoming[id];
			bool upstreamFailed = false;
			for (const Link* link : links)
			{
				if (failed.count(link->fromNode) != 0)
				{
					upstreamFailed = true;
					break;
				}
			}
			if (upstreamFailed)
			{
				nodeResult.output.error = "upstream failed";
				failed.insert(id);
				continue;
			}

			const AttributeSet settings = EffectiveSettings(def, node);
			const uint64 nodeSeed = Hash(ctx.instanceSeed, id);

			uint64 key = Mix(0x2545f4914f6cdd1dull, id, StringHash(node.type));
			key = Mix(key, AttributesHash(settings), nodeSeed);
			for (const Link* link : links)
			{
				key = Mix(key, StringHash(link->toPin), pinHashes[link->fromNode][link->fromPin]);
			}
			if (def.readsContext)
			{
				key = Mix(key, contextHash, 1);
			}

			if (cache != nullptr)
			{
				const auto it = cache->m_entries.find(id);
				if (it != cache->m_entries.end() && it->second.key == key)
				{
					nodeResult.output = it->second.output;
					nodeResult.hash = it->second.hash;
					nodeResult.fromCache = true;
					pinHashes[id] = it->second.pinHashes;
					continue;
				}
			}

			std::map<String, Collection> inputs;
			for (const Link* link : links)
			{
				const Collection& source = result.nodes[link->fromNode].output.pins[link->fromPin];
				Collection& target = inputs[link->toPin];
				target.insert(target.end(), source.begin(), source.end());
			}

			const auto start = std::chrono::steady_clock::now();
			if (def.execute)
			{
				nodeResult.output = def.execute(ctx, nodeSeed, settings, inputs);
			}
			else
			{
				nodeResult.output.error = "node type '" + def.type + "' has no implementation";
			}
			const auto end = std::chrono::steady_clock::now();
			nodeResult.milliseconds = std::chrono::duration<double, std::milli>(end - start).count();

			NormalizeOutputs(def, nodeResult.output);

			if (!nodeResult.output.error.empty())
			{
				failed.insert(id);
				if (cache != nullptr)
				{
					cache->m_entries.erase(id);
				}
				continue;
			}

			std::map<String, uint64>& hashes = pinHashes[id];
			for (const auto& [name, collection] : nodeResult.output.pins)
			{
				hashes[name] = ContentHash(collection);
			}
			nodeResult.hash = OutputHash(hashes);

			if (cache != nullptr)
			{
				GraphCache::Entry& entry = cache->m_entries[id];
				entry.key = key;
				entry.output = nodeResult.output;
				entry.pinHashes = hashes;
				entry.hash = nodeResult.hash;
			}
		}

		if (cache != nullptr)
		{
			for (auto it = cache->m_entries.begin(); it != cache->m_entries.end();)
			{
				if (nodes.count(it->first) == 0)
				{
					it = cache->m_entries.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		result.ok = true;
		return result;
	}
}
