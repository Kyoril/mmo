// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "evaluator.h"
#include "controls.h"
#include "hashing.h"

#include "pcg/pcg_random.h"

#include <algorithm>
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
		/// Folds a value into a running hash.
		uint64 Mix(const uint64 h, const uint64 a, const uint64 b = 0)
		{
			return Hash(h, static_cast<int64>(a), static_cast<int64>(b));
		}

		/// Hashes of the context parts a node can depend on (see context_dep).
		struct ContextPartHashes
		{
			uint64 parameters = 0;
			uint64 region = 0;
			uint64 controls = 0;
			uint64 terrain = 0;
			uint64 pointLimit = 0;

			explicit ContextPartHashes(const EvalContext& ctx)
			{
				parameters = Mix(0x9e3779b97f4a7c15ull, HashAttributes(ctx.parameters), ctx.parameterWarnings.size());
				for (const String& warning : ctx.parameterWarnings)
				{
					parameters = Mix(parameters, StringHash(warning), 2);
				}
				region = HashSpline(ctx.region);
				controls = Mix(0x3c6ef372fe94f82bull, ctx.controls != nullptr ? 1 : 0, ctx.controlsRevision);
				terrain = Mix(0xa54ff53a5f1d36f1ull, ctx.terrain != nullptr ? 1 : 0, ctx.terrain != nullptr ? ctx.terrain->Revision() : 0);
				pointLimit = Mix(0x510e527fade682d1ull, static_cast<uint64>(ctx.pointLimit));
			}

			/// Folds the parts selected by a context_dep mask into a cache key.
			uint64 Apply(uint64 key, const uint32 deps) const
			{
				key = Mix(key, deps, 1);
				if ((deps & context_dep::Parameters) != 0)
				{
					key = Mix(key, parameters, context_dep::Parameters);
				}
				if ((deps & context_dep::Region) != 0)
				{
					key = Mix(key, region, context_dep::Region);
				}
				if ((deps & context_dep::Controls) != 0)
				{
					key = Mix(key, controls, context_dep::Controls);
				}
				if ((deps & context_dep::Terrain) != 0)
				{
					key = Mix(key, terrain, context_dep::Terrain);
				}
				if ((deps & context_dep::PointLimit) != 0)
				{
					key = Mix(key, pointLimit, context_dep::PointLimit);
				}
				return key;
			}
		};

		/// Declared settings take the instance value (bindings resolved, "$$" unescaped) or the def's default;
		/// undeclared instance settings are kept as they are. Sets error (and returns what it has) when a binding
		/// cannot be resolved.
		AttributeSet EffectiveSettings(const NodeTypeDef& def, const NodeInstance& node, const AttributeSet& parameters, String& error)
		{
			AttributeSet settings = node.settings;
			for (const SettingDef& setting : def.settings)
			{
				const auto it = settings.find(setting.name);
				if (it == settings.end())
				{
					settings.emplace(setting.name, setting.defaultValue);
					continue;
				}
				String parameterName;
				if (!IsParameterBinding(it->second, parameterName))
				{
					it->second = UnescapeSettingValue(it->second);
					continue;
				}
				const auto parameter = parameters.find(parameterName);
				if (parameter == parameters.end())
				{
					error = "setting '" + setting.name + "' is bound to the unknown parameter '" + parameterName + "'";
					return settings;
				}
				if (TypeOf(parameter->second) != TypeOf(setting.defaultValue))
				{
					error = "setting '" + setting.name + "' is bound to parameter '" + parameterName + "' of another type";
					return settings;
				}
				it->second = parameter->second;
				const String* value = std::get_if<String>(&it->second);
				if (value != nullptr && !setting.enumValues.empty() && std::find(setting.enumValues.begin(), setting.enumValues.end(), *value) == setting.enumValues.end())
				{
					error = "setting '" + setting.name + "' got the unknown value '" + *value + "' from parameter '" + parameterName + "'";
					return settings;
				}
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
		if (ctx.controls != nullptr && ctx.controlsRevision == 0)
		{
			ctx.controlsRevision = ControlsHash(*ctx.controls);
		}

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

		const ContextPartHashes contextHashes(ctx);
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

			String settingsError;
			const AttributeSet settings = EffectiveSettings(def, node, ctx.parameters, settingsError);
			if (!settingsError.empty())
			{
				nodeResult.output.error = settingsError;
				NormalizeOutputs(def, nodeResult.output);
				failed.insert(id);
				if (cache != nullptr)
				{
					cache->m_entries.erase(id);
				}
				continue;
			}
			const uint64 nodeSeed = Hash(ctx.instanceSeed, id);

			uint64 key = Mix(0x2545f4914f6cdd1dull, id, StringHash(node.type));
			key = Mix(key, HashAttributes(settings), nodeSeed);
			for (const Link* link : links)
			{
				key = Mix(key, StringHash(link->toPin), pinHashes[link->fromNode][link->fromPin]);
			}
			key = contextHashes.Apply(key, def.contextDeps);

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

			// A node cannot know its own id; Terrain items carry it as their composition order.
			for (auto& [pinName, collection] : nodeResult.output.pins)
			{
				for (DataItem& item : collection)
				{
					if (item.kind == ItemKind::Terrain)
					{
						item.op.order = id;
					}
				}
			}

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
